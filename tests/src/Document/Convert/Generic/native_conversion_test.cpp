/******************************************************************************
* Native conversion representation, ownership and failure boundaries.
* GNU GPL version 3 or later; see LICENSE.
******************************************************************************/
#include <QApplication>
#include <QDir>
#include <QTemporaryDir>
#include <QtTest/QtTest>
#include <memory>
#include <stdexcept>
#include <vector>
#include "App/Runtime/server.hpp"
#include "App/Frontend/new_buffer.hpp"
#include "App/Frontend/new_window.hpp"
#include "Document/Model/buffer_export.hpp"
#include "UI/Abstract/gui.hpp"
#include "Document/Convert/Generic/conversion_executor.hpp"
#include "Document/Convert/Generic/conversion_graph.hpp"
#include "Document/Convert/Generic/conversion_registry.hpp"
#include "Document/Convert/Generic/image_conversion.hpp"
#include "Document/Convert/Xml/html_import.hpp"
#include "Document/Convert/Xml/athena_document_xml.hpp"
#include "convert.hpp"
#include "boot.hpp"
#include "data_cache.hpp"
#include "file.hpp"
#include "math_tree_v3.hpp"
#include "node_metadata.hpp"
#include "native_dispatch.hpp"
#include "sys_utils.hpp"

class TestNativeConversion: public QObject {
  Q_OBJECT
private slots:
  void exportMetadataAndRecovery ();
  void mathmlRoutes ();
  void htmlImportSemantics ();
  void htmlStructure ();
  void htmlSerializer ();
  void htmlMarkupAndResources ();
  void passiveBufferExport ();
  void binaryPayloadsAndFileDestinations ();
  void imageConversionDestinations ();
};

bool headless_mode= true;
bool is_headless () { return true; }

namespace {
std::unique_ptr<QTemporaryDir> profile;
tree canonical_literals (tree value) {
  // The textual reader unescapes literals; converter syntax stores the quoted
  // representation produced by scm_quote, including embedded quotes/slashes.
  if (is_atomic (value))
    return is_quoted (value->label) ? tree (scm_quote (raw_unquote (value->label))) : value;
  for (int i=0; i<N(value); ++i) value[i]= canonical_literals (value[i]);
  return value;
}
tree serial (const char* input) { return canonical_literals (string_to_scheme_tree (input)); }
tree source (const char* input) { return scheme_tree_to_tree (serial (input)); }
string text (conversion_payload input, string from, string to,
             conversion_options options= {}) {
  return std::get<string> (conversion_dispatch (std::move (input), from, to,
                                               std::move (options)));
}
template<class F> string failure (F operation) {
  try { operation (); }
  catch (const std::exception& error) { return string (error.what ()); }
  return "";
}
bool contains (const tree& value, const tree& expected) {
  if (value == expected) return true;
  for (int i=0; i<N(value); ++i)
    if (contains (value[i], expected)) return true;
  return false;
}
void node_property (tree& value, const char* key, athena::node::property data) {
  athena::node::metadata metadata;
  if (const auto* existing= athena::node::get (value)) metadata= *existing;
  metadata.properties[key]= std::move (data);
  athena::node::set (value, metadata);
}
void node_identity (tree& value) {
  athena::node::metadata metadata;
  if (const auto* existing= athena::node::get (value)) metadata= *existing;
  metadata.id= athena::node::new_id ();
  athena::node::set (value, metadata);
}
const athena::node::property& node_property (const tree& value, const char* key) {
  const auto* metadata= athena::node::get (value);
  if (!metadata) throw std::runtime_error ("Missing fixture metadata");
  return metadata->properties.at (key);
}
bool clean (string output) {
  return !occurs ("nonconverted", output) && !occurs ("athena-data", output);
}
string read (url file) {
  string data;
  if (load_string (file, data, false)) throw std::runtime_error ("Cannot read fixture output");
  return data;
}
url local (const QTemporaryDir& directory, const char* name) {
  const auto path= directory.filePath (QString::fromUtf8 (name)).toUtf8 ();
  return url_system (string (path.constData (), path.size ()));
}
}

