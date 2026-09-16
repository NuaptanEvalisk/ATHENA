/******************************************************************************
* MODULE     : vault_anchors.cpp
* DESCRIPTION: Native structural anchors for ATHENA vault documents
******************************************************************************/

#include "ATHENA/Data/vault_anchors.hpp"

#include "ATHENA/Data/artifact_title_filter.hpp"
#include "ATHENA/Data/new_buffer.hpp"
#include "ATHENA/Data/vault.hpp"
#include "analyze.hpp"
#include "converter.hpp"
#include "qt_utilities.hpp"

#include <QString>
#include <filesystem>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace fs= std::filesystem;

namespace {

using Counts= std::unordered_map<std::string,int>;

std::string native_string (string value) {
  return std::string (value.data (), N(value));
}

string tm_string (const std::string& value) {
  return string (value.data (), static_cast<int> (value.size ()));
}

string tag_name (tree t) {
  return is_compound (t) ? as_string (L(t)) : string ();
}

bool tag_is (tree t, const char* name) {
  return is_compound (t) && tag_name (t) == name;
}

const std::unordered_set<std::string>& enunciation_tags () {
  static const std::unordered_set<std::string> value= {
    "theorem","lemma","corollary","proposition","axiom","definition",
    "notation","convention","conjecture","law","remark","note","example",
    "warning","disambiguation","acknowledgments","exercise","problem",
    "question","solution","solution*","answer","proof","proof-alternative",
    "proof-standard","proof-of","quote-env","render-theorem","render-remark",
    "render-exercise","render-solution","render-proof",
    "render-proof-alternative","render-proof-standard"};
  return value;
}

const std::unordered_set<std::string>& parenthesized_title_tags () {
  static const std::unordered_set<std::string> value= {
    "theorem","lemma","proposition","corollary","conjecture","question"};
  return value;
}

const std::unordered_set<std::string>& direct_title_tags () {
  static const std::unordered_set<std::string> value= {
    "definition","notation","convention","axiom","law","remark","note",
    "example","warning","disambiguation","acknowledgments","exercise",
    "problem","solution","answer","quote-env"};
  return value;
}

const std::unordered_set<std::string>& proof_tags () {
  static const std::unordered_set<std::string> value= {
    "proof","proof-alternative","proof-standard","proof-of","solution",
    "solution*","render-proof","render-proof-alternative",
    "render-proof-standard","render-solution"};
  return value;
}

const std::unordered_set<std::string>& separated_owner_tags () {
  static const std::unordered_set<std::string> value= {
    "theorem","lemma","corollary","proposition","axiom","definition",
    "conjecture","remark","law","example","question","render-theorem",
    "render-remark","render-exercise"};
  return value;
}

string base_tag (string tag) {
  if (tag == "render-theorem") return "theorem";
  if (tag == "render-remark") return "remark";
  if (tag == "render-exercise") return "exercise";
  if (tag == "render-solution") return "solution";
  if (tag == "render-proof") return "proof";
  if (tag == "render-proof-alternative") return "proof-alternative";
  if (tag == "render-proof-standard") return "proof-standard";
  return tag;
}

int heading_level (string tag) {
  if (tag == "section" || tag == "section*") return 1;
  if (tag == "subsection" || tag == "subsection*") return 2;
  if (tag == "subsubsection" || tag == "subsubsection*") return 3;
  if (tag == "paragraph" || tag == "paragraph*") return 4;
  if (tag == "subparagraph" || tag == "subparagraph*") return 5;
  return 0;
}

bool anchor_label (tree t) {
  return is_func (t, LABEL, 1) && is_atomic (t[0]);
}

string label_text (tree t) {
  return anchor_label (t) ? t[0]->label : string ();
}

string label_key (string label) {
  if (ends (label, " {") || ends (label, " }"))
    return label (0, N(label) - 2);
  return label;
}

bool upper_label (tree t) {
  return anchor_label (t) && occurs ("{", label_text (t));
}

bool lower_label (tree t) {
  return anchor_label (t) && occurs ("}", label_text (t));
}

bool ignorable (tree t) {
  return is_atomic (t) && to_qstring (t->label).trimmed ().isEmpty ();
}

bool cjk (uint code) {
  return (code >= 0x3400 && code <= 0x4dbf) ||
         (code >= 0x4e00 && code <= 0x9fff) ||
         (code >= 0xf900 && code <= 0xfaff) ||
         (code >= 0x20000 && code <= 0x2a6df) ||
         (code >= 0x2a700 && code <= 0x2b73f) ||
         (code >= 0x2b740 && code <= 0x2b81f) ||
         (code >= 0x2b820 && code <= 0x2ceaf) ||
         (code >= 0x2ceb0 && code <= 0x2ebef) ||
         (code >= 0x30000 && code <= 0x3134f);
}

string collapse_whitespace (string value) {
  return from_qstring (to_qstring (value).simplified ());
}

string sanitize_text (string value, int limit) {
  QString input= to_qstring (collapse_whitespace (value));
  QString output;
  bool space= true;
  int count= 0;
  const QList<uint> chars= input.toUcs4 ();
  for (uint code: chars) {
    if (limit > 0 && count >= limit) break;
    if (QChar::isSpace (code)) {
      if (!space) { output.append (' '); ++count; space= true; }
      continue;
    }
    bool ascii= (code >= '0' && code <= '9') ||
                (code >= 'A' && code <= 'Z') ||
                (code >= 'a' && code <= 'z');
    if (!ascii && !cjk (code)) continue;
    char32_t character= static_cast<char32_t> (code);
    output.append (QString::fromUcs4 (&character, 1));
    ++count;
    space= false;
  }
  return from_qstring (output.trimmed ());
}

string normalize_title_candidate (string value) {
  QString q= to_qstring (sanitize_text (value, 0));
  for (int i=0; i<q.size (); ++i)
    if (q[i] >= 'A' && q[i] <= 'Z') q[i]= q[i].toLower ();
  return from_qstring (q);
}

bool format_wrapper (tree t) {
  return tag_is (t, "with") || tag_is (t, "style-with");
}

std::vector<tree> visible_children (tree t) {
  std::vector<tree> out;
  if (!is_compound (t)) return out;
  if (format_wrapper (t)) {
    if (N(t) >= 3) out.push_back (t[N(t)-1]);
    return out;
  }
  if (tag_is (t, "hlink") && N(t) >= 1) {
    out.push_back (t[0]);
    return out;
  }
  for (int i=0; i<N(t); ++i) out.push_back (t[i]);
  return out;
}

string plain_text (tree t) {
  if (is_atomic (t)) return t->label;
  string tag= tag_name (t);
  if (tag == "label" || tag == "reference" || tag == "pageref" ||
      tag == "image" || tag == "include" || tag == "transclude" ||
      tag == "TRANSCLUDE") return "";
  string joined;
  for (tree child: visible_children (t)) {
    string part= plain_text (child);
    if (part == "") continue;
    if (joined != "") joined << " ";
    joined << part;
  }
  return collapse_whitespace (joined);
}

bool bold_format_wrapper (tree t) {
  if (!format_wrapper (t) || N(t) < 3) return false;
  for (int i=0; i+1<N(t)-1; i += 2) {
    if (!is_atomic (t[i]) || !is_atomic (t[i+1])) continue;
    string key= t[i]->label, value= t[i+1]->label;
    if ((key == "font-series" || key == "fontseries") &&
        (value == "bold" || value == "bold-series")) return true;
  }
  return false;
}

string first_strong_text (tree t) {
  if (!is_compound (t)) return "";
  if (tag_is (t, "strong") && N(t) >= 1) return plain_text (t[0]);
  if (bold_format_wrapper (t)) return plain_text (t[N(t)-1]);
  for (tree child: visible_children (t)) {
    string found= first_strong_text (child);
    if (found != "") return found;
  }
  return "";
}

string parenthesized_title (string value) {
  QString q= to_qstring (value).trimmed ();
  if (q.size () <= 2 || !q.startsWith ('(')) return "";
  int end= q.indexOf (')', 1);
  if (end < 0) return "";
  return from_qstring (q.mid (1, end - 1).trimmed ());
}

string parenthesized_title_from_tree (tree t) {
  string strong= first_strong_text (t);
  string result= parenthesized_title (strong);
  return result != "" ? result : parenthesized_title (plain_text (t));
}

std::mutex title_filter_mutex;
std::string title_filter_key;
std::unordered_set<std::string> title_filter_values;
bool title_filter_valid= false;

const std::unordered_set<std::string>& title_filter () {
  std::lock_guard<std::mutex> guard (title_filter_mutex);
  std::string key;
  AthenaArtifactTitleFilter filter;
  if (vault_active ()) {
    string root= as_string (concretize (vault_get_root ()), URL_SYSTEM);
    key.assign (root.data (), N(root));
  }
  if (title_filter_valid && key == title_filter_key) return title_filter_values;
  if (key.empty ()) filter= athena_artifact_title_filter_defaults ();
  else {
    std::string error;
    if (!athena_artifact_title_filter_read (fs::path (key), filter, error))
      filter= athena_artifact_title_filter_defaults ();
  }
  title_filter_values.clear ();
  for (const std::string& entry: filter.entries)
    title_filter_values.insert (
      native_string (normalize_title_candidate (tm_string (entry))));
  title_filter_key= key;
  title_filter_valid= true;
  return title_filter_values;
}

bool common_title_candidate (string value) {
  return title_filter ().count (
    native_string (normalize_title_candidate (value))) != 0;
}

bool is_render_enunciation (string tag) {
  return tag == "render-theorem" || tag == "render-remark" ||
         tag == "render-exercise" || tag == "render-solution" ||
         tag == "render-proof" || tag == "render-proof-alternative" ||
         tag == "render-proof-standard";
}

tree enunciation_body (tree t) {
  string tag= tag_name (t);
  if ((is_render_enunciation (tag) || tag == "proof-of") && N(t) >= 2)
    return t[1];
  return N(t) >= 1 ? t[0] : tree ("");
}

tree render_title_tree (tree t) {
  return is_render_enunciation (tag_name (t)) && N(t) >= 2
           ? t[0] : tree ("");
}

bool enunciation (tree t) {
  return is_compound (t) && enunciation_tags ().count (native_string (tag_name (t)));
}

bool proof (tree t) {
  return is_compound (t) && proof_tags ().count (native_string (tag_name (t)));
}

bool separated_proof_owner (tree t) {
  return is_compound (t) &&
         separated_owner_tags ().count (native_string (tag_name (t)));
}

string heading_title (tree t) {
  return N(t) >= 1 ? collapse_whitespace (plain_text (t[0])) : string ();
}

string title_from_enunciation (tree t) {
  string tag= base_tag (tag_name (t));
  tree rendered_tree= render_title_tree (t);
  string rendered= sanitize_text (plain_text (rendered_tree), 100);
  tree body= enunciation_body (t);
  string strong= first_strong_text (body);
  string render_paren= parenthesized_title_from_tree (rendered_tree);
  string body_paren= parenthesized_title_from_tree (body);
  string paren= render_paren != "" ? render_paren : body_paren;
  string strong_title= (strong == "" || common_title_candidate (strong))
                         ? string () : sanitize_text (strong, 100);
  if (tag == "proof-of" && N(t) >= 2)
    return sanitize_text (plain_text (t[0]), 80);
  if (parenthesized_title_tags ().count (native_string (tag)))
    return paren != "" ? sanitize_text (paren, 100)
                        : (strong_title != "" ? strong_title : rendered);
  if (rendered != "") return rendered;
  if (direct_title_tags ().count (native_string (tag))) return strong_title;
  return "";
}

string anchor_prefix (string tag, string title) {
  string base= base_tag (tag);
  if (base == "proof-of") return title == "" ? "proof" : "proof:" * title;
  return base;
}

string separated_proof_prefix (tree t) {
  string tag= base_tag (tag_name (t));
  string title= title_from_enunciation (t);
  if (tag == "proof-alternative" || tag == "proof-standard") return tag;
  if (tag == "solution" || tag == "solution*") return "solution";
  if (tag == "proof-of") return title == "" ? "proof" : "proof:" * title;
  if (tag == "proof" && title != "") return "proof:" * title;
  return "proof";
}

string enunciation_suffix (string id) {
  int pos= search_forwards (":", id);
  return pos >= 0 ? id (pos + 1, N(id)) : id;
}

string id_for_enunciation (tree t, string proof_context= "") {
  string tag= base_tag (tag_name (t));
  string title= title_from_enunciation (t);
  string sample= sanitize_text (plain_text (enunciation_body (t)), 100);
  string prefix= anchor_prefix (tag, title);
  if (proof_context != "" && proof (t))
    return separated_proof_prefix (t) * ":" * enunciation_suffix (proof_context);
  if (title != "" && !proof (t)) return tag * ":" * title;
  if (proof (t) && sample != "") {
    string p= separated_proof_prefix (t);
    return p * (occurs (":", p) ? " " : ":") * sample;
  }
  if (tag == "proof-of" && title != "" && sample != "")
    return prefix * " " * sample;
  if (sample != "") return prefix * ":" * sample;
  return prefix;
}

string id_for_heading (tree t) {
  return "H" * as_string (heading_level (tag_name (t))) * " " * heading_title (t);
}

void register_existing_labels (tree t, Counts& counts) {
  if (anchor_label (t)) {
    std::string key= native_string (label_key (label_text (t)));
    if (!counts.count (key)) counts[key]= 1;
  }
  if (is_compound (t))
    for (int i=0; i<N(t); ++i) register_existing_labels (t[i], counts);
}

string unique_id (string raw, Counts& counts) {
  std::string key= native_string (raw);
  int count= counts[key];
  counts[key]= count + 1;
  return count == 0 ? raw : raw * " (" * as_string (count) * ")";
}

bool decimal_string (string value) {
  if (N(value) == 0) return false;
  for (int i=0; i<N(value); ++i)
    if (value[i] < '0' || value[i] > '9') return false;
  return true;
}

bool compatible_id (string existing, string raw) {
  if (existing == raw) return true;
  string prefix= raw * " (";
  return starts (existing, prefix) && ends (existing, ")") &&
         N(existing) > N(prefix) + 1 &&
         decimal_string (existing (N(prefix), N(existing) - 1));
}

tree wrapper_label (string id, string suffix) {
  return compound ("label", id * suffix);
}

bool wrapper_key (tree previous, tree next, string& result) {
  if (!upper_label (previous) || !lower_label (next)) return false;
  string upper= label_key (label_text (previous));
  string lower= label_key (label_text (next));
  if (upper != lower) return false;
  result= upper;
  return true;
}

bool heading_label (tree t, int level) {
  return anchor_label (t) && starts (label_text (t),
    "H" * as_string (level) * " ");
}

void note (VaultAnchorSummary& summary, string value) {
  summary.notes.push_back (value);
}

void add_rename (VaultAnchorSummary& summary, string old_label, string new_label) {
  if (old_label != "" && new_label != "" && old_label != new_label)
    summary.renames.emplace_back (old_label, new_label);
}

void rename_wrapper (VaultAnchorSummary& summary, string old_id, string new_id) {
  add_rename (summary, old_id * " {", new_id * " {");
  add_rename (summary, old_id * " }", new_id * " }");
}

int next_substantive (const std::vector<tree>& children, int from) {
  for (int i=from; i<(int) children.size (); ++i)
    if (!ignorable (children[i])) return i;
  return -1;
}

void replace_last_substantive (std::vector<tree>& out, tree replacement) {
  for (int i=(int) out.size () - 1; i>=0; --i)
    if (!ignorable (out[i])) { out[i]= replacement; return; }
}

tree transform_tree (tree t, Counts& counts, VaultAnchorSummary& summary,
                     bool dry_run);

tree transform_document_children (tree document, Counts& counts,
                                  VaultAnchorSummary& summary, bool dry_run) {
  std::vector<tree> children;
  children.reserve (N(document));
  for (int i=0; i<N(document); ++i)
    children.push_back (transform_tree (document[i], counts, summary, dry_run));

  std::vector<tree> out;
  tree previous;
  bool has_previous= false;
  string proof_context;
  for (int i=0; i<(int) children.size ();) {
    tree current= children[i];
    if (ignorable (current)) {
      out.push_back (current); ++i; continue;
    }

    int next_index= next_substantive (children, i + 1);
    if (upper_label (current) && next_index >= 0 && lower_label (children[next_index])) {
      ++summary.dead_pairs;
      note (summary, "remove dead anchors: " * label_key (label_text (current)));
      i= next_index + 1;
      continue;
    }

    if (anchor_label (current)) {
      out.push_back (current); previous= current; has_previous= true; ++i; continue;
    }

    if (enunciation (current)) {
      tree next= next_index >= 0 ? children[next_index] : tree ();
      string context= proof (current) ? proof_context : string ();
      string raw= id_for_enunciation (current, context);
      string existing;
      bool has_wrapper= has_previous && next_index >= 0 &&
                        wrapper_key (previous, next, existing);
      if (has_wrapper && compatible_id (existing, raw)) {
        out.push_back (current);
        previous= current;
        proof_context= (!proof (current) && separated_proof_owner (current))
                         ? existing : string ();
        ++i;
        continue;
      }
      if (has_wrapper) {
        string id= unique_id (raw, counts);
        tree upper= wrapper_label (id, " {");
        tree lower= wrapper_label (id, " }");
        ++summary.updated;
        note (summary, "update " * tag_name (current) * " anchor: " *
                       existing * " -> " * id);
        rename_wrapper (summary, existing, id);
        if (!dry_run) {
          replace_last_substantive (out, upper);
          out.push_back (current);
          out.push_back (lower);
          previous= lower;
          i= next_index + 1;
        }
        else {
          out.push_back (current);
          previous= current;
          ++i;
        }
        has_previous= true;
        proof_context= separated_proof_owner (current) ? id : string ();
        continue;
      }

      string id= unique_id (raw, counts);
      ++summary.wrapped;
      note (summary, "wrap " * tag_name (current) * ": " * id);
      if (!dry_run) out.push_back (wrapper_label (id, " {"));
      out.push_back (current);
      if (!dry_run) {
        tree lower= wrapper_label (id, " }");
        out.push_back (lower);
        previous= lower;
      }
      else previous= current;
      has_previous= true;
      proof_context= separated_proof_owner (current) ? id : string ();
      ++i;
      continue;
    }

    if (vault_anchor_heading (current)) {
      int level= heading_level (tag_name (current));
      string raw= id_for_heading (current);
      string existing= has_previous && heading_label (previous, level)
                         ? label_text (previous) : string ();
      if (existing != "" && compatible_id (existing, raw)) {
        out.push_back (current); previous= current; proof_context= ""; ++i;
        continue;
      }
      if (existing != "") {
        string id= unique_id (raw, counts);
        ++summary.updated;
        note (summary, "update heading anchor: " * existing * " -> " * id);
        add_rename (summary, existing, id);
        if (!dry_run) replace_last_substantive (out, compound ("label", id));
      }
      else {
        string id= unique_id (raw, counts);
        ++summary.headings;
        note (summary, "anchor heading: " * id);
        if (!dry_run) out.push_back (compound ("label", id));
      }
      out.push_back (current); previous= current; has_previous= true;
      proof_context= ""; ++i;
      continue;
    }

    out.push_back (current);
    previous= current;
    has_previous= true;
    proof_context= "";
    ++i;
  }

  if (dry_run) return copy (document);
  tree result (DOCUMENT);
  for (tree child: out) result << child;
  return result;
}

tree transform_tree (tree t, Counts& counts, VaultAnchorSummary& summary,
                     bool dry_run) {
  if (is_atomic (t)) return copy (t);
  if (is_func (t, DOCUMENT))
    return transform_document_children (t, counts, summary, dry_run);
  tree result (L(t), N(t));
  for (int i=0; i<N(t); ++i)
    result[i]= transform_tree (t[i], counts, summary, dry_run);
  return result;
}

VaultAnchorTransform run_transform (tree body, bool dry_run) {
  Counts counts;
  register_existing_labels (body, counts);
  VaultAnchorTransform result;
  result.body= transform_tree (body, counts, result.summary, dry_run);
  if (dry_run) result.body= copy (body);
  return result;
}

string tab_safe (string value) {
  QString q= to_qstring (value);
  q.replace ('\t', ' ');
  q.replace ('\n', ' ');
  return from_qstring (q);
}

string maintenance_result (string status, const VaultAnchorSummary& summary,
                           bool changed, string message, string renames) {
  return status * "\t" * as_string ((int) summary.wrapped) * "\t" *
         as_string ((int) summary.dead_pairs) * "\t" *
         as_string ((int) summary.headings) * "\t" *
         as_string ((int) summary.updated) * "\t" *
         (changed ? "1" : "0") * "\t" * tab_safe (message) * "\t" * renames;
}

} // namespace

