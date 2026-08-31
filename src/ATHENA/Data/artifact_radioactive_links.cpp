/******************************************************************************
* MODULE     : artifact_radioactive_links.cpp
* DESCRIPTION: Fast automatic links to stable semantic artifacts
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
*******************************************************************************/

#include "ATHENA/Data/artifact_radioactive_links.hpp"

#include "ATHENA/Data/vault.hpp"
#include "ATHENA/Data/vaultfile_json.hpp"
#include "analyze.hpp"
#include "converter.hpp"
#include "message.hpp"
#include "wencoding.hpp"

#include <QCryptographicHash>
#include <QHash>
#include <QRegularExpression>
#include <QString>
#include <QVector>

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
#include <utility>

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
  std::string vault_root;
  std::vector<TrieNode> nodes= {TrieNode ()};
  std::unordered_map<std::string,AthenaArtifactRecord> records;
  std::unordered_map<std::string,std::vector<std::string>> records_by_key;
};

struct TextProjection {
  QString text;
  QVector<int> source_boundary;
};

std::shared_ptr<const RadioactiveIndex> cached_index;
std::mutex index_build_mutex;

std::string to_std (string value) {
  return std::string (as_charp (value), (size_t) N(value));
}

bool has_universal_glyph (string value) {
  if (looks_universal (value)) return true;
  for (int i=0; i+1<N(value); i++)
    if (value[i] == '<' && value[i+1] == '#') return true;
  return false;
}

