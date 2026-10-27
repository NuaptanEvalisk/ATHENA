/******************************************************************************
* MODULE     : native_v3_export_test.cpp
* DESCRIPTION: Native XML v3 LaTeX and MathJax export without editor teardown
* COPYRIGHT  : (C) 2026 ATHENA contributors
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
******************************************************************************/
#include <QtTest/QtTest>
#include <QFile>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <cstdlib>
#include <stdexcept>

#include "analyze.hpp"
#include "convert.hpp"
#include "Data/NodeContracts/execution_state.hpp"
#include "math_tree_v3.hpp"
#include "node_metadata.hpp"
#include "scheme.hpp"
#include "Scheme/Guile/guile_tm.hpp"
#include "Xml/athena_document_xml.hpp"

using athena::node::property;
using namespace athena::math_v3;

namespace {

void set_property (tree& source, const std::string& name, property value) {
  athena::node::metadata metadata;
  if (const auto* existing= athena::node::get (source)) metadata= *existing;
  metadata.properties[name]= std::move (value);
  athena::node::set (source, metadata);
}

tree block (tree body, bool numbered= false) {
  tree result (MATH_BLOCK, body);
  set_property (result, "math:block-numbered", property (numbered));
  return result;
}

tree grid (const char* kind, tree rows) {
  tree result (MATH_GRID);
  for (int i=0; i<N(rows); ++i) result << rows[i];
  set_property (result, "math:grid-kind", property (std::string (kind)));
  return result;
}

tree accent (const char* glyph, const char* side, bool stretch) {
  tree result (MATH_ACCENT, glyph, side, "xy");
  set_property (result, "math:accent-stretch", property (stretch));
  return result;
}

string output (tree source, bool mathjax= false) {
  latex_export_context_reset ();
  latex_export_set_mathjax (mathjax);
  latex_export_env_set ("mode", object ("math"));
  scheme_tree result= latex_export_convert_node (
    latex_export_prepare_v3_source (source));
  if (mathjax) result= latex_export_mathjax_pre (result);
  result= latex_export_expand_macros (result);
  if (mathjax) result= latex_export_mathjax (result);
  return serialize_latex (result);
}

bool contains (const string& text, const char* part) {
  return search_forwards (string (part), 0, text) >= 0;
}

} // namespace

class TestNativeV3Export: public QObject {
  Q_OBJECT
  using property= athena::node::property;
private slots:
  void unicodeOperators () {
    const std::pair<const char*, const char*> supported[]= {
      {"\u2211", "\\sum"}, {"\u220f", "\\prod"}, {"\u2210", "\\coprod"},
      {"\u222b", "\\int"}, {"\u222c", "\\iint"}, {"\u222d", "\\iiint"},
      {"\u2a0c", "\\iiiint"}, {"\u222e", "\\oint"},
      {"\u22c3", "\\bigcup"}, {"\u22c2", "\\bigcap"},
      {"\u22c1", "\\bigvee"}, {"\u22c0", "\\bigwedge"},
      {"\u2a01", "\\bigoplus"}, {"\u2a02", "\\bigotimes"},
      {"\u2a00", "\\bigodot"}, {"\u2a04", "\\biguplus"},
      {"\u2a06", "\\bigsqcup"}
    };
    for (const auto& [glyph, command]: supported)
      for (const bool mathjax: {false, true})
        QVERIFY2 (contains (output (big_operator (glyph), mathjax), command), command);
    const std::pair<const char*, const char*> latex_only[]= {
      {"\u222f", "\\oiint"}, {"\u2230", "\\oiiint"},
      {"\u2a0d", "\\fint"}, {"\u2a05", "\\bigsqcap"}
    };
    for (const auto& [glyph, command]: latex_only) {
      QVERIFY (contains (output (big_operator (glyph)), command));
      QVERIFY_THROWS_EXCEPTION (std::runtime_error, output (big_operator (glyph), true));
    }
    QVERIFY_THROWS_EXCEPTION (std::runtime_error, output (big_operator ("\u222b", true)));
    QVERIFY_THROWS_EXCEPTION (std::runtime_error, output (big_operator ("sum")));
  }

