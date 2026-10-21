/******************************************************************************
* MODULE     : QTMVaultSearch.cpp
* DESCRIPTION: Vault link search helpers
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "QTMVaultSearch.hpp"
#include "ATHENA/Data/enunciation_model.hpp"
#include "QTMVaultSearchWorker.hpp"
#include "QTMContinuousRag.hpp"
#include "rag_index.hpp"
#include "rag_embedding.hpp"
#include "buffer_actor.hpp"
#include "buffer_name_catalog.hpp"
#include "node_metadata.hpp"
#include "math_tree_v3.hpp"
#include "tree_cursor.hpp"
#include "Data/Convert/Xml/document_file_codec.hpp"
#include "convert.hpp"
#include "boot.hpp"
#include "math_token.hpp"
#include "QTMVaultAnchorModel.hpp"
#include "analyze.hpp"
#include "drd_mode.hpp"
#include "fuzzy_rank.hpp"
#include "utf8_edit.hpp"
#include "qt_utilities.hpp"
#include "neighborhoods.hpp"
#include "namespaces.hpp"
#include "vault.hpp"
#include <QFileInfo>
#include <QFile>
#include <QThread>
#include <QRegularExpression>
#include <QDir>
#include <QPointer>
#include <QDateTime>
#include <rapidfuzz/distance/Levenshtein.hpp>
#include <rapidfuzz/fuzz.hpp>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <utility>
#include <stdexcept>
#include <map>

bool
vault_search_candidate_files (url origin, string namespace_name,
                              bool neighborhoods_only,
                              std::vector<url>& files, string& error) {
  files.clear ();
  error= "";
  std::map<QString, url> neighborhood_files;
  if (neighborhoods_only) {
    const auto neighborhoods= athena_neighborhoods_for_file (origin);
    if (!neighborhoods.valid) {
      error= neighborhoods.error;
      return false;
    }
    for (const auto& row: neighborhoods.rows) {
      if (row.warning != "") {
        error= row.warning;
        return false;
      }
      for (const auto& entry: row.files)
        neighborhood_files.emplace (to_qstring (entry.canonical_path), entry.file);
    }
  }

  std::vector<url> candidates;
  if (namespace_name != "") {
    std::shared_ptr<const athena_namespace_definition> definition;
    if (!athena_namespace_get (namespace_name, definition)) {
      error= "Unknown namespace: " * namespace_name;
      return false;
    }
    auto members= athena_namespace_members (namespace_name, error);
    if (error != "") return false;
    for (const auto& member: members) candidates.push_back (member.file_url ());
  }
  else if (neighborhoods_only) {
    for (const auto& entry: neighborhood_files) candidates.push_back (entry.second);
  }
  else {
    const auto all= vault_get_all_files ();
    for (int i= 0; i < N(all); ++i) candidates.push_back (all[i]);
  }

  std::map<QString, url> unique;
  for (const auto& file: candidates) {
    const auto extension= suffix (file);
    if (extension != "ath" && extension != "tm") continue;
    const QString key= QFileInfo (to_qstring (concretize (file))).canonicalFilePath ();
    if (key.isEmpty ()) continue;
    if (neighborhoods_only && !neighborhood_files.count (key)) continue;
    unique.emplace (key, file);
  }
  for (const auto& entry: unique) files.push_back (entry.second);
  return true;
}

static thread_local const std::atomic<bool>* search_cancel_flag= nullptr;
static thread_local std::string search_source_revision;

VaultSearchCancellationScope::VaultSearchCancellationScope (
  const std::atomic<bool>* flag): previous (search_cancel_flag) {
  search_cancel_flag= flag;
}

VaultSearchCancellationScope::~VaultSearchCancellationScope () {
  search_cancel_flag= previous;
}

bool vault_search_cancelled () {
  return search_cancel_flag && search_cancel_flag->load (std::memory_order_relaxed);
}

int vault_search_worker_limit () {
  return std::max (8, QThread::idealThreadCount () - 3);
}

int vault_search_worker_count () {
  bool valid= false;
  int count= to_qstring (get_user_preference (
    "vault link search workers", "8")).toInt (&valid);
  return std::clamp (valid ? count : 8, 1, vault_search_worker_limit ());
}

QThreadPool& vault_search_workers () {
  static QThreadPool pool;
  static const bool configured= [] {
    pool.setExpiryTimeout (-1);
    return true;
  } ();
  (void) configured;
  return pool;
}

static athena::document::xml_format_version
snapshot_version (const actor_command_record& response) {
  using athena::document::xml_format_version;
  switch (response.argument[0]) {
  case 1: return xml_format_version::v1;
  case 2: return xml_format_version::v2;
  case 3: return xml_format_version::v3;
  default: throw std::runtime_error ("Unknown live search source storage version");
  }
}

athena::document::native_xml_read_result
vault_search_buffer_snapshot (url name) {
  const auto source= published_buffer_source (to_qstring (as_string (name)).toStdString ());
  actor_command_record response;
  if (source.first == ATHENA_NO_ACTOR ||
      !buffer_actor::invoke_on (source.first, actor_command_kind::snapshot_document,
        source.second, ATHENA_NO_BLOB, ATHENA_NO_BLOB, &response))
    throw std::runtime_error ("Could not snapshot source buffer");
  const tree document= actor_tree_registry::instance ().take (response.payload0);
  return {snapshot_version (response), extract (document, "body")};
}

void vault_search_set_query (VaultSearchOptions& options, const tree& query,
                            athena::document::xml_format_version version) {
  // Search constrains properties, never the query editor's persistent UUID.
  const auto* metadata= athena::node::get (query);
  const bool plain= is_atomic (query) &&
    (!metadata || metadata->properties.empty ());
  options.query= is_atomic (query) ? to_qstring (query->label) :
    to_qstring (tree_as_string (query));
  options.serializedQuery= plain ? std::string () :
    athena::document::encode_native_xml_bytes (
      query, version, athena::document::xml_kind::fragment);
  options.queryFormat= plain ? std::nullopt : std::make_optional (version);
}

tree vault_search_decode_query (const VaultSearchOptions& options) {
  using namespace athena::document;
  if (options.serializedQuery.empty () && !options.queryFormat)
    return tree (from_qstring (options.query));
  if (options.serializedQuery.empty () || !options.queryFormat)
    throw codec_exception (codec_error::invalid_structure,
      "Search query fragment and source format must both be present");
  const auto decoded= decode_native_xml_bytes (options.serializedQuery, xml_kind::fragment);
  if (decoded.version != *options.queryFormat)
    throw codec_exception (codec_error::unsupported_version,
      "Search query fragment version does not match its source format");
  return decoded.document;
}

tree vault_search_read_body (const QString& file, bool semantic,
                            athena::document::xml_format_version* source_version) {
  search_source_revision.clear ();
  const auto source= published_buffer_source (file.toStdString ());
  if (source.first != ATHENA_NO_ACTOR) {
    actor_command_record response;
    if (!buffer_actor::invoke_on (source.first, (semantic || source_version) ? actor_command_kind::snapshot_document : actor_command_kind::snapshot_body,
          source.second, ATHENA_NO_BLOB, ATHENA_NO_BLOB, &response))
      throw std::runtime_error ("Could not read live search source");
    tree document= actor_tree_registry::instance ().take (response.payload0);
    if (!semantic && !source_version) return document;
    const auto version= snapshot_version (response);
    if (source_version) *source_version= version;
    if (semantic)
      search_source_revision= athena::document::semantic_document_fingerprint (document, version);
    tree body= extract (document, "body");
    return is_empty (body) ? document : body;
  }
  QFile input (file);
  if (!input.open (QIODevice::ReadOnly)) throw std::runtime_error ("Cannot read search file");
  QByteArray bytes;
  while (!input.atEnd ()) {
    if (vault_search_cancelled ()) {
      if (source_version) throw std::runtime_error ("Source snapshot cancelled");
      return tree (DOCUMENT, "");
    }
    QByteArray block= input.read (256 * 1024);
    if (input.error () != QFileDevice::NoError)
      throw std::runtime_error ("Cannot read search file");
    bytes.append (block);
  }
  if (bytes.isEmpty ()) {
    if (source_version) throw std::runtime_error ("Empty source has no stored format");
    return tree (DOCUMENT, "");
  }
  // Search parses persisted documents, without converter/Scheme callbacks,
  // current-buffer fallback, link registration, or preview image rewriting.
  const auto decoded= athena::document::decode_document_bytes (
    std::string_view (bytes.constData (), (std::size_t) bytes.size ()),
    std::filesystem::path (file.toStdString ()));
  tree doc= decoded.document;
  if (semantic || source_version) {
    using namespace athena::document;
    xml_format_version version;
    switch (decoded.format) {
    case document_source_format::xml_v1: version= xml_format_version::v1; break;
    case document_source_format::xml_v2: version= xml_format_version::v2; break;
    case document_source_format::xml_v3: version= xml_format_version::v3; break;
    case document_source_format::legacy_markup:
    case document_source_format::legacy_scheme:
      version= xml_format_version::v2; break;
    default: throw std::runtime_error ("Unknown search source format");
    }
    if (source_version) *source_version= version;
    if (semantic) search_source_revision= semantic_document_fingerprint (doc, version);
  }
  tree body= extract (doc, "body");
  return is_empty (body) ? doc : body;
}

bool vault_search_has_live_source (const QString& file) {
  return published_buffer_source (file.toStdString ()).first != ATHENA_NO_ACTOR;
}

QString vault_search_node_uuid (tree body, path position) {
  auto id= athena::node::id (body);
  while (!is_nil (position) && is_compound (body) && position->item >= 0 && position->item < N(body)) {
    body= body[position->item]; position= position->next;
    const auto nested= athena::node::id (body);
    if (!nested.empty ()) id= nested;
  }
  return QString::fromStdString (id);
}

QString vault_search_identity (tree body, url file, path position) {
  auto id= vault_search_node_uuid (body, position);
  if (!id.isEmpty ()) return id;
  QString result= to_qstring (concretize (file));
  for (; !is_nil (position); position= position->next) result+= "/" + QString::number (position->item);
  return result;
}

void vault_search_semantic_matches (tree body, url file,
    const VaultSearchOptions& options, std::vector<VaultContentMatch>& matches) {
  if (!options.semanticCandidates) return;
  const auto found= options.semanticCandidates->find (to_qstring (concretize (file)));
  if (found == options.semanticCandidates->end ()) return;
  for (const auto& hit: found->second) {
    if (vault_search_cancelled ()) return;
    if (hit.semanticRevision.empty () || hit.semanticRevision != search_source_revision) {
      options.staleCandidates->fetch_add (1); continue;
    }
    tree node= body;
    path where;
    bool valid= true, kindMatches= options.enunciation.isEmpty ();
    const auto& registry= athena::enunciation::standard_registry ();
    auto matchKind= [&] {
      if (options.enunciation == "__athena_paragraph__")
        return is_atomic (node) || is_func (node, PARA);
      return registry.matches_filter (node, options.enunciation.toStdString ());
    };
    if (!kindMatches) kindMatches= matchKind ();
    if (!hit.treePath.empty ()) for (const auto& part: QString::fromStdString (hit.treePath).split ('.')) {
      bool ok= false; int index= part.toInt (&ok);
      if (!ok || !is_compound (node) || index < 0 || index >= N(node)) { valid= false; break; }
      node= node[index]; where= where * index;
      if (!kindMatches) kindMatches= matchKind ();
    }
    if (!valid || !kindMatches) continue;
    VaultContentMatch match;
    match.start= start (body, where); match.end= end (body, where);
    match.exact= false; match.score= 0; match.semantic= true; match.semanticScore= hit.score;
    match.sourceUuid= vault_search_node_uuid (body, where);
    matches.push_back (std::move (match));
  }
}

static QThreadPool& semantic_search_workers () {
  static QThreadPool pool;
  static const bool initialized= [] { pool.setMaxThreadCount (1); pool.setExpiryTimeout (-1); return true; } ();
  (void) initialized;
  return pool;
}

void vault_search_semantic_candidates (QObject* owner,
    const std::vector<std::string>& files, const VaultSearchOptions& options,
    std::shared_ptr<VaultSearchControl> control,
    std::function<void(std::shared_ptr<const VaultSemanticCandidates>, QString)> complete) {
  const QPointer<QObject> receiver= owner;
  QString queryText= options.query;
  if (!options.serializedQuery.empty () || options.queryFormat) {
    try {
      queryText= QString::fromStdString (athena::rag::rag_query_text (
        vault_search_decode_query (options)));
    }
    catch (const std::exception& error) { complete ({}, QString::fromUtf8 (error.what ())); return; }
  }
  if (queryText.trimmed ().isEmpty ()) { complete ({}, "The query has no text for semantic retrieval."); return; }
  const QString model= to_qstring (get_user_preference ("rag embedding model", ""));
  const QString device= to_qstring (get_user_preference ("rag embedding device", "auto"));
  const QString npuModel= to_qstring (get_user_preference ("rag npu openvino model", "")).trimmed ();
  const bool useNpu= !npuModel.isEmpty ();
  const QString tokenizer= to_qstring (get_user_preference ("rag npu tokenizer gguf", ""));
  auto modelRevision= [] (const QString& path) {
    const QFileInfo info (path);
    return path + '\n' + QString::number (info.size ()) + '\n' + QString::number (info.lastModified ().toMSecsSinceEpoch ());
  };
  // GUI-owned, bounded query cache; never mixes model revisions or devices.
  struct CachedQuery { std::vector<float> vector; std::string space; };
  static std::map<QString, CachedQuery> queryCache;
  const QString cacheKey= modelRevision (model) + '\n' + device + '\n' + modelRevision (npuModel) + '\n' + modelRevision (tokenizer) + '\n' + queryText;
  auto retrieve= [receiver, files, options, control, complete, cacheKey] (std::vector<float> vector, std::string space, QString error) {
    if (!receiver || control->cancelled) return;
    if (!error.isEmpty ()) { complete ({}, error); return; }
    if (queryCache.size () >= 64) queryCache.erase (queryCache.begin ());
    queryCache[cacheKey]= {vector, space};
    semantic_search_workers ().start ([receiver, files, options, control, complete, vector= std::move (vector), space= std::move (space)] {
      auto candidates= std::make_shared<VaultSemanticCandidates> ();
      QString error;
      try {
        athena::rag::RagConfig config;
        config.vault_root= options.root.toStdString ();
        config.db_path= athena::rag::rag_default_db_path (config.vault_root);
        config.read_only_query= true; config.load_embedding_model= false; config.progress= false;
        athena::rag::RagIndex index;
        if (!index.open (config)) throw std::runtime_error (index.status ().last_error);
        std::set<std::string> allowed;
        const QDir root (options.root);
        for (const auto& file: files) allowed.insert (root.relativeFilePath (QString::fromStdString (file)).toStdString ());
        const auto hits= index.search_vector (vector, space, allowed, 0, [control] { return control->cancelled.load (); });
        if (!index.status ().last_error.empty ()) throw std::runtime_error (index.status ().last_error);
        for (const auto& hit: hits)
          (*candidates)[root.absoluteFilePath (QString::fromStdString (hit.rel_path))].push_back (
            {hit.tree_path, hit.semantic_revision, hit.vector_similarity});
        if (hits.empty ()) error= "No indexed semantic candidates in the selected scope.";
      }
      catch (const std::exception& e) { error= QString::fromUtf8 (e.what ()); }
      if (receiver) QMetaObject::invokeMethod (receiver, [receiver, control, candidates, complete, error] {
        if (receiver && !control->cancelled) complete (candidates, error);
      }, Qt::QueuedConnection);
    });
  };
  if (const auto cached= queryCache.find (cacheKey); cached != queryCache.end ()) {
    retrieve (cached->second.vector, cached->second.space, {}); return;
  }
  if (useNpu) {
    auto cancel= qtm_rag_embed_query (owner, queryText, retrieve);
    QObject::connect (owner, &QObject::destroyed, std::move (cancel));
  }
  else {
    semantic_search_workers ().start ([receiver, control, model, device, query= queryText, retrieve] {
      std::vector<float> vector;
      std::string space;
      QString error;
      try {
        if (control->cancelled) return;
        if (model.isEmpty ()) throw std::runtime_error ("Configure a RAG embedding model to use semantic search.");
        static thread_local athena::rag::RagEmbedder embedder;
        static thread_local QString runtime;
        const QFileInfo info (model);
        const QString key= model + '\n' + device + '\n' + QString::number (info.size ()) + '\n' + QString::number (info.lastModified ().toMSecsSinceEpoch ());
        if (runtime != key || !embedder.available ()) {
          if (!embedder.open (model.toStdString (), device.toStdString ())) throw std::runtime_error ("Could not load the RAG query embedding model.");
          runtime= key;
        }
        vector= embedder.embed (query.toStdString ()); space= embedder.space_id ();
        if (vector.empty ()) throw std::runtime_error ("RAG query embedding failed.");
      }
      catch (const std::exception& e) { error= QString::fromUtf8 (e.what ()); }
      if (receiver) QMetaObject::invokeMethod (receiver, [receiver, control, retrieve, vector= std::move (vector), space, error] () mutable {
        if (receiver && !control->cancelled) retrieve (std::move (vector), space, error);
      }, Qt::QueuedConnection);
    });
  }
}

static QByteArray
longest_ascii_word (const QString& query) {
  QByteArray best;
  QByteArray current;
  for (QChar ch: query) {
    ushort code= ch.unicode ();
    bool word= (code >= '0' && code <= '9') ||
               (code >= 'A' && code <= 'Z') ||
               (code >= 'a' && code <= 'z') || code == '_';
    if (word) current.append ((char) code);
    else {
      if (current.size () > best.size ()) best= current;
      current.clear ();
    }
  }
  if (current.size () > best.size ()) best= current;
  return best;
}

VaultRawSearchPrefilter::VaultRawSearchPrefilter (
  const QString& query, bool case_insensitive, bool fuzzy)
  : caseInsensitive (case_insensitive)
{
  // Approximate structural matches need not preserve any particular query
  // token.  Parsing every file in fuzzy mode is therefore the only
  // false-negative-free policy for this raw-source prefilter.
  if (!fuzzy) needle= longest_ascii_word (query);
  if (caseInsensitive) needle= needle.toLower ();
}

bool
VaultRawSearchPrefilter::isEffective () const {
  return !needle.isEmpty ();
}

bool
VaultRawSearchPrefilter::fileMayMatch (url file) const {
  if (!isEffective ()) return true;
  return fileMayMatch (to_qstring (concretize (file)));
}

bool
VaultRawSearchPrefilter::fileMayMatch (const QString& file) const {
  if (!isEffective ()) return true;
  QFile input (file);
  if (!input.open (QIODevice::ReadOnly)) return true;
  QByteArray tail;
  while (!input.atEnd ()) {
    if (vault_search_cancelled ()) return false;
    QByteArray source= input.read (256 * 1024);
    if (input.error () != QFileDevice::NoError) return true;
    if (caseInsensitive) source= source.toLower ();
    source.prepend (tail);
    if (source.contains (needle)) return true;
    tail= source.right (needle.size () - 1);
  }
  return false;
}

static int
fuzzy_subsequence_score (const QString& text, const QString& query) {
  int qi= 0;
  int spread= 0;
  int first= -1;
  for (int i=0; i<text.length () && qi<query.length (); i++) {
    if (text[i] == query[qi]) {
      if (first < 0) first= i;
      spread= i - first;
      qi++;
    }
  }
  if (qi != query.length ()) return -1;
  return 50000 - (10 * spread) - text.length ();
}

int
fuzzy_score (const QString& text, const QString& query) {
  QString normalized= text.toLower ();
  if (query.isEmpty ()) return 0;
  if (normalized == query) return 100000;
  if (normalized.startsWith (query)) return 90000 - normalized.length ();
  if (normalized.contains (query)) return 80000 - normalized.length ();
  return fuzzy_subsequence_score (normalized, query);
}

int
list_filter_score (const QString& text, const QString& query,
                   bool caseInsensitive, bool fuzzy) {
  QString haystack= caseInsensitive ? text.toCaseFolded () : text;
  QString needle= caseInsensitive ? query.toCaseFolded () : query;
  if (needle.isEmpty ()) return 0;
  if (haystack == needle) return 100000;
  if (haystack.startsWith (needle)) return 90000 - haystack.length ();
  if (haystack.contains (needle)) return 80000 - haystack.length ();
  if (!fuzzy) return -1;
  return fuzzy_subsequence_score (haystack, needle);
}

int
fuzzy_file_score (const WikilinkFileEntry& file, string query) {
  array<fuzzy_rank_field> fields;
  fields << fuzzy_rank_field (file.searchStem, 100);
  fields << fuzzy_rank_field (file.searchPath, 35);
  fuzzy_rank_result result= fuzzy_rank (query, fields);
  return result.matched ? result.score : -1;
}

std::vector<VaultFileMatch>
rank_vault_link_files (
    const std::vector<WikilinkFileEntry>& files, string query) {
  std::vector<VaultFileMatch> matches;
  for (int i=0; i<(int) files.size (); ++i) {
    const int score= fuzzy_file_score (files[i], query);
    if (score >= 0) matches.push_back ({score, i});
  }
  const bool emptyQuery= query == "";
  std::sort (
    matches.begin (), matches.end (),
    [&] (const VaultFileMatch& a, const VaultFileMatch& b) {
      const WikilinkFileEntry& fa= files[a.index];
      const WikilinkFileEntry& fb= files[b.index];
      // Current-file bias is useful only for the unfiltered initial list.
      // Once the user types, relevance must be authoritative; otherwise an
      // approximate match in the current buffer steals selection from an
      // exact target and completion jumps back to the current file.
      if (emptyQuery && fa.isCurrent != fb.isCurrent) return fa.isCurrent;
      if (a.score != b.score) return a.score > b.score;
      if (fa.mtime != fb.mtime) return fa.mtime > fb.mtime;
      return fa.relPath < fb.relPath;
    });
  return matches;
}

namespace {

struct FuzzyAtomicText {
  std::u32string text;
  std::vector<int> starts;
  std::vector<int> ends;
};

static void
append_normalized_character (FuzzyAtomicText& out, const QString& value,
                             int byteStart, int byteEnd,
                             bool caseInsensitive) {
  QString normalized= caseInsensitive ? value.toCaseFolded () : value;
  QList<uint> codepoints= normalized.toUcs4 ();
  for (uint codepoint: codepoints) {
    out.text.push_back ((char32_t) codepoint);
    out.starts.push_back (byteStart);
    out.ends.push_back (byteEnd);
  }
}

static FuzzyAtomicText
normalize_atomic_text (const string& source, bool caseInsensitive) {
  FuzzyAtomicText out;
  for (int start=0; start<N(source); ) {
    if (vault_search_cancelled ()) break;
    // Vault source atoms are native UTF-8.  tm_char_next is the legacy
    // TeXmacs/Cork token scanner (one byte or <...>) and may split a Unicode
    // scalar; fuzzy ranges must remain valid editor UTF-8 byte positions.
    int end= utf8_grapheme_next (source, start);
    append_normalized_character (
      out, to_qstring (source (start, end)), start, end, caseInsensitive);
    start= end;
  }
  return out;
}

static std::u32string
normalize_query (const string& source, bool caseInsensitive) {
  return normalize_atomic_text (source, caseInsensitive).text;
}

static bool
same_atomic_path (path position, path atom) {
  return !is_nil (position) && path_up (position) == atom;
}

static std::vector<std::pair<size_t,size_t>>
unblocked_regions (const FuzzyAtomicText& text, path atom,
                   const std::vector<VaultContentMatch>& exact) {
  std::vector<std::pair<size_t,size_t>> blocked;
  for (const VaultContentMatch& match: exact) {
    if (!match.exact || !same_atomic_path (match.start, atom) ||
        !same_atomic_path (match.end, atom))
      continue;
    int byteStart= last_item (match.start);
    int byteEnd= last_item (match.end);
    size_t first= 0;
    while (first < text.ends.size () && text.ends[first] <= byteStart) first++;
    size_t last= first;
    while (last < text.starts.size () && text.starts[last] < byteEnd) last++;
    if (first < last) blocked.push_back ({first, last});
  }
  std::sort (blocked.begin (), blocked.end ());

  std::vector<std::pair<size_t,size_t>> regions;
  size_t cursor= 0;
  for (const auto& interval: blocked) {
    size_t first= std::max (cursor, interval.first);
    size_t last= std::max (first, interval.second);
    if (cursor < first) regions.push_back ({cursor, first});
    cursor= std::max (cursor, last);
  }
  if (cursor < text.text.size ()) regions.push_back ({cursor, text.text.size ()});
  return regions;
}

static void
append_fuzzy_atomic_matches (
  std::vector<VaultContentMatch>& out, const FuzzyAtomicText& text,
  const std::u32string& query, path atom, int limit, double cutoff,
  const rapidfuzz::fuzz::CachedPartialRatio<char32_t>& scorer,
  const std::vector<VaultContentMatch>& exact)
{
  if (limit <= 0 || text.text.empty ()) return;
  std::deque<std::pair<size_t,size_t>> pending;
  for (const auto& region: unblocked_regions (text, atom, exact))
    pending.push_back (region);

  while (!pending.empty () && limit > 0) {
    if (vault_search_cancelled ()) return;
    auto region= pending.front ();
    pending.pop_front ();
    if (region.second <= region.first) continue;

    auto first= text.text.begin () + (std::ptrdiff_t) region.first;
    auto last= text.text.begin () + (std::ptrdiff_t) region.second;
    double quick= scorer.similarity (first, last, cutoff);
    if (quick < cutoff) continue;
    rapidfuzz::ScoreAlignment<double> alignment=
      rapidfuzz::fuzz::partial_ratio_alignment (
        query.begin (), query.end (), first, last, cutoff);
    size_t alignedStart= region.first + alignment.dest_start;
    size_t radius= std::max<size_t> (
      1, (query.size () * (size_t) (100.0 - cutoff) + 99) / 100);
    size_t startFirst= alignedStart > radius ? alignedStart - radius : region.first;
    startFirst= std::max (startFirst, region.first);
    size_t startLast= std::min (region.second, alignedStart + radius + 1);
    size_t lengthFirst= query.size () > radius ? query.size () - radius : 1;
    size_t lengthLast= query.size () + radius;

    size_t matchStart= 0;
    size_t matchEnd= 0;
    double matchScore= 0.0;
    size_t bestLengthDifference= (size_t) -1;
    for (size_t candidateStart=startFirst; candidateStart<startLast;
         candidateStart++) {
      for (size_t length=lengthFirst; length<=lengthLast; length++) {
        size_t candidateEnd= candidateStart + length;
        if (candidateEnd > region.second) break;
        double score= 100.0 * rapidfuzz::levenshtein_normalized_similarity (
          query.begin (), query.end (),
          text.text.begin () + (std::ptrdiff_t) candidateStart,
          text.text.begin () + (std::ptrdiff_t) candidateEnd,
          rapidfuzz::LevenshteinWeightTable {1, 1, 1}, cutoff / 100.0);
        size_t difference= length > query.size () ? length - query.size () :
          query.size () - length;
        if (score > matchScore ||
            (score == matchScore && difference < bestLengthDifference)) {
          matchScore= score;
          matchStart= candidateStart;
          matchEnd= candidateEnd;
          bestLengthDifference= difference;
        }
      }
    }
    if (matchScore < cutoff || matchStart >= matchEnd ||
        matchEnd > text.text.size ())
      continue;

    VaultContentMatch match;
    match.start= atom * text.starts[matchStart];
    match.end= atom * text.ends[matchEnd - 1];
    match.exact= false;
    match.score= matchScore;
    out.push_back (match);
    limit--;

    if (region.first < matchStart)
      pending.push_back ({region.first, matchStart});
    if (matchEnd < region.second)
      pending.push_back ({matchEnd, region.second});
  }
}

static void
collect_fuzzy_atomic_matches (
  std::vector<VaultContentMatch>& out, tree t, path base,
  const std::u32string& query, int& remaining, double cutoff,
  bool caseInsensitive,
  const rapidfuzz::fuzz::CachedPartialRatio<char32_t>& scorer,
  const std::vector<VaultContentMatch>& exact)
{
  if (remaining <= 0 || vault_search_cancelled ()) return;
  if (is_atomic (t)) {
    size_t before= out.size ();
    append_fuzzy_atomic_matches (
      out, normalize_atomic_text (t->label, caseInsensitive), query, base,
      remaining, cutoff, scorer, exact);
    remaining -= (int) (out.size () - before);
    return;
  }
  if (is_func (t, RAW_DATA)) return;
  for (int i=0; i<N(t) && remaining>0; i++)
    if (!athena::math_v3::hidden_search_descriptor_slot (t, i))
      collect_fuzzy_atomic_matches (out, t[i], base * i, query, remaining,
                                    cutoff, caseInsensitive, scorer, exact);
}

} // namespace

static bool
math_match_boundary (tree t, path base, path position) {
  path relative= position / base;
  if (is_nil (relative)) return true;
  tree value= subtree (t, path_up (relative));
  if (!is_atomic (value)) return true;
  int wanted= last_item (relative), pos= 0;
  while (pos < wanted && pos < N(value->label))
    pos= math_word_end (value->label, pos);
  return pos == wanted;
}

void
append_content_matches (std::vector<VaultContentMatch>& out, tree t,
                        tree query, path base, int limit,
                        bool caseInsensitive, bool fuzzy) {
  if (limit <= 0 || vault_search_cancelled ()) return;
  // Keep the mathematical context but select the expression inside it,
  // rather than requiring the enclosing formula to equal the whole query.
  tree pattern= query;
  if (is_compound (query, "math", 1)) {
    pattern= compound ("math", compound ("select-region", query[0]));
    athena::node::copy_metadata (query, pattern);
  }
  range_set exactRanges= search (t, pattern, base, caseInsensitive, limit);
  std::vector<VaultContentMatch> exact;
  for (int i=0; i+1<N(exactRanges); i+=2) {
    if (is_compound (query, "math", 1) &&
        (!math_match_boundary (t, base, exactRanges[i]) ||
         !math_match_boundary (t, base, exactRanges[i+1]))) continue;
    VaultContentMatch match;
    match.start= exactRanges[i];
    match.end= exactRanges[i + 1];
    match.exact= true;
    match.score= 100.0;
    exact.push_back (match);
    out.push_back (match);
  }
  // A fuzzy text hit cannot satisfy an explicit typed property constraint.
  // Do not let the approximate path bypass the strict structured matcher.
  const auto* query_meta= athena::node::get (query);
  if (!fuzzy || !is_atomic (query) ||
      (query_meta && !query_meta->properties.empty ()) ||
      (int) exact.size () >= limit) return;

  static thread_local string cachedQuery;
  static thread_local bool cachedInsensitive= false;
  static thread_local std::u32string normalizedQuery;
  static thread_local std::unique_ptr<rapidfuzz::fuzz::CachedPartialRatio<char32_t>> scorer;
  if (!scorer || cachedQuery != query->label || cachedInsensitive != caseInsensitive) {
    cachedQuery= copy (query->label);
    cachedInsensitive= caseInsensitive;
    normalizedQuery= normalize_query (cachedQuery, caseInsensitive);
    scorer= std::make_unique<rapidfuzz::fuzz::CachedPartialRatio<char32_t>> (normalizedQuery);
  }
  if (normalizedQuery.size () < 4) return;
  double cutoff= normalizedQuery.size () <= 5 ? 75.0 : 80.0;
  int remaining= limit - (int) exact.size ();
  collect_fuzzy_atomic_matches (out, t, base, normalizedQuery, remaining,
                                cutoff, caseInsensitive, *scorer, exact);
}

void
collect_enunciation_matches (std::vector<VaultContentMatch>& out, tree t,
                             tree query, const string& tag, path base,
                             int limit, bool caseInsensitive, bool fuzzy) {
  if (limit <= 0 || is_atomic (t)) return;
  if (athena::enunciation::standard_registry ().matches_filter (
        t, std::string_view (tag.data (), N(tag)))) {
    append_content_matches (out, t, query, base, limit,
                            caseInsensitive, fuzzy);
    return;
  }
  size_t initialSize= out.size ();
  for (int i=0; i<N(t); i++) {
    int found= (int) (out.size () - initialSize);
    int remaining= limit - found;
    if (remaining <= 0) return;
    collect_enunciation_matches (out, t[i], query, tag, base * i, remaining,
                                  caseInsensitive, fuzzy);
  }
}

namespace {

static tree
subtree_at_path (tree t, path where) {
  while (!is_nil (where)) {
    int index= where->item;
    if (is_atomic (t) || index < 0 || index >= N(t)) return tree ();
    t= t[index];
    where= where->next;
  }
  return t;
}

static void
append_atomic_descendant_matches (
  std::vector<VaultContentMatch>& out, tree t, tree query, path base,
  int& remaining, bool caseInsensitive, bool fuzzy)
{
  if (remaining <= 0) return;
  if (is_atomic (t)) {
    size_t before= out.size ();
    append_content_matches (out, t, query, base, remaining,
                            caseInsensitive, fuzzy);
    remaining -= (int) (out.size () - before);
    return;
  }
  if (is_func (t, RAW_DATA)) return;
  for (int i=0; i<N(t) && remaining>0; i++)
    if (!athena::math_v3::hidden_search_descriptor_slot (t, i))
      append_atomic_descendant_matches (out, t[i], query, base * i, remaining,
                                        caseInsensitive, fuzzy);
}

static bool
same_match_range (const VaultContentMatch& a, const VaultContentMatch& b) {
  return a.start == b.start && a.end == b.end;
}

static bool
path_starts_with (path value, path prefix) {
  if (is_nil (prefix)) return true;
  if (is_nil (value) || value->item != prefix->item) return false;
  return path_starts_with (value->next, prefix->next);
}

static bool
formatting_wrapper (tree t) {
  return is_compound (t, "with") || is_compound (t, "style-with");
}

static bool
bold_wrapper (tree t) {
  if (is_compound (t, "strong")) return N(t) >= 1;
  if (!formatting_wrapper (t) || N(t) < 3) return false;
  for (int i=0; i+1<N(t)-1; i+=2)
    if (is_atomic (t[i]) && is_atomic (t[i+1]) &&
        (t[i]->label == "font-series" || t[i]->label == "fontseries") &&
        (t[i+1]->label == "bold" || t[i+1]->label == "bold-series"))
      return true;
  return false;
}

static bool
enunciation_node (tree t) {
  return athena::enunciation::standard_registry ().body_index (t) >= 0;
}

static bool
heading_node (tree t) {
  return is_compound (t, "section") || is_compound (t, "section*") ||
         is_compound (t, "subsection") || is_compound (t, "subsection*") ||
         is_compound (t, "subsubsection") ||
         is_compound (t, "subsubsection*") ||
         is_compound (t, "paragraph") || is_compound (t, "paragraph*") ||
         is_compound (t, "subparagraph") ||
         is_compound (t, "subparagraph*");
}

static bool
tree_has_visible_text (tree t) {
  if (is_atomic (t)) return !to_qstring (t->label).trimmed ().isEmpty ();
  if (is_func (t, LABEL) || is_func (t, RAW_DATA)) return false;
  for (int i=0; i<N(t); i++)
    if (!athena::math_v3::hidden_search_descriptor_slot (t, i) &&
        tree_has_visible_text (t[i])) return true;
  return false;
}

static QString
visible_tree_text (tree t) {
  if (is_atomic (t)) return to_qstring (t->label);
  if (is_func (t, LABEL) || is_func (t, RAW_DATA)) return QString ();
  QStringList parts;
  for (int i=0; i<N(t); i++) {
    if (athena::math_v3::hidden_search_descriptor_slot (t, i)) continue;
    QString part= visible_tree_text (t[i]).trimmed ();
    if (!part.isEmpty ()) parts << part;
  }
  return parts.join (" ").simplified ();
}

static bool
leading_bold_scope (tree t, path base, path& scope, tree& titleTree) {
  if (is_atomic (t)) return false;
  if (bold_wrapper (t)) {
    scope= base;
    titleTree= t;
    return true;
  }
  if (formatting_wrapper (t) && N(t) >= 1)
    return leading_bold_scope (t[N(t)-1], base * (N(t)-1), scope,
                               titleTree);
  for (int i=0; i<N(t); i++) {
    if (athena::math_v3::hidden_search_descriptor_slot (t, i)) continue;
    if (!tree_has_visible_text (t[i])) continue;
    return leading_bold_scope (t[i], base * i, scope, titleTree);
  }
  return false;
}

static bool
preferred_anchor_title (tree t, QString& title) {
  if (!is_func (t, LABEL, 1)) return false;
  QString anchor= to_qstring (tree_as_string (t[0])).trimmed ();
  QRegularExpressionMatch heading=
    QRegularExpression ("^H[1-6]\\s+(.+)$").match (anchor);
  if (heading.hasMatch ()) {
    title= heading.captured (1).trimmed ();
    return true;
  }
  if (!is_upper_anchor (anchor)) return false;
  QString tag= anchor_pair_tag (anchor);
  for (const WikilinkEnunciationFilterEntry& entry:
       wikilink_enunciation_filters ())
    if (tag == normalized_enunciation_tag (entry.tag)) {
      QString key= clean_anchor_display (anchor);
      int colon= key.indexOf (':');
      title= (colon < 0 ? key : key.mid (colon + 1)).trimmed ();
      return !title.isEmpty ();
    }
  return false;
}

struct PreferredSearchScope {
  path where;
  QString title;
};

static int
rapidfuzz_title_score (QString title, QString query, bool caseInsensitive) {
  title= title.simplified ();
  query= query.simplified ();
  if (caseInsensitive) {
    title= title.toCaseFolded ();
    query= query.toCaseFolded ();
  }
  if (title.isEmpty () || query.isEmpty ()) return -1;
  if (title == query) return 100000;
  std::u32string titleText= title.toStdU32String ();
  std::u32string queryText= query.toStdU32String ();
  return (int) std::lround (
    1000.0 * rapidfuzz::fuzz::WRatio (titleText, queryText));
}

static void
collect_preferred_scopes (tree t, path base,
                          std::vector<PreferredSearchScope>& scopes) {
  if (is_atomic (t)) return;
  QString anchorTitle;
  if (preferred_anchor_title (t, anchorTitle))
    scopes.push_back ({base, anchorTitle});
  if (heading_node (t))
    scopes.push_back ({base, visible_tree_text (t)});
  if (enunciation_node (t)) {
    path title;
    tree titleTree;
    if (leading_bold_scope (t, base, title, titleTree))
      scopes.push_back ({title, visible_tree_text (titleTree)});
  }
  for (int i=0; i<N(t); i++)
    collect_preferred_scopes (t[i], base * i, scopes);
}

} // namespace

void
append_heading_matches (std::vector<VaultContentMatch>& out, tree t,
                        tree query, path base, int limit,
                        bool caseInsensitive, bool fuzzy) {
  if (limit <= 0) return;
  std::vector<TransclusionAnchorPair> headings=
    collect_heading_anchor_targets (t, base);
  std::vector<VaultContentMatch> additions;
  int remaining= limit;
  for (const TransclusionAnchorPair& heading: headings) {
    if (remaining <= 0) break;
    path relative= heading.lowerWhere;
    for (int i=0; i<N(base) && !is_nil (relative); i++)
      relative= relative->next;
    tree node= subtree_at_path (t, relative);
    append_atomic_descendant_matches (additions, node, query,
                                      heading.lowerWhere, remaining,
                                      caseInsensitive, fuzzy);
  }
  for (const VaultContentMatch& addition: additions) {
    bool duplicate= false;
    for (const VaultContentMatch& current: out)
      if (same_match_range (current, addition)) {
        duplicate= true;
        break;
      }
    if (!duplicate) out.push_back (addition);
  }
}

void
score_search_match_titles (
  tree t, tree query, path base, std::vector<VaultContentMatch>& matches,
  bool caseInsensitive, bool) {
  std::vector<PreferredSearchScope> scopes;
  collect_preferred_scopes (t, base, scopes);
  QString queryText= to_qstring (tree_as_string (query)).simplified ();
  for (VaultContentMatch& match: matches)
    for (const PreferredSearchScope& scope: scopes)
      if (path_starts_with (match.start, scope.where)) {
        int score= rapidfuzz_title_score (scope.title, queryText,
                                          caseInsensitive);
        match.titleMatchScore= std::max (match.titleMatchScore, score);
      }
}

bool
vault_search_match_precedes (const VaultContentMatch& a,
                             const VaultContentMatch& b) {
  if (a.titleMatchScore != b.titleMatchScore)
    return a.titleMatchScore > b.titleMatchScore;
  if (a.exact != b.exact) return a.exact;
  if (!a.exact && a.score != b.score) return a.score > b.score;
  return path_less (a.start, b.start);
}