void TestNativeConversion::exportMetadataAndRecovery () {
    using athena::node::property;
    tree scripts= athena::math_v3::scripts (tree ("x"), tree (""), tree (""),
                                           tree ("i"), tree ("n"), 'o');
    tree input= compound ("math", scripts);
    const tree before= copy (input);
    for (const char* target: {"stm-document", "stm-snippet", "texmacs-stree"})
      QVERIFY (occurs ("cannot preserve native node metadata", failure ([&] {
        conversion_dispatch (input, "texmacs-tree", target);
      })));
    for (const char* target: {"latex-document", "latex-snippet", "html-document", "html-snippet"})
      QVERIFY (clean (text (input, "texmacs-tree", target)));
    QVERIFY (input == before);
    QVERIFY (athena::node::equal_metadata (input[0], before[0]));

    QCOMPARE (std::get<tree> (conversion_dispatch (
      text (input, "texmacs-tree", "texmacs-snippet"),
      "texmacs-snippet", "texmacs-tree")), input);
    QVERIFY (occurs ("\\xrightarrow[i]{n}", text (
      source (R"((math (math-long-arrow "→" "i" "n")))"), "texmacs-tree", "latex-document")));
    QVERIFY (occurs ("\\xrightarrow{}", text (
      source (R"((math (math-long-arrow "→" "" "")))"), "texmacs-tree", "latex-document")));
    for (const auto& entry: std::vector<std::pair<const char*, const char*>> {
        {"bf", "\\mathbf{A}"}, {"bb", "\\mathbb{A}"}, {"cal", "\\mathcal{A}"},
        {"mbf", "\\boldsymbol{A}"}, {"mbfu", "\\mathbf{A}"}, {"frak", "\\mathfrak{A}"}}) {
      QVERIFY (occurs (entry.second, text (
        compound ("math", tree (MATH_VARIANT, entry.first, "A")),
        "texmacs-tree", "latex-document")));
    }
    text (compound ("math", tree (MATH_VARIANT, "mbf", "A")), "texmacs-tree", "latex-document");
    QVERIFY (contains (string ("amsmath"), latex_export_latex_extra_packages ()));
    text (compound ("math", tree (MATH_VARIANT, "frak", "A")), "texmacs-tree", "latex-document");
    QVERIFY (contains (string ("amsfonts"), latex_export_latex_extra_packages ()));

    tree block (MATH_BLOCK, "x");
    node_property (block, "math:block-numbered", property (true));
    const tree document (DOCUMENT, compound ("style", "generic"),
                          compound ("body", tree (DOCUMENT, block)));
    const string numbered= text (document, "texmacs-tree", "html-document");
    QVERIFY (occurs ("\\begin{equation}", numbered));
    QVERIFY (!occurs ("\\[\\begin{equation}", numbered));
    const std::string html (numbered.data (), N(numbered));
    const auto configuration= html.find ("window.MathJax.tex.tags = 'ams'");
    const auto loader= html.find ("npm/mathjax@3/");
    QVERIFY (configuration != std::string::npos && loader != std::string::npos);
    QVERIFY (configuration < loader);
    const conversion_stree unsupported {serial (
      R"((math (scripts "o" (unknown "cannot export") "" "" "i" "n")))")};
    QVERIFY (occurs ("placeholder", failure ([&] {
      text (unsupported, "texmacs-stree", "latex-document");
    })));
    const string html_failure= failure ([&] {
      conversion_dispatch (unsupported, "texmacs-stree", "html-stree");
    });
    const c_string html_reason (html_failure);
    QVERIFY2 (occurs ("preservation/nonconverted", html_failure), html_reason);
    QVERIFY (occurs ("unknown-envelope", failure ([&] {
      text (compound ("unknown-envelope", block), "texmacs-tree", "html-document");
    })));

    const tree rejected= source (R"((math (big-operator "∫" "1")))");
    const auto depth_restored= [] {
      const tree placeholder= latex_export_placeholder (serial (R"((unknown "depth sentinel")))"));
      const string marker= scm_unquote (placeholder->label);
      latex_export_enter ();
      const bool cleared= latex_export_placeholder_ref (marker) == tree ("#f");
      latex_export_leave ();
      return cleared;
    };
    for (int attempt=0; attempt<3; ++attempt) {
      QVERIFY (occurs ("big-operator", failure ([&] {
        text (rejected, "texmacs-tree", "latex-document");
      })));
      QVERIFY (depth_restored ());
      QVERIFY (clean (text (input, "texmacs-tree", "latex-document")));
      QVERIFY (depth_restored ());
    }
    for (const auto& malformed: {
        source (R"((math-block "missing typed metadata")))"),
        source (R"((math-long-arrow (math-symbol (concat "differential-d")) "" "")))")})
      QVERIFY (occurs ("malformed", failure ([&] { latex_export_prepare_v3_source (malformed); })));
    for (const auto& descriptor: std::vector<std::pair<tree, int>> {
        {source (R"((big-operator "∑" "0")))"), 0},
        {source (R"((big-operator "∑" "0")))"), 1},
        {source (R"((math-middle "|")))"), 0}}) {
      tree attributed= copy (descriptor.first);
      node_identity (attributed[descriptor.second]);
      QVERIFY (occurs ("malformed", failure ([&] { latex_export_prepare_v3_source (attributed); })));
    }
    for (int slot=0; slot<5; ++slot) {
      tree attributed= source (R"tm((delimited "(" ")" "m" "1" "2" "x"))tm");
      node_identity (attributed[slot]);
      QVERIFY (occurs ("malformed", failure ([&] { latex_export_prepare_v3_source (attributed); })));
    }
    for (const char* mode: {"invented", "auto", "exact", "min", "max"}) {
      tree grid (MATH_GRID, tree (ROW, tree (CELL, "x")));
      node_property (grid, "math:grid-kind", property (std::string ("matrix")));
      node_property (grid[0][0], "math:cell-format", property (property::dictionary {
        {"height-mode", property (std::string (mode))}}));
      const string reason= failure ([&] { latex_export_prepare_v3_source (grid); });
      QVERIFY (occurs (string (mode) == "invented" ? "malformed" :
                       "Unsupported MathGrid cell formatting", reason));
    }
    QVERIFY (clean (text (input, "texmacs-tree", "latex-document")));
  }

