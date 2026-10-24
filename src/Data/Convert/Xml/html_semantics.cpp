/******************************************************************************
* MODULE     : html_semantics.cpp
* DESCRIPTION: Native semantic plans and HTML presentation
* COPYRIGHT  : (C) 2026 ATHENA authors
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* See the file LICENSE in the root directory.
******************************************************************************/

#include "convert.hpp"
#include "html_export.hpp"
#include "file.hpp"
#include "analyze.hpp"
#include "named_symbol.hpp"
#include "scheme.hpp"
#include "Scheme/Scheme/glue.hpp"
#include "Scheme/Scheme/object.hpp"
#include "ATHENA/Data/enunciation_model.hpp"
#include "Data/Convert/Generic/conversion_options.hpp"
#include "Data/Convert/Generic/conversion_registry.hpp"
#include "System/Boot/boot.hpp"
#include "editor.hpp"
#include "new_view.hpp"
#include "config.h"
#include <algorithm>
#include <stdexcept>
#include <map>
#include <vector>
#include <nlohmann/json.hpp>

namespace {
bool head (scheme_tree t, const char* name) {
  return is_tuple (t) && N(t) > 0 && t[0] == name;
}

void check_export_node (tree source, string path) {
  if (!is_tuple (source) || !N(source)) return;
  for (const char* tag: {"eqnarray", "eqnarray*", "equation", "equation*",
       "equation-lab", "equations-base", "rsub", "rsup", "lsub", "lsup",
       "rsubsup", "lprime", "rprime", "left", "mid", "right", "big",
       "around", "around*", "big-around", "inline-tag", "open-tag",
       "middle-tag", "close-tag", "inactive", "inactive*", "multicol",
       "hybrid", "latex", "symbol", "rewrite-inactive"})
    if (head (source, tag))
      throw std::runtime_error (std::string ("Unsupported HTML export source at ") +
        path.c_str () + ": " + tag + "; export requires canonical v3 content");
}

void check_export_source (tree source, string path) {
  check_export_node (source, path);
  if (is_tuple (source))
    for (int i=1; i<N(source); ++i)
      check_export_source (source[i], path * "/" * as_string (i-1));
}

string text (const std::string& s) { return string (s.data (), int (s.size ())); }
scheme_tree quoted (string s) { return tree (scm_quote (s)); }

string atom (scheme_tree t) {
  if (!is_atomic (t)) throw std::runtime_error ("HTML export: expected text");
  return is_quoted (t->label) ? scm_unquote (t->label) : t->label;
}

scheme_tree attributes (string name, string value) {
  return tuple ("@", tuple (name, quoted (value)));
}

void append_nodes (scheme_tree& target, scheme_tree nodes) {
  if (!is_tuple (nodes)) throw std::runtime_error ("HTML export: expected a node set");
  for (int i=0; i<N(nodes); ++i) target << nodes[i];
}

void check_math_output (scheme_tree t, string path) {
  if (t == tree ("#f") || head (t, "nonconverted") ||
      head (t, "!athena-data-inline") || head (t, "athena-preserved-object"))
    throw std::runtime_error (std::string ("Unsupported HTML mathematics at ") +
      path.c_str () + ": LaTeX preservation/nonconverted output is not HTML mathematics");
  if (is_compound (t))
    for (int i=0; i<N(t); ++i)
      check_math_output (t[i], path * "/" * as_string (i));
}

string force_text (scheme_tree t) {
  if (is_atomic (t)) return atom (t);
  if (head (t, "quote") && N(t) == 2) return force_text (t[1]);
  if (head (t, "tmlen") && (N(t) == 2 || N(t) == 4))
    return force_text (t[N(t) == 2 ? 1 : 2]) * "tmpt";
  if (head (t, "tuple")) {
    string out;
    for (int i=1; i<N(t); ++i) {
      if (i > 1) out << ";";
      out << force_text (t[i]);
    }
    return out;
  }
  return tree_to_verbatim (scheme_tree_to_tree (t), false, "utf-8");
}

scheme_tree setting (scheme_tree source, const char* key) {
  if (head (source, "with"))
    for (int i=1; i+1<N(source)-1; i+=2)
      if (source[i] == quoted (key)) return source[i+1];
  return tree ("#f");
}

scheme_tree find_title (scheme_tree source) {
  if (!is_tuple (source) || N(source) == 0) return tree ("#f");
  if ((head (source, "doc-title") || head (source, "web-title") ||
       head (source, "hidden-title")) && N(source) == 2)
    return source[1];
  for (int i=1; i<N(source); ++i) {
    tree title= find_title (source[i]);
    if (title != tree ("#f")) return title;
  }
  return tree ("#f");
}

bool has_style (scheme_tree styles, const char* name) {
  for (int i=0; i<N(styles); ++i)
    if (styles[i] == quoted (name)) return true;
  return false;
}

scheme_tree css_link (string href) {
  tree attrs= attributes ("rel", "stylesheet");
  attrs << tuple ("href", quoted (href)) << tuple ("type", quoted ("text/css"));
  return tuple ("h:link", attrs);
}

void extra_resources (scheme_tree& out, scheme_tree source, const char* name,
                      bool css, bool external) {
  tree value= setting (source, name);
  if (head (value, "quote") && N(value) == 2) value= value[1];
  if (value == tree ("#f")) return;
  if (!head (value, "tuple"))
    throw std::runtime_error (std::string ("HTML export: ") + name + " must be a tuple");
  for (int i=1; i<N(value); ++i) {
    if (css) out << css_link (force_text (value[i]));
    else {
      tree attrs= attributes ("defer", "<implicit>");
      if (external) attrs << tuple ("src", quoted (force_text (value[i])));
      tree script= tuple ("h:script", attrs);
      if (!external) script << quoted (force_text (value[i]));
      out << script;
    }
  }
}
} // namespace

namespace {
struct markup_entry { const char* source; const char* html; bool first; };
const markup_entry markup_tags[]= {
  {"strong", "h:strong", false}, {"em", "h:em", false},
  {"dfn", "h:dfn", false}, {"code*", "h:code", false},
  {"samp", "h:samp", false}, {"kbd", "h:kbd", false},
  {"var", "h:var", false}, {"abbr", "h:abbr", false},
  {"acronym", "h:acronym", false}, {"tt", "h:tt", false},
  {"hrule", "h:hr", false}, {"item*", "h:dt", false},
  {"chapter-title", "h:h1", false}, {"section-title", "h:h2", false},
  {"subsection-title", "h:h3", false}, {"subsubsection-title", "h:h4", false},
  {"paragraph-title", "h:h5", false}, {"subparagraph-title", "h:h6", false},
  {"chapter", "h:h1", true}, {"chapter*", "h:h1", true},
  {"section", "h:h2", true}, {"section*", "h:h2", true},
  {"subsection", "h:h3", true}, {"subsection*", "h:h3", true},
  {"subsubsection", "h:h4", true}, {"subsubsection*", "h:h4", true},
  {"paragraph", "h:h5", true}, {"paragraph*", "h:h5", true},
  {"subparagraph", "h:h6", true}, {"subparagraph*", "h:h6", true},
  {"doc-title", "h:h1", true}, {"doc-subtitle", "h:h2", true}
};

const markup_entry* markup (tree source) {
  if (!is_tuple (source) || N(source) == 0) return nullptr;
  for (const auto& entry: markup_tags)
    if (source[0] == entry.source) return &entry;
  return nullptr;
}

int custom_markup_arity (tree source) {
  if (head (source, "html-attr") || head (source, "html-video")) return 3;
  if (head (source, "html-tag") || head (source, "html-style") ||
      head (source, "html-class") || head (source, "html-div-style") ||
      head (source, "html-div-class")) return 2;
  if (head (source, "html-javascript") || head (source, "html-javascript-src"))
    return 1;
  return 0;
}

bool logo_markup (tree source) {
  return head (source, "TeX") || head (source, "LaTeX") ||
         head (source, "TeXmacs") || head (source, "ATHENA") || head (source, "athena");
}

string figure_kind (tree source) {
  if (head (source, "big-figure") || head (source, "small-figure") ||
      head (source, "render-big-figure") || head (source, "render-small-figure"))
    return "figure";
  if (head (source, "big-table") || head (source, "small-table") ||
      head (source, "render-big-table") || head (source, "render-small-table"))
    return "table";
  return "";
}

tree logo_script (const char* tag, const char* value, bool smallcaps= false) {
  string style= "font-size: 70%; line-height: 0";
  if (smallcaps) style << "; font-variant: small-caps";
  return tuple (tag, attributes ("style", style), quoted (value));
}

tree logo_output (tree source) {
  if (head (source, "ATHENA") || head (source, "athena")) {
    tree attrs= attributes ("class", "athena-logo");
    attrs << tuple ("style", quoted ("white-space: nowrap; font-weight: bold"));
    tree result= tuple ("h:span", attrs, quoted ("A"));
    result << tuple ("h:span", attributes ("style", "vertical-align: 0.25em"), quoted ("T"));
    result << quoted ("H");
    result << tuple ("h:span", attributes ("style", "vertical-align: -0.25em"), quoted ("E"));
    result << quoted ("NA");
    return result;
  }
  bool latex= head (source, "LaTeX"), texmacs= head (source, "TeXmacs");
  tree attrs= attributes ("class", latex ? "latex-logo" : texmacs ? "texmacs-logo" : "tex-logo");
  attrs << tuple ("style", quoted ("white-space: nowrap"));
  tree result= tuple ("h:span", attrs);
  if (latex) result << quoted ("L") << logo_script ("h:sup", "A");
  result << quoted ("T") << logo_script ("h:sub", "E") << quoted ("X");
  if (texmacs) result << logo_script ("h:sub", "macs", true);
  return result;
}

tree append_attribute (tree node, string key, string value) {
  if (head (node, "h:p") && N(node) == 2 && head (node[1], "h:p"))
    return append_attribute (node[1], key, value);
  if ((head (node, "h:img") && key == "style") || !is_tuple (node))
    return tuple ("h:span", attributes (key, value), node);
  tree attrs= tuple ("@");
  int start= 1;
  bool appended= false;
  if (N(node) > 1 && head (node[1], "@")) {
    start= 2;
    for (int i=1; i<N(node[1]); ++i) {
      tree attr= node[1][i];
      if (!appended && is_tuple (attr) && N(attr) == 2 && attr[0] == key &&
          is_atomic (attr[1]) && is_quoted (attr[1]->label)) {
        attr= tuple (key, quoted (atom (attr[1]) * " " * value));
        appended= true;
      }
      attrs << attr;
    }
  }
  if (!appended) attrs << tuple (key, quoted (value));
  tree result= tuple (node[0], attrs);
  for (int i=start; i<N(node); ++i) result << node[i];
  return result;
}
} // namespace

