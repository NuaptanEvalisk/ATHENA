/******************************************************************************
* MODULE     : artifact_radioactive_links.cpp
* DESCRIPTION: Fast automatic links to stable semantic artifacts
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
*******************************************************************************/

#include "ATHENA/Data/artifact_radioactive_links.hpp"

#include "ATHENA/Data/artifact_title_filter.hpp"
#include "ATHENA/Data/node_reference_export.hpp"
#include "ATHENA/Data/vault.hpp"
#include "ATHENA/Data/vaultfile_json.hpp"
#include "analyze.hpp"
#include "convert.hpp"
#include "Xml/athena_document_xml.hpp"
#include "message.hpp"
#include "node_metadata.hpp"
#include "unicode_text.hpp"

#include <QCryptographicHash>
#include <QHash>
#include <QRegularExpression>
#include <QString>
#include <QVector>
#include <QSet>

extern "C" {
#include "api.h"
#include "stem_UTF_8_english.h"
}

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <deque>
#include <tuple>

namespace fs= std::filesystem;

namespace {

constexpr int maximum_term_tokens= 12;
constexpr int maximum_term_characters= 160;

struct Token {
  QString key;
  int start= 0;
  int end= 0;
};

struct TrieNode {
  QHash<QString,int> children;
  std::vector<std::string> uuids;
  std::string key;
};

struct RadioactiveIndex {
  std::uint64_t matching_revision= 0;
  std::string vault_root;
  std::vector<TrieNode> nodes= {TrieNode ()};
  std::vector<int> free_nodes;
  std::unordered_map<std::string,AthenaArtifactRecord> records;
  std::unordered_map<std::string,std::vector<std::string>> records_by_path;
  std::unordered_map<std::string,std::vector<std::string>> records_by_key;
  std::unordered_map<std::string,std::vector<std::string>> record_keys;
  struct Name {
    std::vector<Token> tokens;
    std::string display;
    std::string fragment;
  };
  std::unordered_map<std::string,Name> names;
  AthenaArtifactTitleFilter title_filter;
  bool has_title_filter= false;
  bool has_structured_names= false;
  std::shared_ptr<const RadioactiveIndex> base;
  std::unordered_map<std::string, std::shared_ptr<const RadioactiveIndex>> overlays;
  std::unordered_set<std::string> live_paths;
  QHash<QString, std::vector<const RadioactiveIndex*>> overlay_heads;
};

struct TextProjection {
  QString text;
  QVector<int> source_boundary;
};

struct MatchChoice {
  int length= 0;
  std::vector<std::string> uuids;
  std::string key;
};

struct MatchPart {
  std::string source;
  TextProjection projection;
  std::vector<Token> tokens;
  QString structure;
};

struct MatchQuery {
  bool initialized= false, structured= false, visited= false;
  std::uint64_t revision= 0;
  QString text;
  std::vector<Token> tokens;
  std::vector<std::pair<path,path>> positions;
  QSet<QString> heads;
  std::vector<std::shared_ptr<const MatchChoice>> choices;
  std::vector<AthenaArtifactRadioactiveMatch> selected;
  std::vector<MatchPart> parts;
};
}

struct AthenaArtifactRadioactiveDependencies {
  std::unordered_map<std::string, MatchQuery> queries;
  std::uint64_t revision= 0;
  std::uint64_t generation= 0;
  AthenaArtifactRadioactiveWork work;
};

namespace {

std::shared_ptr<const RadioactiveIndex> cached_index;
std::mutex index_build_mutex;
std::atomic<std::uint64_t> requested_revision {1};
std::uint64_t loaded_revision= 0;
std::string loaded_incarnation;
std::shared_ptr<const RadioactiveIndex> disk_index;
struct Overlay {
  std::uint64_t owner;
  std::shared_ptr<const RadioactiveIndex> index;
};
std::unordered_map<std::string, Overlay> overlays;
std::unordered_map<std::string, std::shared_ptr<const RadioactiveIndex>> saved_overlays;
std::uint64_t saved_epoch= 0;
std::atomic<std::uint64_t> matching_revision {0};
std::deque<std::pair<std::uint64_t, QSet<QString>>> matching_changes;
thread_local std::shared_ptr<AthenaArtifactRadioactiveDependencies>* queried_block= nullptr;
thread_local const void* queried_owner= nullptr;

void note_matching_changes (const RadioactiveIndex* before, const RadioactiveIndex* after) {
  QSet<QString> heads;
  auto inspect= [&] (const RadioactiveIndex* a, const RadioactiveIndex* b) {
    if (!a) return;
    for (const auto& entry: a->records_by_key) {
      if (b) {
        auto other= b->records_by_key.find (entry.first);
        if (other != b->records_by_key.end () && other->second == entry.second) continue;
      }
      auto name= a->names.find (entry.first);
      if (name != a->names.end () && !name->second.tokens.empty ())
        heads.insert (name->second.tokens.front ().key);
    }
  };
  inspect (before, after); inspect (after, before);
  if (heads.empty ()) return;
  matching_changes.emplace_back (++matching_revision, std::move (heads));
  if (matching_changes.size () > 128) matching_changes.pop_front ();
}

const AthenaArtifactRecord* lookup (const RadioactiveIndex& index,
                                  const std::string& uuid) {
  auto own= index.records.find (uuid);
  if (own != index.records.end ()) return &own->second;
  for (const auto& entry: index.overlays) {
    auto found= entry.second->records.find (uuid);
    if (found != entry.second->records.end ()) return &found->second;
  }
  if (index.base) {
    auto found= index.base->records.find (uuid);
    if (found != index.base->records.end () &&
        !index.overlays.count (found->second.relative_path)) return &found->second;
  }
  return nullptr;
}

void publish_index () {
  auto next= std::make_shared<RadioactiveIndex> ();
  next->matching_revision= matching_revision.load (std::memory_order_relaxed);
  next->base= disk_index;
  if (disk_index) {
    next->vault_root= disk_index->vault_root;
    next->title_filter= disk_index->title_filter;
    next->has_title_filter= disk_index->has_title_filter;
    next->has_structured_names= disk_index->has_structured_names;
  }
  // Only live-document indices are visited here, never the vault-wide trie.
  next->overlays= saved_overlays;
  for (const auto& entry: overlays) {
    next->overlays[entry.first]= entry.second.index;
    next->live_paths.insert (entry.first);
  }
  for (const auto& entry: next->overlays) {
    const auto& layer= entry.second;
    next->has_structured_names |= layer->has_structured_names;
    for (auto i= layer->nodes[0].children.constBegin ();
         i != layer->nodes[0].children.constEnd (); ++i)
      next->overlay_heads[i.key ()].push_back (layer.get ());
  }
  std::atomic_store_explicit (&cached_index,
    std::shared_ptr<const RadioactiveIndex> (std::move (next)), std::memory_order_release);
}

std::string to_std (string value) {
  return std::string (as_charp (value), (size_t) N(value));
}

QString qstring_from_utf8 (const std::string& bytes) {
  athena::text::require_utf8 (bytes);
  return QString::fromUtf8 (bytes.data (), (qsizetype) bytes.size ());
}

class EnglishStemmer {
public:
  EnglishStemmer (): environment (english_UTF_8_create_env ()) {}
  ~EnglishStemmer () { english_UTF_8_close_env (environment); }