void TestNativeConversion::mathmlRoutes () {
    const string xml= "<math><munderover><mo>&sum;</mo><mi>i</mi><mi>n</mi></munderover></math>";
    for (const char* format: {"mathml-v3-snippet", "mathml-v3-document"}) {
      const tree result= generic_to_tree (xml, format);
      QVERIFY (is_func (result[0], MATH_SCRIPTS, 5));
      QCOMPARE (std::get<std::string> (node_property (result[0], "math:scripts-policy").data), std::string ("d"));
      QVERIFY (is_func (result[0][0], BIG_OPERATOR, 2));
    }
    const tree parsed= std::get<tree> (conversion_dispatch (
      conversion_stree {parse_xml (xml)}, "mathml-v3-stree", "texmacs-tree"));
    QCOMPARE (std::get<std::string> (node_property (parsed[0], "math:scripts-policy").data), std::string ("d"));
    const tree formatted= generic_to_tree (
      "<math display='block'><mtable columnalign='left right'><mtr>"
      "<mtd mathcolor='red'><mi>x</mi></mtd><mtd><mi>y</mi></mtd>"
      "</mtr></mtable></math>", "mathml-v3-snippet");
    QCOMPARE (L(formatted), MATH_BLOCK);
    QVERIFY (!std::get<bool> (node_property (formatted, "math:block-numbered").data));
    QCOMPARE (std::get<std::string> (node_property (formatted[0], "math:grid-kind").data), std::string ("generic"));
    const auto& left= std::get<athena::node::property::dictionary> (
      node_property (formatted[0][0][0], "math:cell-format").data);
    const auto& right= std::get<athena::node::property::dictionary> (
      node_property (formatted[0][0][1], "math:cell-format").data);
    QCOMPARE (std::get<std::string> (left.at ("align").data), std::string ("l"));
    QCOMPARE (std::get<std::string> (right.at ("align").data), std::string ("r"));
    QCOMPARE (std::get<std::string> (left.at ("color").data), std::string ("red"));
    QCOMPARE (generic_to_tree ("<math><mo>&Sum;</mo></math>", "mathml-v3-snippet"),
              source (R"((math (big-operator "∑" "0")))"));
    for (const char* escaped: {"&amp;", "&#38;", "&#038;", "&#00038;", "&#x26;", "&#x0026;", "&#X26;"})
      QCOMPARE (generic_to_tree ("<math><mtext>" * string (escaped) * "Sum;</mtext></math>",
                                "mathml-v3-snippet"),
                source (R"((math (with "mode" "text" (concat "&" "Sum;"))))"));
    for (const auto& entry: std::vector<std::pair<const char*, const char*>> {
        {"<![CDATA[&Sum;]]>", R"((concat "&" "Sum;"))"},
        {"<![CDATA[&amp;Sum;]]>", R"((concat "&" "amp;Sum;"))"},
        {"name &amp;Sum; plus &Sum;", R"((concat "name " "&" "Sum; plus " "∑"))"},
        {"&amp;amp;", R"((concat "&" "amp;"))"},
        {"<![CDATA[A&&B]]>", R"((concat "A" "&" "&" "B"))"}})
      QCOMPARE (generic_to_tree ("<math><mtext>" * string (entry.first) * "</mtext></math>",
                                "mathml-v3-snippet"),
                compound ("math", tree (WITH, "mode", "text", source (entry.second))));
    for (const auto& entry: std::vector<std::pair<const char*, const char*>> {
        {"bold", "mbfu"}, {"bold-italic", "mbf"}, {"fraktur", "frak"},
        {"double-struck", "bb"}, {"script", "cal"}})
      QCOMPARE (generic_to_tree ("<math><mi mathvariant='" * string (entry.first) * "'>A</mi></math>",
                                "mathml-v3-snippet"),
                compound ("math", tree (MATH_VARIANT, entry.second, "A")));
    for (const char* format: {"mathml-v3-snippet", "mathml-v3-document"}) {
      QVERIFY (occurs ("annotation", failure ([&] { generic_to_tree (
        "<math><semantics><mi>x</mi><annotation encoding='application/x-tex'>x^2</annotation></semantics></math>", format); })));
      QVERIFY (occurs ("maction", failure ([&] { generic_to_tree (
        "<math><maction><mi>x</mi></maction></math>", format); })));
    }
  }