scheme_tree html_export_prepare_source (scheme_tree source) {
  check_export_source (source, "source");
  return source;
}

string html_export_stylesheet (string link, string visited, string selection,
                               string background, bool override_background) {
  string css;
  if (load_string (url ("$ATHENA_PATH/misc/html/export.css"), css, false))
    throw std::runtime_error ("HTML export: could not load the packaged stylesheet");
  css << "\n::selection { background: " << selection << "; }\n";
  css << "::-moz-selection { background: " << selection << "; }\n";
  css << "a:link { color: " << link << "; }\n";
  css << "a:visited { color: " << visited << "; }\n";
  if (override_background) css << "html, body { background: " << background << "; }\n";
  return css;
}

scheme_tree html_export_length (scheme_tree source, string mode) {
  string value= force_text (source), unit;
  if (mode != "css" && mode != "attribute" && mode != "pixels")
    throw std::runtime_error ("HTML length: unknown output mode");
  while (starts (value, "--")) value= value (2, N(value));
  if (mode != "pixels" && is_double (value)) value << "tmpt";
  double amount= 0;
  if (ends (value, "%") || ends (value, "@")) {
    unit= value (N(value)-1, N(value));
    string number= value (0, N(value)-1);
    if (number != "" && !is_double (number)) return tree ("#f");
    amount= number == "" ? 0 : as_double (number);
  }
  else parse_length (value, amount, unit);
  struct conversion { const char* unit; double cm; };
  static const conversion units[]= {
    {"mm", .1}, {"cm", 1}, {"in", 2.54}, {"pt", 3.514598e-2},
    {"tmpt", 2.7457797e-5}, {"fn", .4}, {"em", .4}, {"ex", .2},
    {"spc", .2}, {"pc", .42175}, {"par", 16}, {"pag", 12},
    {"px", .025}, {"ln", .025}, {"%", 1}, {"@", 1}
  };
  const conversion* found= nullptr;
  for (const auto& candidate: units)
    if (unit == candidate.unit) { found= &candidate; break; }
  if (!found) return tree ("#f");
  double pixels= amount * found->cm / .025;
  if (mode == "pixels") return tree (as_string (pixels));
  if (unit == "par" || unit == "pag") return quoted (as_string (100 * amount) * "%");
  if (mode == "attribute") return quoted (as_string (pixels));
  if (unit == "@") return quoted ("auto");
  if (unit == "tmpt") return quoted (as_string (pixels) * "px");
  if (unit == "fn") return quoted (as_string (amount) * "em");
  if (unit == "spc") return quoted (as_string (amount / 2) * "em");
  if (unit == "ln" || unit == "px") return quoted (as_string (amount) * "px");
  return quoted (value);
}

string html_export_color (scheme_tree source, bool background) {
  string value= force_text (source);
  struct mapping { const char* name; const char* css; };
  static const mapping foreground[]= {
    {"light grey", "#d0d0d0"}, {"pastel grey", "#dfdfdf"}, {"dark grey", "#707070"},
    {"dark red", "#800000"}, {"dark green", "#008000"}, {"dark blue", "#000080"},
    {"dark yellow", "#808000"}, {"dark magenta", "#800080"}, {"dark cyan", "#008080"},
    {"dark orange", "#804000"}, {"dark brown", "#401000"}, {"broken white", "#ffffdf"},
    {"pastel red", "#ffdfdf"}, {"pastel green", "#dfffdf"}, {"pastel blue", "#dfdfff"},
    {"pastel yellow", "#ffffdf"}, {"pastel magenta", "#ffdfff"}, {"pastel cyan", "#dfffff"},
    {"pastel orange", "#ffdfbf"}, {"pastel brown", "#dfbfbf"}
  };
  static const mapping backgrounds[]= {
    {"light grey", "#80808060"}, {"pastel grey", "#80808040"}, {"broken white", "#ffff0020"},
    {"pastel red", "#ff000020"}, {"pastel green", "#00ff0020"}, {"pastel blue", "#0000ff20"},
    {"pastel yellow", "#ffff0020"}, {"pastel magenta", "#ff00ff20"}, {"pastel cyan", "#00ffff20"},
    {"pastel orange", "#ff800040"}, {"pastel brown", "#80000040"},
    {"#c0c0ff", "#8080ff80"}, {"#c0ffc0", "#80ff8080"}, {"#ffc0c0", "#ff808080"},
    {"#aaf", "#0000ff55"}, {"#afa", "#00ff0055"}, {"#faa", "#ff000055"},
    {"#ffa", "#ffff0055"}, {"#faf", "#ff00ff55"}, {"#aff", "#00ffff55"},
    {"#fa6", "#ff800099"}, {"", "#00000000"}
  };
  if (background)
    for (const auto& entry: backgrounds) if (value == entry.name) return entry.css;
  for (const auto& entry: foreground) if (value == entry.name) return entry.css;
  return value;
}

namespace {
struct presentation_entry {
  const char* source;
  const char* html;
  const char* css_class;
  int child; // Zero: no body; negative: final argument.
};

const presentation_entry* presentation (tree source) {
  static const presentation_entry entries[]= {
    {"rigid", "", "", 1}, {"hgroup", "", "", 1},
    {"vgroup", "", "", 1}, {"freeze", "", "", 1},
    {"unfreeze", "", "", 1}, {"move", "", "", 1},
    {"shift", "", "", 1}, {"resize", "", "", 1},
    {"clipped", "", "", 1}, {"repeat", "", "", 1},
    {"repeat*", "", "", 1}, {"syntax", "", "", 1},
    {"action", "", "", 1}, {"mark", "", "", 2},
    {"datoms", "", "", -1}, {"dlines", "", "", -1},
    {"dpages", "", "", -1}, {"dbox", "", "", -1},
    {"locus", "", "", -1},
    {"underline", "h:span", "underline", -1},
    {"overline", "h:span", "overline", -1},
    {"strike-through", "h:span", "strike-through", -1},
    {"fill-out", "h:span", "fill-out", -1},
    {"deleted", "h:del", "", -1}, {"marked", "h:mark", "", -1},
    {"new-line", "h:br", "", 0}, {"next-line", "h:br", "", 0},
    {"vspace", "", "", 0}, {"vspace*", "", "", 0}
  };
  for (const auto& entry: entries) if (head (source, entry.source)) return &entry;
  return nullptr;
}

string list_element (tree source) {
  for (const char* tag: {"itemize", "itemize-minus", "itemize-dot", "itemize-arrow"})
    if (head (source, tag)) return "h:ul";
  for (const char* tag: {"enumerate", "enumerate-numeric", "enumerate-roman",
                       "enumerate-Roman", "enumerate-alpha", "enumerate-Alpha"})
    if (head (source, tag)) return "h:ol";
  for (const char* tag: {"description", "description-compact", "description-dash",
                       "description-aligned", "description-long", "description-paragraphs"})
    if (head (source, tag)) return "h:dl";
  return "";
}

bool balloon_markup (tree source) {
  for (const char* tag: {"mouse-over-balloon", "mouse-over-balloon*",
                       "hover-balloon", "hover-balloon*", "popup-balloon",
                       "popup-balloon*"})
    if (head (source, tag)) return true;
  return false;
}

string container_element (tree source) {
  return is_multi_paragraph (scheme_tree_to_tree (source)) ? "h:div" : "h:span";
}

string exported_link (string dest) {
  int end= 0;
  while (end < N(dest) && dest[end] != '#' && dest[end] != '?') ++end;
  string path= dest (0, end);
  url target (path);
  if (is_rooted_web (target) || is_rooted_tmfs (target)) return dest;
  string ext= suffix (target);
  if (ext == "ath" || ext == "avd")
    return path (0, N(path)-N(ext)) * "html" * dest (end, N(dest));
  return dest;
}
}