bool vault_anchor_heading (tree t) {
  return is_compound (t) && heading_level (tag_name (t)) > 0 && N(t) >= 1 &&
         heading_title (t) != "";
}

void vault_anchor_title_filter_invalidate () {
  std::lock_guard<std::mutex> guard (title_filter_mutex);
  title_filter_valid= false;
  title_filter_key.clear ();
  title_filter_values.clear ();
}

VaultAnchorSummary vault_anchor_plan (tree body) {
  return run_transform (body, true).summary;
}

VaultAnchorTransform vault_anchor_transform (tree body) {
  return run_transform (body, false);
}

VaultAnchorTransform vault_anchor_transform_document (tree document) {
  if (!is_func (document, DOCUMENT)) return vault_anchor_transform (document);
  for (int i=0; i<N(document); ++i) {
    if (!tag_is (document[i], "body") || N(document[i]) < 1) continue;
    VaultAnchorTransform inner= vault_anchor_transform (document[i][0]);
    tree output= copy (document);
    if (!inner.summary.empty ()) {
      tree body= copy (document[i]);
      body[0]= inner.body;
      output[i]= body;
    }
    inner.body= output;
    return inner;
  }
  return vault_anchor_transform (document);
}

string vault_anchor_renames_string (const VaultAnchorSummary& summary) {
  string output;
  for (size_t i=0; i<summary.renames.size (); ++i) {
    if (i != 0) output << (char) 30;
    output << tab_safe (summary.renames[i].first) << (char) 31
           << tab_safe (summary.renames[i].second);
  }
  return output;
}

string vault_anchor_maintenance_file_native (url file, bool check_only) {
  try {
    tree doc= import_tree (file, "texmacs");
    if (doc == "error" || is_func (doc, _ERROR)) {
      VaultAnchorSummary empty;
      return maintenance_result (
        "error", empty, false, "could not import document", "");
    }
    VaultAnchorTransform transformed= vault_anchor_transform_document (doc);
    bool changed= !transformed.summary.empty ();
    if (changed && !check_only && export_tree (transformed.body, file, "texmacs"))
      return maintenance_result (
        "error", transformed.summary, false, "could not export document", "");
    return maintenance_result (
      "ok", transformed.summary, changed, "",
      check_only ? string () : vault_anchor_renames_string (transformed.summary));
  }
  catch (...) {
    VaultAnchorSummary empty;
    return maintenance_result ("error", empty, false, "native anchor failure", "");
  }
}
