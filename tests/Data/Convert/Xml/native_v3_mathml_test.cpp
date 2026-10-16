/******************************************************************************
* MODULE     : native_v3_mathml_test.cpp
* DESCRIPTION: Direct, strict Presentation MathML import into native v3 trees
* COPYRIGHT  : (C) 2026 ATHENA contributors
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
******************************************************************************/
#include <QtTest/QtTest>
#include <stdexcept>

#include "convert.hpp"
#include "drd_std.hpp"
#include "math_tree_v3.hpp"
#include "node_metadata.hpp"
#include "Xml/athena_document_xml.hpp"

namespace {

tree import_mathml (const char* source) { return mathml_to_tree_v3 (parse_xml (source)); }

tree formula (const char* source) {
  const tree result= import_mathml (source);
  if (!is_func (result, make_tree_label ("math"), 1))
    throw std::runtime_error ("Expected inline math fixture");
  return result[0];
}

const athena::node::property& node_property (const tree& source, const char* key) {
  const auto* meta= athena::node::get (source);
  if (!meta) throw std::runtime_error ("Missing native math metadata");
  return meta->properties.at (key);
}

std::string policy (const tree& source) {
  return std::get<std::string> (node_property (source, "math:scripts-policy").data);
}

bool native_only (const tree& source) {
  if (!athena::math_v3::valid_native_math (source)) return false;
  if (is_atomic (source)) return true;
  switch (L(source)) {
  case LEFT: case RIGHT: case MID: case BIG: case AROUND: case VAR_AROUND:
  case BIG_AROUND: case RSUB: case RSUP: case LSUB: case LSUP:
  case WIDE: case VAR_WIDE: case NAMED_SYMBOL: case TFORMAT: case TABLE:
    return false;
  default: break;
  }
  for (int i=0; i<N(source); ++i) if (!native_only (source[i])) return false;
  return true;
}

} // namespace

class TestNativeV3Mathml: public QObject {
  Q_OBJECT
private slots:
  void initTestCase () { init_std_drd (); }

  void scriptsAndLimitPolicies () {
    const tree scripts= formula (
      "<math><msubsup><mi>x</mi><mi>i</mi><mn>2</mn></msubsup></math>");
    QVERIFY (is_func (scripts, MATH_SCRIPTS, 5));
    QCOMPARE (scripts[0], tree ("x"));
    QCOMPARE (scripts[1], tree (""));
    QCOMPARE (scripts[2], tree (""));
    QCOMPARE (scripts[3], tree ("i"));
    QCOMPARE (scripts[4], tree ("2"));
    QCOMPARE (policy (scripts), std::string ("r"));

    const tree sum= formula (
      "<math><munderover><mo>&Sum;</mo><mi>i</mi><mi>n</mi></munderover></math>");
    QVERIFY (is_func (sum[0], BIG_OPERATOR, 2));
    QCOMPARE (sum[0][0], tree ("\u2211"));
    QCOMPARE (policy (sum), std::string ("d"));
    const tree integral= formula (
      "<math><munder><mo>&Integral;</mo><mi>D</mi></munder></math>");
    QCOMPARE (policy (integral), std::string ("o"));
    const tree fixed= formula (
      "<math><munder><mo movablelimits='false'>&Sum;</mo><mi>i</mi></munder></math>");
    QCOMPARE (policy (fixed), std::string ("o"));
    const tree explicit_limits= formula (
      "<math><munder><mo movablelimits='true'>lim</mo><mi>x</mi></munder></math>");
    QCOMPARE (policy (explicit_limits), std::string ("d"));
  }

  void prescriptsAndMissingSlots () {
    const tree value= formula (
      "<math><mmultiscripts><mi>T</mi><mi>i</mi><none/>"
      "<mprescripts/><none/><mi>j</mi></mmultiscripts></math>");
    QVERIFY (is_func (value, MATH_SCRIPTS, 5));
    QCOMPARE (value[1], tree (""));
    QCOMPARE (value[2], tree ("j"));
    QCOMPARE (value[3], tree ("i"));
    QCOMPARE (value[4], tree (""));
    QCOMPARE (policy (value), std::string ("r"));
  }