scheme_tree html_export_markup_plan (scheme_tree source) {
  check_export_node (source, "markup");
  tree inputs (TUPLE);
  if (is_tuple (source) && N(source) && is_atomic (source[0]) &&
      starts (source[0]->label, "tmdoc-"))
    throw std::runtime_error ("HTML export does not support tmdoc");
  if (logo_markup (source)) return inputs;
  if (head (source, "named-symbol")) return inputs;
  if (head (source, "!left") || head (source, "!middle") || head (source, "!right")) {
    tree concat= tuple ("concat");
    for (int i=1; i<N(source); ++i) concat << source[i];
    inputs << concat;
    return inputs;
  }
  if (const auto* entry= presentation (source)) {
    int index= entry->child < 0 ? N(source)-1 : entry->child;
    if (index) {
      if (index >= N(source) || index < 1)
        throw std::runtime_error (std::string ("HTML export: missing body for ") + entry->source);
      inputs << source[index];
    }
    return inputs;
  }
  if (head (source, "hidden") || head (source, "shown")) {
    inputs << source[1]; return inputs;
  }
  if (head (source, "float") || head (source, "wide-float")) {
    inputs << source[N(source)-1]; return inputs;
  }
  if (head (source, "hspace") || head (source, "space") || head (source, "htab"))
    return inputs;
  if (N(list_element (source))) {
    if (N(source) != 2) throw std::runtime_error ("HTML export: invalid list");
    inputs << html_export_list_items (source[1]);
    return inputs;
  }
  if (head (source, "!item")) {
    if (N(source) != 3) throw std::runtime_error ("HTML export: invalid list item");
    if (!head (source[1], "item") && N(source[1]) > 1) inputs << source[1][1];
    inputs << source[2];
    return inputs;
  }
  if (head (source, "label")) return inputs;
  if (head (source, "hlink") || head (source, "hyper-link") ||
      head (source, "doc-title-block")) {
    inputs << source[1];
    return inputs;
  }
  if (balloon_markup (source)) {
    inputs << source[1] << source[2];
    return inputs;
  }
  if (figure_kind (source) != "") {
    bool rendered= starts (atom (source[0]), "render-") && N(source) >= 5;
    inputs << (rendered ? source[3] : N(source) > 1 ? source[1] : tuple ("document", quoted ("")));
    inputs << (rendered ? source[2] : quoted (""));
    inputs << (rendered ? source[4] : N(source) > 2 ? source[2] : quoted (""));
    return inputs;
  }
  if (const auto* entry= markup (source)) {
    int end= entry->first ? std::min (N(source), 2) : N(source);
    for (int i=1; i<end; ++i) inputs << source[i];
    return inputs;
  }
  if (head (source, "nbsp")) return inputs;
  int arity= custom_markup_arity (source);
  if (!arity) return tree ("#f");
  if (N(source) != arity+1)
    throw std::runtime_error (std::string ("HTML export: invalid arguments for ") +
                              atom (source[0]).c_str ());
  if (arity == 2 || head (source, "html-attr")) inputs << source[arity];
  return inputs;
}

scheme_tree html_export_markup_output (scheme_tree source, scheme_tree children) {
  tree content (TUPLE);
  for (int i=0; i<N(children); ++i) append_nodes (content, children[i]);
  tree output (TUPLE);
  if (logo_markup (source)) output << logo_output (source);
  else if (head (source, "named-symbol")) {
    string identity= atom (source[1]);
    const auto* symbol= athena::text::standard_named_symbols ().lookup (
      std::string_view (identity.data (), static_cast<size_t> (N(identity))));
    if (symbol && !symbol->glyph_utf8.empty ()) output << quoted (text (symbol->glyph_utf8));
    else {
      tree attrs= attributes ("class", "athena-named-symbol");
      attrs << tuple ("data-athena-symbol", quoted (identity));
      output << tuple ("h:span", attrs,
        quoted (starts (identity, "texmacs:") ? identity (8, N(identity)) : identity));
    }
  }
  else if (head (source, "!left") || head (source, "!middle") || head (source, "!right")) {
    content= html_export_concat_output (content);
    if (!N(content) || content == tuple (quoted (""))) return output;
    tree element= tuple ("h:div", attributes ("class",
      head (source, "!left") ? "left-tab" : head (source, "!middle") ? "center-tab" : "right-tab"));
    append_nodes (element, content);
    output << element;
  }
  else if (const auto* entry= presentation (source)) {
    if (!entry->html[0]) return content;
    tree element= tuple (string (entry->html));
    if (entry->css_class[0]) element << attributes ("class", entry->css_class);
    append_nodes (element, content);
    output << element;
  }
  else if (head (source, "hidden") || head (source, "shown")) {
    tree attrs= attributes ("class", "toggle");
    attrs << tuple ("style", quoted (head (source, "hidden") ? "display: none" : "display: block"));
    tree element= tuple (container_element (source[1]), attrs);
    append_nodes (element, content);
    output << element;
  }
  else if (head (source, "float") || head (source, "wide-float")) {
    string kind= force_text (source[1]);
    tree element= tuple ("h:div", attributes ("class", N(kind) ? kind : string ("unknown-float")));
    append_nodes (element, content);
    output << element;
  }
  else if (head (source, "hspace") || head (source, "space") || head (source, "htab")) {
    tree length= html_export_length (source[N(source) == 2 ? 1 : 2], "css");
    if (length != tree ("#f"))
      output << tuple ("h:span", attributes ("style", "margin-left: " * atom (length)));
  }
  else if (N(list_element (source))) {
    tree element= tuple (list_element (source));
    append_nodes (element, content);
    output << element;
  }
  else if (head (source, "!item")) {
    if (N(children) == 2) {
      tree term= tuple ("h:dt"), definition= tuple ("h:dd");
      append_nodes (term, children[0]);
      append_nodes (definition, children[1]);
      output << term << definition;
    }
    else {
      tree item= tuple ("h:li");
      append_nodes (item, children[0]);
      output << item;
    }
  }
  else if (head (source, "label"))
    output << tuple ("h:a", attributes ("id", force_text (source[1])));
  else if (head (source, "hlink") || head (source, "hyper-link")) {
    string dest= force_text (source[2]);
    if (starts (dest, "$")) return content;
    tree anchor= tuple ("h:a", attributes ("href", exported_link (dest)));
    append_nodes (anchor, content);
    output << anchor;
  }
  else if (head (source, "doc-title-block")) {
    tree cell= tuple ("h:td");
    append_nodes (cell, content);
    output << tuple ("h:table", attributes ("class", "title-block"), tuple ("h:tr", cell));
  }
  else if (balloon_markup (source)) {
    tree anchor= tuple (container_element (source[1]), attributes ("class", "balloon-anchor"));
    tree attrs= attributes ("class", "balloon-body");
    attrs << tuple ("hidden", quoted ("hidden"));
    tree body= tuple (container_element (source[2]), attrs);
    append_nodes (anchor, children[0]);
    append_nodes (body, children[1]);
    anchor << body;
    output << anchor;
  }
  else if (figure_kind (source) != "") {
    tree body= tuple ("h:div", attributes ("class", "figure-body"));
    append_nodes (body, children[0]);
    tree figure= tuple ("h:div", attributes ("class", "figure figure-" * figure_kind (source)), body);
    if (N(children[1]) || N(children[2])) {
      tree caption= tuple ("h:div", attributes ("class", "figure-caption"));
      if (N(children[1])) {
        tree label= tuple ("h:span", attributes ("class", "figure-caption-label"));
        append_nodes (label, children[1]);
        caption << label;
        if (N(children[2])) caption << quoted (". ");
      }
      append_nodes (caption, children[2]);
      figure << caption;
    }
    output << figure;
  }
  else if (const auto* entry= markup (source)) {
    if (entry->first && N(source) == 1) return output;
    tree element= tuple (string (entry->html));
    append_nodes (element, html_export_simplify_nodes (content));
    output << element;
  }
  else if (head (source, "nbsp")) output << quoted ("\xc2\xa0");
  else if (head (source, "html-tag")) {
    tree element= tuple (force_text (source[1]));
    append_nodes (element, content);
    output << element;
  }
  else if (head (source, "html-attr") || head (source, "html-style") ||
           head (source, "html-class")) {
    string key= head (source, "html-attr") ? force_text (source[1])
                 : head (source, "html-style") ? "style" : "class";
    string value= force_text (source[head (source, "html-attr") ? 2 : 1]);
    for (int i=0; i<N(content); ++i)
      output << append_attribute (content[i], key, value);
  }
  else if (head (source, "html-div-style") || head (source, "html-div-class")) {
    tree element= tuple ("h:div", attributes (
      head (source, "html-div-style") ? "style" : "class", force_text (source[1])));
    append_nodes (element, content);
    output << element;
  }
  else if (head (source, "html-javascript") || head (source, "html-javascript-src")) {
    tree attrs= attributes ("language", "javascript");
    bool external= head (source, "html-javascript-src");
    if (external) attrs << tuple ("src", quoted (force_text (source[1])));
    tree script= tuple ("h:script", attrs);
    if (!external) script << quoted (force_text (source[1]));
    output << script;
  }
  else if (head (source, "html-video")) {
    string dest= force_text (source[1]);
    tree size= attributes ("width", force_text (source[2]));
    size << tuple ("height", quoted (force_text (source[3])));
    tree controls= copy (size);
    controls << tuple ("controls", quoted ("controls"));
    tree video= tuple ("h:video", controls);
    for (const char* suffix: {"mp4", "ogg", "webm"}) {
      tree attrs= attributes ("src", dest * "." * suffix);
      attrs << tuple ("type", quoted (string ("video/") * suffix));
      video << tuple ("h:source", attrs);
    }
    output << video;
  }
  else throw std::runtime_error ("HTML export: unknown native markup");
  return output;
}