  QString stem (const QString& word) {
    if (!environment) return word;
    QByteArray bytes= word.toUtf8 ();
    if (SN_set_current (environment, bytes.size (),
                        reinterpret_cast<const symbol*> (bytes.constData ())) < 0 ||
        english_UTF_8_stem (environment) < 0)
      return word;
    return QString::fromUtf8 (
      reinterpret_cast<const char*> (environment->p), environment->l);
  }

private:
  SN_env* environment;
};

QString normalize_word (QString word) {
  word= word.normalized (QString::NormalizationForm_C).toCaseFolded ();
  bool english= !word.isEmpty ();
  bool has_letter= false;
  for (QChar character: word) {
    ushort code= character.unicode ();
    if (code >= 'a' && code <= 'z') has_letter= true;
    else if (code != '\'' && code != 0x2019) { english= false; break; }
  }
  if (!english || !has_letter) return word;
  static thread_local EnglishStemmer stemmer;
  word.replace (QChar (0x2019), QChar ('\''));
  if (word.endsWith (QStringLiteral ("'s"))) word.chop (2);
  else if (word.endsWith ('\'')) word.chop (1);
  word= stemmer.stem (word);

  // Classical mathematical eponyms routinely alternate between a surname
  // and its -ian adjective: Euler/Eulerian, Artin/Artinian,
  // Gauss/Gaussian, Lagrange/Lagrangian, and so on.  Treat this as a lexical
  // inflection beside Snowball, rather than maintaining an incomplete name
  // list.  Requiring a four-letter base avoids ordinary words such as median.
  if (word.endsWith (QStringLiteral ("ian")) && word.size () >= 7) {
    QString base= word.left (word.size () - 3);
    if (base.size () >= 4) word= stemmer.stem (base);
  }
  return word;
}

const QRegularExpression& token_expression () {
  static const QRegularExpression expression (
    QStringLiteral (R"([\p{L}\p{N}]+(?:['\x{2019}][\p{L}\p{N}]+)*|[^\p{L}\p{N}\s]+)"),
    QRegularExpression::UseUnicodePropertiesOption);
  return expression;
}

std::vector<Token> tokenize (const QString& text) {
  std::vector<Token> tokens;
  auto matches= token_expression ().globalMatch (text);
  while (matches.hasNext ()) {
    QRegularExpressionMatch match= matches.next ();
    QString token= match.captured ();
    bool word= !token.isEmpty () && token.front ().isLetterOrNumber ();
    tokens.push_back ({word ? normalize_word (token)
                            : token.normalized (QString::NormalizationForm_C),
                       (int) match.capturedStart (),
                       (int) match.capturedEnd ()});
  }
  return tokens;
}

std::string token_key (const std::vector<Token>& tokens) {
  QCryptographicHash digest (QCryptographicHash::Sha256);
  for (const Token& token: tokens) {
    QByteArray bytes= token.key.toUtf8 ();
    digest.addData (bytes);
    digest.addData (QByteArrayView ("\x1f", 1));
  }
  return digest.result ().toHex ().toStdString ();
}

std::vector<QString> artifact_terms (
  const AthenaArtifactRecord& record,
  const AthenaArtifactTitleFilter* filter= nullptr) {
  if (record.type == "completion") return {};
  std::vector<QString> terms;
  terms.reserve (record.semantic_names.size ());
  for (const std::string& name: record.semantic_names) {
    QString term= qstring_from_utf8 (name).simplified ();
    QByteArray utf8= term.toUtf8 ();
    if (!term.isEmpty () &&
        (filter == nullptr ||
         !athena_artifact_title_filter_contains (
           *filter, std::string (utf8.constData (), (size_t) utf8.size ()))))
      terms.push_back (term);
  }
  return terms;
}

QString artifact_term (const AthenaArtifactRecord& record) {
  std::vector<QString> terms= artifact_terms (record);
  return terms.empty () ? QString () : terms.front ();
}

void add_term (RadioactiveIndex& index, const std::vector<Token>& tokens,
               const std::string& uuid) {
  if (uuid.empty ()) return;
  if (tokens.empty () || tokens.size () > maximum_term_tokens) return;
  if (tokens.size () == 1 && tokens[0].key.size () < 3 &&
      tokens[0].key.front ().unicode () < 128)
    return;
  int node= 0;
  for (const Token& token: tokens) {
    auto child= index.nodes[(size_t) node].children.constFind (token.key);
    if (child == index.nodes[(size_t) node].children.constEnd ()) {
      int next;
      if (index.free_nodes.empty ()) {
        next= int (index.nodes.size ());
        index.nodes.emplace_back ();
      }
      else {
        next= index.free_nodes.back ();
        index.free_nodes.pop_back ();
      }
      index.nodes[(size_t) node].children.insert (token.key, next);
      node= next;
    }
    else node= child.value ();
  }
  TrieNode& terminal= index.nodes[(size_t) node];
  if (terminal.key.empty ()) terminal.key= token_key (tokens);
  if (std::find (terminal.uuids.begin (), terminal.uuids.end (), uuid) ==
      terminal.uuids.end ()) terminal.uuids.push_back (uuid);
}

// Hash native structure directly, without copying trees or storing actor-owned
// nodes in the immutable cross-thread index. Length framing preserves identity.
void hash_structure (QCryptographicHash& hash, const tree& value) {
  QByteArray header= is_atomic (value) ? QByteArray ("s") : QByteArray ("t");
  string label= is_atomic (value) ? value->label : as_string (L(value));
  header += QByteArray::number (N(label)) + ':';
  hash.addData (header);
  hash.addData (QByteArrayView (as_charp (label), N(label)));
  if (is_compound (value)) {
    hash.addData (QByteArray::number (N(value)) + ':');
    for (int i=0; i<N(value); ++i) hash_structure (hash, value[i]);
  }
}

QString structure_key (const tree& value) {
  QCryptographicHash hash (QCryptographicHash::Sha256);
  hash_structure (hash, value);
  return QString (QChar (0)) + QString::fromLatin1 (hash.result ().toHex ());
}

std::vector<Token> tree_tokens (tree name) {
  if (is_func (name, DOCUMENT, 1)) name= name[0];
  std::vector<Token> result;
  auto append= [&] (const tree& part) {
    if (is_atomic (part)) {
      auto tokens= tokenize (qstring_from_utf8 (to_std (part->label)));
      result.insert (result.end (), tokens.begin (), tokens.end ());
    }
    else result.push_back ({structure_key (part), 0, 0});
  };
  if (is_func (name, CONCAT))
    for (int j=0; j<N(name); ++j) append (name[j]);
  else append (name);
  return result;
}

tree read_name_fragment (const std::string& bytes) {
  using namespace athena::document;
  switch (inspect_xml_version (bytes)) {
    case xml_format_version::v1: return read_xml (bytes, xml_kind::fragment);
    case xml_format_version::v2: return read_xml_v2 (bytes, xml_kind::fragment);
    case xml_format_version::v3: return read_xml_v3 (bytes, xml_kind::fragment);
  }
  throw std::runtime_error ("Unsupported Artifact name format");
}

std::vector<Token> name_tokens (const AthenaArtifactRecord& record, size_t i) {
  if (i >= record.semantic_name_trees.size () || record.semantic_name_trees[i].empty ())
    return tokenize (qstring_from_utf8 (record.semantic_names[i]));
  const std::string& bytes= record.semantic_name_trees[i];
  return tree_tokens (read_name_fragment (bytes));
}

void add_record (RadioactiveIndex* index, const AthenaArtifactRecord& record,
                 const AthenaArtifactTitleFilter* filter) {
    index->records.emplace (record.uuid, record);
    index->records_by_path[record.relative_path].push_back (record.uuid);
    if (record.type == "completion") return;
    for (size_t i=0; i<record.semantic_names.size (); ++i) {
      QString term= qstring_from_utf8 (record.semantic_names[i]).simplified ();
      std::string fragment= i < record.semantic_name_trees.size () ?
        record.semantic_name_trees[i] : std::string ();
      tree name= fragment.empty () ? tree (string (record.semantic_names[i].c_str ())) :
        read_name_fragment (fragment);
      if (term.isEmpty () || term.size () > maximum_term_characters ||
          (filter && athena_artifact_title_filter_contains (*filter, name)))
        continue;
      auto tokens= tree_tokens (name);
      add_term (*index, tokens, record.uuid);
      for (const Token& token: tokens)
        if (token.key.startsWith (QChar (0))) index->has_structured_names= true;
      std::string key= token_key (tokens);
      index->record_keys[record.uuid].push_back (key);
      if (fragment.empty ()) fragment= athena::document::write_xml (
        name, athena::document::xml_kind::fragment);
      index->names.emplace (key, RadioactiveIndex::Name {
        std::move (tokens), term.toStdString (), std::move (fragment)});
      std::vector<std::string>& matches= index->records_by_key[key];
      if (std::find (matches.begin (), matches.end (), record.uuid) ==
          matches.end ())
        matches.push_back (record.uuid);
    }
}

void remove_record (RadioactiveIndex& index, const AthenaArtifactRecord& record) {
  auto erase_id= [&] (auto& ids) {
    ids.erase (std::remove (ids.begin (), ids.end (), record.uuid), ids.end ());
  };
  erase_id (index.records_by_path[record.relative_path]);
  if (index.records_by_path[record.relative_path].empty ())
    index.records_by_path.erase (record.relative_path);
  for (const auto& key: index.record_keys[record.uuid]) {
    auto name= index.names.find (key);
    if (name == index.names.end ()) continue;
    int node= 0;
    std::vector<std::pair<int, QString>> trail;
    for (const auto& token: name->second.tokens) {
      auto child= index.nodes[node].children.constFind (token.key);
      if (child == index.nodes[node].children.constEnd ()) { node= -1; break; }
      trail.push_back ({node, token.key});
      node= child.value ();
    }
    if (node >= 0) {
      erase_id (index.nodes[node].uuids);
      for (auto step= trail.rbegin (); step != trail.rend (); ++step) {
        if (!index.nodes[node].uuids.empty () || !index.nodes[node].children.empty ()) break;
        index.nodes[node]= TrieNode ();
        index.free_nodes.push_back (node);
        index.nodes[step->first].children.remove (step->second);
        node= step->first;
      }
    }
    auto& ids= index.records_by_key[key];
    erase_id (ids);
    if (ids.empty ()) {
      index.records_by_key.erase (key);
      index.names.erase (key);
    }
  }
  index.record_keys.erase (record.uuid);
  index.records.erase (record.uuid);
}

std::shared_ptr<const RadioactiveIndex> build_index (
  const std::vector<AthenaArtifactRecord>& records,
  const std::string& vault_root= {},
  const AthenaArtifactTitleFilter* filter= nullptr,
  const RadioactiveIndex* previous= nullptr) {
  const bool reusable= previous && previous->has_title_filter == (filter != nullptr) &&
    (!filter || (previous->title_filter.normalized == filter->normalized &&
                 previous->title_filter.structured == filter->structured));
  auto index= reusable ? std::make_shared<RadioactiveIndex> (*previous) :
                        std::make_shared<RadioactiveIndex> ();
  index->vault_root= vault_root;
  if (filter != nullptr) {
    index->title_filter= *filter;
    index->has_title_filter= true;
  }
  if (reusable) {
    std::unordered_set<std::string> present;
    for (const auto& record: records) present.insert (record.uuid);
    for (const auto& entry: previous->records)
      if (!present.count (entry.first)) remove_record (*index, entry.second);
  }
  for (const auto& record: records) {
    auto old= index->records.find (record.uuid);
    if (old != index->records.end ()) {
      if (old->second.type == record.type &&
          old->second.relative_path == record.relative_path &&
          old->second.semantic_names == record.semantic_names &&
          old->second.semantic_name_trees == record.semantic_name_trees) {
        old->second= record;
        continue;
      }
      const auto removed= old->second;
      remove_record (*index, removed);
    }
    add_record (index.get (), record, filter);
  }
  return index;
}

bool contains_tokens (const std::vector<Token>& name,
                      const std::vector<Token>& query) {
  if (query.empty () || name.size () < query.size ()) return false;
  for (size_t start=0; start + query.size () <= name.size (); ++start) {
    bool matches= true;
    for (size_t i=0; i<query.size (); ++i) {
      const QString& haystack= name[start+i].key;
      const QString& needle= query[i].key;
      // Structural tokens are opaque identities, never substrings of hashes.
      bool same= haystack == needle;
      if (!same && !haystack.startsWith (QChar (0)) &&
          !needle.startsWith (QChar (0))) {
        if (query.size () == 1) same= haystack.contains (needle);
        else if (i == 0) same= haystack.endsWith (needle);
        else if (i+1 == query.size ()) same= haystack.startsWith (needle);
      }
      if (!same) { matches= false; break; }
    }
    if (matches) return true;
  }
  return false;
}

AthenaArtifactNameResolution resolve_tokens (
  const RadioactiveIndex& index, const std::vector<Token>& query,
  const std::string& display) {
  AthenaArtifactNameResolution result;
  result.query= display;
  if (query.empty ()) return result;
  if (index.base) {
    std::unordered_set<std::string> seen;
    auto append= [&] (const RadioactiveIndex& layer, bool base) {
      auto part= resolve_tokens (layer, query, display);
      auto collect= [&] (const auto& from, auto& into) {
        for (const auto& record: from)
          if ((!base || !index.overlays.count (record.relative_path)) &&
              seen.insert (record.uuid).second) into.push_back (record);
      };
      collect (part.exact, result.exact);
      collect (part.partial, result.partial);
    };
    append (*index.base, true);
    for (const auto& entry: index.overlays) append (*entry.second, false);
    auto less= [] (const auto& a, const auto& b) {
      return std::tie (a.semantic_names, a.relative_path, a.uuid) <
             std::tie (b.semantic_names, b.relative_path, b.uuid);
    };
    std::sort (result.exact.begin (), result.exact.end (), less);
    std::sort (result.partial.begin (), result.partial.end (), less);
    return result;
  }
  std::unordered_set<std::string> exact, partial;
  auto full= index.records_by_key.find (token_key (query));
  if (full != index.records_by_key.end ())
    exact.insert (full->second.begin (), full->second.end ());
  for (const auto& entry: index.names) {
    if (!contains_tokens (entry.second.tokens, query)) continue;
    for (const auto& uuid: index.records_by_key.at (entry.first))
      if (!exact.count (uuid)) partial.insert (uuid);
  }
  auto collect= [&] (const auto& ids, auto& records) {
    for (const auto& id: ids) {
      auto found= index.records.find (id);
      if (found != index.records.end ()) records.push_back (found->second);
    }
    std::sort (records.begin (), records.end (), [] (const auto& a, const auto& b) {
      if (a.semantic_names != b.semantic_names) return a.semantic_names < b.semantic_names;
      if (a.relative_path != b.relative_path) return a.relative_path < b.relative_path;
      return a.uuid < b.uuid;
    });
  };
  collect (exact, result.exact);
  collect (partial, result.partial);
  return result;
}

TextProjection project_text (string source) {
  TextProjection projection;
  athena::text::require_utf8 (
    std::string_view (source.data (), static_cast<std::size_t> (N(source))));
  const bool ascii= std::all_of (source.data (), source.data () + N(source),
    [] (char c) { return static_cast<unsigned char> (c) < 0x80; });
  if (ascii) {
    projection.text= QString::fromLatin1 (as_charp (source), N(source));
    projection.source_boundary.resize (N(source) + 1);
    for (int i=0; i<=N(source); i++) projection.source_boundary[i]= i;
    return projection;
  }
  projection.source_boundary.push_back (0);
  int position= 0;
  while (position < N(source)) {
    const auto bytes= std::string_view (
      source.data (), static_cast<std::size_t> (N(source)));
    int next= static_cast<int> (
      athena::text::next_scalar (bytes, static_cast<std::size_t> (position)));
    string utf8= source (position, next);
    QString character= QString::fromUtf8 (as_charp (utf8), N(utf8));
    if (character.isEmpty ()) {
      projection.source_boundary.back ()= next;
      position= next;
      continue;
    }
    projection.text += character;
    for (qsizetype i=1; i<character.size (); i++)
      projection.source_boundary.push_back (position);
    projection.source_boundary.push_back (next);
    position= next;
  }
  return projection;
}

QString tree_display_text (const tree& value) {
  if (is_atomic (value)) return qstring_from_utf8 (to_std (value->label));
  if (is_func (value, NAMED_SYMBOL, 1) && is_atomic (value[0])) {
    std::string identity= to_std (value[0]->label);
    constexpr std::string_view prefix= "texmacs:";
    if (identity.size () >= prefix.size () &&
        std::string_view (identity).substr (0, prefix.size ()) == prefix)
      identity.erase (0, prefix.size ());
    return qstring_from_utf8 (identity);
  }
  QString result;
  for (int i=0; i<N(value); ++i) {
    QString child= tree_display_text (value[i]);
    if (child.isEmpty ()) continue;
    if (!result.isEmpty () && !is_func (value, CONCAT)) result += QLatin1Char (' ');
    result += child;
  }
  return result.simplified ();
}

std::shared_ptr<const MatchChoice> match_at (
  const RadioactiveIndex& index, const std::vector<Token>& tokens,
  const QString& text, size_t start) {
    int best_end= -1;
    std::vector<std::string> best_uuids;
    std::string best_key;
    size_t limit= std::min (tokens.size (), start + maximum_term_tokens);
    auto search= [&] (const RadioactiveIndex& layer, bool base) {
    int node= 0;
    for (size_t position=start; position<limit; position++) {
      auto child= layer.nodes[(size_t) node].children.constFind (
        tokens[position].key);
      if (child == layer.nodes[(size_t) node].children.constEnd ()) break;
      node= child.value ();
      const TrieNode& candidate= layer.nodes[(size_t) node];
      if (!candidate.uuids.empty ()) {
        QString surface= text.mid (
          tokens[start].start, tokens[position].end - tokens[start].start);
        QByteArray utf8= surface.toUtf8 ();
        if (index.has_title_filter &&
            athena_artifact_title_filter_contains (
              index.title_filter,
              std::string (utf8.constData (), (size_t) utf8.size ())))
          continue;
        std::vector<std::string> ids;
        for (const auto& uuid: candidate.uuids) {
          auto record= layer.records.find (uuid);
          if (!base || (record != layer.records.end () &&
              !index.overlays.count (record->second.relative_path))) ids.push_back (uuid);
        }
        // Mask before choosing the longest match: a removed long name must
        // not hide an otherwise valid shorter match from another document.
        if (ids.empty () || (int) position < best_end) continue;
        if ((int) position > best_end) {
          best_end= (int) position; best_uuids.clear (); best_key= candidate.key;
        }
        for (const auto& uuid: ids)
          if (std::find (best_uuids.begin (), best_uuids.end (), uuid) == best_uuids.end ())
            best_uuids.push_back (uuid);
      }
    }
    };
    if (index.base) {
      search (*index.base, true);
      auto heads= index.overlay_heads.constFind (tokens[start].key);
      if (heads != index.overlay_heads.constEnd ())
        for (const auto* layer: heads.value ()) search (*layer, false);
    }
    else search (index, false);
    if (best_end < 0) return {};
    std::sort (best_uuids.begin (), best_uuids.end ());
    return std::make_shared<const MatchChoice> (MatchChoice {
      best_end - int (start) + 1, std::move (best_uuids), std::move (best_key)});
}

std::vector<AthenaArtifactRadioactiveMatch> match_tokens (
  const RadioactiveIndex& index, const std::vector<Token>& tokens,
  const QString& text) {
  std::vector<AthenaArtifactRadioactiveMatch> result;
  for (size_t start=0; start<tokens.size (); ) {
    auto choice= match_at (index, tokens, text, start);
    if (!choice) { ++start; continue; }
    result.push_back ({int (start), int (start) + choice->length,
                      choice->uuids, choice->key});
    start += choice->length;
  }
  return result;
}

std::vector<AthenaArtifactRadioactiveMatch> match_index (
  const RadioactiveIndex& index, string source) {
  if (N(source) == 0) return {};
  TextProjection projection= project_text (source);
  auto tokens= tokenize (projection.text);
  auto result= match_tokens (index, tokens, projection.text);
  for (auto& match: result) {
    match.start= projection.source_boundary[tokens[match.start].start];
    match.end= projection.source_boundary[tokens[match.end-1].end];
  }
  return result;
}

std::vector<AthenaArtifactRadioactiveTreeMatch> match_tree (
  const RadioactiveIndex& index, const tree& source) {
  if (!index.has_structured_names || !is_func (source, CONCAT)) return {};
  std::vector<Token> tokens;
  std::vector<std::pair<path,path>> positions;
  QString text;
  for (int i=0; i<N(source); ++i) {
    int offset= text.size ();
    if (is_atomic (source[i])) {
      TextProjection part= project_text (source[i]->label);
      for (Token token: tokenize (part.text)) {
        positions.push_back ({path (i, part.source_boundary[token.start]),
                              path (i, part.source_boundary[token.end])});
        token.start += offset;
        token.end += offset;
        tokens.push_back (token);
      }
      text += part.text;
    }
    else {
      tokens.push_back ({structure_key (source[i]), offset, offset+1});
      positions.push_back ({path (i, 0), path (i, 1)});
      text += QChar (0xfffc);
    }
  }
  std::vector<AthenaArtifactRadioactiveTreeMatch> result;
  for (const auto& match: match_tokens (index, tokens, text)) {
    bool structured= false;
    for (int i=match.start; i<match.end; ++i)
      structured |= tokens[i].key.startsWith (QChar (0));
    if (structured)
      result.push_back ({positions[match.start].first,
                        positions[match.end-1].second, match});
  }
  return result;
}

std::vector<Token> update_tokens (const QString& before,
    const std::vector<Token>& previous, const QString& text,
    AthenaArtifactRadioactiveWork& work) {
  if (before == text) return previous;
  int first= 0, old_end= before.size (), new_end= text.size ();
  while (first < old_end && first < new_end && before[first] == text[first]) ++first;
  while (old_end > first && new_end > first && before[old_end-1] == text[new_end-1]) {
    --old_end; --new_end;
  }
  // Tokens cannot cross whitespace. Expand to those boundaries so apostrophes,
  // punctuation runs and UTF-16 surrogate pairs use the unchanged tokenizer.
  while (first > 0 && !before[first-1].isSpace ()) --first;
  const int delta= text.size () - before.size ();
  while (old_end < before.size () && !before[old_end].isSpace ()) ++old_end;
  new_end= old_end + delta;
  std::vector<Token> result;
  for (const auto& token: previous)
    if (token.end <= first) result.push_back (token);
  work.tokenized_characters += new_end - first;
  for (auto token: tokenize (text.mid (first, new_end-first))) {
    token.start += first; token.end += first;
    result.push_back (std::move (token));
  }
  for (auto token: previous)
    if (token.start >= old_end) {
      token.start += delta; token.end += delta;
      result.push_back (std::move (token));
    }
  return result;
}

QSet<QString> changed_heads (std::uint64_t before, std::uint64_t after) {
  QSet<QString> result;
  if (before == after) return result;
  std::lock_guard<std::mutex> guard (index_build_mutex);
  if (before > after || matching_changes.empty () || before + 1 < matching_changes.front ().first)
    result.insert (QString ()); // The bounded journal no longer covers this view.
  for (const auto& change: matching_changes)
    if (change.first > before && change.first <= after) result.unite (change.second);
  return result;
}

bool same_matches (const std::vector<AthenaArtifactRadioactiveMatch>& a,
                   const std::vector<AthenaArtifactRadioactiveMatch>& b) {
  if (a.size () != b.size ()) return false;
  for (size_t i=0; i<a.size (); ++i)
    if (a[i].start != b[i].start || a[i].end != b[i].end ||
        a[i].uuids != b[i].uuids || a[i].disambiguation_key != b[i].disambiguation_key)
      return false;
  return true;
}

bool update_query (MatchQuery& query, const RadioactiveIndex& index,
    QString text, std::vector<Token> tokens, AthenaArtifactRadioactiveWork& work) {
  const auto heads= changed_heads (query.revision, index.matching_revision);
  const int old_count= query.tokens.size (), count= tokens.size ();
  int first= 0, suffix= 0;
  // Include surface text and gaps: blacklists apply to surface forms, not only
  // stems. A whitespace-only edit may change a filtered multiword candidate.
  while (first < old_count && first < count) {
    int old_start= first ? query.tokens[first-1].end : 0;
    int new_start= first ? tokens[first-1].end : 0;
    if (query.tokens[first].key != tokens[first].key ||
        query.text.mid (old_start, query.tokens[first].end-old_start) !=
        text.mid (new_start, tokens[first].end-new_start)) break;
    ++first;
  }
  while (suffix < old_count-first && suffix < count-first) {
    int oi= old_count-suffix-1, ni= count-suffix-1;
    int oe= suffix ? query.tokens[oi+1].start : query.text.size ();
    int ne= suffix ? tokens[ni+1].start : text.size ();
    if (query.tokens[oi].key != tokens[ni].key ||
        query.text.mid (query.tokens[oi].start, oe-query.tokens[oi].start) !=
        text.mid (tokens[ni].start, ne-tokens[ni].start)) break;
    ++suffix;
  }
  const bool edited= !query.initialized || first != count || first != old_count;
  std::vector<std::shared_ptr<const MatchChoice>> choices (count);
  for (int i=0; i<first; ++i) choices[i]= query.choices[i];
  for (int i=0; i<suffix; ++i) choices[count-i-1]= query.choices[old_count-i-1];
  const int dirty_start= std::max (0, first-maximum_term_tokens+1);
  bool recomputed= false;
  for (int i=0; i<count; ++i)
    if (!query.initialized || (edited && i >= dirty_start && i < count-suffix) ||
        heads.contains (QString ()) || heads.contains (tokens[i].key)) {
      choices[i]= match_at (index, tokens, text, size_t (i));
      ++work.matched_positions;
      recomputed= true;
    }
  query.tokens= std::move (tokens);
  if (edited) {
    query.heads.clear ();
    for (const auto& token: query.tokens) query.heads.insert (token.key);
  }
  query.text= std::move (text);
  query.choices= std::move (choices);
  query.revision= index.matching_revision;
  query.initialized= true;
  if (!edited && !recomputed) return false;
  std::vector<AthenaArtifactRadioactiveMatch> selected;
  for (int i=0; i<count; ) {
    const auto& choice= query.choices[i];
    if (!choice) { ++i; continue; }
    bool include= !query.structured;
    for (int j=i; !include && j<i+choice->length; ++j)
      include= query.tokens[j].key.startsWith (QChar (0));
    if (include) selected.push_back ({i, i+choice->length, choice->uuids, choice->key});
    i += choice->length;
  }
  const bool changed= !same_matches (query.selected, selected);
  query.selected= std::move (selected);
  return changed;
}

AthenaArtifactRadioactiveDependencies& query_dependencies () {
  if (!*queried_block)
    *queried_block= std::make_shared<AthenaArtifactRadioactiveDependencies> ();
  return **queried_block;
}

MatchQuery& source_query (AthenaArtifactRadioactiveDependencies& dependencies,
                         path source, bool structured) {
  std::string key= structured ? "tree:" : "text:";
  for (; !is_nil (source); source= source->next)
    key += std::to_string (source->item) + "/";
  auto& query= dependencies.queries[key];
  query.structured= structured;
  query.visited= true;
  return query;
}

bool update_part (MatchPart& part, string source, AthenaArtifactRadioactiveWork& work) {
  if (part.structure.isEmpty () && part.source == to_std (source)) return false;
  auto projection= project_text (source);
  part.tokens= update_tokens (part.projection.text, part.tokens, projection.text, work);
  part.projection= std::move (projection);
  part.source= to_std (source);
  part.structure.clear ();
  return true;
}

std::vector<AthenaArtifactRadioactiveMatch> cached_text_matches (
    const RadioactiveIndex& index, string text, path source) {
  auto& dependencies= query_dependencies ();
  auto& query= source_query (dependencies, source, false);
  query.parts.resize (1);
  auto& part= query.parts[0];
  const bool edited= update_part (part, text, dependencies.work);
  if (edited || !query.initialized || query.revision != index.matching_revision)
    update_query (query, index, part.projection.text, part.tokens, dependencies.work);
  auto result= query.selected;
  for (auto& match: result) {
    match.start= part.projection.source_boundary[query.tokens[match.start].start];
    match.end= part.projection.source_boundary[query.tokens[match.end-1].end];
  }
  return result;
}

std::vector<AthenaArtifactRadioactiveTreeMatch> cached_tree_matches (
    const RadioactiveIndex& index, const tree& text, path source) {
  if (!is_func (text, CONCAT)) return {};
  auto& dependencies= query_dependencies ();
  auto& query= source_query (dependencies, source, true);
  query.parts.resize (N(text));
  std::vector<Token> tokens;
  QString surface;
  query.positions.clear ();
  for (int i=0; i<N(text); ++i) {
    auto& part= query.parts[i];
    int offset= surface.size ();
    if (is_atomic (text[i])) {
      update_part (part, text[i]->label, dependencies.work);
      for (auto token: part.tokens) {
        query.positions.push_back ({path (i, part.projection.source_boundary[token.start]),
                                    path (i, part.projection.source_boundary[token.end])});
        token.start += offset; token.end += offset;
        tokens.push_back (std::move (token));
      }
      surface += part.projection.text;
    }
    else {
      part= MatchPart ();
      part.structure= structure_key (text[i]);
      tokens.push_back ({part.structure, offset, offset+1});
      query.positions.push_back ({path (i, 0), path (i, 1)});
      surface += QChar (0xfffc);
    }
  }
  update_query (query, index, std::move (surface), std::move (tokens), dependencies.work);
  std::vector<AthenaArtifactRadioactiveTreeMatch> result;
  for (const auto& match: query.selected)
    result.push_back ({query.positions[match.start].first,
                      query.positions[match.end-1].second, match});
  return result;
}

std::shared_ptr<const RadioactiveIndex> active_index () {
  return std::atomic_load_explicit (&cached_index, std::memory_order_acquire);
}

} // namespace