  void pairedFencesAndDivision () {
    const tree divided= formula ("<math><mi>A</mi><mo>/</mo><mi>B</mi></math>");
    QVERIFY (is_func (divided, CONCAT, 3));
    QCOMPARE (divided[1], tree ("/"));
    const tree pair= formula (
      "<math><mrow><mo>(</mo><mi>x</mi><mo>[</mo><mi>y</mi><mo>]</mo><mo>)</mo></mrow></math>");
    QVERIFY (is_func (pair, DELIMITED, 6));
    QCOMPARE (pair[0], tree ("("));
    QCOMPARE (pair[1], tree (")"));
    QVERIFY (is_func (pair[5][1], DELIMITED, 6));
    const tree natural= formula (
      "<math><mo stretchy='false'>(</mo><mi>x</mi><mo stretchy='false'>]</mo></math>");
    QCOMPARE (natural[2], tree ("f"));
    QCOMPARE (natural[1], tree ("]"));
    const tree bar= formula (
      "<math><mo fence='true' form='prefix'>|</mo><mi>x</mi>"
      "<mo fence='true' form='postfix'>|</mo></math>");
    QVERIFY (is_func (bar, DELIMITED, 6));
    const tree separator= formula (
      "<math><mo>{</mo><mi>x</mi><mo stretchy='true'>|</mo><mi>y</mi><mo>}</mo></math>");
    QVERIFY (is_func (separator[5][1], MATH_MIDDLE, 1));
  }

  void mfencedDefaultsEmptyEndpointsAndRepeatingSeparators () {
    const tree defaults= formula (
      "<math><mfenced><mi>a</mi><mi>b</mi><mi>c</mi></mfenced></math>");
    QVERIFY (is_func (defaults, DELIMITED, 6));
    QCOMPARE (defaults[5][1], tree (","));
    QCOMPARE (defaults[5][3], tree (","));
    const tree repeated= formula (
      "<math><mfenced open='' close=']' separators=', ;'>"
      "<mi>a</mi><mi>b</mi><mi>c</mi><mi>d</mi></mfenced></math>");
    QCOMPARE (repeated[0], tree (""));
    QCOMPARE (repeated[1], tree ("]"));
    QCOMPARE (repeated[5][1], tree (","));
    QCOMPARE (repeated[5][3], tree (";"));
    QCOMPARE (repeated[5][5], tree (";"));
    const tree unfenced= formula (
      "<math><mfenced open='' close='' separators=''><mi>a</mi><mi>b</mi></mfenced></math>");
    QVERIFY (is_func (unfenced, CONCAT, 2));
  }

  void accentsAnnotationsAndArrows () {
    const tree hat= formula (
      "<math><mover accent='true'><mi>x</mi><mo stretchy='false'>&Hat;</mo></mover></math>");
    QVERIFY (is_func (hat, MATH_ACCENT, 3));
    QCOMPARE (hat[0], tree ("\u0302"));
    QCOMPARE (hat[1], tree ("o"));
    QVERIFY (!std::get<bool> (node_property (hat, "math:accent-stretch").data));
    const tree brace= formula (
      "<math><munder><mi>x</mi><mo>&UnderBrace;</mo></munder></math>");
    QVERIFY (is_func (brace, MATH_ACCENT, 3));
    QCOMPARE (brace[1], tree ("u"));
    QVERIFY (std::get<bool> (node_property (brace, "math:accent-stretch").data));
    const tree annotation= formula (
      "<math><mover accent='false'><mi>x</mi><mo>^</mo></mover></math>");
    QVERIFY (is_func (annotation, MATH_ANNOTATION, 3));
    QCOMPARE (annotation[2], tree ("^"));
    const tree arrow= formula (
      "<math><munderover><mo stretchy='true'>&RightArrow;</mo>"
      "<mi>i</mi><mi>f</mi></munderover></math>");
    QVERIFY (is_func (arrow, MATH_LONG_ARROW, 3));
    QCOMPARE (arrow[0], tree ("\u2192"));
    QCOMPARE (arrow[1], tree ("i"));
    QCOMPARE (arrow[2], tree ("f"));
  }

  void variantsSymbolsAndSharedStructures () {
    const tree value= formula (
      "<math><mstyle mathvariant='double-struck'><mfrac><mi>R</mi>"
      "<mi mathvariant='normal'>x</mi></mfrac></mstyle></math>");
    QVERIFY (is_func (value, FRAC, 2));
    QVERIFY (is_func (value[0], MATH_VARIANT, 2));
    QCOMPARE (value[0][0], tree ("bb"));
    QCOMPARE (value[1][0], tree ("rm"));
    const tree differential= formula ("<math><mo>&DifferentialD;</mo></math>");
    QVERIFY (is_func (differential, MATH_SYMBOL, 1));
    QCOMPARE (differential[0], tree ("differential-d"));
    const tree root= formula ("<math><mroot><mi>x</mi><mn>3</mn></mroot></math>");
    QVERIFY (is_func (root, SQRT, 2));
    const tree stack= formula (
      "<math><mfrac linethickness='0'><mi>x</mi><mi>y</mi></mfrac></math>");
    QVERIFY (is_func (stack, MATH_GRID, 2));
    QCOMPARE (std::get<std::string> (node_property (stack, "math:grid-kind").data),
              std::string ("stack"));
  }