void TestNativeConversion::htmlImportSemantics () {
    const tree imported= html_import_document_serial (parse_html_snippet (
      "<table width='50%' border='2' bgcolor='aqua'><tr><td rowspan='2'><p>A</p><p>B</p></td><td>C</td></tr><tr><td><font color='green'>D</font></td></tr></table>"));
    const tree spanning= html_import_document_serial (parse_html_snippet (
      "<table><tr><td rowspan='2' colspan='2'>A</td><td>B</td></tr><tr><td>C</td></tr></table>"));
    QVERIFY (contains (spanning, serial (R"tm((row (cell (document "")) (cell (document "")) (cell (document "C"))))tm")));
    for (const char* expected: {
        R"tm((twith "table-width" "0.5par"))tm", R"tm((twith "table-lborder" "2px"))tm",
        R"tm((cwith "1" "-1" "1" "-1" "cell-background" "#00FFFF"))tm",
        R"tm((cwith "1" "1" "1" "1" "cell-row-span" "2"))tm",
        R"tm((with "color" "#008000" "D"))tm", R"tm((cell (document "A" "B")))tm"})
      QVERIFY (contains (imported, serial (expected)));
    QCOMPARE (html_import_serial (serial (R"tm(("  " (label "before") " first " (next-line) " second "
      (document (quotation (document "block"))) " tail "))tm"), false),
      serial (R"tm((document (concat (label "before") "first" (next-line) "second")
        (quotation (document "block")) "tail"))tm"));
    QCOMPARE (html_import_element ("list-body", serial ("()"), serial (
      R"tm((document (item) "first" "continuation" (item*) (item) "last"))tm")),
      serial (R"tm((document (concat (item) "first") "continuation" (item*) (concat (item) "last")))tm"));
    QCOMPARE (html_import_element ("label", serial (R"tm(((target "heading")))tm"),
      serial (R"tm((document (section "Title") "body"))tm")),
      serial (R"tm((document (concat (section "Title") (label "heading")) "body"))tm"));
    QCOMPARE (html_import_whitespace ("pre", serial (R"tm(("\nkept\n\n"))tm"), false),
      serial (R"tm(((h:p "") (h:p "kept") (h:p "") (h:p "")))tm"));
    QCOMPARE (html_import_whitespace ("mixed", serial (
      R"tm(("  a" "\t b " (h:strong "c") " d\n "))tm"), false),
      serial (R"tm(("a b " (h:strong "c") " d"))tm"));
    QCOMPARE (html_import_element ("img", serial (
      R"tm(((src "figure.svg") (width "50%") (height "24")))tm"), serial (R"tm("")tm")),
      serial (R"tm((image "figure.svg" "0.5par" "24px" "" ""))tm"));
  }

void TestNativeConversion::htmlStructure () {
    QCOMPARE (html_export_finish_body (serial (
      R"tm(((h:p (h:img (@ (src "symbol.png"))) "word tail")))tm"), true),
      serial (R"tm(((h:p (h:span (@ (class "no-breaks")) (h:img (@ (src "symbol.png"))) "word") " tail")))tm"));
    QCOMPARE (html_export_concat_output (serial (
      R"tm(((h:a (@ (id "before"))) (h:h2 "Heading") (h:a (@ (id "after"))) "body"))tm")),
      serial (R"tm(((h:h2 (@ (id "before")) (h:a (@ (id "after"))) "Heading") "body"))tm"));
    QCOMPARE (html_export_concat_output (serial (
      R"tm(((h:a (@ (id "before"))) (h:h2 (@ (id "own")) "Heading") (h:a (@ (id "after")))))tm")),
      serial (R"tm(((h:h2 (@ (id "own")) "Heading") (h:a (@ (id "before"))) (h:a (@ (id "after")))))tm"));
    QCOMPARE (html_export_concat_output (serial (
      R"tm(((h:a (@ (id "left"))) (h:table (h:tr (h:td "Cell"))) (h:a (@ (id "right")))))tm")),
      serial (R"tm(((h:a (@ (id "left"))) (h:table (@ (id "right")) (h:tr (h:td "Cell")))))tm"));
    QCOMPARE (html_export_list_items (serial (R"tm((document "preamble" (concat (item* "Term") "first")
      "continuation" (itemize (document (concat (item) "nested")))
      (concat (assign "item-nr" "2") (item) "second")))tm")),
      serial (R"tm((document (document "preamble")
        (!item (item* "Term") (document (concat "first") "continuation" (itemize (document (concat (item) "nested")))))
        (!item (item) (document (concat "second")))))tm"));
    QCOMPARE (html_export_normalize_document (serial (
      R"tm((surround "before " " after" (with "color" "red" (document "first" "" "last"))))tm"), true),
      serial (R"tm((document (concat "before " (with "color" "red" (concat "first")))
        (concat (with "color" "red" (concat))) (concat (with "color" "red" (concat "last")) " after")))tm"));
    QCOMPARE (html_export_normalize_document (serial (R"tm((surround "[" "]" (document "one")))tm"), true),
      serial (R"tm((document (concat "[one]")))tm"));
    QCOMPARE (html_export_finalize (serial (
      R"tm(((h:p (h:div (@ (h:custom "h:value")) "h:text")) (svg:svg (svg:path (@ (d "M0 0"))))))tm"), false),
      serial (R"tm((*TOP* (div (@ (h:custom "h:value")) "h:text") (svg:svg (svg:path (@ (d "M0 0"))))))tm"));
    QCOMPARE (html_export_concat_plan (serial (
      R"tm((concat "left" (htab "1fn" "first") "middle" (htab "1fn" "last") "right"))tm")),
      serial (R"tm(((!left "left") (!middle "middle") (!right "right")))tm"));
    QCOMPARE (html_export_paragraph (
      serial (R"tm((concat (vspace* "1em") (vspace* "2px") "before" (vspace "3px")))tm"),
      serial (R"tm(("before" (h:span (@ (style "color: red")) (h:p "inside")) "after"))tm")),
      serial (R"tm(((h:p (@ (style "margin-top: max(1em, 2px)")) "before")
        (h:div (@ (style "color: red")) (h:p "inside")) (h:p (@ (style "margin-bottom: 3px")) "after")))tm"));
    const tree plan= html_export_table_plan (serial (R"tm((tformat
      (cwith "1" "1" "1" "1" "cell-background" "red")
      (cwith "1" "-1" "1" "-1" "cell-background" "blue")
      (cwith "1" "1" "1" "1" "cell-row-span" "2")
      (cwith "1" "1" "1" "1" "cell-col-span" "2")
      (table (row (cell "merged") (cell "hidden") (cell "top"))
        (row (cell "hidden") (cell "hidden") (tformat (cwith "cell-background" "green") (cell "bottom"))))))tm"));
    QCOMPARE (plan[1], serial (R"tm(("merged" "top" "bottom"))tm"));
    const tree output= html_export_table_output (plan, serial (R"tm((("merged") ("top") ("bottom")))tm"));
    QVERIFY (contains (output, serial (
      R"tm((h:td (@ (style "background-color: blue") (rowspan "2") (colspan "2")) "merged"))tm")));
    QVERIFY (contains (output, serial (R"tm((h:td (@ (style "background-color: green")) "bottom"))tm")));
  }

void TestNativeConversion::htmlSerializer () {
    QCOMPARE (parse_html_fragment ("<p>A &amp; &lt; B</p>"), serial (R"tm((*TOP* (p "A & < B")))tm"));
    for (const char* fixture: {
        R"tm((*TOP* (p (@ (title "a\"b&c")) "<&amp;>")))tm",
        R"tm((*TOP* (pre " a\n  b ") (script "if (a < b && c) f();") (style "p::after { content: '>'; }")))tm",
        R"tm((*TOP* (template (p "inside"))))tm"}) {
      const tree original= serial (fixture);
      const tree restored= parse_html_fragment (serialize_html (original));
      const c_string diagnostic ("Expected " * scheme_tree_to_string (original) *
        "\nActual " * scheme_tree_to_string (restored));
      QVERIFY2 (restored == original, diagnostic);
    }
    QCOMPARE (parse_html_fragment ("<table><tr><td>x</table>"),
      serial (R"tm((*TOP* (table (tbody (tr (td "x"))))))tm"));
    QCOMPARE (parse_html_fragment ("<math><mi>x</mi></math>"),
      serial (R"tm((*TOP* (math (@ (xmlns "http://www.w3.org/1998/Math/MathML")) (mi "x"))))tm"));
    QCOMPARE (parse_html_fragment ("<svg><foreignObject><p>x</p></foreignObject></svg>"),
      serial (R"tm((*TOP* (svg (@ (xmlns "http://www.w3.org/2000/svg"))
        (foreignObject (p (@ (xmlns "http://www.w3.org/1999/xhtml")) "x")))))tm"));
    const string input= "<!doctype html><html><head><script src='mathjax@3/tex-mml-chtml.js'></script></head>"
      "<body><p>before \\(x&lt;y\\) after</p><code>$literal$</code></body></html>";
    const tree expected= serial (R"tm((*TOP* (*DOCTYPE* html)
      (html (head (script (@ (src "mathjax@3/tex-mml-chtml.js"))))
        (body (p "before " (mathjax "\\(x<y\\)") " after") (code "$literal$")))))tm");
    QCOMPARE (parse_html (input), expected);
    QCOMPARE (parse_html (input), expected);
    QVERIFY_THROWS_EXCEPTION (std::runtime_error, serialize_html (serial (R"tm((*TOP* (br "lost")))tm")));
    QVERIFY_THROWS_EXCEPTION (std::runtime_error,
      html_export_math_output (serial (R"tm((nonconverted "lost"))tm"), false, false));
  }