scheme_tree html_export_math_plan (scheme_tree source, bool display, bool block) {
  check_export_source (source, "math");
  while (head (source, "with")) {
    if (N(source) == 2) source= source[1];
    else if (N(source) == 4 && source[1] == quoted ("math-display") &&
             source[2] == quoted ("true")) {
      display= true;
      source= source[3];
    }
    else break;
  }
  if (block && !head (source, "math-block"))
    throw std::runtime_error ("HTML MathBlock export requires a prepared MathBlock");
  tree plan (TUPLE);
  plan << source << tree (display ? "#t" : "#f") << tree (block ? "#t" : "#f");
  return plan;
}

scheme_tree html_export_math_output (scheme_tree latex, bool display, bool block) {
  check_math_output (latex, "math");
  string source= serialize_latex (latex);
  tree output (TUPLE);
  if (block) {
    // MathBlock already owns its equation environment and numbering.
    output << tuple ("h:div", attributes ("class", "athena-math-block"),
                     quoted (source));
    return output;
  }
  string math= display ? "\\[\\displaystyle " * source * "\\]"
                       : "\\(" * source * "\\)";
  output << quoted (math);
  return output;
}

scheme_tree html_export_enunciation_plan (scheme_tree source) {
  if (!is_tuple (source) || N(source) == 0 || !is_atomic (source[0]))
    return tree ("#f");
  string tag= source[0]->label;
  if (tag == "enunciation")
    throw std::runtime_error (
      "HTML export: a property-backed enunciation must pass through native "
      "document expansion before conversion; tree->stree cannot retain its fields");
  const auto& registry= athena::enunciation::standard_registry ();
  const auto* legacy= registry.legacy (tag.c_str ());
  if (!legacy) return tree ("#f");
  const auto* definition= registry.kind (legacy->kind);
  const auto& html= definition->declaration.at ("html");
  string kind= text (html.value ("class", definition->kind));
  string group= text (html.at ("group").get<std::string> ());
  string background= text (registry.declaration ().at ("html_groups")
    .at (group.c_str ()).get<std::string> ());
  std::string title= definition->display_name;
  if (!legacy->variant.empty ()) {
    const auto& variant= definition->variants.at (legacy->variant);
    title= variant.display_name;
    if (!starts (tag, "render-")) kind= kind * "-" * text (legacy->variant);
  }
  tree heading= quoted (text (title));
  tree body;
  if (legacy->layout == athena::enunciation::legacy_layout::body) {
    if (N(source) == 2) body= source[1];
    else {
      body= tuple ("document");
      for (int i=1; i<N(source); ++i) body << source[i];
    }
  }
  else {
    if (N(source) != 3)
      throw std::runtime_error (std::string ("HTML export: ") + tag.c_str () +
                                " requires title/subject and body");
    if (source[1] != quoted ("")) heading= source[1];
    body= source[2];
  }
  tree plan (TUPLE);
  plan << quoted (kind) << quoted (group) << quoted (background) << heading << body;
  return plan;
}

scheme_tree html_export_enunciation_output (scheme_tree plan,
    scheme_tree title, scheme_tree body, string color) {
  if (!is_tuple (plan) || N(plan) != 5)
    throw std::runtime_error ("HTML export: invalid enunciation plan");
  tree attrs= attributes ("class", "enunciation enunciation-" * atom (plan[0]) *
                                  " enunciation-" * atom (plan[1]));
  attrs << tuple ("style", quoted ("background-color:" * color));
  tree heading= tuple ("h:div", attributes ("class", "enunciation-title"));
  append_nodes (heading, title);
  tree element= tuple ("h:div", attrs, heading);
  append_nodes (element, body);
  tree result (TUPLE);
  result << element;
  return result;
}

scheme_tree html_export_enunciation_tags () {
  tree result (TUPLE);
  for (const auto& [tag, definition]: athena::enunciation::standard_registry ().legacy_tags ())
    result << tree (text (tag));
  return result;
}

tree html_export_environment_patch () {
  static const std::vector<string> preserved= [] {
    string source;
    if (load_string (url ("$ATHENA_PATH/misc/convert/html-expansion.json"), source, false))
      throw std::runtime_error ("Cannot load HTML expansion registry");
    auto data= nlohmann::json::parse (source.data (), source.data () + N(source));
    if (data.at ("version").get<int> () != 1)
      throw std::runtime_error ("Unsupported HTML expansion registry version");
    std::vector<string> result;
    for (const auto& name: data.at ("preserve"))
      result.emplace_back (name.get<std::string> ().c_str ());
    return result;
  } ();
  tree result (COLLECTION);
  // Preserve unsupported source tags too, so export rejects them instead of
  // silently accepting their style-expanded presentation.
  auto preserve= [&] (string name) {
    result << tree (ASSOCIATE, name, tree (XMACRO, "x", tree (EVAL_ARGS, "x")));
  };
  for (const auto& name: preserved) preserve (name);
  for (const auto& [tag, definition]: athena::enunciation::standard_registry ().legacy_tags ())
    preserve (text (tag));
  return result;
}

scheme_tree html_export_document_plan (scheme_tree source, scheme_tree styles,
    string css, string stylesheet, string generator, bool mathjax) {
  check_export_source (source, "body");
  if (has_style (styles, "tmdoc"))
    throw std::runtime_error ("HTML export does not support the tmdoc document style");
  tree title= find_title (source);
  tree explicit_title= setting (source, "html-title");
  tree environment_title= setting (source, "html-doc-title");
  if (explicit_title != tree ("#f")) title= quoted (force_text (explicit_title));
  else if (title == tree ("#f") && environment_title != tree ("#f"))
    title= quoted (force_text (environment_title));
  else if (title == tree ("#f")) title= quoted ("No title");
  else {
    string value= force_text (title);
    title= quoted (value);
  }
  tree resources (TUPLE);
  tree generator_attrs= attributes ("name", "generator");
  generator_attrs << tuple ("content", quoted (generator));
  resources << tuple ("h:meta", generator_attrs);
  tree custom_css= setting (source, "html-css");
  resources << (custom_css != tree ("#f") ? css_link (force_text (custom_css))
    : tuple ("h:style", attributes ("type", "text/css"), quoted (css)));
  tree external_script= setting (source, "html-head-javascript-src");
  if (external_script != tree ("#f"))
    resources << tuple ("h:script", attributes ("src", force_text (external_script)));
  tree script= setting (source, "html-head-javascript");
  if (script != tree ("#f")) resources << tuple ("h:script", quoted (force_text (script)));
  tree favicon= setting (source, "html-head-favicon");
  if (favicon != tree ("#f")) {
    tree attrs= attributes ("rel", "icon");
    attrs << tuple ("href", quoted (force_text (favicon)));
    resources << tuple ("h:link", attrs);
  }
  if (custom_css == tree ("#f") && ends (stylesheet, ".css"))
    resources << css_link (stylesheet);
  extra_resources (resources, source, "html-extra-css", true, true);
  extra_resources (resources, source, "html-extra-javascript-src", false, true);
  extra_resources (resources, source, "html-extra-javascript", false, false);
  if (mathjax) {
    resources << tuple ("h:script", quoted (
      "window.MathJax = window.MathJax || {}; window.MathJax.tex = "
      "window.MathJax.tex || {}; window.MathJax.tex.tags = 'ams';"));
    resources << tuple ("h:script", attributes ("src",
      "https://cdn.jsdelivr.net/npm/mathjax@3/es5/tex-mml-chtml.js"));
  }
  tree site_version= setting (source, "html-site-version");
  if (site_version != tree ("#f")) site_version= quoted (force_text (site_version));
  tree plan (TUPLE);
  plan << title << site_version << resources;
  return plan;
}