QString qstring_from_tm_or_utf8 (const std::string& bytes) {
  string value (bytes.data (), (int) bytes.size ());
  if (looks_utf8 (value) && !has_universal_glyph (value))
    return QString::fromUtf8 (bytes.data (), (qsizetype) bytes.size ());
  string utf8= cork_to_utf8 (value);
  return QString::fromUtf8 (as_charp (utf8), N(utf8));
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

std::vector<QString> artifact_terms (const AthenaArtifactRecord& record) {
  if (record.type == "completion") return {};
  std::vector<QString> terms;
  terms.reserve (record.semantic_names.size ());
  for (const std::string& name: record.semantic_names) {
    QString term= qstring_from_tm_or_utf8 (name).simplified ();
    if (!term.isEmpty ()) terms.push_back (term);
  }
  return terms;
}

QString artifact_term (const AthenaArtifactRecord& record) {
  std::vector<QString> terms= artifact_terms (record);
  return terms.empty () ? QString () : terms.front ();
}

void add_term (RadioactiveIndex& index, const QString& term,
               const std::string& uuid) {
  if (uuid.empty () || term.isEmpty () || term.size () > maximum_term_characters)
    return;
  std::vector<Token> tokens= tokenize (term);
  if (tokens.empty () || tokens.size () > maximum_term_tokens) return;
  if (tokens.size () == 1 && tokens[0].key.size () < 3 &&
      tokens[0].key.front ().unicode () < 128)
    return;
  int node= 0;
  for (const Token& token: tokens) {
    auto child= index.nodes[(size_t) node].children.constFind (token.key);
    if (child == index.nodes[(size_t) node].children.constEnd ()) {
      int next= (int) index.nodes.size ();
      index.nodes[(size_t) node].children.insert (token.key, next);
      index.nodes.emplace_back ();
      node= next;
    }
    else node= child.value ();
  }
  TrieNode& terminal= index.nodes[(size_t) node];
  if (terminal.key.empty ()) terminal.key= token_key (tokens);
  if (std::find (terminal.uuids.begin (), terminal.uuids.end (), uuid) ==
      terminal.uuids.end ()) terminal.uuids.push_back (uuid);
}

std::shared_ptr<const RadioactiveIndex> build_index (
  const std::vector<AthenaArtifactRecord>& records,
  const std::string& vault_root= {}) {
  auto index= std::make_shared<RadioactiveIndex> ();
  index->vault_root= vault_root;
  for (const AthenaArtifactRecord& record: records) {
    index->records.emplace (record.uuid, record);
    for (const QString& term: artifact_terms (record)) {
      add_term (*index, term, record.uuid);
      std::string key= token_key (tokenize (term));
      std::vector<std::string>& matches= index->records_by_key[key];
      if (std::find (matches.begin (), matches.end (), record.uuid) ==
          matches.end ())
        matches.push_back (record.uuid);
    }
  }
  return index;
}

TextProjection project_text (string source) {
  TextProjection projection;
  bool universal= has_universal_glyph (source);
  if (looks_ascii (source) && !universal) {
    projection.text= QString::fromLatin1 (as_charp (source), N(source));
    projection.source_boundary.resize (N(source) + 1);
    for (int i=0; i<=N(source); i++) projection.source_boundary[i]= i;
    return projection;
  }
  projection.source_boundary.push_back (0);
  bool direct_utf8= looks_utf8 (source) && !universal;
  int position= 0;
  while (position < N(source)) {
    int next= position;
    string utf8;
    if (direct_utf8) {
      (void) decode_from_utf8 (source, next);
      utf8= source (position, next);
    }
    else {
      tm_char_forwards (source, next);
      utf8= cork_to_utf8 (source (position, next));
    }
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

std::vector<AthenaArtifactRadioactiveMatch> match_index (
  const RadioactiveIndex& index, string source) {
  std::vector<AthenaArtifactRadioactiveMatch> result;
  if (index.nodes.size () <= 1 || N(source) == 0) return result;
  TextProjection projection= project_text (source);
  std::vector<Token> tokens= tokenize (projection.text);
  for (size_t start=0; start<tokens.size (); ) {
    int node= 0;
    int best_end= -1;
    std::vector<std::string> best_uuids;
    std::string best_key;
    size_t limit= std::min (tokens.size (), start + maximum_term_tokens);
    for (size_t position=start; position<limit; position++) {
      auto child= index.nodes[(size_t) node].children.constFind (
        tokens[position].key);
      if (child == index.nodes[(size_t) node].children.constEnd ()) break;
      node= child.value ();
      const TrieNode& candidate= index.nodes[(size_t) node];
      if (!candidate.uuids.empty ()) {
        best_end= (int) position;
        best_uuids= candidate.uuids;
        best_key= candidate.key;
      }
    }
    if (best_end < 0) { start++; continue; }
    int begin_utf16= tokens[start].start;
    int end_utf16= tokens[(size_t) best_end].end;
    if (begin_utf16 >= 0 && end_utf16 < projection.source_boundary.size ())
      result.push_back ({projection.source_boundary[begin_utf16],
                         projection.source_boundary[end_utf16], best_uuids,
                         best_key});
    start= (size_t) best_end + 1;
  }
  return result;
}

std::shared_ptr<const RadioactiveIndex> active_index () {
  auto index= std::atomic_load_explicit (&cached_index,
                                         std::memory_order_acquire);
  // Vault load/close and a successful artifact rebuild invalidate this
  // snapshot.  Returning it directly keeps the per-text-node hot path free of
  // URL concretization, filesystem checks, and SQLite access.
  if (index) return index;
  if (!vault_active ()) return {};
  std::string root= to_std (concretize (vault_get_root ()));
  std::lock_guard<std::mutex> guard (index_build_mutex);
  index= std::atomic_load_explicit (&cached_index, std::memory_order_acquire);
  if (index) return index;
  AthenaVaultfileInfo vaultfile;
  std::string error;
  if (!athena_vaultfile_read (fs::path (root), vaultfile, error) ||
      !fs::exists (fs::path (root) / vaultfile.artifacts_path)) {
    index= build_index ({}, root);
    std::atomic_store_explicit (&cached_index, index,
                                std::memory_order_release);
    return index;
  }
  std::vector<AthenaArtifactRecord> records;
  if (!athena_artifacts_query (fs::path (root), records, error)) {
    std_warning << "Could not build radioactive artifact link index: "
                << error.c_str () << LF;
    // Cache the failure as an empty immutable snapshot.  Otherwise one broken
    // or temporarily unavailable database would be queried once per atomic
    // text node during typesetting.
    index= build_index ({}, root);
    std::atomic_store_explicit (&cached_index, index,
                                std::memory_order_release);
    return index;
  }
  index= build_index (records, root);
  std::atomic_store_explicit (&cached_index, index, std::memory_order_release);
  return index;
}

} // namespace

struct AthenaArtifactRadioactiveMatcher::Impl {
  explicit Impl (const std::vector<AthenaArtifactRecord>& records)
    : index (build_index (records)) {}

  std::shared_ptr<const RadioactiveIndex> index;
};

AthenaArtifactRadioactiveMatcher::AthenaArtifactRadioactiveMatcher (
    const std::vector<AthenaArtifactRecord>& records)
  : impl (std::make_shared<const Impl> (records)) {}

AthenaArtifactRadioactiveMatcher::~AthenaArtifactRadioactiveMatcher () = default;

std::vector<AthenaArtifactRadioactiveMatch>
AthenaArtifactRadioactiveMatcher::matches (string text) const {
  return impl && impl->index ? match_index (*impl->index, text)
                             : std::vector<AthenaArtifactRadioactiveMatch> ();
}

std::string
athena_artifact_radioactive_destination (
  const AthenaArtifactRadioactiveMatch& match) {
  if (match.uuids.empty ()) return {};
  if (match.uuids.size () == 1)
    return "tmfs://artifact/" + match.uuids.front ();
  return match.disambiguation_key.empty () ? std::string ()
    : "tmfs://artifact-disambiguation/" + match.disambiguation_key;
}

string
athena_artifact_radioactive_name (const AthenaArtifactRecord& record) {
  QByteArray utf8= artifact_term (record).toUtf8 ();
  return utf8_to_cork (string (utf8.constData (), (int) utf8.size ()));
}

std::string
athena_artifact_radioactive_key (const AthenaArtifactRecord& record) {
  return token_key (tokenize (artifact_term (record)));
}

std::vector<AthenaArtifactRadioactiveMatch>
athena_artifact_radioactive_matches (string text) {
  auto index= active_index ();
  return index ? match_index (*index, text)
               : std::vector<AthenaArtifactRadioactiveMatch> ();
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
  auto found= index->records.find (uuid);
  if (found == index->records.end ()) return false;
  record= found->second;
  return true;
}

bool
athena_artifact_radioactive_records_for_key (
  const std::string& key, std::vector<AthenaArtifactRecord>& records) {
  records.clear ();
  auto index= active_index ();
  if (!index) return false;
  auto matches= index->records_by_key.find (key);
  if (matches == index->records_by_key.end ()) return true;
  records.reserve (matches->second.size ());
  for (const std::string& uuid: matches->second) {
    auto found= index->records.find (uuid);
    if (found != index->records.end ()) records.push_back (found->second);
  }
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
  fs::path root= fs::path (index->vault_root).lexically_normal ();
  fs::path file=
    fs::path (to_std (concretize (current_file))).lexically_normal ();
  fs::path relative= file.lexically_relative (root);
  if (relative.empty () || relative.string ().rfind ("..", 0) == 0)
    return false;
  std::string relative_path= relative.generic_string ();
  for (const std::string& uuid: match.uuids) {
    auto found= index->records.find (uuid);
    if (found == index->records.end () ||
        found->second.relative_path != relative_path) continue;
    if (athena_artifact_is_defining_occurrence (
          document, source_path, found->second)) return true;
  }
  return false;
}

namespace {

tree
suppress_radioactive_links_at (const tree& value, path where) {
  if (is_nil (where))
    return tree (WITH, "athena-radioactive-links-suppressed", "true", value);
  if (is_atomic (value) || where->item < 0 || where->item >= N(value))
    return value;
  tree result (value, N(value));
  for (int i=0; i<N(value); i++) result[i]= value[i];
  result[where->item]=
    suppress_radioactive_links_at (value[where->item], where->next);
  return result;
}

tree
suppress_enunciation_titles (const tree& value) {
  if (is_atomic (value)) return value;
  tree result (value, N(value));
  for (int i=0; i<N(value); i++)
    result[i]= suppress_enunciation_titles (value[i]);
  path title_path;
  if (athena_artifact_enunciation_title_path (value, title_path))
    result= suppress_radioactive_links_at (result, title_path);
  return result;
}

} // namespace

tree
athena_artifact_radioactive_suppress_enunciation_titles (
  const tree& document) {
  return suppress_enunciation_titles (document);
}

void
athena_artifact_radioactive_invalidate () {
  std::atomic_store_explicit (
    &cached_index, std::shared_ptr<const RadioactiveIndex> (),
    std::memory_order_release);
}