void TestNativeConversion::htmlMarkupAndResources () {
    const conversion_stree input {serial (R"tm((document
      (section "Native heading")
      (html-class "second" (html-class "first" (strong "A & B")))
      (html-tag "aside" (em "Side content"))
      (table (row (cell "Standalone cell")))
      (itemize (document (concat (item) "First item") (concat (item) "Second item")))
      (hlink "Local document" "other.ath?view=1#target") (hover-balloon "Hover" "Explanation")
      (with "ornament-hpadding" "2px" "ornament-color" "red" (ornament "Decorated"))
      (code (document " a--b " "``x''")) "After code -- normal text"
      (render-small-figure "unused" (strong "Figure 3") (ATHENA) (em "Caption"))
      (html-javascript "if (x < y) f('&');")))tm")};
    const tree before= copy (input.data);
    const string output= text (input, "texmacs-stree", "html-snippet",
                               {{"texmacs->html:css", "on"}});
    QCOMPARE (input.data, before);
    for (const char* expected: {
        "<h2>Native heading</h2>", "class=\"first second\"", "A &amp; B",
        "<aside><em>Side content</em></aside>", "Standalone cell", "<ul>",
        "First item", "Second item", "other.html?view=1#target", "balloon-anchor",
        "Explanation", "padding-left: 2px", "padding-right: 2px", " a--b \n``x''",
        "After code – normal text", "figure-caption-label", "<strong>Figure 3</strong>",
        "<em>Caption</em>", "athena-logo", "if (x < y) f('&');"})
      QVERIFY2 (occurs (expected, output), expected);
    QVERIFY (occurs ("unsupported-html-fixture", failure ([] {
      text (source (R"tm((unsupported-html-fixture "must not disappear"))tm"), "texmacs-tree", "html-snippet");
    })));
    QVERIFY (occurs ("raster fallback", failure ([] {
      text (source (R"tm((specific "image" (math "x")))tm"), "texmacs-tree", "html-snippet");
    })));
    for (const char* html: {
        "<p>x<sub>i<sup>2</sup></sub></p>", "<math><msub><mi>y</mi><mn>1</mn></msub></math>",
        "<math><semantics><mrow><mi>x</mi><mo>+</mo><mn>1</mn></mrow><annotation encoding='application/x-tex'>x+1</annotation></semantics></math>",
        "<img class='tex' alt='z^3'>",
        "<script src='https://example.test/mathjax.js'></script><p>\\(x_i\\)</p>"}) {
      const tree native= std::get<tree> (conversion_dispatch (string (html), "html-snippet", "texmacs-tree"));
      const tree restored= std::get<tree> (conversion_dispatch (
        text (native, "texmacs-tree", "texmacs-snippet"), "texmacs-snippet", "texmacs-tree"));
      latex_export_prepare_v3_source (restored);
      const string output= text (restored, "texmacs-tree", "html-snippet");
      QVERIFY (occurs ("\\(", output) || occurs ("\\[", output) || occurs ("athena-math-block", output));
    }
    const tree plan= html_export_document_plan (serial (R"tm((with "html-title" "A & B" "html-site-version" "2"
      "html-extra-css" (quote (tuple "one.css" "two.css"))
      "html-head-javascript" "if (a < b) run();" (document "body")))tm"),
      serial (R"tm(("generic"))tm"), "p { color: red }", "---", "ATHENA", true);
    const string planned= serialize_html (html_export_document_output (
      plan, serial (R"tm(("A & B"))tm"), serial (R"tm(((p "body")))tm")));
    for (const char* expected: {"A &amp; B", "one.css", "two.css", "if (a < b) run();", "mathjax@3"})
      QVERIFY (occurs (expected, planned));
    const tree fresh= html_export_document_plan (serial (R"tm((document "body"))tm"),
      serial (R"tm(("generic"))tm"), "", "---", "ATHENA", false);
    QCOMPARE (fresh[1], tree ("#f"));
    QVERIFY (!occurs ("mathjax", serialize_html (html_export_document_output (
      fresh, serial (R"tm(("Fresh"))tm"), serial ("()")))));
  }