scheme_tree html_export_document_output (scheme_tree plan,
    scheme_tree title, scheme_tree body) {
  if (!is_tuple (plan) || N(plan) != 3)
    throw std::runtime_error ("HTML export: invalid document plan");
  tree heading= tuple ("h:title");
  append_nodes (heading, title);
  tree head_node= tuple ("h:head",
    tuple ("h:meta", attributes ("charset", "utf-8")), heading);
  append_nodes (head_node, plan[2]);
  tree body_node= tuple ("h:body");
  append_nodes (body_node, body);
  return tuple ("h:html", head_node, body_node);
}

namespace {
using html_properties= std::map<std::string, tree>;

tree table_unwrap (tree source, tree& rules) {
  while (head (source, "tformat") ||
         (head (source, "document") && N(source) == 2)) {
    for (int i=1; i<N(source)-1; ++i) rules << source[i];
    source= source[N(source)-1];
  }
  return source;
}

int table_index (tree value, int size) {
  int index= std::stoi (atom (value).c_str ());
  return index < 0 ? size+index : index-1;
}

void table_cell_properties (html_properties& props, tree rules,
                           int row, int col, int rows, int cols, int scope) {
  for (int i=0; i<N(rules); ++i) {
    tree rule= rules[i];
    if (head (rule, "twith")) continue;
    if (!head (rule, "cwith") || N(rule) != 7-2*scope)
      throw std::runtime_error ("HTML export: malformed table formatting rule");
    if (scope == 0 && (row < table_index (rule[1], rows) ||
                      row > table_index (rule[2], rows))) continue;
    int offset= scope == 0 ? 3 : 1;
    if (scope < 2 && (col < table_index (rule[offset], cols) ||
                     col > table_index (rule[offset+1], cols))) continue;
    props[atom (rule[N(rule)-2]).c_str ()]= rule[N(rule)-1];
  }
}

string property_text (const html_properties& props, const char* key) {
  auto it= props.find (key);
  return it == props.end () ? string () : force_text (it->second);
}

void css_declaration (string& css, string key, string value) {
  if (N(css)) css << "; ";
  css << key << ": " << value;
}

string table_style (const html_properties& props, bool cell) {
  string css;
  const string prefix= cell ? "cell-" : "table-";
  for (auto& prop: props) {
    string key= text (prop.first), value;
    if (!starts (key, prefix)) continue;
    key= key (N(prefix), N(key));
    string target;
    bool border= false;
    if (key == "width" || key == "height") target= key;
    for (auto side: {std::pair<const char*, const char*> ("l", "left"),
                    {"r", "right"}, {"t", "top"}, {"b", "bottom"}}) {
      if (key == string (side.first) * "border") {
        target= string ("border-") * side.second; border= true;
      }
      if (key == string (side.first) * "sep")
        target= string ("padding-") * side.second;
    }
    if (N(target)) {
      tree length= html_export_length (prop.second, "css");
      if (length != tree ("#f"))
        css_declaration (css, target, atom (length) * (border ? " solid" : ""));
    }
    else if (key == "halign") {
      value= atom (prop.second);
      if (value == "l" || value == "c" || value == "r")
        css_declaration (css, "text-align",
                         value == "l" ? "left" : value == "c" ? "center" : "right");
    }
    else if (key == "valign" && cell) {
      value= atom (prop.second);
      if (value == "t" || value == "c" || value == "b" || value == "B")
        css_declaration (css, "vertical-align", value == "t" ? "top" :
          value == "c" ? "middle" : value == "b" ? "bottom" : "baseline");
    }
    else if (key == "background") {
      tree bg= prop.second;
      if (is_atomic (bg))
        css_declaration (css, "background-color", html_export_color (bg, true));
      else if (head (bg, "pattern") && N(bg) == 4) {
        string uri= force_text (bg[1]), escaped;
        for (int i=0; i<N(uri); ++i) {
          if (uri[i] == '\\' || uri[i] == '\'') escaped << "\\";
          escaped << uri[i];
        }
        css_declaration (css, "background-image", "url('" * escaped * "')");
        tree w= html_export_length (bg[2], "css");
        tree h= html_export_length (bg[3], "css");
        css_declaration (css, "background-size",
          (w == tree ("#f") ? string ("100%") : atom (w)) * " " *
          (h == tree ("#f") ? string ("100%") : atom (h)));
      }
    }
  }
  if (cell) {
    string block= property_text (props, "cell-block");
    string hyphen= property_text (props, "cell-hyphen");
    if (block == "no" || (block == "auto" &&
        hyphen != "t" && hyphen != "c" && hyphen != "b"))
      css_declaration (css, "white-space", "nowrap");
  }
  else {
    auto width= props.find ("table-width");
    if (width == props.end () ||
        html_export_length (width->second, "css") != quoted ("100%")) {
      css_declaration (css, "display", "inline-table");
      css_declaration (css, "vertical-align", "middle");
    }
  }
  return css;
}
}

scheme_tree html_export_table_plan (scheme_tree source) {
  tree rules= tuple ();
  tree table= table_unwrap (source, rules);
  if (!head (table, "table"))
    throw std::runtime_error ("HTML export: expected a table");
  int rows= N(table)-1, cols= 0;
  std::vector<tree> row_nodes, row_rules;
  for (int r=0; r<rows; ++r) {
    tree rr= tuple (), row= table_unwrap (table[r+1], rr);
    if (!head (row, "row"))
      throw std::runtime_error ("HTML export: expected a table row");
    cols= std::max (cols, N(row)-1);
    row_nodes.push_back (row); row_rules.push_back (rr);
  }
  html_properties table_props;
  for (int i=0; i<N(rules); ++i)
    if (head (rules[i], "twith") && N(rules[i]) == 3)
      table_props[atom (rules[i][1]).c_str ()]= rules[i][2];
  tree sources= tuple (), body= tuple ("h:tbody");
  std::vector<bool> covered (rows*cols, false);
  for (int r=0; r<rows; ++r) {
    std::vector<html_properties> props (cols);
    std::vector<tree> cells;
    double sum= 0;
    for (int c=0; c<cols; ++c) {
      tree cr= tuple ();
      tree cell= c+1 < N(row_nodes[r]) ?
        table_unwrap (row_nodes[r][c+1], cr) : tuple ("cell", quoted (""));
      if (!head (cell, "cell") || N(cell) != 2)
        throw std::runtime_error ("HTML export: expected a table cell");
      cells.push_back (cell[1]);
      table_cell_properties (props[c], rules, r, c, rows, cols, 0);
      table_cell_properties (props[c], row_rules[r], r, c, rows, cols, 1);
      table_cell_properties (props[c], cr, r, c, rows, cols, 2);
      string part= property_text (props[c], "cell-hpart");
      if (N(part)) sum+= as_double (part);
    }
    tree row= tuple ("h:tr");
    for (int c=0; c<cols; ++c) {
      if (covered[r*cols+c]) continue;
      string part= property_text (props[c], "cell-hpart");
      if (sum != 0 && N(part) && as_double (part) != 0)
        props[c]["cell-width"]= quoted (as_string (as_double (part)/sum) * "par");
      tree attrs= attributes ("style", table_style (props[c], true));
      string rs= property_text (props[c], "cell-row-span");
      string cs= property_text (props[c], "cell-col-span");
      int nr= N(rs) ? std::stoi (rs.c_str ()) : 1;
      int nc= N(cs) ? std::stoi (cs.c_str ()) : 1;
      nr= std::min (rows-r, std::max (1, nr));
      nc= std::min (cols-c, std::max (1, nc));
      if (nr > 1) attrs << tuple ("rowspan", quoted (as_string (nr)));
      if (nc > 1) attrs << tuple ("colspan", quoted (as_string (nc)));
      for (int y=r; y<r+nr; ++y)
        for (int x=c; x<c+nc; ++x) covered[y*cols+x]= true;
      row << tuple ("h:td", attrs);
      sources << cells[c];
    }
    body << row;
  }
  return tuple (tuple ("h:table", attributes ("style", table_style (table_props, false)), body), sources);
}