AthenaArtifactRadioactiveScope::AthenaArtifactRadioactiveScope (
    std::shared_ptr<AthenaArtifactRadioactiveDependencies>& dependencies,
    const void* owner, bool enabled)
  : previous (queried_block), previous_owner (queried_owner) {
  if (enabled && (!queried_block || queried_owner == owner)) {
    queried_block= &dependencies;
    queried_owner= owner;
    current= &dependencies;
    if (dependencies)
      for (auto& entry: dependencies->queries) entry.second.visited= false;
  }
}

AthenaArtifactRadioactiveScope::~AthenaArtifactRadioactiveScope () {
  if (completed && current && *current) {
    auto& queries= (*current)->queries;
    for (auto i= queries.begin (); i != queries.end (); )
      if (!i->second.visited) i= queries.erase (i);
      else ++i;
  }
  queried_block= previous;
  queried_owner= previous_owner;
}

void athena_artifact_radioactive_reuse_queries () {
  if (queried_block && *queried_block)
    for (auto& entry: (*queried_block)->queries) entry.second.visited= true;
}

struct AthenaArtifactRadioactiveMatcher::Impl {
  explicit Impl (const std::vector<AthenaArtifactRecord>& records)
    : index (build_index (records)) {}
  Impl (const std::vector<AthenaArtifactRecord>& records,
        const AthenaArtifactTitleFilter& filter)
    : index (build_index (records, {}, &filter)) {}