void TestNativeConversion::passiveBufferExport () {
    using athena::node::property;
    QTemporaryDir directory;
    QVERIFY (directory.isValid ());
    const url origin= local (directory, "source.ath");
    const url target= local (directory, "source.html");
    const string payload= "Unicode π∈ℤ & < followed by several words";
    const string image= "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"20\" height=\"30\">"
      "<rect width=\"20\" height=\"30\" fill=\"red\"/></svg>";
    QVERIFY (!save_string (local (directory, "asset.svg"), image, false));
    tree grid (MATH_GRID, tree (ROW, tree (CELL,
      athena::math_v3::scripts (tree ("x"), tree (""), tree (""), tree ("i"), tree ("2"), 'r'))));
    node_property (grid, "math:grid-kind", property (std::string ("stack")));
    tree block (MATH_BLOCK, grid);
    node_property (block, "math:block-numbered", property (false));
    tree enunciation= compound ("enunciation", tree (DOCUMENT, "Semantic body remains visible.", block));
    node_property (enunciation, "kind", property (std::string ("definition")));
    node_property (enunciation, "numbered", property (false));
    node_property (enunciation, "name", property (athena::node::rich_text {
      compound ("strong", "Native semantic title")}));
    node_property (enunciation, "year", property (std::string ("1827")));
    const tree document (DOCUMENT, compound ("style", tuple ("generic")),
      compound ("body", tree (DOCUMENT, payload, enunciation,
        tree (IMAGE, "asset.svg", "20px", "30px", "", ""))));
    const string saved= text (document, "texmacs-tree", "texmacs-document");
    QCOMPARE (conversion_format_determine (saved, "unknown"), string ("texmacs"));
    const tree restored= std::get<tree> (conversion_dispatch (saved, "texmacs-document", "texmacs-tree"));
    QVERIFY (!save_string (origin, text (restored, "texmacs-tree", "texmacs-document"), false));
    QCOMPARE (conversion_format_determine ("<?xml version='1.0'?><!--header--><html><body>x</body></html>", "unknown"), string ("html"));
    set_preference ("document save mode", "manual");
    set_preference ("texmacs->html:mathjax", "on");
    QVERIFY (is_none (get_current_buffer_safe ()));
    QVERIFY (!buffer_load (origin));
    struct release_buffer { url name; ~release_buffer () { kill_buffer (name); } } release {origin};
    QVERIFY (!buffer_export (origin, target, "html"));
    const string output= read (target);
    for (const char* expected: {"π∈ℤ", "&amp; &lt; followed", "followed by several words",
        "Native semantic title", "1827", "Semantic body remains visible.",
        "athena-math-block", "\\begin{gather", "mathjax@3"})
      QVERIFY2 (occurs (expected, output), expected);
    QVERIFY (is_none (get_current_buffer_safe ()));
    auto options= conversion_options_snapshot ();
    options["texmacs->html:css-stylesheet"]= "request-only.css";
    QVERIFY (!buffer_export_request (origin, target, "html", options));
    QVERIFY (occurs ("request-only.css", read (target)));
    QVERIFY (!buffer_export (origin, target, "html"));
    QVERIFY (!occurs ("request-only.css", read (target)));
    QVERIFY (QDir (directory.path ()).mkdir ("export"));
    QVERIFY (!save_string (local (directory, "export/asset.svg"), "wrong destination asset", false));
    QVERIFY (!buffer_export (origin, local (directory, "export/site.html"), "html"));
    QCOMPARE (read (local (directory, "export/site-1.svg")), image);
    QVERIFY (occurs ("width: 20px; height: 30px", read (local (directory, "export/site.html"))));
    for (const char* identifier: {"svg", "asset.svg"}) {
      const tree embedded (IMAGE, tuple (tree (RAW_DATA, image), identifier), "20px", "30px", "", "");
      const auto converted= conversion_execute (embedded, "texmacs-tree", "html-stree", {},
        origin, local (directory, "embedded.html"));
      QVERIFY (occurs ("embedded-1.svg", serialize_html (std::get<conversion_stree> (converted).data)));
      QCOMPARE (read (local (directory, "embedded-1.svg")), image);
    }
  }