scheme_tree html_export_table_output (scheme_tree plan, scheme_tree children) {
  if (!is_tuple (plan) || N(plan) != 2 || !is_tuple (children) ||
      N(children) != N(plan[1]))
    throw std::runtime_error ("HTML export: invalid converted table cells");
  tree result= copy (plan[0]);
  int next= 0;
  for (int r=1; r<N(result[2]); ++r)
    for (int c=1; c<N(result[2][r]); ++c)
      append_nodes (result[2][r][c], children[next++]);
  return tuple (result);
}

namespace {
bool contains_surround (tree source) {
  if (head (source, "surround")) return true;
  if (is_tuple (source))
    for (int i=1; i<N(source); ++i)
      if (contains_surround (source[i])) return true;
  return false;
}
}

scheme_tree html_export_ornament (scheme_tree source, scheme_tree body,
                                 scheme_tree environment) {
  struct rule { const char* key; const char* first; const char* second; int type; };
  static const rule rules[]= {
    {"ornament-hpadding", "padding-left", "padding-right", 0},
    {"ornament-vpadding", "padding-top", "padding-bottom", 0},
    {"ornament-border", "border-width", nullptr, 0},
    {"ornament-shape", "border-radius", nullptr, 1},
    {"ornament-color", "background-color", nullptr, 2},
    {"ornament-shadow-color", "border-bottom-color", "border-right-color", 2},
    {"ornament-sunny-color", "border-left-color", "border-top-color", 2}
  };
  string css;
  for (const auto& rule: rules) {
    tree value ("#f");
    for (int i=0; i<N(environment); ++i)
      if (atom (environment[i][0]) == rule.key) value= environment[i][1];
    if (value == tree ("#f")) continue;
    string raw= force_text (value);
    std::vector<string> values;
    int begin= 0;
    for (int i=0; i<=N(raw); ++i) {
      if (i != N(raw) && raw[i] != ';') continue;
      string part= raw (begin, i);
      begin= i+1;
      if (rule.type == 0) {
        tree length= html_export_length (quoted (part), "css");
        if (length == tree ("#f"))
          throw std::runtime_error (std::string ("HTML export: unsupported ") + rule.key);
        part= atom (length);
      }
      else if (rule.type == 1) part= part == "rounded" ? "15px" : "0px";
      else part= html_export_color (quoted (part), false);
      values.push_back (part);
    }
    if (rule.second) {
      if (values.size () > 2)
        throw std::runtime_error (std::string ("HTML export: too many values for ") + rule.key);
      css_declaration (css, rule.first, values[0]);
      css_declaration (css, rule.second, values[values.size ()-1]);
    }
    else {
      string combined;
      for (auto& part: values) {
        if (N(combined)) combined << " ";
        combined << part;
      }
      css_declaration (css, rule.first, combined);
    }
  }
  string tag= container_element (source);
  if (tag == "h:div" || contains_surround (source))
    css_declaration (css, "display", "block");
  tree attrs= attributes ("class", "ornament");
  if (N(css)) attrs << tuple ("style", quoted (css));
  tree out= tuple (tag, attrs);
  append_nodes (out, body);
  return tuple (out);
}

scheme_tree html_export_style_plan (scheme_tree source, double left, double right) {
  if ((head (source, "verbatim") || head (source, "code")) && N(source) == 2) {
    tree body= source[1];
    bool block= false;
    if (head (body, "with") && N(body) == 2) body= body[1];
    if (head (body, "with") && N(body) == 4 && body[1] == quoted ("locus-color"))
      body= body[3];
    if (head (body, "action") && N(body) > 1) {
      body= body[1]; block= is_atomic (body);
    }
    block= block || is_multi_paragraph (scheme_tree_to_tree (body));
    tree attrs= attributes ("class", "verbatim");
    if (block) attrs << tuple ("xml:space", quoted ("preserve"));
    tree plan= tuple (body, block ? quoted ("preformatted") : tree ("#f"),
                     tree ("#t"), tuple (block ? "h:pre" : "h:code", attrs));
    plan << tree ("#f");
    return plan;
  }
  if (!head (source, "with") || N(source) != 4)
    throw std::runtime_error ("HTML export: expected one formatting assignment");
  string var= force_text (source[1]), value= force_text (source[2]);
  tree body= source[3], key ("#f"), state ("#f"), wrapper ("#f");
  bool trim_empty= false;
  auto style= [&] (string css, bool block) {
    wrapper= tuple (block ? "h:div" : "h:span", attributes ("style", css));
  };
  if (var == "mode") {
    key= quoted ("math"); state= tree (value == "math" ? "#t" : "#f");
    if (value == "prog") body= tuple ("verbatim", body);
  }
  else if (var == "math-display") {
    key= quoted ("math-display"); state= tree (value == "true" ? "#t" : "#f");
  }
  else if (var == "color") style ("color: " * html_export_color (quoted (value), false), false);
  else if (var == "font-size") {
    if (!is_double (value)) throw std::runtime_error ("HTML export: invalid font size");
    key= quoted ("mag"); state= quoted (value);
    style ("font-size: " * as_string (100 * as_double (value)) * "%", false);
  }
  else if (var == "par-left" || var == "par-right") {
    tree length= html_export_length (quoted (value), "pixels");
    if (length != tree ("#f")) {
      bool leading= var == "par-left";
      key= quoted (leading ? "left-margin" : "right-margin"); state= length;
      style (string (leading ? "margin-left: " : "margin-right: ") *
             as_string (as_double (atom (length)) - (leading ? left : right)) * "px", true);
      body= html_export_blockify (body); trim_empty= true;
    }
  }
  else if (var == "par-first") {
    tree length= html_export_length (quoted (value), "css");
    if (length != tree ("#f")) {
      style ("text-indent: " * atom (length), true);
      body= html_export_blockify (body); trim_empty= true;
    }
  }
  else if (var == "par-par-sep") {
    tree length= html_export_length (quoted (value), "pixels");
    if (length != tree ("#f") && as_double (atom (length)) == 0)
      wrapper= tuple ("h:div", attributes ("class", "compact-block"));
  }
  else if (var == "par-mode" &&
           (value == "left" || value == "right" || value == "center" || value == "justify"))
    style ("text-align: " * value, true);
  else if (var == "font-family") {
    if (value == "tt") wrapper= tuple ("h:span", attributes ("class", "verbatim"));
    if (value == "ss") style ("font-family: sans-serif", false);
  }
  else if (var == "font-series" && (value == "bold" || value == "medium"))
    style (value == "bold" ? "font-weight: bold" : "font-weight: normal", false);
  else if (var == "font-shape") {
    if (value == "italic") style ("font-style: italic", false);
    if (value == "right") style ("font-style: normal", false);
    if (value == "small-caps") style ("font-variant: small-caps", false);
  }
  else if (var == "font" || var == "math-font") {
    struct font { const char* name; const char* family; };
    static const font text_fonts[]= {
      {"roman", "Times New Roman"}, {"times", "Times New Roman"},
      {"helvetica", "Helvetica"}, {"courier", "Courier New, Courier, monospace"},
      {"bonum", "TeX Gyre Bonum, Bookman Old Style, Georgia, serif"},
      {"pagella", "TeX Gyre Pagella, Palatino Linotype, Palatino, Georgia, serif"},
      {"schola", "TeX Gyre Schola, Century Schoolbook, Georgia, serif"},
      {"termes", "TeX Gyre Termes, Times New Roman, Times, serif"},
      {"stix", "STIX Two Text, STIXGeneral, Times New Roman, serif"},
      {"dejavu", "DejaVu Serif, Georgia, serif"}
    };
    static const font math_fonts[]= {
      {"cal", "Flemish Script"}, {"frak", "Bernhard Modern"},
      {"math-bonum", "TeX Gyre Bonum Math, TeX Gyre Bonum, serif"},
      {"math-pagella", "TeX Gyre Pagella Math, TeX Gyre Pagella, serif"},
      {"math-schola", "TeX Gyre Schola Math, TeX Gyre Schola, serif"},
      {"math-termes", "TeX Gyre Termes Math, TeX Gyre Termes, serif"},
      {"math-stix", "STIX Two Math, STIXGeneral, serif"}
    };
    if (var == "font") {
      for (auto& font: text_fonts)
        if (value == font.name) style (string ("font-family: ") * font.family, false);
    }
    else for (auto& font: math_fonts)
      if (value == font.name) style (string ("font-family: ") * font.family, false);
  }
  else for (const char* name: {"ornament-hpadding", "ornament-vpadding",
        "ornament-border", "ornament-shape", "ornament-color",
        "ornament-shadow-color", "ornament-sunny-color"})
    if (var == name) { key= quoted (var); state= quoted (value); break; }
  tree plan= tuple (body, key, state, wrapper);
  plan << tree (trim_empty ? "#t" : "#f");
  return plan;
}

