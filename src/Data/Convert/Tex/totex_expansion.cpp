/******************************************************************************
* MODULE     : totex_expansion.cpp
* DESCRIPTION: Native macro-expansion environment support for LaTeX export
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
*******************************************************************************/

#include "totex_internal.hpp"
#include "tree.hpp"

#include <set>
#include <vector>

namespace {

using namespace latex_export_internal;

const char* always_expand_names[]= {
  "render-theorem", "render-remark", "render-exercise", "render-proof",
  "algorithm", "algorithm*", "named-algorithm", "named-algorithm-old",
  "specified-algorithm", "specified-algorithm*", "named-specified-algorithm",
  "algorithm-body", "numbered",
  "tmdoc-title", "icon", "shortcut", "key", "prefix",
  "menu", "render-menu", "submenu", "subsubmenu", "subsubsubmenu",
  "markup", "tmstyle", "tmpackage", "tmdtd", "def-index",
  "src-arg", "src-var", "scm-arg", "scm-args",
  "descriptive-table", "tm-fragment", "framed-fragment",
  "explain", "explain-synopsis", "explain-macro",
  "small-envbox", "big-envbox", "small-focus", "big-focus",
  "cursor", "math-cursor", "TeXmacs-version", "c++", "BibTeX"
};

// Keep the legacy registry order because tmtex-env-patch historically used
// logic-table query order when building its expansion collection.  These are
// names, not executable dispatch policy; execution itself is native now.
static const char* primitive_names[]= {
  "unknown", "uninit", "error", "raw-data", "document", "athena-preserved-object",
  "!athena-data-inline", "para", "surround", "concat", "rigid", "hgroup", "vgroup", "hidden",
  "hspace", "vspace*", "vspace", "space", "htab", "move", "shift", "resize", "clipped", "repeat",
  "float", "datoms", "dlines", "dpages", "dbox", "line-note", "with-limits", "line-break",
  "new-line", "next-line", "emdash", "no-break", "no-indent", "yes-indent", "no-indent*",
  "yes-indent*", "page-break*", "page-break", "no-page-break*", "no-page-break", "no-break-here*",
  "no-break-here", "no-break-start", "no-break-end", "new-page*", "new-page", "new-dpage*",
  "new-dpage", "around", "around*", "big-around", "left", "mid", "!middle", "right", "big",
  "long-arrow", "lprime", "rprime", "below", "above", "lsub", "lsup", "rsub", "rsup", "modulo",
  "frac", "sqrt", "wide", "neg", "wide*", "tree", "tformat", "twith", "cwith", "tmarker", "table",
  "row", "cell", "subtable", "assign", "with", "provides", "value", "quote-value", "quote-value",
  "drd-props", "arg", "quote-arg", "compound", "experimental-build-warning", "xmacro", "get-label",
  "get-arity", "map-args", "eval-args", "mark", "eval", "quasi", "unquote*", "copy", "if", "if*",
  "case", "while", "for-each", "extern", "include", "use-package", "syntax", "or", "xor", "and",
  "not", "plus", "minus", "times", "over", "div", "mod", "merge", "length", "range", "find-file",
  "is-tuple", "look-up", "equal", "unequal", "less", "lesseq", "greater", "greatereq", "number",
  "change-case", "date", "cm-length", "mm-length", "in-length", "pt-length", "bp-length",
  "dd-length", "pc-length", "cc-length", "fs-length", "fbs-length", "em-length", "ln-length",
  "sep-length", "yfrac-length", "ex-length", "fn-length", "fns-length", "bls-length", "spc-length",
  "xspc-length", "par-length", "pag-length", "gm-length", "gh-length", "style-with", "style-with*",
  "style-only", "style-only*", "active", "active*", "inactive", "inactive*", "rewrite-inactive",
  "inline-tag", "open-tag", "middle-tag", "close-tag", "symbol", "latex", "hybrid", "tuple",
  "attr", "tmlen", "collection", "associate", "backup", "set-binding", "get-binding",
  "hidden-binding", "label", "reference", "pageref", "write", "specific", "tag", "meaning", "flag",
  "graphics", "commutative-diagram", "superpose", "gr-group", "gr-transform", "text-at", "cline",
  "arc", "carc", "spline", "spine*", "cspline", "fill", "image", "box-info", "frame-direct",
  "frame-inverse", "format", "set", "reset", "expand", "expand*", "hide-expand", "display-baloon",
  "apply", "begin", "end", "func", "env", "shown", "mtm", "!file", "!arg",
};

static const char* extra_names[]= {
  "wide-float", "phantom-float", "marginal-note", "marginal-normal-note", "marginal-left-note",
  "marginal-even-left-note", "marginal-right-note", "marginal-even-right-note", "!ilx",
};

static const char* style_names[]= {
  "section", "subsection", "subsubsection", "paragraph", "subparagraph", "part", "chapter",
  "hide-preamble", "show-preamble", "hide-part", "show-part", "doc-title-options", "author-data",
  "appendix", "appendix*", "proof-alternative", "proof-standard", "proof-of", "theorem",
  "proposition", "lemma", "corollary", "proof", "axiom", "definition", "notation", "conjecture",
  "remark", "note", "example", "convention", "warning", "acknowledgments", "exercise", "problem",
  "question", "solution", "answer", "quote-env", "quotation", "verse", "theorem*", "proposition*",
  "lemma*", "corollary*", "axiom*", "definition*", "notation*", "conjecture*", "remark*", "note*",
  "example*", "convention*", "warning*", "acknowledgments*", "exercise*", "problem*", "question*",
  "solution*", "answer*", "new-theorem", "new-remark", "new-exercise", "verbatim", "center",
  "padded-center", "padded-left-aligned", "padded-right-aligned", "compact", "compressed",
  "amplified", "indent", "jump-in", "algorithm-indent", "footnote", "wide-footnote",
  "footnotemark", "footnotemark*", "description", "description-compact", "description-aligned",
  "description-dash", "description-long", "description-paragraphs", "itemize", "itemize-minus",
  "itemize-dot", "itemize-arrow", "enumerate", "enumerate-numeric", "enumerate-roman",
  "enumerate-Roman", "enumerate-alpha", "enumerate-Alpha", "folded", "unfolded", "folded-plain",
  "unfolded-plain", "folded-std", "unfolded-std", "folded-explain", "unfolded-explain",
  "folded-env", "unfolded-env", "folded-documentation", "unfolded-documentation", "folded-grouped",
  "unfolded-grouped", "summarized", "detailed", "summarized-plain", "summarized-std",
  "summarized-env", "summarized-documentation", "summarized-grouped", "summarized-raw",
  "summarized-tiny", "detailed-plain", "detailed-std", "detailed-env", "detailed-documentation",
  "detailed-grouped", "detailed-raw", "detailed-tiny", "padded", "underlined", "overlined",
  "bothlined", "leftlined", "rightlined", "verticallined", "framed", "ornamented", "really-tiny",
  "very-tiny", "tiny", "really-small", "very-small", "smaller", "small", "flat-size",
  "normal-size", "sharp-size", "large", "larger", "very-large", "really-large", "really-huge",
  "british", "bulgarian", "chinese", "croatian", "czech", "danish", "dutch", "english",
  "esperanto", "finnish", "french", "german", "greek", "hungarian", "italian", "japanese",
  "korean", "polish", "portuguese", "romanian", "russian", "slovak", "slovene", "spanish",
  "swedish", "chineset", "ukrainian", "math", "text", "math-up", "math-ss", "math-tt", "math-bf",
  "math-sl", "math-it", "math-separator", "math-quantifier", "math-imply", "math-or", "math-and",
  "math-not", "math-relation", "math-union", "math-intersection", "math-exclude", "math-plus",
  "math-minus", "math-times", "math-over", "math-big", "math-prefix", "math-postfix", "math-open",
  "math-close", "math-ordinary", "math-ignore", "eqnarray", "eqnarray*", "leqnarray*", "gather",
  "multline", "gather*", "multline*", "align", "flalign", "alignat", "align*", "flalign*",
  "alignat*", "eq-number", "separating-space", "application-space", "code", "cpp-code", "scm-code",
  "shell-code", "scilab-code", "verbatim-code", "cpp", "scm", "shell", "scilab", "frame",
  "colored-frame", "fcolorbox", "rotate", "condensed", "translate", "localize", "render-key",
  "key", "key*", "minipage", "latex-picture-fallback", "latex_preview", "picture-mixed",
  "source-mixed", "listing", "the-index", "glossary",
  "glossary-explain", "glossary-2", "the-glossary", "table-of-contents", "small-figure",
  "big-figure", "small-table", "big-table", "item", "item*", "render-proof",
  "render-proof-alternative", "render-proof-standard", "nbsp", "nbhyph", "hrule", "frac*", "hlink",
  "cardlink", "transclude", "material-citation", "referenced-materials", "action", "href", "slink",
  "eqref", "smart-ref", "choose", "tt", "strong", "em", "name", "samp", "abbr", "dfn", "kbd",
  "var", "acronym", "person", "render-line-number", "menu", "set-header", "set-footer",
  "set-this-page-header", "set-this-page-footer", "doc-data", "abstract-data", "abstract",
  "abstract-acm", "abstract-arxiv", "abstract-msc", "abstract-pacs", "abstract-keywords",
  "doc-title", "doc-running-title", "doc-subtitle", "doc-note", "doc-misc", "doc-date",
  "doc-running-author", "doc-author", "author-name", "author-affiliation", "author-misc",
  "author-note", "author-email", "author-homepage", "doc-subtitle-ref", "doc-date-ref",
  "doc-note-ref", "doc-misc-ref", "author-affiliation-ref", "author-email-ref",
  "author-homepage-ref", "author-note-ref", "author-misc-ref", "doc-subtitle-label",
  "doc-date-label", "doc-note-label", "doc-misc-label", "author-affiliation-label",
  "author-email-label", "author-homepage-label", "author-note-label", "author-misc-label",
  "equation", "equation*", "elsevier-frontmatter", "conferenceinfo", "CopyrightYear", "slide",
  "tit", "crdata",
};

std::vector<string>
registry_names (const char* const* names, size_t count) {
  std::vector<string> result;
  result.reserve (count);
  for (size_t i=0; i<count; ++i) result.push_back (names[i]);
  return result;
}

bool
known_tmtex_name (string name) {
  const char* const* groups[]= { primitive_names, extra_names, style_names };
  const size_t sizes[]= {
    sizeof (primitive_names) / sizeof (primitive_names[0]),
    sizeof (extra_names) / sizeof (extra_names[0]),
    sizeof (style_names) / sizeof (style_names[0])
  };
  for (int g=0; g<3; ++g)
    for (size_t i=0; i<sizes[g]; ++i)
      if (name == groups[g][i]) return true;
  return false;
}

std::vector<string>
object_strings (object values) {
  std::vector<string> result;
  if (!is_list (values)) return result;
  array<object> items= as_array_object (values);
  for (int i=0; i<N(items); ++i) {
    if (is_string (items[i])) result.push_back (as_string (items[i]));
    else if (is_symbol (items[i])) result.push_back (as_symbol (items[i]));
  }
  return result;
}

std::vector<string>
ordered_difference (const std::vector<string>& values,
                    const std::set<string>& removed) {
  std::vector<string> result;
  for (const string& value: values)
    if (!removed.count (value)) result.push_back (value);
  return result;
}

void
ordered_union_append (std::vector<string>& target, std::set<string>& seen,
                      const std::vector<string>& values) {
  for (const string& value: values)
    if (seen.insert (value).second) target.push_back (value);
}

void
collect_extension_labels (tree t, std::vector<string>& out,
                          std::set<string>& seen) {
  if (!is_compound (t)) return;
  string name= as_string (L(t));
  if (is_extension (t) && seen.insert (name).second) out.push_back (name);
  for (int i=0; i<N(t); ++i) collect_extension_labels (t[i], out, seen);
}

scheme_tree
env_macro (string name) {
  scheme_tree associate= stree_apply ("associate");
  associate << stree_string (name);
  scheme_tree xmacro= stree_apply ("xmacro");
  xmacro << stree_string ("x");
  scheme_tree eval_args= stree_apply ("eval-args");
  eval_args << stree_string ("x");
  xmacro << eval_args;
  associate << xmacro;
  return associate;
}

} // namespace