  void semanticSymbols () {
    const tree dotted (MATH_SYMBOL, "multiple-integral-with-dots");
    QVERIFY (contains (output (dotted), "\\idotsint"));
    QVERIFY (contains (output (dotted, true), "\\idotsint"));
    for (const char* id: {"differential-d", "backward-assignment", "long-hookrightarrow"}) {
      const string tex= output (tree (MATH_SYMBOL, id), true);
      QVERIFY (!tex.empty ());
      QVERIFY (!contains (tex, "nonconverted"));
      QVERIFY (!contains (tex, "ATHENA-DATA"));
    }
    QVERIFY_THROWS_EXCEPTION (std::runtime_error,
      output (tree (MATH_SYMBOL, "not-a-registered-symbol")));
    // Registered source identities without a TeX recipe must not become
    // \nonconverted placeholders, even though the source is valid v3.
    QVERIFY_THROWS_EXCEPTION (std::runtime_error,
      output (tree (MATH_SYMBOL, "oover")));
    QVERIFY_THROWS_EXCEPTION (std::runtime_error,
      output (tree (MATH_SYMBOL, "llbracket"), true));
  }

  void independentDelimiterSizesAndNullEndpoints () {
    for (const bool mathjax: {false, true}) {
      const string fixed= output (delimited ("", "|", 'f', "x"), mathjax);
      QVERIFY (!contains (fixed, "."));
      QVERIFY (contains (fixed, "\\mathclose"));
      const string manual= output (delimited ("", "|", 'm', "x", "0", "3"), mathjax);
      QVERIFY (!contains (manual, "."));
      QVERIFY (contains (manual, "\\biggr|"));
      const string independent= output (delimited ("\u2191", "\u21d3", 'm', "x", "1", "4"), mathjax);
      QVERIFY (contains (independent, "\\bigl\\uparrow"));
      QVERIFY (contains (independent, "\\Biggr\\Downarrow"));
      const string automatic= output (delimited ("", "|", 'a', "x"), mathjax);
      QVERIFY (contains (automatic, "\\left."));
      QVERIFY (contains (automatic, "\\right|"));
    }
    QVERIFY_THROWS_EXCEPTION (std::runtime_error,
      output (delimited ("(", ")", 'm', "x", "5", "1")));
    QVERIFY_THROWS_EXCEPTION (std::runtime_error,
      output (delimited ("\u27e6", "\u27e7", 'a', "x"), true));
  }

  void textualVariantsAndStretchPolicy () {
    for (const bool mathjax: {false, true}) {
      QVERIFY (contains (output (tree (MATH_VARIANT, "bf", "sin x"), mathjax), "\\textbf"));
      QVERIFY (contains (output (tree (MATH_VARIANT, "rm", "abc"), mathjax), "\\mathrm"));
      QVERIFY (contains (output (tree (MATH_VARIANT, "bb", "R"), mathjax), "\\mathbb"));
      QVERIFY (contains (output (accent ("\u0305", "o", false), mathjax), "\\bar"));
      QVERIFY (contains (output (accent ("\u0305", "o", true), mathjax), "\\overline"));
      QVERIFY (contains (output (accent ("\u20db", "o", false), mathjax), "\\dddot"));
      QVERIFY (contains (output (accent ("\u20dc", "o", false), mathjax), "\\ddddot"));
      QVERIFY (contains (output (accent ("\u20e1", "u", true), mathjax), "\\underleftrightarrow"));
      QVERIFY_THROWS_EXCEPTION (std::runtime_error,
        output (accent ("\u0302", "u", true), mathjax));
      QVERIFY_THROWS_EXCEPTION (std::runtime_error,
        output (accent ("\u0307", "o", true), mathjax));
    }
    QVERIFY (contains (output (tree (MATH_VARIANT, "sl", "abc")), "\\textsl"));
    QVERIFY_THROWS_EXCEPTION (std::runtime_error,
      output (tree (MATH_VARIANT, "sl", "abc"), true));
    const string arrow= output (tree (MATH_LONG_ARROW, "\u21a6", "i", "f"), true);
    QVERIFY (contains (arrow, "\\require{mathtools}"));
    QVERIFY (contains (arrow, "\\xmapsto"));
  }

  void typedGridFormattingAndSourceIdentity () {
    tree source= grid ("matrix", tree (TUPLE,
      tree (ROW, tree (CELL, "a"), tree (CELL, "b")),
      tree (ROW, tree (CELL, "c"), tree (CELL, "d"))));
    for (int i=0; i<2; ++i)
      set_property (source[i][0], "math:cell-format", property (
        property::dictionary {{"align", property (std::string ("l"))},
                              {"color", property (std::string ("#123456"))}}));
    athena::node::edit (source)->id= athena::node::new_id ();
    set_property (source, "fixture:typed", property (std::int64_t (42)));
    const auto before= athena::document::write_xml_v3 (tree (DOCUMENT, source));
    for (const bool mathjax: {false, true}) {
      const string tex= output (block (source, true), mathjax);
      QVERIFY (contains (tex, "\\begin{equation}"));
      QVERIFY (contains (tex, "\\begin{array}{@{}lc@{}}"));
      QVERIFY (contains (tex, "\\color"));
      QVERIFY (contains (tex, "123456"));
      QVERIFY (!contains (tex, "!math-cell-format"));
      QCOMPARE (athena::document::write_xml_v3 (tree (DOCUMENT, source)), before);
    }
    set_property (source[1][0], "math:cell-format", property (
      property::dictionary {{"align", property (std::string ("r"))}}));
    QVERIFY_THROWS_EXCEPTION (std::runtime_error, output (source));
    set_property (source[0][0], "math:cell-format", property (
      property::dictionary {{"border-left", property (std::string ("1ln"))}}));
    QVERIFY_THROWS_EXCEPTION (std::runtime_error, latex_export_prepare_v3_source (source));
  }