  void tableFormattingDisplayAndRoundtrip () {
    const tree source= import_mathml (
      "<m:math xmlns:m='http://www.w3.org/1998/Math/MathML' display='block'>"
      "<m:mtable columnalign='left right' mathcolor='red'>"
      "<m:mtr><m:mtd><m:mi>x</m:mi></m:mtd><m:mtd><m:mi>y</m:mi></m:mtd></m:mtr>"
      "<m:mtr columnalign='center'><m:mtd><m:mi>u</m:mi></m:mtd>"
      "<m:mtd columnalign='left' mathbackground='yellow' id='cell-v'><m:mi>v</m:mi></m:mtd>"
      "</m:mtr></m:mtable></m:math>");
    QVERIFY (is_func (source, MATH_BLOCK, 1));
    QVERIFY (!std::get<bool> (node_property (source, "math:block-numbered").data));
    const tree grid= source[0];
    QVERIFY (is_func (grid, MATH_GRID, 2));
    QCOMPARE (std::get<std::string> (node_property (grid, "math:grid-kind").data),
              std::string ("generic"));
    const auto& format= std::get<athena::node::property::dictionary> (
      node_property (grid[1][1], "math:cell-format").data);
    QCOMPARE (std::get<std::string> (format.at ("align").data), std::string ("l"));
    QCOMPARE (std::get<std::string> (format.at ("color").data), std::string ("red"));
    QCOMPARE (std::get<std::string> (format.at ("background").data), std::string ("yellow"));
    QVERIFY (native_only (source));
    using namespace athena::document;
    const auto xml= write_xml_v3 (source, xml_kind::fragment);
    const tree restored= read_xml_v3 (xml, xml_kind::fragment);
    QCOMPARE (write_xml_v3 (restored, xml_kind::fragment), xml);
    QVERIFY (athena::node::equal_metadata (restored, source));
  }

  void unsupportedAndMalformedInputFails () {
    const char* rejected[]= {
      "<math><mfrac><mi>x</mi></mfrac></math>",
      "<math><mo>(</mo><mi>x</mi></math>",
      "<math><mi>x</mi><mo>)</mo></math>",
      "<math><mo stretchy='false'>(</mo><mi>x</mi><mo>)</mo></math>",
      "<math><mo minsize='2em'>(</mo><mi>x</mi><mo>)</mo></math>",
      "<math><mi mathvariant='fraktur'>x</mi></math>",
      "<math><mi>&not-defined;</mi></math>",
      "<math><mi style='color:red'>x</mi></math>",
      "<math><mi displaystyle='yes'>x</mi></math>",
      "<math><mspace width='2em'/></math>",
      "<math><menclose notation='box'><mi>x</mi></menclose></math>",
      "<math><unknown><mi>x</mi></unknown></math>",
      "<math><mi><mn>2</mn></mi></math>",
      "<math>text outside a token</math>",
      "<math><none/></math>",
      "<math><munder><mo>lim</mo><mi>x</mi></munder></math>",
      "<math><mmultiscripts><mi>x</mi><mi>i</mi></mmultiscripts></math>",
      "<math><mmultiscripts><mi>x</mi><mi>i</mi><none/><mi>j</mi><none/></mmultiscripts></math>",
      "<math><mtable><mlabeledtr><mtd><mn>1</mn></mtd><mtd><mi>x</mi></mtd></mlabeledtr></mtable></math>",
      "<math><mtable><mtr><mtd columnspan='2'><mi>x</mi></mtd></mtr></mtable></math>",
      "<math><semantics><mi>x</mi><annotation encoding='TeX'>x</annotation></semantics></math>",
      "<math><mi id='same'>x</mi><mi id='same'>y</mi></math>",
      "<math xmlns='urn:not-mathml'><mi>x</mi></math>",
      "<other:math><other:mi>x</other:mi></other:math>"
    };
    for (const char* source: rejected)
      QVERIFY_THROWS_EXCEPTION (std::runtime_error, import_mathml (source));
  }

  void v2EntryRetainsItsExistingTrees () {
    const scheme_tree source (TUPLE, "mo", scm_quote ("&Sum;"));
    QCOMPARE (mathml_to_tree (source), tree (BIG, "\u2211"));
    QCOMPARE (mathml_to_tree_v3 (source), tree (BIG_OPERATOR, "\u2211", "0"));
    const scheme_tree base (TUPLE, "mi", scm_quote ("x"));
    const scheme_tree index (TUPLE, "mi", scm_quote ("i"));
    const scheme_tree script (TUPLE, "msub", base, index);
    QVERIFY (is_func (mathml_to_tree (script)[1], RSUB, 1));
    QVERIFY (is_func (mathml_to_tree_v3 (script), MATH_SCRIPTS, 5));
  }
};

QTEST_GUILESS_MAIN (TestNativeV3Mathml)
#include "native_v3_mathml_test.moc"