  std::shared_ptr<const RadioactiveIndex> index;
};

AthenaArtifactRadioactiveMatcher::AthenaArtifactRadioactiveMatcher (
    const std::vector<AthenaArtifactRecord>& records)
  : impl (std::make_shared<const Impl> (records)) {}

AthenaArtifactRadioactiveMatcher::AthenaArtifactRadioactiveMatcher (
    const std::vector<AthenaArtifactRecord>& records,
    const AthenaArtifactTitleFilter& filter)
  : impl (std::make_shared<const Impl> (records, filter)) {}

AthenaArtifactRadioactiveMatcher::~AthenaArtifactRadioactiveMatcher () = default;

std::vector<AthenaArtifactRadioactiveMatch>
AthenaArtifactRadioactiveMatcher::matches (string text) const {
  return impl && impl->index ? match_index (*impl->index, text)
                             : std::vector<AthenaArtifactRadioactiveMatch> ();
}

std::vector<AthenaArtifactRadioactiveTreeMatch>
AthenaArtifactRadioactiveMatcher::matches_tree (const tree& text) const {
  return impl && impl->index ? match_tree (*impl->index, text)
    : std::vector<AthenaArtifactRadioactiveTreeMatch> ();
}

AthenaArtifactNameResolution
AthenaArtifactRadioactiveMatcher::resolve (const tree& query) const {
  if (!impl || !impl->index) return {};
  QByteArray display= tree_display_text (query).toUtf8 ();
  return resolve_tokens (*impl->index, tree_tokens (query),
    std::string (display.constData (), (std::size_t) display.size ()));
}