scheme_tree html_export_style_output (scheme_tree plan, scheme_tree children) {
  if (!is_tuple (plan) || N(plan) != 5)
    throw std::runtime_error ("HTML export: invalid formatting plan");
  if (plan[4] == tree ("#t") && (!N(children) || children == tuple (quoted ("")) ||
      children == tuple (tuple ("h:p")) || children == tuple (tuple ("h:p", quoted ("")))))
    return tuple ();
  if (plan[3] == tree ("#f")) return children;
  tree wrapper= copy (plan[3]);
  append_nodes (wrapper, children);
  return tuple (wrapper);
}

scheme_tree html_export_image_output (scheme_tree source, string location) {
  if (!head (source, "image") || N(source) < 4)
    throw std::runtime_error ("HTML export: invalid image source");
  tree attrs= attributes ("class", "image");
  attrs << tuple ("src", quoted (location));
  string css;
  for (int i=2; i<=3; ++i) {
    tree length= html_export_length (source[i], "css");
    if (length != tree ("#f"))
      css_declaration (css, i == 2 ? "width" : "height", atom (length));
  }
  if (N(css)) attrs << tuple ("style", quoted (css));
  return tuple (tuple ("h:img", attrs));
}

namespace {
void picture_labels (tree source, tree& labels) {
  if (head (source, "label") && N(source) == 2) {
    string label= force_text (source[1]);
    for (int i=0; i<N(labels); ++i) if (labels[i] == quoted (label)) return;
    labels << quoted (label);
  }
  else if (is_tuple (source))
    for (int i=1; i<N(source); ++i) picture_labels (source[i], labels);
}
}

scheme_tree html_export_picture_output (scheme_tree source, string location,
                                        scheme_tree extents, double scale) {
  if (!is_tuple (extents) || N(extents) != 10)
    throw std::runtime_error ("HTML export: invalid rendered picture extents");
  double e[10];
  for (int i=0; i<10; ++i) {
    string value= atom (extents[i]);
    if (!is_double (value))
      throw std::runtime_error ("HTML export: nonnumeric rendered picture extent");
    e[i]= as_double (value);
  }
  if (e[8] <= 0 || e[9] <= 0 || scale <= 0)
    throw std::runtime_error ("HTML export: invalid rendered picture scale");
  double unit= 6000.0/(e[8]*e[9]*scale);
  string css;
  css_declaration (css, "margin-left", as_string ((e[0]-e[4])*unit) * "em");
  css_declaration (css, "margin-bottom", as_string ((e[1]-e[5])*unit) * "em");
  css_declaration (css, "margin-right", as_string ((e[6]-e[2])*unit) * "em");
  css_declaration (css, "margin-top", as_string ((e[7]-e[3])*unit) * "em");
  css_declaration (css, "vertical-align", as_string (e[5]*unit) * "em");
  css_declaration (css, "height", as_string ((e[3]-e[1])*unit) * "em");
  tree attrs= attributes ("src", location);
  attrs << tuple ("style", quoted (css));
  tree labels= tuple (), output= tuple ();
  picture_labels (source, labels);
  if (N(labels)) attrs << tuple ("id", labels[0]);
  for (int i=1; i<N(labels); ++i)
    output << tuple ("h:a", tuple ("@", tuple ("id", labels[i])));
  output << tuple ("h:img", attrs);
  return output;
}

namespace {
using export_environment= std::map<std::string, tree>;

class html_exporter {
  url target;
  string stylesheet;
  url image_root;
  string image_name;
  int image_serial= 0;
  hashmap<tree, tree> pictures {UNINIT};

  std::pair<url, string> image_names (string extension) {
    string postfix= "-" * as_string (++image_serial) * "." * extension;
    return {glue (image_root, postfix), image_name * postfix};
  }

  static bool contains_math (tree source) {
    if (!is_tuple (source)) return false;
    for (const char* tag: {"math", "math-block", "math-grid", "scripts", "math-accent",
         "math-annotation", "math-long-arrow", "math-middle", "math-symbol",
         "math-variant", "delimited", "big-operator"})
      if (head (source, tag)) return true;
    for (int i=0; i<N(source); ++i)
      if (contains_math (source[i])) return true;
    return false;
  }

  tree picture (tree source) {
    if (contains_math (source))
      throw std::runtime_error ("Unsupported XML v3 HTML export: raster fallback would discard native mathematics");
    if (pictures->contains (source)) return pictures[source];
    auto [destination, location]= image_names ("png");
    array<SI> rendered= get_current_editor ()->print_snippet (
      destination, scheme_tree_to_tree (source), true);
    if (!exists (destination))
      throw std::runtime_error ("HTML export: rendered picture was not produced");
    tree extents (TUPLE);
    for (int i=0; i<N(rendered); ++i) extents << tree (as_string (rendered[i]));
    double scale= starts (as_string (tail (url (stylesheet))), "web-") ? 19000.0 : 20625.0;
    tree output= html_export_picture_output (source, location, extents, scale);
    pictures(source)= output;
    return output;
  }

  tree image (tree source, url source_url) {
    tree name= source[1];
    string extension;
    if (is_atomic (name)) extension= suffix (url_unix (atom (name)));
    else if (is_tuple (name, "tuple", 2) && is_atomic (name[2])) {
      extension= suffix (url_unix (atom (name[2])));
      if (extension == "") extension= atom (name[2]);
    }
    if (extension != "gif" && extension != "jpg" && extension != "jpeg" &&
        extension != "png" && extension != "bmp" && extension != "svg")
      return picture (source);
    if (is_tuple (name, "tuple", 2) && is_tuple (name[1], "raw-data", 1)) {
      auto [destination, location]= image_names (extension);
      if (save_string (destination, atom (name[1][1]), false))
        throw std::runtime_error ("HTML export: could not write embedded image");
      tree output= copy (source);
      output[1]= quoted (location);
      return html_export_image_output (output, location);
    }
    if (!is_atomic (name)) return picture (source);
    string location= atom (name);
    url relative_name= url_unix (location);
    if (!is_rooted_web (relative_name)) {
      url original= relative (source_url, relative_name);
      if (!exists (original))
        throw std::runtime_error ("HTML export: image source does not exist: " +
                                 std::string (as_string (original).c_str ()));
      if (original != relative (target, relative_name)) {
        auto names= image_names (extension);
        copy (original, names.first);
        location= names.second;
      }
    }
    return html_export_image_output (source, location);
  }

  static string preference_color (string key, string fallback) {
    string value= get_user_preference (key);
    return value == "" || value == "default" ? fallback : value;
  }

  tree request (const char* operation, tree source, const export_environment& env) {
    string op (operation);
    url source_url= url (atom (env.at ("source-url")));
    if (op == "math")
      return latex_export_convert_top (source[0], conversion_options_to_scheme (
        {{"texmacs->latex:mathjax", "on"}}), source_url, target);
    if (op == "picture") return picture (source);
    if (op == "image") return image (source, source_url);
    if (op == "include") {
      url included= relative (source_url, url_unix (atom (source[1])));
      string bytes;
      if (load_string (included, bytes, false))
        throw std::runtime_error ("HTML export: included source could not be read");
      tree document= conversion_read_document (bytes);
      tree body= extract (document, "body");
      return tuple (quoted (as_string (included)),
        html_export_prepare_source (latex_export_prepare_v3_source (body)));
    }
    if (op == "document-plan") {
      auto color= [] (string key, string fallback) {
        return html_export_color (quoted (preference_color (key, fallback)), false);
      };
      string css= html_export_stylesheet (color ("locus-color", "#404080"),
        color ("visited-color", "#702070"), color ("gui selection color", "red"),
        color ("white document background override color", "#f7f3e8"),
        get_user_preference ("override white document background") == "on");
      tree styles (TUPLE);
      for (int i=1; i<N(source[2]); ++i) styles << source[2][i];
      return html_export_document_plan (source[1], styles, css, stylesheet,
                                       "ATHENA " * string (ATHENA_VERSION), true);
    }
    if (op == "enunciation-color") {
      string color= get_user_preference ("vault " * atom (source[0]) * " color");
      if (color == "" || color == "none")
        color= get_user_preference ("vault " * atom (source[1]) * " color");
      return quoted (color == "" || color == "none" ? atom (source[2]) : color);
    }
    throw std::runtime_error ("Unknown HTML resource operation: " + std::string (operation));
  }