void TestNativeConversion::binaryPayloadsAndFileDestinations () {
    QTemporaryDir directory;
    QVERIFY (directory.isValid ());
    const string bytes ("\0\x80\xff\x01", 4);
    const url destination= local (directory, "bytes with spaces.png");
    QCOMPARE (std::get<url> (conversion_to_file (
      conversion_bytes {bytes}, "png-document", "png-file", destination)), destination);
    QCOMPARE (read (destination), bytes);
    const auto loaded= conversion_dispatch (destination, "png-file", "png-document");
    QCOMPARE (std::get<conversion_bytes> (loaded).data, bytes);
    const url copied= local (directory, "copy.png");
    QCOMPARE (std::get<url> (conversion_to_file (destination, "png-file", "png-file", copied)), copied);
    QCOMPARE (read (copied), bytes);
    QCOMPARE (read (destination), bytes);
    const url retained= std::get<url> (conversion_dispatch (loaded, "png-document", "png-file"));
    const string retained_bytes= read (retained);
    remove (retained);
    QCOMPARE (retained_bytes, bytes);
    QCOMPARE (text (string (""), "verbatim-document", "verbatim-document"), string (""));
    QVERIFY (is_none (conversion_source_url ()));
    QVERIFY (is_none (conversion_target_url ()));
    QVERIFY (conversion_rendering_editor () == nullptr);
  }