std::string
athena_artifact_radioactive_destination (
  const AthenaArtifactRadioactiveMatch& match) {
  if (match.uuids.empty ()) return {};
  return match.disambiguation_key.empty () ? std::string ()
    : "tmfs://artifact-disambiguation/" + match.disambiguation_key;
}

string
athena_artifact_radioactive_name (const AthenaArtifactRecord& record) {
  QByteArray utf8= artifact_term (record).toUtf8 ();
  return string (utf8.constData (), (int) utf8.size ());
}

bool athena_artifact_radioactive_name_for_key (
  const std::string& key, std::string& fragment, std::string& display) {
  auto index= active_index ();
  if (!index) return false;
  auto read= [&] (const RadioactiveIndex& layer) {
    auto found= layer.names.find (key);
    if (found == layer.names.end ()) return false;
    fragment= found->second.fragment;
    display= found->second.display;
    return true;
  };
  for (const auto& entry: index->overlays)
    if (read (*entry.second)) return true;
  return read (*index) || (index->base && read (*index->base));
}

std::string
athena_artifact_radioactive_key (const AthenaArtifactRecord& record) {
  return token_key (record.semantic_names.empty () ? std::vector<Token> ()
                                                  : name_tokens (record, 0));
}

std::vector<AthenaArtifactRadioactiveMatch>
athena_artifact_radioactive_matches (string text, path source) {
  auto index= active_index ();
  // Remember negative queries even before the first background publication.
  // Otherwise a buffer initially typeset against an empty index never learns
  // that a newly created name now matches its existing text.
  static const RadioactiveIndex empty;
  return queried_block ? cached_text_matches (index ? *index : empty, text, source) :
                         match_index (index ? *index : empty, text);
}