  tree children (tree inputs, const export_environment& env) {
    tree output= tuple ();
    for (int i=0; i<N(inputs); ++i) output << run (inputs[i], env);
    return output;
  }

  tree mathematics (tree source, bool display, bool block, const export_environment& env) {
    tree plan= html_export_math_plan (source, display, block);
    return html_export_math_output (request ("math", plan, env),
                                    plan[1] == tree ("#t"), plan[2] == tree ("#t"));
  }

public:
  html_exporter (url destination, const conversion_options& options): target (destination) {
    auto css= options.find ("texmacs->html:css-stylesheet");
    stylesheet= css == options.end () ? string ("---") : string (css->second.c_str ());
    string extension= suffix (target);
    image_root= extension == "html" || extension == "htm"
      ? unglue (target, N(extension)+1) : url_unix ("image");
    image_name= as_string (tail (image_root));
  }

  tree run (tree source, const export_environment& env) {
    if (head (source, "math-block")) return mathematics (source, true, true, env);
    if (env.at ("math") == tree ("#t"))
      return mathematics (source, env.at ("math-display") == tree ("#t"), false, env);
    if (is_atomic (source)) return html_export_text (atom (source), env.at ("preformatted") == tree ("#t"));
    if (!is_tuple (source) || !N(source))
      throw std::runtime_error ("HTML export: invalid source node");
    if (head (source, "document") || head (source, "para")) {
      tree plan= html_export_sequence_plan (source, env.at ("preformatted") == tree ("#t"));
      return html_export_sequence_output (plan, children (plan[0], env));
    }
    if (head (source, "concat")) {
      tree groups= children (html_export_concat_plan (source), env), output= tuple ();
      for (int i=0; i<N(groups); ++i) append_nodes (output, groups[i]);
      return html_export_concat_output (output);
    }
    if (head (source, "surround"))
      return run (html_export_normalize_document (source, true), env);
    if (head (source, "with") || head (source, "verbatim") || head (source, "code")) {
      if (head (source, "with")) {
        if (N(source) == 2) return run (source[1], env);
        if (N(source) < 4 || N(source)%2)
          throw std::runtime_error ("HTML export: invalid formatting assignment");
        if (head (source[N(source)-1], "graphics")) return request ("picture", source, env);
        if (N(source) > 4) {
          tree rest= tuple ("with");
          for (int i=3; i<N(source); ++i) rest << source[i];
          source= tuple ("with", source[1], source[2], rest);
        }
      }
      tree plan= html_export_style_plan (source, as_double (atom (env.at ("left-margin"))),
                                        as_double (atom (env.at ("right-margin"))));
      export_environment local= env;
      if (plan[1] != tree ("#f")) local[atom (plan[1]).c_str ()]= plan[2];
      return html_export_style_output (plan, run (plan[0], local));
    }
    if (head (source, "table") || head (source, "tformat")) {
      tree plan= html_export_table_plan (source);
      export_environment local= env;
      local["left-margin"]= tree ("0");
      return html_export_table_output (plan, children (plan[1], local));
    }
    if (head (source, "ornament") || head (source, "ornamented")) {
      tree settings= tuple ();
      for (const auto& entry: env)
        if (entry.first.compare (0, 9, "ornament-") == 0)
          settings << tuple (quoted (text (entry.first)), entry.second);
      return html_export_ornament (source[1], run (source[1], env), settings);
    }
    if (head (source, "math")) {
      tree body= N(source) == 2 ? source[1] : tuple ("concat");
      if (N(source) != 2) for (int i=1; i<N(source); ++i) body << source[i];
      tree plan= html_export_math_plan (body, false, false);
      tree output= mathematics (body, plan[1] == tree ("#t"), false, env);
      if (plan[1] == tree ("#t")) {
        tree block= tuple ("h:div", attributes ("style", "text-align: center"));
        append_nodes (block, output);
        return tuple (block);
      }
      return output;
    }
    for (const char* tag: {"math-grid", "scripts", "math-accent", "math-annotation",
         "math-long-arrow", "math-middle", "math-symbol", "math-variant", "delimited", "big-operator"})
      if (head (source, tag)) return mathematics (tuple ("math", source), false, false, env);
    if (head (source, "image")) return request ("image", source, env);
    if (head (source, "graphics") || head (source, "tree")) return request ("picture", source, env);
    if (head (source, "include")) {
      tree included= request ("include", source, env);
      export_environment local= env;
      local["source-url"]= included[0];
      return run (included[1], local);
    }
    if (head (source, "specific")) {
      string target= atom (source[1]);
      if (target == "html") return tuple (quoted (force_text (source[2])));
      if (target == "html*") return run (source[2], env);
      if (target == "image") return request ("picture", source[2], env);
      return tuple ();
    }
    if (head (source, "compound") || head (source, "value") || head (source, "quote-value")) {
      tree expanded= tuple (force_text (source[1]));
      for (int i=2; i<N(source); ++i) expanded << source[i];
      return run (expanded, env);
    }
    if (head (source, "!file")) {
      tree plan= request ("document-plan", source, env);
      tree body= html_export_finish_body (run (source[1], env), true);
      return html_export_document_output (plan, run (plan[0], env), body);
    }
    tree inputs= html_export_markup_plan (source);
    if (inputs != tree ("#f")) return html_export_markup_output (source, children (inputs, env));
    tree enunciation= html_export_enunciation_plan (source);
    if (enunciation != tree ("#f")) {
      tree title= is_atomic (enunciation[3]) ? tuple (enunciation[3]) : run (enunciation[3], env);
      return html_export_enunciation_output (enunciation, title, run (enunciation[4], env),
                                              atom (request ("enunciation-color", enunciation, env)));
    }
    for (const char* tag: {"with-limits", "line-break", "no_break", "no-indent", "yes-indent",
         "no-indent*", "yes-indent*", "page-break*", "page-break", "no-page-break*", "no-page-break",
         "no-break-here*", "no-break-here", "no-break-start", "no-break-end", "new-page*", "new-page",
         "new-dpage*", "new-dpage", "assign", "provides", "macro", "drd-props", "arg", "quote-arg",
         "xmacro", "get-label", "get-arity", "map-args", "eval-args", "eval", "if", "if*", "case",
         "while", "for-each", "extern", "use-package", "or", "xor", "and", "not", "plus", "minus",
         "times", "over", "div", "mod", "merge", "length", "range", "number", "date", "translate",
         "is-tuple", "look-up", "equal", "unequal", "less", "lesseq", "greater", "greatereq",
         "style-with", "style-with*", "style-only", "style-only*", "active", "active*",
         "reference", "pageref", "write", "tuple", "collection", "associate",
         "point", "line", "arc", "bezier", "format", "tag", "meaning", "switch", "fold", "exclusive",
         "progressive", "superposed", "hidden-title", "web-title"})
      if (head (source, tag)) return tuple ();
    throw std::runtime_error (std::string ("Unsupported HTML export markup: ") + atom (source[0]).c_str ());
  }
};
}

scheme_tree html_export_convert (scheme_tree source, object options, url source_url, url target) {
  source= html_export_prepare_source (source);
  export_environment env= {{"source-url", quoted (as_string (source_url))}, {"math", tree ("#f")},
    {"math-display", tree ("#f")}, {"preformatted", tree ("#f")}, {"mag", quoted ("1")},
    {"left-margin", tree ("0")}, {"right-margin", tree ("0")}};
  return html_exporter (target, conversion_options_from_scheme (options)).run (source, env);
}

scheme_tree html_export_document (object input, object options, url origin, url target) {
  tree source= is_tree (input) ? latex_export_prepare_v3_source (as_tree (input))
                                : as_scheme_tree (input);
  source= html_export_prepare_source (source);
  if (head (source, "document")) {
    tree body= tree ("#f"), style= tuple ("tuple"), initial= tuple ("collection");
    for (int i=1; i<N(source); ++i) {
      tree field= source[i];
      if (head (field, "body") && N(field) == 2) body= field[1];
      if (head (field, "style") && N(field) == 2)
        style= head (field[1], "tuple") ? field[1] : tuple ("tuple", field[1]);
      if (head (field, "initial") && N(field) == 2) initial= field[1];
    }
    if (body != tree ("#f")) {
      for (const char* key: {"font", "math-font", "font-family"})
        for (int i=1; i<N(initial); ++i)
          if (is_tuple (initial[i], "associate", 2) && initial[i][1] == quoted (key)) {
            tree value= initial[i][2];
            if (value != quoted ("")) body= tuple ("with", quoted (key), value, body);
            break;
          }
      source= tuple ("!file", body, style);
    }
  }
  return html_export_finalize (html_export_convert (source, options, origin, target),
                               head (source, "!file"));
}