  void nestedGridScopeAndNumbering () {
    const tree aligned= grid ("align", tree (TUPLE,
      tree (ROW, tree (CELL, "a"), tree (CELL, "b")),
      tree (ROW, tree (CELL, "c"), tree (CELL, "d"))));
    const tree matrix= grid ("matrix", tree (TUPLE,
      tree (ROW, tree (CELL, aligned)), tree (ROW, tree (CELL, "x"))));
    const string tex= output (block (matrix, true), true);
    QVERIFY (contains (tex, "\\begin{equation}"));
    QVERIFY (contains (tex, "\\begin{matrix}"));
    QVERIFY (contains (tex, "\\begin{aligned}"));
    QVERIFY (!contains (tex, "\\begin{align*}"));
    QVERIFY (contains (output (block (aligned, true)), "\\begin{align}"));
    QVERIFY (contains (output (block (aligned)), "\\begin{align*}"));
    QVERIFY (contains (output (aligned), "\\begin{aligned}"));
    QVERIFY_THROWS_EXCEPTION (std::runtime_error,
      output (grid ("generic", tree (TUPLE, tree (ROW, tree (CELL, "x"))))));
  }

  void rowNumberingAndCustomTags () {
    tree source= grid ("align", tree (TUPLE,
      tree (ROW, tree (CELL, "a"), tree (CELL, "b")),
      tree (ROW, tree (CELL, "c"), tree (CELL, "d")),
      tree (ROW, tree (CELL, "e"), tree (CELL, "f"))));
    set_property (source[1], "math:row-numbering",
                  property (std::string ("unnumbered")));
    set_property (source[2], "math:row-tag",
                  property (athena::node::rich_text {tree ("Special")}));
    athena::node::edit (source[2])->id= "12345678-1234-4234-8234-123456789abc";
    for (bool mathjax: {false, true}) {
      const string tex= output (block (source, true), mathjax);
      QVERIFY (contains (tex, "\\begin{align}"));
      QVERIFY (contains (tex, "\\nonumber"));
      QVERIFY (contains (tex, "\\tag{Special}"));
      QVERIFY (contains (tex, "\\label{athena-node-12345678-1234-4234-8234-123456789abc}"));
      QVERIFY (!contains (tex, "!math-row-policy"));
      const string unnumbered= output (block (source, false), mathjax);
      QVERIFY (contains (unnumbered, "\\begin{align*}"));
      QVERIFY (contains (unnumbered, "\\tag{Special}"));
      set_property (source[0], "math:row-numbering",
                    property (std::string ("numbered")));
      const string forced= output (block (source, false), mathjax);
      QVERIFY (contains (forced, "\\begin{align}"));
      QVERIFY (contains (forced, "\\nonumber"));
      athena::node::edit (source[0])->properties.erase ("math:row-numbering");
    }
    set_row_numbering (source[2], row_numbering::unnumbered);
    set_row_tag (source[2], tree (""));
    for (bool mathjax: {false, true}) {
      const string empty= output (block (source, false), mathjax);
      QVERIFY (contains (empty, "\\tag{}"));
      QVERIFY (contains (empty, "\\begin{align*}"));
      set_row_tag (source[2], tree (CONCAT, "Case ", compound ("math", "x")));
      const string rich= output (block (source, false), mathjax);
      QVERIFY (contains (rich, "\\tag{Case"));
      QVERIFY (contains (rich, "$x$"));
      set_row_tag (source[2], scripts ("x", "", "", "i", ""));
      const string native= output (block (source, false), mathjax);
      QVERIFY (contains (native, "$x_i$"));
      set_row_tag (source[2], tree (""));
    }
    QVERIFY_THROWS_EXCEPTION (std::runtime_error, output (source));
    tree missing_number= copy (source);
    missing_number[1][1][0]= tree (CONCAT, "d", tree (LABEL, "no-number"));
    QVERIFY_THROWS_EXCEPTION (std::runtime_error, output (block (missing_number, true)));
    tree matrix= grid ("matrix", tree (TUPLE, tree (ROW, tree (CELL, source))));
    QVERIFY_THROWS_EXCEPTION (std::runtime_error, output (block (matrix, true)));

    tree eqnarray= grid ("eqnarray", tree (TUPLE,
      tree (ROW, tree (CELL, "x"), tree (CELL, "="), tree (CELL, "y")),
      tree (ROW, tree (CELL, "z"), tree (CELL, "="), tree (CELL, "w"))));
    set_property (eqnarray[0], "math:row-numbering",
                  property (std::string ("unnumbered")));
    QVERIFY (contains (output (block (eqnarray, true)), "\\nonumber"));
    set_property (eqnarray[1], "math:row-tag",
                  property (athena::node::rich_text {tree ("Q")}));
    QVERIFY_THROWS_EXCEPTION (std::runtime_error, output (block (eqnarray, true)));
  }

