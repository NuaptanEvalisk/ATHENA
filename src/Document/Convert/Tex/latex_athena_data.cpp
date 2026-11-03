/******************************************************************************
* MODULE     : latex_athena_data.cpp
* DESCRIPTION: Restore ATHENA metadata embedded in LaTeX exports
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
*******************************************************************************/

#include "Tex/convert_tex.hpp"
#include "base64.hpp"
#include "message.hpp"
#include "path.hpp"

#include <functional>
#include <vector>

namespace {

const string DATA_SEARCH= "ATHENA-DATA cmd=\"";
const string INLINE_PREFIX= "\\INLINE_COMMENT{";
const string PLACEHOLDER_PREFIX= "ATHENAIMPORTOBJECT";
const string PLACEHOLDER_SUFFIX= "X";

struct DataRecord {
  int start= -1;
  int end= -1;
  string body;
  bool present= false;
};

struct ParsedRecord {
  string command;
  std::vector<string> values;
  bool valid= false;
};

struct PreservedObject {
  string marker;
  tree value;
};

using AuxEntry= std::vector<string>;

struct PreprocessedData {
  string latex;
  std::vector<PreservedObject> objects;
  std::vector<AuxEntry> aux;
};

void
data_warning (string message) {
  std_warning << "ATHENA-DATA warning: " << message << LF;
}

void
data_error (string message) {
  std_warning << "ATHENA-DATA error: " << message << LF;
}

int
find_char (string s, int pos, char c) {
  for (int i=pos; i<N(s); ++i)
    if (s[i] == c) return i;
  return -1;
}

int
data_skip_spaces (string s, int pos) {
  int i= pos;
  while (i<N(s) && (s[i] == ' ' || s[i] == '\t')) ++i;
  return i;
}

int
line_start (string s, int pos) {
  for (int i=pos-1; i>=0; --i)
    if (s[i] == '\n') return i+1;
  return 0;
}

int
line_end (string s, int pos) {
  int i= search_forwards ("\n", pos, s);
  return i < 0 ? N(s) : i;
}

int
after_line (string s, int pos) {
  int i= search_forwards ("\n", pos, s);
  return i < 0 ? N(s) : i+1;
}

int
inline_close (string s, int open) {
  int depth= 1;
  for (int i=open+1; i<N(s); ++i) {
    if (s[i] == '\\' && i+1<N(s)) { ++i; continue; }
    if (s[i] == '{') ++depth;
    else if (s[i] == '}') {
      if (depth == 1) return i;
      --depth;
    }
  }
  return -1;
}

DataRecord
inline_record (string s, int pos) {
  DataRecord out;
  int start= pos - N(INLINE_PREFIX);
  if (start < 0 || s (start, pos) != INLINE_PREFIX) return out;
  int close= inline_close (s, pos-1);
  if (close < 0) return out;
  out.start= start;
  out.end= close + 1;
  out.body= s (pos, close);
  out.present= true;
  return out;
}

DataRecord
comment_record (string s, int pos) {
  DataRecord out;
  int start= line_start (s, pos);
  int i= data_skip_spaces (s, start);
  if (i>=N(s) || s[i] != '%') return out;
  int j= data_skip_spaces (s, i+1);
  if (j != pos) return out;
  out.start= start;
  out.end= after_line (s, pos);
  out.body= s (pos, line_end (s, pos));
  out.present= true;
  return out;
}

DataRecord
find_next_record (string s, int pos) {
  int i= search_forwards (DATA_SEARCH, pos, s);
  while (i >= 0) {
    DataRecord r= inline_record (s, i);
    if (r.present) return r;
    r= comment_record (s, i);
    if (r.present) return r;
    i= search_forwards (DATA_SEARCH, i+1, s);
  }
  return DataRecord ();
}

bool
parse_quoted (string s, int pos, string& value, int& next) {
  if (pos>=N(s) || s[pos] != '"') return false;
  string out;
  for (int i=pos+1; i<N(s); ++i) {
    if (s[i] == '\\' && i+1<N(s)) {
      out << s[i+1];
      ++i;
    }
    else if (s[i] == '"') {
      value= out;
      next= i+1;
      return true;
    }
    else out << s[i];
  }
  return false;
}

bool
parse_values (string s, int pos, std::vector<string>& values) {
  int i= pos;
  while (true) {
    int j= data_skip_spaces (s, i);
    if (j>=N(s)) return false;
    if (s[j] == ')') return true;
    if (s[j] == ',') { i= j+1; continue; }
    if (s[j] != '"') return false;
    string value;
    int next= j;
    if (!parse_quoted (s, j, value, next)) return false;
    values.push_back (value);
    i= next;
  }
}

ParsedRecord
parse_record (string body) {
  ParsedRecord out;
  int cmd_pos= search_forwards ("cmd=\"", 0, body);
  if (cmd_pos < 0) return out;
  int cmd_start= cmd_pos + 5;
  int cmd_end= find_char (body, cmd_start, '"');
  if (cmd_end < 0) return out;
  int val_pos= search_forwards ("val=(", cmd_end, body);
  if (val_pos < 0) return out;
  out.command= body (cmd_start, cmd_end);
  if (!parse_values (body, val_pos+5, out.values)) return ParsedRecord ();
  out.valid= true;
  return out;
}

string
strip_inline_definition (string s) {
  s= replace (s, "\\long\\def\\INLINE_COMMENT#1{}\n", "");
  return replace (s, "\\long\\def\\INLINE_COMMENT#1{}", "");
}

tree
unwrap_snippet (tree t) {
  if (is_document (t) && N(t) == 1) return t[0];
  return t;
}

bool
decode_embedded_tree (const std::vector<string>& values, int len_index,
                      int payload_index, tree& result, string kind) {
  if ((int) values.size () <= payload_index) return false;
  string length_text= values[(size_t) len_index];
  string payload= values[(size_t) payload_index];
  if (is_int (length_text) && as_int (length_text) != N(payload))
    data_warning (kind * " payload length mismatch: expected " * length_text *
                  ", got " * as_string (N(payload)));
  string raw= decode_base64 (payload);
  if (raw == "<error|compound athena-preserved-object>") return false;
  result= unwrap_snippet (texmacs_to_tree (raw));
  return true;
}

string
placeholder (int serial) {
  return PLACEHOLDER_PREFIX * as_string (serial) * PLACEHOLDER_SUFFIX;
}

void
pop_skip (std::vector<string>& stack, string id) {
  if (stack.empty ()) {
    data_error ("skip_end without skip_begin: " * id);
    return;
  }
  if (stack.back () == id) {
    stack.pop_back ();
    return;
  }
  int found= -1;
  for (int i=(int) stack.size ()-1; i>=0; --i)
    if (stack[(size_t) i] == id) { found= i; break; }
  if (found < 0) {
    data_error ("skip_end with unseen id: " * id);
    return;
  }
  data_error ("misnested skip_end: " * id);
  stack.resize ((size_t) found);
}

PreprocessedData
preprocess_data (string source) {
  PreprocessedData out;
  if (search_forwards (DATA_SEARCH, 0, source) < 0) {
    out.latex= source;
    return out;
  }

  string s= strip_inline_definition (source);
  string rendered;
  std::vector<string> skip;
  int serial= 0;
  int pos= 0;
  bool saw_version= false;

  while (true) {
    DataRecord rec= find_next_record (s, pos);
    if (!rec.present) {
      if (!skip.empty ()) {
        string ids;
        for (int i=(int) skip.size ()-1; i>=0; --i) {
          if (N(ids)) ids << ", ";
          ids << skip[(size_t) i];
        }
        data_error ("unterminated skip_begin: " * ids);
      }
      else rendered << s (pos, N(s));
      break;
    }

    if (skip.empty ()) rendered << s (pos, rec.start);
    pos= rec.end;
    ParsedRecord parsed= parse_record (rec.body);
    if (!parsed.valid) {
      data_warning ("invalid record: " * rec.body);
      continue;
    }

    if (parsed.command == "version") {
      if (!saw_version) {
        saw_version= true;
        if (!parsed.values.empty () &&
            version_inf (string (ATHENA_VERSION), parsed.values[0]))
          data_warning ("file was exported by ATHENA " * parsed.values[0] *
                        ", newer than this ATHENA " * string (ATHENA_VERSION));
      }
    }
    else if (parsed.command == "aux") {
      if (skip.empty ()) out.aux.push_back (parsed.values);
    }
    else if (parsed.command == "skip_begin") {
      skip.push_back (parsed.values.empty () ? string ("") : parsed.values[0]);
    }
    else if (parsed.command == "skip_end") {
      pop_skip (skip, parsed.values.empty () ? string ("") : parsed.values[0]);
    }
    else if (parsed.command == "object") {
      if (!skip.empty ()) continue;
      tree value;
      if (!decode_embedded_tree (parsed.values, 0, 1, value, "object")) {
        if (parsed.values.size () < 2)
          data_warning ("object record has too few values");
        continue;
      }
      string marker= placeholder (serial++);
      rendered << marker;
      PreservedObject item;
      item.marker= marker;
      item.value= value;
      out.objects.push_back (item);
    }
    else data_warning ("unknown command: " * parsed.command);
  }

  out.latex= rendered;
  return out;
}

tree replace_objects (tree t, const std::vector<PreservedObject>& objects);

tree
replace_in_string (string s, const std::vector<PreservedObject>& objects) {
  int best_pos= -1;
  const PreservedObject* best= nullptr;
  for (const auto& object: objects) {
    int pos= search_forwards (object.marker, 0, s);
    if (pos >= 0 && (best_pos < 0 || pos < best_pos)) {
      best_pos= pos;
      best= &object;
    }
  }
  if (best == nullptr) return tree (s);

  int end= best_pos + N(best->marker);
  string pre= s (0, best_pos);
  string post= s (end, N(s));
  tree post_tree= replace_in_string (post, objects);
  tree result (CONCAT);
  if (N(pre)) result << tree (pre);
  result << best->value;
  if (!(is_atomic (post_tree) && post_tree == "")) result << post_tree;
  if (N(result) == 1) return result[0];
  return result;
}

tree
replace_objects (tree t, const std::vector<PreservedObject>& objects) {
  if (objects.empty ()) return t;
  if (is_atomic (t)) return replace_in_string (t->label, objects);
  tree out (L(t));
  for (int i=0; i<N(t); ++i) out << replace_objects (t[i], objects);
  return out;
}

string
normalized_label (string label) {
  if (label == "proofalternative") return "proof-alternative";
  if (label == "proofstandard") return "proof-standard";
  if (label == "proofof") return "proof-of";
  if (label == "renderproofalternative") return "render-proof-alternative";
  if (label == "renderproofstandard") return "render-proof-standard";
  return label;
}

string
standard_enunciation_name (string var) {
  if (var == "theorem-name" || var == "theoremname") return "Theorem";
  if (var == "proposition-name" || var == "propositionname") return "Proposition";
  if (var == "lemma-name" || var == "lemmaname") return "Lemma";
  if (var == "corollary-name" || var == "corollaryname") return "Corollary";
  if (var == "axiom-name" || var == "axiomname") return "Axiom";
  if (var == "definition-name" || var == "definitionname") return "Definition";
  if (var == "notation-name" || var == "notationname") return "Notation";
  if (var == "conjecture-name" || var == "conjecturename") return "Conjecture";
  if (var == "law-name" || var == "lawname") return "Law";
  if (var == "remark-name" || var == "remarkname") return "Remark";
  if (var == "example-name" || var == "examplename") return "Example";
  if (var == "note-name" || var == "notename") return "Note";
  if (var == "warning-name" || var == "warningname") return "Warning";
  if (var == "disambiguation-name" || var == "disambiguationname") return "Disambiguation";
  if (var == "convention-name" || var == "conventionname") return "Convention";
  if (var == "acknowledgments-name" || var == "acknowledgmentsname" ||
      var == "acknowledgment-name" || var == "acknowledgmentname") return "Acknowledgments";
  if (var == "question-name" || var == "questionname") return "Question";
  if (var == "answer-name" || var == "answername") return "Answer";
  if (var == "exercise-name" || var == "exercisename") return "Exercise";
  if (var == "problem-name" || var == "problemname") return "Problem";
  if (var == "solution-name" || var == "solutionname") return "Solution";
  if (var == "proof-name" || var == "proofname") return "Proof";
  return "";
}

bool
obsolete_enunciation_name (tree t) {
  if (!is_compound (t, "assign", 2) || !is_atomic (t[0]) ||
      !is_compound (t[1], "macro", 1) || !is_atomic (t[1][0])) return false;
  string expected= standard_enunciation_name (t[0]->label);
  return N(expected) && t[1][0] == expected;
}

bool
lyx_zero_width_space (tree t) {
  return is_compound (t) && N(t) == 0 &&
         as_string (L(t)) == "LyXZeroWidthSpace";
}

bool
lyx_zero_width_definition (tree t) {
  return is_compound (t, "assign", 2) && is_atomic (t[0]) &&
         t[0] == "LyXZeroWidthSpace" && is_compound (t[1], "macro", 1) &&
         is_compound (t[1][0], "space", 1) && is_atomic (t[1][0][0]) &&
         t[1][0][0] == "0pt";
}

bool
empty_structural (tree t) {
  if (is_atomic (t)) return t == "";
  string label= as_string (L(t));
  if (label != "concat" && label != "para" && label != "document" &&
      label != "hide-preamble") return false;
  for (int i=0; i<N(t); ++i)
    if (!empty_structural (t[i])) return false;
  return true;
}

tree
normalize_legacy (tree t) {
  if (is_atomic (t)) return t;
  if (obsolete_enunciation_name (t) || lyx_zero_width_definition (t))
    return tree ("");
  if (lyx_zero_width_space (t)) return compound ("space", "0pt");

  string label= normalized_label (as_string (L(t)));
  bool filtering= label == "concat" || label == "para" || label == "document" ||
                  label == "body" || label == "hide-preamble";
  tree out (make_tree_label (label));
  for (int i=0; i<N(t); ++i) {
    tree child= normalize_legacy (t[i]);
    if (!filtering || !empty_structural (child)) out << child;
  }
  if (label == "hide-preamble" && N(out) == 0) return tree ("");
  return out;
}

bool
empty_tree (tree t) {
  if (is_atomic (t)) return t == "";
  for (int i=0; i<N(t); ++i)
    if (!empty_tree (t[i])) return false;
  return true;
}

bool
empty_doc_date (tree t) {
  if (!is_compound (t, "doc-date")) return false;
  for (int i=0; i<N(t); ++i)
    if (!empty_tree (t[i])) return false;
  return true;
}

tree
strip_empty_doc_dates (tree t) {
  if (is_atomic (t)) return t;
  tree out (L(t));
  for (int i=0; i<N(t); ++i) {
    if (is_compound (t, "doc-data") && empty_doc_date (t[i])) continue;
    out << strip_empty_doc_dates (t[i]);
  }
  return out;
}

bool
aux_is (const AuxEntry& values, string kind) {
  return !values.empty () && values[0] == kind;
}

bool
aux_ref (const AuxEntry& values, string key, string& result) {
  for (size_t i=1; i+1<values.size (); i += 2)
    if (values[i] == key) {
      result= values[i+1];
      return true;
    }
  return false;
}

string
strip_quotes (string s) {
  if (N(s) >= 2 && s[0] == '"' && s[N(s)-1] == '"') return s (1, N(s)-1);
  return s;
}

bool
decode_aux_tree (const AuxEntry& values, tree& result) {
  if (values.size () < 3) return false;
  return decode_embedded_tree (values, 1, 2, result, "aux");
}

bool
first_aux_tree (const std::vector<AuxEntry>& aux, string kind, tree& result) {
  for (const auto& values: aux)
    if (aux_is (values, kind)) return decode_aux_tree (values, result);
  return false;
}

tree
sized_image (tree image, const AuxEntry& values) {
  string path= N(image)>0 && is_atomic (image[0]) ? strip_quotes (image[0]->label) : "";
  string old_width= N(image)>1 && is_atomic (image[1]) ? image[1]->label : "";
  string old_height= N(image)>2 && is_atomic (image[2]) ? image[2]->label : "";
  string old_x= N(image)>3 && is_atomic (image[3]) ? image[3]->label : "";
  string old_y= N(image)>4 && is_atomic (image[4]) ? image[4]->label : "";
  string width= old_width, height= old_height;
  (void) aux_ref (values, "width", width);
  (void) aux_ref (values, "height", height);
  return compound ("image", path, width, height, old_x, old_y);
}

bool
find_image (tree t, tree& image) {
  if (is_compound (t, "image")) { image= t; return true; }
  if (is_atomic (t)) return false;
  for (int i=0; i<N(t); ++i)
    if (find_image (t[i], image)) return true;
  return false;
}

tree
apply_image_aux (tree t, const std::vector<AuxEntry>& aux) {
  std::vector<const AuxEntry*> pending;
  for (const auto& values: aux)
    if (aux_is (values, "img_size")) pending.push_back (&values);
  if (pending.empty ()) return t;
  size_t next= 0;

  std::function<tree(tree)> rewrite= [&] (tree node) -> tree {
    if (is_compound (node, "image")) {
      if (next >= pending.size ()) return node;
      return sized_image (node, *pending[next++]);
    }
    if (is_compound (node, "resizebox") || is_compound (node, "scalebox")) {
      tree image;
      if (find_image (node, image) && next < pending.size ())
        return sized_image (image, *pending[next++]);
    }
    if (is_atomic (node)) return node;
    tree out (L(node));
    for (int i=0; i<N(node); ++i) out << rewrite (node[i]);
    return out;
  };

  return rewrite (t);
}

bool
tmfile (tree t) {
  if (!is_document (t)) return false;
  bool version= false, body= false;
  for (int i=0; i<N(t); ++i) {
    if (!is_compound (t[i]) || N(t[i]) == 0) return false;
    string label= as_string (L(t[i]));
    if (label == "TeXmacs") version= true;
    if (label == "body") body= true;
  }
  return version && body;
}

tree
tmfile_assign (tree t, string label, tree value) {
  tree out= copy (t);
  for (int i=N(out)-1; i>=0; --i)
    if (is_compound (out[i]) && as_string (L(out[i])) == label) {
      out[i]= tree (make_tree_label (label), value);
      return out;
    }
  tree appended (DOCUMENT);
  appended << A(out);
  appended << tree (make_tree_label (label), value);
  return appended;
}

tree
apply_document_aux (tree t, const std::vector<AuxEntry>& aux) {
  if (!tmfile (t)) return t;
  tree style, initial;
  bool has_style= first_aux_tree (aux, "document_style", style);
  bool has_initial= first_aux_tree (aux, "document_initial", initial);
  if (has_style) t= tmfile_assign (t, "style", style);
  if (has_initial) t= tmfile_assign (t, "initial", initial);
  return t;
}

} // namespace

tree
latex_native_document_to_texmacs (string source, bool as_pic) {
  PreprocessedData data= preprocess_data (source);
  tree parsed= conservative_latex_to_texmacs (data.latex, as_pic);
  tree restored= replace_objects (parsed, data.objects);
  tree normalized= normalize_legacy (restored);
  tree without_empty_dates= strip_empty_doc_dates (normalized);
  tree with_images= apply_image_aux (without_empty_dates, data.aux);
  return apply_document_aux (with_images, data.aux);
}