std::vector<AthenaArtifactRadioactiveTreeMatch>
athena_artifact_radioactive_matches_tree (const tree& text, path source) {
  auto index= active_index ();
  static const RadioactiveIndex empty;
  return queried_block ? cached_tree_matches (index ? *index : empty, text, source) :
                         match_tree (index ? *index : empty, text);
}

std::vector<AthenaArtifactRadioactiveMatch>
athena_artifact_radioactive_matches_for_records (
  const std::vector<AthenaArtifactRecord>& records, string text) {
  return AthenaArtifactRadioactiveMatcher (records).matches (text);
}

bool
athena_artifact_radioactive_record (
  const std::string& uuid, AthenaArtifactRecord& record) {
  auto index= std::atomic_load_explicit (&cached_index,
                                         std::memory_order_acquire);
  if (!index) return false;
  const auto* found= lookup (*index, uuid);
  if (!found) return false;
  record= *found;
  return true;
}

bool
athena_artifact_radioactive_records_for_key (
  const std::string& key, std::vector<AthenaArtifactRecord>& records) {
  records.clear ();
  auto index= active_index ();
  if (!index) return false;
  std::unordered_set<std::string> seen;
  auto collect= [&] (const RadioactiveIndex& layer, bool base) {
    auto matches= layer.records_by_key.find (key);
    if (matches == layer.records_by_key.end ()) return;
    for (const auto& uuid: matches->second) {
      auto saved= layer.records.find (uuid);
      if (base && saved != layer.records.end () &&
          index->overlays.count (saved->second.relative_path)) continue;
      const auto* record= lookup (*index, uuid);
      if (record && seen.insert (uuid).second) records.push_back (*record);
    }
  };
  collect (index->base ? *index->base : *index, bool (index->base));
  for (const auto& entry: index->overlays) collect (*entry.second, false);
  return true;
}