  void failuresRestoreExportScopes () {
    latex_export_context_reset ();
    latex_export_env_set ("mode", object ("math"));
    const tree unsupported= big_operator ("\u222b", true);
    const tree nested= delimited ("(", ")", 'a', unsupported);
    QVERIFY_THROWS_EXCEPTION (std::runtime_error,
      latex_export_convert_node (latex_export_prepare_v3_source (nested)));
    const string middle= serialize_latex (latex_export_convert_node (
      latex_export_prepare_v3_source (tree (MATH_MIDDLE, "|"))));
    QVERIFY (contains (middle, "\\mid"));
    QVERIFY (!contains (middle, "\\middle"));
    QVERIFY_THROWS_EXCEPTION (std::runtime_error,
      latex_export_convert_node (latex_export_prepare_v3_source (
        tree (MATH_VARIANT, "bf", unsupported))));
    QCOMPARE (as_string (latex_export_env_get ("mode")), string ("math"));
  }

  void latexRowReferencesUseMatchingCounterValues () {
    const QString latex= QStandardPaths::findExecutable ("pdflatex");
    if (latex.isEmpty ()) QSKIP ("pdflatex is not installed");
    tree source= grid ("align", tree (TUPLE,
      tree (ROW, tree (CELL, "a"), tree (CELL, tree (CONCAT, "b", tree (LABEL, "first")))),
      tree (ROW, tree (CELL, "c"), tree (CELL, "d")),
      tree (ROW, tree (CELL, "e"), tree (CELL, tree (CONCAT, "f", tree (LABEL, "custom")))),
      tree (ROW, tree (CELL, "g"), tree (CELL, tree (CONCAT, "h", tree (LABEL, "last"))))));
    set_row_numbering (source[1], row_numbering::unnumbered);
    set_row_tag (source[2], tree ("T"));
    const string converted= output (block (source, true));
    QTemporaryDir temp;
    QVERIFY (temp.isValid ());
    QFile file (temp.filePath ("rows.tex"));
    QVERIFY (file.open (QIODevice::WriteOnly));
    const QByteArray document= QByteArray ("\\documentclass{article}\n\\usepackage{amsmath}\n\\begin{document}\n") +
      QByteArray (converted.data (), N(converted)) + "\n\\end{document}\n";
    QCOMPARE (file.write (document), document.size ());
    file.close ();
    for (int pass= 0; pass<2; ++pass) {
      QProcess process;
      process.setWorkingDirectory (temp.path ());
      process.setProcessChannelMode (QProcess::MergedChannels);
      process.start (latex, {"-interaction=nonstopmode", "-halt-on-error", "rows.tex"});
      QVERIFY (process.waitForStarted ());
      if (!process.waitForFinished (30000)) {
        process.kill ();
        process.waitForFinished ();
        QFAIL ("pdflatex timed out");
      }
      const QByteArray log= process.readAll ();
      QVERIFY2 (process.exitStatus () == QProcess::NormalExit && process.exitCode () == 0,
                log.constData ());
    }
    QFile aux (temp.filePath ("rows.aux"));
    QVERIFY (aux.open (QIODevice::ReadOnly));
    const QByteArray references= aux.readAll ();
    QVERIFY2 (references.contains ("\\newlabel{first}{{1}"), references.constData ());
    QVERIFY2 (references.contains ("\\newlabel{custom}{{{T}}"), references.constData ());
    QVERIFY2 (references.contains ("\\newlabel{last}{{2}"), references.constData ());
  }
};

static void run_tests (int argc, char** argv) {
  initialize_scheme ();
  athena::contracts::initialize_builtins ();
  TestNativeV3Export test;
  std::exit (QTest::qExec (&test, argc, argv));
}

int main (int argc, char** argv) {
  QCoreApplication app (argc, argv);
  start_scheme (argc, argv, run_tests);
  return 1;
}

#include "native_v3_export_test.moc"