void TestNativeConversion::imageConversionDestinations () {
    if (!conversion_graph_path ("svg-file", "pnm-file"))
      QSKIP ("This existing image conversion check requires rsvg-convert and ImageMagick");
    QTemporaryDir directory;
    QVERIFY (directory.isValid ());
    const url origin= local (directory, "source ' with spaces.svg");
    const url target= local (directory, "converted result.pnm");
    QVERIFY (!save_string (origin,
      "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"20\" height=\"30\">"
      "<rect width=\"20\" height=\"30\" fill=\"red\"/></svg>", false));
    QCOMPARE (std::get<url> (conversion_to_file (origin, "svg-file", "pnm-file", target)), target);
    const string pnm= read (target);
    QVERIFY (N(pnm) > 2 && pnm[0] == 'P' && pnm[1] >= '1' && pnm[1] <= '6');
    const url png= conversion_image_run ("svg", "png", origin, url_none (), "150");
    const url pdf= conversion_image_run ("png", "pdf", png, local (directory, "native result.pdf"), "150");
    remove (png);
    QVERIFY (starts (read (pdf), "%PDF"));
    const url preserved= local (directory, "existing output.png");
    QVERIFY (!save_string (preserved, "preserve destination", false));
    QVERIFY (!save_string (origin, "not an SVG", false));
    QVERIFY (!failure ([&] { conversion_image_run ("svg", "png", origin, preserved, "150"); }).empty ());
    QCOMPARE (read (preserved), string ("preserve destination"));
  }

static void run_tests (int argc, char** argv) {
  init_system_state ();
  gui_open (argc, argv);
  int status;
  {
    server application;
    TestNativeConversion tests;
    status= QTest::qExec (&tests, argc, argv);
  }
  gui_close ();
  release_boot_lock ();
  profile.reset ();
  std::exit (status);
}

int main (int argc, char** argv) {
  qputenv ("QT_QPA_PLATFORM", "offscreen");
  QApplication application (argc, argv);
  profile= std::make_unique<QTemporaryDir> (
    QCoreApplication::applicationDirPath () + "/native-conversion-profile-XXXXXX");
  if (!profile->isValid ()) return 1;
  const auto path= profile->path ().toUtf8 ();
  set_env ("ATHENA_HOME_PATH", string (path.constData (), path.size ()));
  cache_initialize ();
  reset_document_tree (current_document_tree ());
  init_athena ();
  run_tests (argc, argv);
  return 1;
}

#include "native_conversion_test.moc"