bool
athena_artifact_resolve_name_key (
  const std::string& key, AthenaArtifactNameResolution& result) {
  result= {};
  auto index= active_index ();
  if (!index) return false;
  auto resolve= [&] (const RadioactiveIndex& layer) {
    auto name= layer.names.find (key);
    if (name == layer.names.end ()) return false;
    result= resolve_tokens (*index, name->second.tokens, name->second.display);
    return true;
  };
  if (resolve (index->base ? *index->base : *index)) return true;
  for (const auto& entry: index->overlays) if (resolve (*entry.second)) break;
  return true;
}

bool
athena_artifact_resolve_name (
  const tree& query, AthenaArtifactNameResolution& result) {
  result= {};
  auto index= active_index ();
  if (!index) return false;
  QByteArray display= tree_display_text (query).toUtf8 ();
  result= resolve_tokens (*index, tree_tokens (query),
    std::string (display.constData (), (std::size_t) display.size ()));
  return true;
}

bool
athena_artifact_radioactive_is_defining_occurrence (
  const AthenaArtifactRadioactiveMatch& match, url current_file,
  const tree& document, path source_path) {
  auto index= active_index ();
  if (!index || index->vault_root.empty () || is_nil (source_path) ||
      is_none (current_file))
    return false;
  // Defining occurrences can only live in files from the active vault.  In
  // particular, do not materialize tmfs/web URLs here: concretizing a help
  // tmfs URL calls back into Scheme's tmfs loader while that help document is
  // being typeset, recursively re-entering document import.
  if (!is_rooted (current_file, "default") &&
      !is_rooted (current_file, "file"))
    return false;
  fs::path root= fs::path (index->vault_root).lexically_normal ();
  fs::path file=
    fs::path (to_std (as_system_string (current_file))).lexically_normal ();
  fs::path relative= file.lexically_relative (root);
  if (relative.empty () || relative.string ().rfind ("..", 0) == 0)
    return false;
  std::string relative_path= relative.generic_string ();
  for (const std::string& uuid: match.uuids) {
    const auto* found= lookup (*index, uuid);
    if (!found || found->relative_path != relative_path) continue;
    if (athena_artifact_is_defining_occurrence (
          document, source_path, *found)) return true;
  }
  return false;
}

namespace {

tree
suppress_definitions (const tree& value) {
  if (is_atomic (value)) return value;
  if (is_compound (value, "definition", 1))
    return tree (WITH, "athena-radioactive-links-suppressed", "true", value);
  tree result (value, N(value));
  // Semantic properties (notably canonical enunciation kind/name/numbering)
  // are required by the renderer.  Rebuilding the tree without metadata turns
  // a valid enunciation into "Malformed enunciation".  This is a presentation
  // copy: only a detached print layout retains source identity for destination
  // placement. Producer bindings never belong to the rendered copy.
  if (const auto* metadata= athena::node::get (value)) {
    auto presented= *metadata;
    if (!athena::node_reference::export_node_destinations_active ())
      presented.id.clear ();
    presented.properties.erase ("athena:artifact-bindings");
    athena::node::set (result, presented);
  }
  for (int i=0; i<N(value); i++)
    result[i]= suppress_definitions (value[i]);
  return result;
}

} // namespace

tree
athena_artifact_radioactive_suppress_definitions (
  const tree& document) {
  return suppress_definitions (document);
}

void
athena_artifact_radioactive_invalidate () {
  requested_revision.fetch_add (1, std::memory_order_release);
  const auto vault= vault_capture_context ();
  std::lock_guard<std::mutex> guard (index_build_mutex);
  if (!vault || vault->incarnation != loaded_incarnation) {
    note_matching_changes (disk_index.get (), nullptr);
    for (const auto& entry: saved_overlays) note_matching_changes (entry.second.get (), nullptr);
    for (const auto& entry: overlays) note_matching_changes (entry.second.index.get (), nullptr);
    overlays.clear (); saved_overlays.clear (); disk_index.reset (); loaded_revision= 0;
    loaded_incarnation.clear ();
    publish_index ();
  }
}

void athena_artifact_radioactive_refresh () {
  const auto vault= vault_capture_context ();
  const auto revision= requested_revision.load (std::memory_order_acquire);
  const std::string incarnation= vault ? vault->incarnation : "";
  std::uint64_t epoch;
  {
    std::lock_guard<std::mutex> guard (index_build_mutex);
    if (incarnation == loaded_incarnation && revision == loaded_revision) return;
    epoch= saved_epoch;
  }
  std::vector<AthenaArtifactRecord> records;
  std::string error;
  auto filter= athena_artifact_title_filter_defaults ();
  if (vault) {
    if (!athena_artifacts_query (vault->root, records, error, true, false))
      throw std::runtime_error (error);
    if (!athena_artifact_title_filter_read (vault->root, filter, error))
      throw std::runtime_error (error);
  }
  auto index= build_index (records, vault ? vault->root.string () : "", &filter);
  std::lock_guard<std::mutex> guard (index_build_mutex);
  if (epoch != saved_epoch || (vault && !vault_context_is_current (vault))) return;
  if (loaded_incarnation != incarnation) overlays.clear ();
  const bool filter_changed= !disk_index || !disk_index->has_title_filter ||
    disk_index->title_filter.normalized != filter.normalized ||
    disk_index->title_filter.structured != filter.structured;
  if (filter_changed) {
    matching_changes.emplace_back (++matching_revision, QSet<QString> {QString ()});
    if (matching_changes.size () > 128) matching_changes.pop_front ();
  }
  if (filter_changed) for (auto& entry: overlays) {
    std::vector<AthenaArtifactRecord> live;
    for (const auto& record: entry.second.index->records) live.push_back (record.second);
    auto rebuilt= build_index (live, {}, &filter);
    note_matching_changes (entry.second.index.get (), rebuilt.get ());
    entry.second.index= std::move (rebuilt);
  }
  if (filter_changed) for (const auto& entry: saved_overlays)
    note_matching_changes (entry.second.get (), index.get ());
  saved_overlays.clear ();
  loaded_incarnation= incarnation;
  loaded_revision= revision;
  note_matching_changes (disk_index.get (), index.get ());
  disk_index= std::move (index);
  publish_index ();
}