scheme_tree
latex_export_env_patch (scheme_tree st, bool expand_user_macros) {
  std::vector<string> l0= registry_names (
    primitive_names, sizeof (primitive_names) / sizeof (primitive_names[0]));
  std::vector<string> l1= registry_names (
    extra_names, sizeof (extra_names) / sizeof (extra_names[0]));
  std::vector<string> l2= registry_names (
    style_names, sizeof (style_names) / sizeof (style_names[0]));
  std::vector<string> l3, l4;
  array<string> native_tags= registry_tag_names ();
  array<string> native_symbols= registry_symbol_names ();
  for (int i=0; i<N(native_tags); ++i) l3.push_back (native_tags[i]);
  for (int i=0; i<N(native_symbols); ++i) l4.push_back (native_symbols[i]);
  std::set<string> l0_set (l0.begin (), l0.end ());
  std::set<string> l4_set (l4.begin (), l4.end ());
  std::set<string> always;
  for (auto name: always_expand_names) always.insert (name);

  std::set<string> excluded_l5= l4_set;
  excluded_l5.insert (always.begin (), always.end ());
  std::vector<string> l5= ordered_difference (l3, excluded_l5);

  std::vector<string> l6= object_strings (latex_export_collect_user_defs (st));
  std::set<string> l6_set (l6.begin (), l6.end ());
  std::vector<string> l7= expand_user_macros ? std::vector<string> () : l6;

  std::vector<string> user_macros;
  std::set<string> user_macro_seen;
  collect_extension_labels (scheme_tree_to_tree (st), user_macros,
                            user_macro_seen);
  std::set<string> excluded_l8= l0_set;
  excluded_l8.insert (l6_set.begin (), l6_set.end ());
  excluded_l8.insert (always.begin (), always.end ());
  std::vector<string> l8= ordered_difference (user_macros, excluded_l8);

  std::vector<string> union_values;
  std::set<string> union_seen;
  ordered_union_append (union_values, union_seen, l1);
  ordered_union_append (union_values, union_seen, l2);
  ordered_union_append (union_values, union_seen, l5);
  ordered_union_append (union_values, union_seen, l7);
  ordered_union_append (union_values, union_seen, l8);
  std::vector<string> l9= ordered_difference (union_values, l0_set);

  std::set<string> short_primitives;
  for (const string& name: l0)
    if (N(name) <= 2 && name != "tt" && name != "em" && name != "op")
      short_primitives.insert (name);
  std::vector<string> l12= ordered_difference (l9, short_primitives);

  scheme_tree result= stree_apply ("collection");
  for (const string& name: l12) result << env_macro (name);
  return result;
}

bool
latex_export_known_tmtex_name (string name) {
  return known_tmtex_name (name);
}