void athena_artifact_radioactive_overlay (
  const std::string& incarnation, const std::string& relative_path,
  std::uint64_t owner, const std::vector<AthenaArtifactRecord>& records) {
  const auto current= active_index ();
  std::shared_ptr<const RadioactiveIndex> previous_index;
  if (current) {
    auto found= current->overlays.find (relative_path);
    if (found != current->overlays.end ()) previous_index= found->second;
  }
  auto index= build_index (records, {}, current && current->has_title_filter ? &current->title_filter : nullptr,
                          previous_index.get ());
  const auto vault= vault_capture_context ();
  if (!vault || vault->incarnation != incarnation) return;
  std::lock_guard<std::mutex> guard (index_build_mutex);
  if (loaded_incarnation != incarnation) {
    overlays.clear (); saved_overlays.clear (); loaded_incarnation= incarnation; loaded_revision= 0;
    disk_index= build_index ({}, vault->root.string ());
  }
  if (!current || current->base != disk_index)
    index= build_index (records, {}, disk_index && disk_index->has_title_filter ?
      &disk_index->title_filter : nullptr);
  for (auto i= overlays.begin (); i != overlays.end (); )
    if (i->second.owner == owner && i->first != relative_path) i= overlays.erase (i);
    else ++i;
  auto previous= overlays.find (relative_path);
  std::shared_ptr<const RadioactiveIndex> saved;
  if (previous == overlays.end () && disk_index) {
    std::vector<AthenaArtifactRecord> records;
    auto ids= disk_index->records_by_path.find (relative_path);
    if (ids != disk_index->records_by_path.end ())
      for (const auto& id: ids->second) records.push_back (disk_index->records.at (id));
    saved= build_index (records);
    auto updated= saved_overlays.find (relative_path);
    if (updated != saved_overlays.end ()) saved= updated->second;
  }
  note_matching_changes (previous == overlays.end () ? saved.get () : previous->second.index.get (), index.get ());
  overlays[relative_path]= {owner, std::move (index)};
  publish_index ();
}

void athena_artifact_radioactive_remove_overlay (std::uint64_t owner) {
  std::lock_guard<std::mutex> guard (index_build_mutex);
  bool changed= false;
  for (auto i= overlays.begin (); i != overlays.end (); )
    if (i->second.owner == owner) {
      note_matching_changes (i->second.index.get (), nullptr);
      // Revealing saved records may reintroduce names absent from the overlay.
      auto updated= saved_overlays.find (i->first);
      if (updated != saved_overlays.end ()) note_matching_changes (nullptr, updated->second.get ());
      else if (disk_index) {
        std::vector<AthenaArtifactRecord> saved;
        auto ids= disk_index->records_by_path.find (i->first);
        if (ids != disk_index->records_by_path.end ())
          for (const auto& id: ids->second) saved.push_back (disk_index->records.at (id));
        auto restored= build_index (saved);
        note_matching_changes (nullptr, restored.get ());
      }
      i= overlays.erase (i); changed= true;
    }
    else ++i;
  if (changed) publish_index ();
}

void athena_artifact_radioactive_merge (
  const fs::path& root, std::vector<AthenaArtifactRecord>& records) {
  auto index= active_index ();
  if (!index || root.lexically_normal () != fs::path (index->vault_root).lexically_normal ()) return;
  records.erase (std::remove_if (records.begin (), records.end (), [&] (const auto& record) {
    return index->live_paths.count (record.relative_path);
  }), records.end ());
  for (const auto& entry: index->overlays)
    if (index->live_paths.count (entry.first))
      for (const auto& item: entry.second->records) records.push_back (item.second);
}

bool athena_artifact_radioactive_baseline (
  const std::string& incarnation, const std::string& relative_path,
  std::vector<AthenaArtifactRecord>& records) {
  std::shared_ptr<const RadioactiveIndex> baseline;
  {
    std::lock_guard<std::mutex> guard (index_build_mutex);
    if (loaded_incarnation != incarnation || !loaded_revision || !disk_index) return false;
    auto updated= saved_overlays.find (relative_path);
    baseline= updated == saved_overlays.end () ? disk_index : updated->second;
  }
  records.clear ();
  auto ids= baseline->records_by_path.find (relative_path);
  if (ids != baseline->records_by_path.end ())
    for (const auto& id: ids->second) records.push_back (baseline->records.at (id));
  return true;
}

bool athena_artifact_radioactive_changed (
    const std::shared_ptr<AthenaArtifactRadioactiveDependencies>& dependencies) {
  if (!dependencies) return false;
  auto index= active_index ();
  static const RadioactiveIndex empty;
  const auto& current= index ? *index : empty;
  if (dependencies->revision == current.matching_revision) return false;
  const auto heads= changed_heads (dependencies->revision, current.matching_revision);
  bool affected= false;
  for (auto& entry: dependencies->queries) {
    auto& query= entry.second;
    if (query.revision == current.matching_revision) continue;
    if (!heads.contains (QString ()) && !heads.intersects (query.heads)) {
      query.revision= current.matching_revision;
      continue;
    }
    affected= update_query (query, current, query.text, query.tokens, dependencies->work) || affected;
  }
  dependencies->revision= current.matching_revision;
  if (affected) ++dependencies->generation;
  return affected;
}

std::uint64_t athena_artifact_radioactive_generation (
    const std::shared_ptr<AthenaArtifactRadioactiveDependencies>& dependencies) {
  return dependencies ? dependencies->generation : 0;
}

std::uint64_t athena_artifact_radioactive_revision () {
  auto index= active_index ();
  return index ? index->matching_revision : 0;
}

AthenaArtifactRadioactiveWork athena_artifact_radioactive_work (
    const std::shared_ptr<AthenaArtifactRadioactiveDependencies>& dependencies) {
  return dependencies ? dependencies->work : AthenaArtifactRadioactiveWork ();
}

void athena_artifact_radioactive_saved_document (
  const fs::path& root, const std::string& relative_path,
  const std::vector<AthenaArtifactRecord>& records) {
  const auto current= active_index ();
  auto index= build_index (records, {}, current && current->has_title_filter ? &current->title_filter : nullptr);
  std::lock_guard<std::mutex> guard (index_build_mutex);
  if (!disk_index || fs::path (disk_index->vault_root) != root.lexically_normal ()) return;
  if (!current || current->base != disk_index)
    index= build_index (records, {}, disk_index->has_title_filter ?
      &disk_index->title_filter : nullptr);
  ++saved_epoch;
  if (!overlays.count (relative_path)) {
    auto old= saved_overlays.find (relative_path);
    if (old != saved_overlays.end ()) note_matching_changes (old->second.get (), index.get ());
    else {
      std::vector<AthenaArtifactRecord> previous;
      auto ids= disk_index->records_by_path.find (relative_path);
      if (ids != disk_index->records_by_path.end ())
        for (const auto& id: ids->second) previous.push_back (disk_index->records.at (id));
      auto before= build_index (previous);
      note_matching_changes (before.get (), index.get ());
    }
  }
  saved_overlays[relative_path]= std::move (index);
  publish_index ();
}
