/******************************************************************************
* MODULE     : xml_v3_test.cpp
* DESCRIPTION: Strict, metadata-preserving compact XML v3 document/tree codec
* COPYRIGHT  : (C) 2026 ATHENA contributors
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
******************************************************************************/
#include <QtTest/QtTest>
#include <QFile>
#include <QTemporaryDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include "Xml/athena_document_xml.hpp"
#include "Xml/document_upgrade_file.hpp"
#include "Xml/math_v2_to_v3.hpp"
#include "Xml/vault_xml_v3_history.hpp"
#include "Xml/vault_xml_v3_indexes.hpp"
#include "Xml/vault_format_upgrade.hpp"
#include "ATHENA/Data/document_history_store.hpp"
#include "ATHENA/Data/vault.hpp"
#include "ATHENA/Data/fossil_delta.hpp"
#include "ATHENA/Hodarium/revisions.hpp"
#include "math_tree_v3.hpp"
#include "node_metadata.hpp"
#include "drd_std.hpp"
#include <limits>
#include <filesystem>
#include <sqlite3.h>

using namespace athena::document;

class TestXmlV3: public QObject {
  Q_OBJECT
private slots:
  void initTestCase () { init_std_drd (); }

  void compactTextIsNeverPolymorphic () {
    tree original (DOCUMENT, tree ("first"), tree (""));
    const auto bytes= write_xml_v3 (original);
    QVERIFY (bytes.find ("<a v=\"3\" k=\"d\">") != std::string::npos);
    QVERIFY (bytes.find ("<n t=\"document\">") != std::string::npos);
    QVERIFY (bytes.find ("<t><v>first</v></t>") != std::string::npos);
    QVERIFY (bytes.find ("<t><v/></t>") != std::string::npos ||
             bytes.find ("<t><v></v></t>") != std::string::npos);
    QVERIFY (read_xml_v3 (bytes) == original);
    QCOMPARE (write_xml_v3 (read_xml_v3 (bytes)), bytes);
  }

  void nativeMathStructures () {
    using namespace athena::math_v3;
    tree expression= delimited ("⟨", "⟩", 'a',
      scripts (tree ("x"), tree (""), tree (""), tree ("i"), tree ("2")));
    tree sum= scripts (big_operator ("∑"), tree (""), tree (""),
                      tree ("i=1"), tree ("n"));
    tree one_sided= delimited ("", "|", 'm', tree ("f(x)"), "0", "3");
    tree middle= delimited ("{", "}", 'a',
      tree (CONCAT, tree ("x"), tree (MATH_MIDDLE, tree ("|")), tree ("P(x)")));
    tree root (DOCUMENT, tree (CONCAT, expression, sum, one_sided, middle));
    const auto xml= write_xml_v3 (root);
    QVERIFY (xml.find ("<d l=\"⟨\" r=\"⟩\" s=\"a\">") != std::string::npos);
    QVERIFY (xml.find ("<o k=\"∑\" u=\"0\"/>") != std::string::npos);
    QVERIFY (xml.find ("<n t=\"scripts\" p=\"a\">") != std::string::npos);
    QVERIFY (xml.find ("<d l=\"\" r=\"|\" s=\"m\" ls=\"0\" rs=\"3\">") != std::string::npos);
    QVERIFY (xml.find ("<m k=\"|\"/>") != std::string::npos);
    auto restored= read_xml_v3 (xml);
    QVERIFY (restored == root);
    QCOMPARE (write_xml_v3 (restored), xml);
  }

  void bigOperatorsRequireOneUnicodeGlyph () {
    for (const string name: {string ("cup"), string ("oplus"),
                             string ("unknown-operator"), string ("")}) {
      const tree invalid (BIG_OPERATOR, name, "0");
      QVERIFY (!athena::math_v3::valid_big_operator (invalid));
      QVERIFY_THROWS_EXCEPTION (std::invalid_argument,
        athena::math_v3::big_operator (name));
      QVERIFY_THROWS_EXCEPTION (codec_exception,
        write_xml_v3 (invalid, xml_kind::fragment));
      const std::string xml= "<a v=\"3\" k=\"t\"><o k=\"" +
        std::string (name.data (), N (name)) + "\" u=\"0\"/></a>";
      QVERIFY_THROWS_EXCEPTION (codec_exception,
        read_xml_v3 (xml, xml_kind::fragment));
    }
  }

  void historicalBigOperatorNamesBecomeGlyphs () {
    const std::pair<const char*, const char*> cases[]= {
      {"cup", "⋃"}, {"cap", "⋂"}, {"wedge", "⋀"}, {"vee", "⋁"},
      {"oplus", "⨁"}, {"otimes", "⨂"}, {"odot", "⨀"},
      {"sqcup", "⨆"}, {"sqcap", "⨅"}, {"pluscup", "⨄"},
      {"amalg", "∐"}, {"times", "×"}, {"bigcup", "⋃"}, {"∫", "∫"}
    };
    for (const auto& [name, glyph]: cases) {
      tree op (BIG, name);
      athena::node::metadata metadata;
      metadata.id= athena::node::new_id ();
      athena::node::set (op, metadata);
      const tree source (DOCUMENT, compound ("math",
        tree (CONCAT, op, tree (RSUB, "n"), tree ("A"))));
      const auto result= migrate_math_v2_tree (source, true);
      QVERIFY (result.warnings.empty ());
      const tree scripted= result.document[0][0][0];
      QVERIFY (is_func (scripted, MATH_SCRIPTS, 5));
      tree expected (BIG_OPERATOR, glyph, "0");
      athena::node::set (expected, metadata);
      QCOMPARE (scripted[0], expected);
      QCOMPARE (athena::node::id (scripted[0]), metadata.id);
      QCOMPARE (scripted[3], tree ("n"));
      const auto xml= write_xml_v3 (result.document);
      QCOMPARE (read_xml_v3 (xml), result.document);
    }
    QVERIFY_THROWS_EXCEPTION (std::runtime_error,
      migrate_math_v2_tree (tree (DOCUMENT, compound ("math",
        tree (BIG, "unknown-operator"))), true));
  }

  void mathematicalAlphabetMigrationPreservesNativeFontSemantics () {
    struct fixture { const char* old; const char* kind; } samples[]= {
      {"math-alpha-bold", "mbf"},
      {"math-alpha-bold-up", "mbfu"},
      {"math-alpha-frak", "frak"},
      {"math-alpha-cal", "cal"},
      {"math-alpha-bbb", "bb"},
      {"math-bf", "bf"}
    };
    for (const auto& example: samples) {
      tree styled= compound (example.old, "A");
      athena::node::metadata identity;
      identity.id= athena::node::new_id ();
      athena::node::set (styled, identity);
      const tree old (DOCUMENT, compound ("math", styled));
      const auto migrated= migrate_math_v2_tree (old, true);
      QVERIFY (migrated.warnings.empty ());
      const tree value= migrated.document[0][0];
      QVERIFY (is_func (value, MATH_VARIANT, 2));
      QCOMPARE (value[0], tree (example.kind));
      QCOMPARE (value[1], tree ("A"));
      QCOMPARE (athena::node::id (value), identity.id);
      const auto xml= write_xml_v3 (migrated.document);
      const tree restored= read_xml_v3 (xml);
      QCOMPARE (restored, migrated.document);
      QCOMPARE (athena::node::id (restored[0][0]), identity.id);
      QCOMPARE (write_xml_v3 (restored), xml);
    }
  }

  void malformedLongArrowSymbolFailsClosed () {
    // A malformed semantic ID subtree used to dereference atomic_rep::label
    // while validating MATH_LONG_ARROW, crashing the XML v3 writer. Neither
    // an isolated native fragment nor a document may serialize it.
    const tree wrong (MATH_LONG_ARROW,
      tree (MATH_SYMBOL, tree (CONCAT, "differential-d")), "", "");
    QVERIFY (!athena::math_v3::valid_native_math (wrong));
    QVERIFY_THROWS_EXCEPTION (codec_exception,
      write_xml_v3 (wrong, xml_kind::fragment));
    QVERIFY_THROWS_EXCEPTION (codec_exception,
      write_xml_v3 (tree (DOCUMENT, wrong)));

    const tree valid (MATH_LONG_ARROW, "→", "i", "n");
    QVERIFY (athena::math_v3::valid_native_math (valid));
    const std::string encoded= write_xml_v3 (valid, xml_kind::fragment);
    QCOMPARE (read_xml_v3 (encoded, xml_kind::fragment), valid);
  }

  void attributedMathDescriptorsCannotDisappearDuringEncoding () {
    // Compact <d>, <o>, and <m> encode descriptor values in attributes, not
    // as child nodes. Child source IDs and typed properties cannot survive
    // that encoding and must be rejected before any bytes are published.
    athena::node::metadata descriptor_identity;
    descriptor_identity.id= athena::node::new_id ();
    for (int index= 0; index<5; ++index) {
      tree value= athena::math_v3::delimited ("(", ")", 'm', "x", "1", "2");
      athena::node::set (value[index], descriptor_identity);
      QVERIFY (!athena::math_v3::valid_delimited (value));
      QVERIFY_THROWS_EXCEPTION (codec_exception,
        write_xml_v3 (value, xml_kind::fragment));
    }
    for (int index= 0; index<2; ++index) {
      tree value= athena::math_v3::big_operator ("∑");
      athena::node::set (value[index], descriptor_identity);
      QVERIFY (!athena::math_v3::valid_big_operator (value));
      QVERIFY_THROWS_EXCEPTION (codec_exception,
        write_xml_v3 (value, xml_kind::fragment));
    }
    tree middle (MATH_MIDDLE, "|");
    athena::node::set (middle[0], descriptor_identity);
    QVERIFY (!athena::math_v3::valid_native_math (middle));
    QVERIFY_THROWS_EXCEPTION (codec_exception,
      write_xml_v3 (middle, xml_kind::fragment));

    // An identity on the expression child belongs to a serialized source
    // node and must still roundtrip normally.
    tree legitimate= athena::math_v3::delimited ("(", ")", 'a', "x");
    athena::node::set (legitimate[5], descriptor_identity);
    const auto bytes= write_xml_v3 (legitimate, xml_kind::fragment);
    const auto restored= read_xml_v3 (bytes, xml_kind::fragment);
    QCOMPARE (athena::node::id (restored[5]), descriptor_identity.id);
    QCOMPARE (write_xml_v3 (restored, xml_kind::fragment), bytes);
  }

  void typedMetadataAndBinary () {
    using athena::node::property;
    auto meta_id= athena::node::new_id ();
    tree source (DOCUMENT, tree ("body"));
    tree raw (RAW_DATA, tree (string ("\0\xff\r", 3)));
    athena::node::metadata bytes_meta;
    bytes_meta.id= athena::node::new_id ();
    athena::node::set (raw[0], bytes_meta);
    source << raw;
    athena::node::metadata meta;
    meta.id= meta_id;
    meta.properties= {
      {"boolean", property (true)},
      {"integer", property (std::int64_t (42))},
      {"float", property (-0.0)},
      {"ref", property (athena::node::reference {bytes_meta.id})},
      {"list", property (property::list {property (true), property (std::string ("text"))})},
      {"dict", property (property::dictionary {{"k", property (std::int64_t (-2))}})},
      {"rich", property (athena::node::rich_text {tree ("rich text")})},
      {std::string ("k\n", 2), property (std::string ("a\r\x01", 3))}
    };
    athena::node::set (source, meta);
    const auto xml= write_xml_v3 (source);
    QVERIFY (xml.find ("<x><p") != std::string::npos);
    QVERIFY (xml.find ("t=\"h\"") != std::string::npos);
    QVERIFY (xml.find ("t=\"d\"") != std::string::npos);
    QVERIFY (xml.find ("ne=\"b\"") != std::string::npos);
    QVERIFY (xml.find ("<b i=") != std::string::npos);
    QVERIFY (xml.find ("<v e=\"b\">AP8N</v>") != std::string::npos);
    const auto restored= read_xml_v3 (xml);
    QVERIFY (restored == source);
    QVERIFY (athena::node::equal_metadata (restored, source));
    QVERIFY (athena::node::equal_metadata (restored[1][0], source[1][0]));
    QCOMPARE (write_xml_v3 (restored), xml);
  }

  void v3ReaderIsStrictNotLegacy () {
    const std::string prefix= "<a v='3' k='t'>";
    const std::string suffix= "</a>";
    for (const std::string body: {
      "<text><value>x</value></text>",
      "<node tag='concat'/>",
      "<t>x</t>", "<t/>", "<t><v>x</v><v>y</v></t>",
      "<n t='document' id='bad'/>",
      "<n t='scripts'><t><v>x</v></t></n>",
      "<n t='scripts' p='a'><t><v>x</v></t></n>",
      "<n t='math-symbol'><t><v>texmacs:mathd</v></t></n>",
      "<n t='math-variant'><t><v>unknown</v></t><t><v>R</v></t></n>",
      "<n t='math-accent'><t><v>̂</v></t><t><v>o</v></t><t><v>x</v></t></n>",
      "<n t='math-block' b='0'><n t='document'><t><v>x</v></t></n></n>",
      "<n t='math-grid' k='eqnarray'><n t='row'><t><v>x</v></t></n></n>",
      "<o k='∑'/>", "<o k='∑' u='true'/>",
      "<d l='(' r=')' s='a'><t><v>x</v></t><t><v>y</v></t></d>",
      "<d l='(' r=')' s='m'><t><v>x</v></t></d>",
      "<d l='(' r=')' s='f' ls='1' rs='1'><t><v>x</v></t></d>",
      "<d l='(' r=')' s='m' ls='01' rs='2'><t><v>x</v></t></d>",
      "<d l='xx' r=')' s='a'><t><v>x</v></t></d>",
      "<b><v e='b'>AP8=</v></b>",
      "<t><v e='b'>@@@</v></t>",
      "<t><x/><v>text</v></t>",
      "<t><x><p n='x' t='bool'>true</p></x><v>text</v></t>",
      "<t><x><p n='x' t='r' i='bad'/></x><v>text</v></t>"
    }) QVERIFY_EXCEPTION_THROWN (
      read_xml_v3 (prefix + body + suffix, xml_kind::fragment), codec_exception);
    const auto older= write_xml_v2 (tree ("x"), xml_kind::fragment);
    QVERIFY_EXCEPTION_THROWN (read_xml_v3 (older, xml_kind::fragment), codec_exception);
    QVERIFY_EXCEPTION_THROWN (read_xml_v3 ("<a v='2' k='t'><t><v>x</v></t></a>",
                                       xml_kind::fragment), codec_exception);
    QVERIFY_EXCEPTION_THROWN (read_xml_v3 (
      "<!DOCTYPE a [<!ENTITY v 'x'>]><a v='3' k='t'><t><v>&v;</v></t></a>",
      xml_kind::fragment), codec_exception);
  }

  void rejectDuplicateIdsAndLimits () {
    const auto id= athena::node::new_id ();
    const std::string xml= "<a v='3' k='d'><n t='document'>"
      "<t i='" + id + "'><v>x</v></t><t i='" + id + "'><v>y</v></t>"
      "</n></a>";
    QVERIFY_EXCEPTION_THROWN (read_xml_v3 (xml), identity_conflict);
    tree root (DOCUMENT, tree ("a"));
    codec_limits limits;
    limits.nodes= 1;
    QVERIFY_EXCEPTION_THROWN (write_xml_v3 (root, xml_kind::document, limits), codec_exception);
    QVERIFY_EXCEPTION_THROWN (read_xml_v3 (write_xml_v3 (root),
                                          xml_kind::document, limits), codec_exception);
  }

  void migratePairedMathAndDisplays () {
    tree inline_math= compound ("math", tree (CONCAT,
      tree (BIG, "∑"), tree (RSUB, "i=1"), tree (RSUP, "n"),
      tree (VAR_AROUND, tree ("<langle>"), tree ("x+y"), tree ("<rangle>"))));
    tree equation= compound ("equation*", tree (DOCUMENT,
      tree (CONCAT, tree ("x"), tree (RSUB, "i"), tree ("+y"))));
    tree grid= tree (TFORMAT, tree (TABLE,
      tree (ROW, tree (CELL, "a"), tree (CELL, "="), tree (CELL, "b"))));
    tree eqnarray= compound ("eqnarray", tree (DOCUMENT, grid));
    tree source (DOCUMENT, compound ("body", tree (DOCUMENT,
      inline_math, equation, eqnarray)));
    // Exercise the real v2 decoder, then convert only in an explicit detached
    // operation. The v3 codec must never be asked to recognize v2 spellings.
    const tree old= read_xml_v2 (write_xml_v2 (source));
    auto converted= migrate_math_v2_tree (old, true);
    QVERIFY (converted.warnings.empty ());
    QCOMPARE (converted.delimiters, std::size_t (1));
    QCOMPARE (converted.operators, std::size_t (1));
    QCOMPARE (converted.math_blocks, std::size_t (2));
    QCOMPARE (converted.grids, std::size_t (1));
    const tree& body= converted.document[0][0];
    QVERIFY (is_func (body[0][0][0], MATH_SCRIPTS, 5));
    QVERIFY (is_func (body[0][0][1], DELIMITED, 6));
    QVERIFY (is_func (body[1], MATH_BLOCK, 1));
    QVERIFY (is_func (body[2][0], MATH_GRID));
    const auto* eqnarray_meta= athena::node::get (body[2]);
    QVERIFY (eqnarray_meta != nullptr);
    QCOMPARE (std::get<bool> (
      eqnarray_meta->properties.at ("math:block-numbered").data), false);
    const auto xml= write_xml_v3 (converted.document);
    QVERIFY (xml.find ("<d l=\"⟨\" r=\"⟩\" s=\"a\">") != std::string::npos);
    QVERIFY (xml.find ("<n t=\"math-block\" b=\"0\"") != std::string::npos);
    QVERIFY (xml.find ("<n t=\"math-grid\" k=\"eqnarray\"") != std::string::npos);
    QVERIFY (read_xml_v3 (xml) == converted.document);
  }

  void namedDelimiterEndpointsAreNotInvisible () {
    const auto endpoint= [] (const char* symbol) {
      return tree (CONCAT, tree (NAMED_SYMBOL, tree (symbol)));
    };
    const tree left= endpoint ("texmacs:lvert");
    const tree right= endpoint ("texmacs:rvert");
    const tree absent= endpoint ("texmacs:nobracket");
    tree source (DOCUMENT, compound ("math", tree (CONCAT,
      tree (VAR_AROUND, left, tree ("X"), right),
      tree (VAR_AROUND, absent, tree ("f"), right),
      tree (VAR_AROUND, left, tree ("g"), absent))));
    const auto converted= migrate_math_v2_tree (
      read_xml_v2 (write_xml_v2 (source)), true);
    QVERIFY (converted.warnings.empty ());
    QCOMPARE (converted.delimiters, std::size_t (3));
    const tree& math= converted.document[0][0];
    QVERIFY (is_func (math, CONCAT, 3));
    for (int i=0; i<3; ++i)
      QVERIFY (is_func (math[i], DELIMITED, 6));
    QCOMPARE (math[0][0], tree ("|"));
    QCOMPARE (math[0][1], tree ("|"));
    QCOMPARE (math[1][0], tree (""));
    QCOMPARE (math[1][1], tree ("|"));
    QCOMPARE (math[2][0], tree ("|"));
    QCOMPARE (math[2][1], tree (""));
    const auto xml= write_xml_v3 (converted.document);
    QVERIFY (xml.find ("<d l=\"|\" r=\"|\" s=\"a\">") != std::string::npos);
    QVERIFY (read_xml_v3 (xml) == converted.document);
  }

  void genuinelyInvisibleDelimiterKeepsOwnedIdentity () {
    tree body (CONCAT, tree ("a"), tree ("+b"));
    athena::node::metadata body_metadata;
    body_metadata.id= athena::node::new_id ();
    athena::node::set (body, body_metadata);
    tree invisible (VAR_AROUND, tree ("."), body, tree ("."));
    athena::node::metadata wrapper_metadata;
    wrapper_metadata.id= athena::node::new_id ();
    athena::node::set (invisible, wrapper_metadata);
    tree unannotated (VAR_AROUND, tree ("."), tree ("x"), tree ("."));
    const tree source (DOCUMENT, compound ("math", tree (CONCAT,
      invisible, unannotated)));
    const auto result= migrate_math_v2_tree (source, true);
    QVERIFY (result.warnings.empty ());
    const tree& math= result.document[0][0];
    QVERIFY (is_func (math, CONCAT, 2));
    QVERIFY (is_func (math[0], CONCAT, 1));
    QCOMPARE (athena::node::id (math[0]), wrapper_metadata.id);
    QCOMPARE (athena::node::id (math[0][0]), body_metadata.id);
    QCOMPARE (math[1], tree ("x"));
    QVERIFY (result.removed_identities.empty ());
    const auto xml= write_xml_v3 (result.document);
    const tree reread= read_xml_v3 (xml);
    QVERIFY (reread == result.document);
    QVERIFY (athena::node::equal_metadata (reread, result.document));
  }

  void bareMathematicalTableBodyGetsCanonicalCellWithoutWarning () {
    tree body (CONCAT, tree ("a"), tree ("+b"));
    athena::node::metadata metadata;
    metadata.id= athena::node::new_id ();
    athena::node::set (body, metadata);
    const tree table (TFORMAT, tree (TABLE,
      tree (ROW, tree (CELL, "lhs"), tree (CELL, "="), body)));
    const tree source (DOCUMENT,
      compound ("eqnarray*", tree (DOCUMENT, table)));
    const auto migrated= migrate_math_v2_tree (source, true);
    QVERIFY (migrated.warnings.empty ());
    const tree& row= migrated.document[0][0][0];
    QVERIFY (is_func (row, ROW, 3));
    QVERIFY (is_func (row[2], CELL, 1));
    QCOMPARE (athena::node::id (row[2][0]), metadata.id);
    const auto xml= write_xml_v3 (migrated.document);
    QVERIFY (read_xml_v3 (xml) == migrated.document);
  }

  void nestedMathContinuesScriptBaseWithoutDroppingIdentity () {
    // The TeXmacs v2 writer can nest redundant MATH/CONCAT wrappers in the
    // same formula: GL + math(_2 (R)), or Cl + math(^x _{r,s}). A local
    // sequence must not mistake those wrapper boundaries for an empty base.
    tree gl ("GL");
    athena::node::metadata base_metadata;
    base_metadata.id= athena::node::new_id ();
    athena::node::set (gl, base_metadata);
    const tree dimension (CONCAT, tree (RSUB, "2"),
      tree (VAR_AROUND, tree ("("), tree ("ℝ"), tree (")")));
    const tree formula= compound ("math", tree (CONCAT, gl,
      compound ("math", dimension)));
    const tree second= compound ("math", tree (CONCAT, tree ("Cl"),
      compound ("math", tree (CONCAT, tree (RSUP, "×"),
        tree (RSUB, "r,s")))));
    tree source (DOCUMENT, formula, second);
    auto result= migrate_math_v2_tree (source, true);
    QVERIFY (result.warnings.empty ());
    const tree& first= result.document[0][0];
    QVERIFY (is_func (first, CONCAT));
    QVERIFY (is_func (first[0], MATH_SCRIPTS, 5));
    QVERIFY (first[0][0] == gl);
    QCOMPARE (athena::node::id (first[0][0]), base_metadata.id);
    QCOMPARE (first[0][3], tree ("2"));
    QVERIFY (is_func (first[1], DELIMITED, 6));
    const tree& other= result.document[1][0];
    QVERIFY (is_func (other, CONCAT));
    QVERIFY (is_func (other[0], MATH_SCRIPTS, 5));
    QVERIFY (other[0][0] == "Cl");
    QCOMPARE (tree_as_string (other[0][3]), string ("r,s"));
    QCOMPARE (other[0][4], tree ("×"));
    const auto xml= write_xml_v3 (result.document);
    QVERIFY (read_xml_v3 (xml) == result.document);
    QVERIFY (athena::node::equal_metadata (read_xml_v3 (xml), result.document));
  }

  void paragraphTextMustNotBeCapturedAsMathScriptBase () {
    const tree source (DOCUMENT, tree (CONCAT,
      tree ("It follows that "),
      compound ("math", tree (CONCAT, tree (RSUB, "p"), tree ("π")))));
    const auto converted= migrate_math_v2_tree (source, true);
    QVERIFY (converted.warnings.empty ());
    const tree& paragraph= converted.document[0];
    QVERIFY (paragraph[0] == "It follows that ");
    QVERIFY (is_func (paragraph[1], make_tree_label ("math"), 1));
    QVERIFY (is_func (paragraph[1][0], CONCAT));
    QVERIFY (is_func (paragraph[1][0][0], MATH_SCRIPTS, 5));
    QCOMPARE (paragraph[1][0][0][0], tree ("π"));
    QCOMPARE (paragraph[1][0][0][1], tree ("p"));
    QCOMPARE (paragraph[1][0][0][3], tree (""));
  }

  void warningsAreFatalInStrictMigration () {
    tree source (DOCUMENT, compound ("math", tree (CONCAT,
      tree (LSUB, "i"), tree (LSUB, "j"), tree ("x"))));
    auto lenient= migrate_math_v2_tree (source);
    QVERIFY (!lenient.warnings.empty ());
    QCOMPARE (lenient.warnings[0].code, std::string ("DUPLICATE_LEFT_SCRIPT"));
    QVERIFY_EXCEPTION_THROWN (migrate_math_v2_tree (source, true), std::runtime_error);
  }

  void leadingScriptsUseTheLeftSlotsOnMathIdentifiers () {
    // Explicit prescripts were historically stored as leading RSUP/RSUB.
    // They are not missing a base: isotope mass and algebraic left indices
    // attach to the following identifier, with later right scripts unchanged.
    const tree isotope= compound ("math", tree (CONCAT,
      tree (RSUP, "14"), compound ("text", "CO"), tree (RSUB, "2")));
    const tree double_index= compound ("math", tree (CONCAT,
      tree (RSUB, "p"), tree (RSUP, "q"), tree ("π"),
      tree (RSUB, "i"), tree (RSUP, "s")));
    const tree lhs= compound ("math", tree (CONCAT,
      tree (RSUP, "σ"), tree ("f=-f")));
    const tree document (DOCUMENT, isotope, double_index, lhs);
    const auto migrated= migrate_math_v2_tree (document, true);
    QVERIFY (migrated.warnings.empty ());
    const tree& isotope_scripts= migrated.document[0][0][0];
    QVERIFY (is_func (isotope_scripts, MATH_SCRIPTS, 5));
    QVERIFY (is_compound (isotope_scripts[0], "text", 1));
    QCOMPARE (isotope_scripts[1], tree (""));
    QCOMPARE (isotope_scripts[2], tree ("14"));
    QCOMPARE (isotope_scripts[3], tree ("2"));
    const tree& pi= migrated.document[1][0][0];
    QVERIFY (is_func (pi, MATH_SCRIPTS, 5));
    QCOMPARE (pi[0], tree ("π"));
    QCOMPARE (pi[1], tree ("p"));
    QCOMPARE (pi[2], tree ("q"));
    QCOMPARE (pi[3], tree ("i"));
    QCOMPARE (pi[4], tree ("s"));
    const tree& formula= migrated.document[2][0];
    QVERIFY (is_func (formula, CONCAT));
    QVERIFY (is_func (formula[0], MATH_SCRIPTS, 5));
    QCOMPARE (formula[0][0], tree ("f"));
    QCOMPARE (formula[0][2], tree ("σ"));
    QVERIFY (N (formula) > 1); // An equality is not the base of ^{σ}.
    const auto xml= write_xml_v3 (migrated.document);
    QVERIFY (read_xml_v3 (xml) == migrated.document);
  }

  void standaloneMathModifiersKeepTheirEmptyBase () {
    tree document (DOCUMENT,
      compound ("math", tree (RSUP, "op")),
      compound ("math", tree (RSUP, "-1")),
      compound ("math", tree (CONCAT,
        tree (RSUP, "-1"), tree (":G⟶G"))),
      compound ("math", tree (RSUB, "i")));
    const auto migrated= migrate_math_v2_tree (document, true);
    QVERIFY (migrated.warnings.empty ());
    for (int i=0; i<4; ++i) {
      const tree& formula= migrated.document[i][0];
      const tree& scripts= i == 2 ? formula[0] : formula;
      QVERIFY (is_func (scripts, MATH_SCRIPTS, 5));
      QCOMPARE (scripts[0], tree (""));
    }
    QCOMPARE (tree_as_string (migrated.document[2][0][0][4]), string ("-1"));
    const auto xml= write_xml_v3 (migrated.document);
    QVERIFY (read_xml_v3 (xml) == migrated.document);
  }

  void evalBarAndPrimeDoNotInventBaselessScripts () {
    tree restriction (CONCAT, tree ("v"),
      tree (VAR_AROUND, tree ("|"), tree (RSUB, "Γ "),
        tree (CONCAT, tree (NAMED_SYMBOL, "texmacs:nobracket"))));
    const tree prime= compound ("math", tree (CONCAT,
      tree ("T∈"),
      tree (WITH, tree ("font"), tree ("cal"), tree ("D")),
      tree (RSUP, tree (RPRIME, "'")),
      tree (VAR_AROUND, tree ("("), tree ("ℝ"), tree (")"))));
    const tree placeholder= compound ("math", tree (ABOVE,
      tree ("⟶"), tree (RSUB, "")));
    const tree old (DOCUMENT, compound ("math", restriction), prime, placeholder);
    const auto migrated= migrate_math_v2_tree (old, true);
    QVERIFY (migrated.warnings.empty ());
    const tree& evaluation= migrated.document[0][0];
    QVERIFY (is_func (evaluation, CONCAT, 1));
    QVERIFY (is_func (evaluation[0], MATH_SCRIPTS, 5));
    QVERIFY (is_func (evaluation[0][0], DELIMITED, 6));
    QCOMPARE (evaluation[0][0][0], tree (""));
    QCOMPARE (evaluation[0][0][1], tree ("|"));
    QCOMPARE (evaluation[0][0][5], tree ("v"));
    QCOMPARE (tree_as_string (evaluation[0][3]), string ("Γ "));
    const tree& formula= migrated.document[1][0];
    QVERIFY (is_func (formula, CONCAT));
    tree derivative;
    for (int i=0; i<N (formula); ++i)
      if (is_func (formula[i], MATH_SCRIPTS, 5)) {
        derivative= formula[i];
        break;
      }
    QVERIFY (is_func (derivative, MATH_SCRIPTS, 5));
    QCOMPARE (derivative[4], tree ("′"));
    const tree& arrow= migrated.document[2][0];
    QVERIFY (is_func (arrow, MATH_ANNOTATION, 3));
    QCOMPARE (arrow[2], tree (""));
    const auto serialized= write_xml_v3 (migrated.document);
    QVERIFY (read_xml_v3 (serialized) == migrated.document);
  }

  void genuinelyBaselessDelimitedSuperscriptStillWarns () {
    // The source cannot tell whether the exponent belongs to |x|, to a
    // missing term inside absolute-value bars, or to some other expression.
    // Preserve the authored nodes exactly; do not silently reassign ^2 to x.
    const tree ambiguous (DOCUMENT,
      compound ("math", tree (CONCAT, tree ("x"),
        tree (VAR_AROUND, tree ("|"), tree (RSUP, "2"), tree ("|")))));
    const auto migrated= migrate_math_v2_tree (ambiguous);
    QCOMPARE (migrated.warnings.size (), std::size_t (1));
    QCOMPARE (migrated.warnings[0].code,
              std::string ("SCRIPT_WITHOUT_SEQUENCE_BASE"));
    const tree& content= migrated.document[0][0][1];
    QVERIFY (is_func (content, DELIMITED, 6));
    QVERIFY (is_func (content[5], MATH_SCRIPTS, 5));
    QCOMPARE (content[5][0], tree (""));
    QCOMPARE (content[5][4], tree ("2"));
    QVERIFY (read_xml_v3 (write_xml_v3 (migrated.document)) == migrated.document);
    QVERIFY_THROWS_EXCEPTION (std::runtime_error,
      migrate_math_v2_tree (ambiguous, true));
  }

  void registeredMathSymbolIdentitiesHaveOneCanonicalSpelling () {
    tree old (DOCUMENT, compound ("math", tree (CONCAT,
      tree (NAMED_SYMBOL, tree ("texmacs:oplus")),
      tree (NAMED_SYMBOL, tree ("texmacs:mathd")))));
    const auto migrated= migrate_math_v2_tree (old, true);
    const tree& symbols= migrated.document[0][0];
    QVERIFY (is_func (symbols, CONCAT, 2));
    QVERIFY (is_func (symbols[0], MATH_SYMBOL, 1));
    QVERIFY (is_func (symbols[1], MATH_SYMBOL, 1));
    QCOMPARE (symbols[0][0], tree ("oplus"));
    QCOMPARE (symbols[1][0], tree ("differential-d"));
    const auto xml= write_xml_v3 (migrated.document);
    QVERIFY (read_xml_v3 (xml) == migrated.document);
    QVERIFY_EXCEPTION_THROWN (write_xml_v3 (
      tree (MATH_SYMBOL, tree ("mathd")), xml_kind::fragment), codec_exception);
    QVERIFY_EXCEPTION_THROWN (write_xml_v3 (
      tree (MATH_SYMBOL, tree ("unregistered")), xml_kind::fragment), codec_exception);
  }

  void migrateLegacyMiddleDelimiters () {
    tree old (DOCUMENT, compound ("math", tree (CONCAT,
      tree (VAR_AROUND, tree ("{"),
        tree (CONCAT, tree ("x"), tree (MID, "|"), tree ("P(x)")),
        tree ("}")),
      tree ("f"), tree (MID, "|"), tree ("g"))));
    auto converted= migrate_math_v2_tree (old, true);
    QVERIFY (converted.warnings.empty ());
    const tree& expr= converted.document[0][0];
    QVERIFY (is_func (expr[0], DELIMITED, 6));
    QVERIFY (is_func (expr[0][5][1], MATH_MIDDLE, 1));
    QCOMPARE (expr[0][5][1][0], tree ("|"));
    QVERIFY (is_func (expr[2], MATH_MIDDLE, 1));
    QCOMPARE (expr[2][0], tree ("|"));
    const auto xml= write_xml_v3 (converted.document);
    QVERIFY (xml.find ("<m k=\"|\"/>") != std::string::npos);
    QVERIFY (read_xml_v3 (xml) == converted.document);
  }

  void migrateSlashAndSetDifferenceAsMathTokens () {
    // A/B is a linear mathematical expression, not a delimiter expression.
    tree input (DOCUMENT, compound ("math", tree (CONCAT,
      tree ("A"), tree (MID, "/"), tree ("B"),
      tree (MID, "backslash"), tree ("C"),
      tree (MID, "|"), tree ("D"))));
    athena::node::metadata slash_meta;
    slash_meta.id= athena::node::new_id ();
    athena::node::set (input[0][0][1], slash_meta);
    const auto result= migrate_math_v2_tree (input, true);
    QVERIFY (result.warnings.empty ());
    const tree& tokens= result.document[0][0];
    QVERIFY (is_func (tokens, CONCAT, 7));
    QVERIFY (is_atomic (tokens[1]));
    QCOMPARE (tokens[1]->label, string ("/"));
    QCOMPARE (athena::node::id (tokens[1]), slash_meta.id);
    QCOMPARE (tokens[3], tree ("\\"));
    QVERIFY (is_func (tokens[5], MATH_MIDDLE, 1));
    QCOMPARE (tokens[5][0], tree ("|"));
    const auto xml= write_xml_v3 (result.document);
    QVERIFY (xml.find ("<v>/</v>") != std::string::npos);
    QVERIFY (xml.find ("<m k=\"|\"/>") != std::string::npos);
    QVERIFY (read_xml_v3 (xml) == result.document);
  }

  void migrationPreservesDeclaredDataArgumentsInsideMath () {
    const tree expression (CONCAT, "x", tree (RSUP, "2"));
    tree offset ("-0.5*p*t");
    athena::node::metadata metadata;
    metadata.properties["test:layout"]= athena::node::property (std::string ("unchanged"));
    athena::node::set (offset, metadata);
    const tree options ("5*p*x,b*o*r*d*e*r:2*p*x*s*o*l*i*d*b*l*a*c*k");
    const tree source (DOCUMENT, compound ("math", tree (CONCAT,
      compound ("raise", offset, expression),
      compound ("kern", offset),
      compound ("bbox*", options, expression))));
    const auto migrated= migrate_math_v2_tree (source, true);
    const tree& result= migrated.document[0][0];
    QVERIFY (is_func (result[0], MOVE, 3));
    QCOMPARE (result[0][2]->label, string ("-0.5pt"));
    QVERIFY (athena::node::equal_metadata (result[0][2], offset));
    QVERIFY (is_func (result[1], SPACE, 1));
    QCOMPARE (result[1][0]->label, string ("-0.5pt"));
    QVERIFY (athena::node::equal_metadata (result[1][0], offset));
    QVERIFY (is_func (result[2], WITH));
    QCOMPARE (result[2][3], tree ("2px"));
    QCOMPARE (result[2][5], tree ("5px"));
    QVERIFY (is_func (result[0][0][0], MATH_SCRIPTS, 5));
    QVERIFY (is_func (result[2][N(result[2])-1][0][0], MATH_SCRIPTS, 5));
    validate_cold_v3_migration (migrated.document);
    QVERIFY (migrated.warnings.empty ());
  }

  void migrationRepairsConfirmedLegacySymbolsAndLayout () {
    tree integral= compound ("fint");
    athena::node::metadata meta;
    meta.id= athena::node::new_id ();
    athena::node::set (integral, meta);
    const tree source (DOCUMENT, compound ("math", tree (CONCAT,
      integral, tree (RSUB, "B"), compound ("ohm"),
      compound ("mathllap", tree (BIG, "iint")),
      compound ("Big", "<"), "x", compound ("Big", ">"), tree (RSUB, "G"))));
    const auto migrated= migrate_math_v2_tree (source, true);
    const tree& body= migrated.document[0][0];
    QVERIFY (is_func (body[0], MATH_SCRIPTS, 5));
    QVERIFY (is_func (body[0][0], BIG_OPERATOR, 2));
    QCOMPARE (body[0][0][0], tree ("⨍"));
    QCOMPARE (athena::node::id (body[0][0]), meta.id);
    QCOMPARE (body[0][3], tree ("B"));
    QCOMPARE (body[1], tree ("Ω"));
    QVERIFY (is_func (body[2], RESIZE, 5));
    QCOMPARE (body[2][1], tree ("1r"));
    QCOMPARE (body[2][3], tree ("1r"));
    QVERIFY (is_func (body[3], MATH_SCRIPTS, 5));
    QCOMPARE (body[3][0][0], tree ("⟨"));
    QCOMPARE (body[3][0][1], tree ("⟩"));
    QCOMPARE (body[3][0][2], tree ("m"));
    QCOMPARE (body[3][0][3], tree ("2"));
    QCOMPARE (body[3][3], tree ("G"));
    validate_cold_v3_migration (migrated.document);
    QCOMPARE (read_xml_v3 (write_xml_v3 (migrated.document)), migrated.document);
    QVERIFY (migrated.removed_identities.empty ());
  }

  void migrationRejectsAmbiguousLegacyLengths () {
    for (const char* length: {"1*2pt", "2*3*p*t", "1pt+2pt", "arbitrary"}) {
      const tree source (DOCUMENT, compound ("math", compound ("kern", length)));
      QVERIFY_THROWS_EXCEPTION (std::runtime_error, migrate_math_v2_tree (source, true));
    }
    const tree source (DOCUMENT, compound ("math", compound ("raise", ".*4*p*t", "x")));
    const auto result= migrate_math_v2_tree (source, true);
    QCOMPARE (result.document[0][0][2], tree (".4pt"));
  }

  void migrationRepairsOnlyCompleteFragmentedCharacterEscapes () {
    const tree source (DOCUMENT, tree (CONCAT,
      "<", compound ("4"), "EC0><", compound ("4"), "E48><",
      compound ("'6"), "5*F*6>候<#FF1F>"));
    const auto result= migrate_math_v2_tree (source, true);
    QCOMPARE (result.document, tree (DOCUMENT, tree (CONCAT, "什么时候？")));
    validate_cold_v3_migration (result.document);
    const tree literal (DOCUMENT, "literal <#4EC0> is not a fragmented escape");
    QCOMPARE (migrate_math_v2_tree (literal, true).document, literal);
    for (const char* rest: {"ZZ>", "EC0", "000000>", "0>"}) {
      const tree invalid (DOCUMENT, tree (CONCAT, "<", compound ("4"), rest));
      QVERIFY_THROWS_EXCEPTION (std::runtime_error, migrate_math_v2_tree (invalid, true));
    }
    tree annotated= copy (source);
    athena::node::metadata meta;
    meta.id= athena::node::new_id ();
    athena::node::set (annotated[0][1], meta);
    QVERIFY_THROWS_EXCEPTION (std::runtime_error, migrate_math_v2_tree (annotated, true));
  }

  void migrationPreservesHistoricalQuoteContentAndLink () {
    const tree body (DOCUMENT, "retained body");
    const tree source (DOCUMENT, compound (">", "source", body, "title", "https://example.org/"));
    const auto migrated= migrate_math_v2_tree (source, true);
    const tree& quote= migrated.document[0];
    QVERIFY (is_compound (quote, "quote-env", 1));
    QCOMPARE (quote[0][0], tree ("source"));
    QCOMPARE (quote[0][1], body);
    QCOMPARE (quote[0][2], tree (HLINK, "title", "https://example.org/"));
    validate_cold_v3_migration (migrated.document);
  }

  void migrationCannotWarmItsOwnColdDeclarations () {
    const auto source= read_xml_v2 (
      "<athena-document version=\"2\" text-model=\"utf-8\">"
      "<node tag=\"document\"><node tag=\"test-v3-legacy-interned-label\"/>"
      "</node></athena-document>");
    QVERIFY (existing_tree_label ("test-v3-legacy-interned-label"));
    const auto migrated= migrate_math_v2_tree (source, true);
    // Same-process round trips cannot prove that a fresh process can read it.
    QCOMPARE (read_xml_v3 (write_xml_v3 (migrated.document)), migrated.document);
    QVERIFY_THROWS_EXCEPTION (std::runtime_error,
      validate_cold_v3_migration (migrated.document));
    tree owner (DOCUMENT, "content");
    athena::node::metadata metadata;
    metadata.properties["test:nested"]= athena::node::property (
      athena::node::property::list {athena::node::property (
        athena::node::rich_text {migrated.document})});
    athena::node::set (owner[0], metadata);
    QVERIFY_THROWS_EXCEPTION (std::runtime_error, validate_cold_v3_migration (owner));
    validate_cold_v3_migration (tree (DOCUMENT,
      compound ("section", "cold"), compound ("description", "source")));
  }

  void migrateNestedHistoricalLeftRightPairs () {
    tree sequence (CONCAT);
    sequence << tree (LEFT, "|") << tree (LEFT, "langle") << tree ("x")
             << tree (MID, "|") << tree ("P(x)")
             << tree (RIGHT, "rangle") << tree (RIGHT, "|")
             << tree (RSUB, "0") << tree (RSUP, "1");
    const tree source (DOCUMENT, compound ("math", sequence));
    const auto converted= migrate_math_v2_tree (source, true);
    QVERIFY (converted.warnings.empty ());
    QCOMPARE (converted.delimiters, std::size_t (2));
    const tree& scripted= converted.document[0][0][0];
    QVERIFY (is_func (scripted, MATH_SCRIPTS, 5));
    QVERIFY (is_func (scripted[0], DELIMITED, 6));
    QCOMPARE (scripted[0][0], tree ("|"));
    QCOMPARE (scripted[0][1], tree ("|"));
    const tree& inner= scripted[0][5][0];
    QVERIFY (is_func (inner, DELIMITED, 6));
    QCOMPARE (inner[0], tree ("⟨"));
    QCOMPARE (inner[1], tree ("⟩"));
    QVERIFY (is_func (inner[5][1], MATH_MIDDLE, 1));
    const auto xml= write_xml_v3 (converted.document);
    QVERIFY (read_xml_v3 (xml) == converted.document);
  }

  void migrateUnregisteredHistoricalPunctuationAsUnicode () {
    const tree source (DOCUMENT, compound ("math", tree (CONCAT,
      tree (NAMED_SYMBOL, "texmacs:comma"),
      tree (NAMED_SYMBOL, "texmacs:textdegree"))));
    const auto migrated= migrate_math_v2_tree (source, true);
    QVERIFY (migrated.warnings.empty ());
    const tree& math= migrated.document[0][0];
    QCOMPARE (math[0], tree (","));
    QCOMPARE (math[1], tree ("°"));
    const auto xml= write_xml_v3 (migrated.document);
    QVERIFY (read_xml_v3 (xml) == migrated.document);
  }

  void migrateNamedMathematicalAccents () {
    tree old (DOCUMENT, compound ("math", tree (CONCAT,
      tree (WIDE, tree ("v"),
        tree (CONCAT, tree (NAMED_SYMBOL, tree ("texmacs:bar")))),
      tree (WIDE, tree ("X"),
        tree (CONCAT, tree (NAMED_SYMBOL, tree ("texmacs:abovering")))),
      tree (VAR_WIDE, tree ("lim"),
        tree (CONCAT, tree (NAMED_SYMBOL,
          tree ("texmacs:wide-varleftarrow")))))));
    const auto migrated= migrate_math_v2_tree (old, true);
    QCOMPARE (migrated.warnings.size (), std::size_t (0));
    const tree& expr= migrated.document[0][0];
    for (int j=0; j<3; ++j)
      QVERIFY (is_func (expr[j], MATH_ACCENT, 3));
    QCOMPARE (expr[0][0], tree ("̅"));
    QCOMPARE (expr[1][0], tree ("̊"));
    QCOMPARE (expr[2][0], tree ("⃖"));
    const auto xml= write_xml_v3 (migrated.document);
    QVERIFY (read_xml_v3 (xml) == migrated.document);
  }

  void migrateLegacyBindAccentToUnicodeOverTie () {
    tree source (DOCUMENT, compound ("math", tree (WIDE,
      tree ("xy"), tree (CONCAT,
        tree (NAMED_SYMBOL, "texmacs:bind")))));
    const auto converted= migrate_math_v2_tree (source, true);
    const tree& accent= converted.document[0][0];
    QVERIFY (is_func (accent, MATH_ACCENT, 3));
    QCOMPARE (accent[0], tree ("͡"));
    QCOMPARE (accent[1], tree ("o"));
    const auto xml= write_xml_v3 (converted.document);
    QVERIFY (read_xml_v3 (xml) == converted.document);
  }

  void nativeLongArrowPreservesStructuredSymbolAndLabels () {
    const tree older (DOCUMENT, compound ("math", tree (LONG_ARROW,
      tree (NAMED_SYMBOL, tree ("texmacs:longhookrightarrow")),
      tree ("f"), tree ("x"))));
    const auto migrated= migrate_math_v2_tree (
      read_xml_v2 (write_xml_v2 (older)), true);
    const tree& arrow= migrated.document[0][0];
    QVERIFY (is_func (arrow, MATH_LONG_ARROW, 3));
    QVERIFY (is_func (arrow[0], MATH_SYMBOL, 1));
    QCOMPARE (arrow[0][0], tree ("long-hookrightarrow"));
    QCOMPARE (arrow[1], tree ("x"));
    QCOMPARE (arrow[2], tree ("f"));
    const auto xml= write_xml_v3 (migrated.document);
    QVERIFY (read_xml_v3 (xml) == migrated.document);
  }

  void migrateHistoricalRubberArrowAsNativeUnicodeGlyph () {
    for (const auto& pair: std::vector<std::pair<string,string>> {
           {"texmacs:rubber-equal", "="},
           {"texmacs:rubber-equiv", "≡"},
           {"texmacs:rubber-rightarrow", "→"},
           {"texmacs:rubber-leftarrow", "←"}}) {
      const tree old (DOCUMENT, compound ("math", tree (LONG_ARROW,
        tree (CONCAT, tree (NAMED_SYMBOL, tree (pair.first))),
        tree ("f"), tree ("x"))));
      const auto migrated= migrate_math_v2_tree (old, true);
      const tree& arrow= migrated.document[0][0];
      QVERIFY (is_func (arrow, MATH_LONG_ARROW, 3));
      QCOMPARE (arrow[0], tree (pair.second));
      const auto xml= write_xml_v3 (migrated.document);
      QVERIFY (read_xml_v3 (xml) == migrated.document);
    }
  }

  void nativeGridPreservesColumnAlignmentPolicy () {
    using athena::math_v3::grid_column_alignment;
    QCOMPARE (grid_column_alignment ("eqnarray", 0), string ("r"));
    QCOMPARE (grid_column_alignment ("eqnarray", 1), string ("c"));
    QCOMPARE (grid_column_alignment ("eqnarray", 2), string ("l"));
    QCOMPARE (grid_column_alignment ("eqnarray", 3), string ("r"));
    QCOMPARE (grid_column_alignment ("align", 0), string ("r"));
    QCOMPARE (grid_column_alignment ("align", 1), string ("l"));
    QCOMPARE (grid_column_alignment ("matrix", 4), string ("c"));
    QCOMPARE (grid_column_alignment ("cases", 4), string ("l"));
    QCOMPARE (grid_column_alignment ("generic", 1), string (""));
    QCOMPARE (grid_column_alignment ("eqnarray", -1), string (""));
  }

  void historicalEqnarrayWithoutStarIsStillUnnumbered () {
    // env-math's actual v2 `eqnarray` definition calls `eqnarray*`. Unlike
    // LaTeX, the source name alone must not add a row or block number.
    tree table (TABLE,
      tree (ROW, tree (CELL, "x"), tree (CELL, "="), tree (CELL, "y")),
      tree (ROW, tree (CELL, "u"), tree (CELL, "="), tree (CELL, "v")));
    tree formula= compound ("eqnarray", tree (DOCUMENT,
      tree (TFORMAT, table)));
    const tree old (DOCUMENT, formula);
    const auto result= migrate_math_v2_tree (read_xml_v2 (write_xml_v2 (old)),
                                              true);
    QVERIFY (result.warnings.empty ());
    const tree& block= result.document[0];
    QVERIFY (is_func (block, MATH_BLOCK, 1));
    QVERIFY (is_func (block[0], MATH_GRID, 2));
    const auto* block_meta= athena::node::get (block);
    QVERIFY (block_meta != nullptr);
    QCOMPARE (std::get<bool> (
      block_meta->properties.at ("math:block-numbered").data), false);
    const auto* grid_meta= athena::node::get (block[0]);
    QCOMPARE (std::get<std::string> (
      grid_meta->properties.at ("math:grid-kind").data),
      std::string ("eqnarray"));
    const auto xml= write_xml_v3 (result.document);
    QVERIFY (xml.find ("<n t=\"math-block\" b=\"0\"") != std::string::npos);
    QVERIFY (read_xml_v3 (xml) == result.document);
  }

  void multilineMathBlockBecomesNativeStackGrid () {
    tree paragraphs (DOCUMENT,
      tree (CONCAT, tree ("a"), tree (RSUB, "i")),
      tree (CONCAT, tree ("b"), tree (RSUP, "2")));
    athena::node::metadata meta;
    meta.id= athena::node::new_id ();
    athena::node::set (paragraphs, meta);
    tree original (DOCUMENT, compound ("equation*", paragraphs));
    const auto converted= migrate_math_v2_tree (original, true);
    QVERIFY (converted.warnings.empty ());
    const tree& block= converted.document[0];
    QVERIFY (is_func (block, MATH_BLOCK, 1));
    QVERIFY (is_func (block[0], MATH_GRID, 2));
    QCOMPARE (athena::node::id (block[0]), meta.id);
    const auto* grid_meta= athena::node::get (block[0]);
    QCOMPARE (std::get<std::string> (
      grid_meta->properties.at ("math:grid-kind").data), std::string ("stack"));
    QVERIFY (is_func (block[0][0][0][0], CONCAT));
    QVERIFY (is_func (block[0][1][0][0], CONCAT));
    const auto xml= write_xml_v3 (converted.document);
    QVERIFY (read_xml_v3 (xml) == converted.document);
  }

  void typedGridCellFormattingPreservesLegacyOverrides () {
    tree left (ROW, tree (CELL, "a"), tree (CELL, "b"));
    tree right (ROW, tree (CELL, "c"), tree (CELL, "d"));
    tree table (TABLE, left, right);
    tree old (DOCUMENT, compound ("eqnarray*", tree (DOCUMENT,
      tree (TFORMAT,
        tree (CWITH, "2", "2", "1", "-1", "cell-bborder", "1ln"),
        tree (CWITH, "1", "-1", "2", "2", "color", "#0055ff"),
        tree (CWITH, "1", "1", "1", "1", "cell-lborder", "1ln"),
        tree (CWITH, "1", "1", "1", "1", "cell-rborder", "2ln"),
        tree (CWITH, "1", "1", "1", "1", "cell-background", "pastel blue"),
        table))));
    const auto converted= migrate_math_v2_tree (old, true);
    QVERIFY (converted.warnings.empty ());
    const tree& grid= converted.document[0][0];
    QVERIFY (is_func (grid, MATH_GRID, 2));
    const auto& meta= *athena::node::get (grid[1][1]);
    const auto format= std::get<athena::node::property::dictionary> (
      meta.properties.at ("math:cell-format").data);
    QCOMPARE (std::get<std::string> (format.at ("border-bottom").data),
              std::string ("1ln"));
    QCOMPARE (std::get<std::string> (format.at ("color").data),
              std::string ("#0055ff"));
    const auto& first_row= *athena::node::get (grid[0][1]);
    const auto top= std::get<athena::node::property::dictionary> (
      first_row.properties.at ("math:cell-format").data);
    QVERIFY (top.count ("border-bottom") == 0);
    QVERIFY (top.count ("color") == 1);
    const auto* special_meta= athena::node::get (grid[0][0]);
    QVERIFY (special_meta != nullptr);
    const auto& special= std::get<athena::node::property::dictionary> (
      special_meta->properties.at ("math:cell-format").data);
    QCOMPARE (std::get<std::string> (special.at ("border-left").data),
              std::string ("1ln"));
    QCOMPARE (std::get<std::string> (special.at ("border-right").data),
              std::string ("2ln"));
    QCOMPARE (std::get<std::string> (special.at ("background").data),
              std::string ("pastel blue"));
    const auto xml= write_xml_v3 (converted.document);
    QVERIFY (read_xml_v3 (xml) == converted.document);
  }

  void explicitCellAlignmentOverridesGridDefault () {
    tree layout (TFORMAT,
      tree (CWITH, "1", "1", "2", "2", "cell-halign", "r"),
      tree (TABLE, tree (ROW, tree (CELL, "a"), tree (CELL, "b"))));
    tree source (DOCUMENT, compound ("eqnarray*", tree (DOCUMENT, layout)));
    const auto converted= migrate_math_v2_tree (source, true);
    const tree& grid= converted.document[0][0];
    QVERIFY (is_func (grid, MATH_GRID, 1));
    const auto* meta= athena::node::get (grid[0][1]);
    QVERIFY (meta != nullptr);
    const auto& fmt= std::get<athena::node::property::dictionary> (
      meta->properties.at ("math:cell-format").data);
    QCOMPARE (std::get<std::string> (fmt.at ("align").data), std::string ("r"));
    const auto xml= write_xml_v3 (converted.document);
    QVERIFY (read_xml_v3 (xml) == converted.document);
  }

  void migrationIncludesMathInsideNestedRichMetadata () {
    using athena::node::property;
    tree inner= compound ("math", tree (VAR_AROUND,
      tree ("<langle>"), tree ("x+y"), tree ("<rangle>")));
    athena::node::metadata rich_meta;
    rich_meta.id= athena::node::new_id ();
    athena::node::set (inner[0], rich_meta);
    tree source (DOCUMENT, tree ("content"));
    athena::node::metadata meta;
    meta.id= athena::node::new_id ();
    meta.properties.emplace ("nested", property (property::list {
      property (property::dictionary {{"formula",
        property (athena::node::rich_text {inner})}})}));
    athena::node::set (source, meta);
    const auto migrated= migrate_math_v2_tree (
      read_xml_v2 (write_xml_v2 (source)), true);
    QCOMPARE (migrated.delimiters, std::size_t (1));
    const auto* migrated_meta= athena::node::get (migrated.document);
    QVERIFY (migrated_meta != nullptr);
    const auto& list= std::get<property::list> (
      migrated_meta->properties.at ("nested").data);
    const auto& dict= std::get<property::dictionary> (list[0].data);
    const tree& rich= std::get<athena::node::rich_text> (
      dict.at ("formula").data).content;
    QVERIFY (is_func (rich, make_tree_label ("math"), 1));
    QVERIFY (is_func (rich[0], DELIMITED, 6));
    QCOMPARE (athena::node::id (rich[0]), rich_meta.id);
    const auto xml= write_xml_v3 (migrated.document);
    const auto restored= read_xml_v3 (xml);
    QVERIFY (restored == migrated.document);
    QVERIFY (athena::node::equal_metadata (restored, migrated.document));
  }

  void optionalReadOnlyDocumentAudit () {
    const QByteArray path= qgetenv ("ATHENA_V3_READ_ONLY_SAMPLE");
    if (path.isEmpty ()) QSKIP ("No read-only representative XML v2 sample configured");
    QFile file (QString::fromUtf8 (path));
    QVERIFY2 (file.open (QIODevice::ReadOnly), "Could not open representative XML v2 input");
    const auto raw= file.readAll ();
    const std::string source (raw.constData (), std::size_t (raw.size ()));
    // Never write to the sample, not even a modified access journal in its
    // directory. Migration is entirely detached and in memory.
    const auto old= read_xml_v2 (source);
    const auto converted= migrate_math_v2_tree (old);
    qInfo ().noquote () << "v3 audited source:" << converted.delimiters
                        << "delimiters," << converted.scripts << "scripts,"
                        << converted.operators << "operators,"
                        << converted.math_blocks << "math blocks,"
                        << converted.grids << "grids,"
                        << converted.warnings.size () << "warnings";
    for (const auto& warning: converted.warnings)
      qInfo ().noquote () << "v3 warning:" << QString::fromStdString (warning.code)
                          << QString::fromStdString (warning.detail);
    std::function<void (const tree&,std::string)> scan=
      [&] (const tree& value, std::string where) {
        if (is_atomic (value)) return;
        const tree_label label= L (value);
        if (label == AROUND || label == VAR_AROUND || label == BIG_AROUND ||
            label == LEFT || label == MID || label == RIGHT || label == BIG ||
            label == LSUB || label == LSUP || label == RSUB || label == RSUP ||
            label == LPRIME || label == RPRIME || label == WIDE ||
            label == VAR_WIDE || label == ABOVE || label == BELOW ||
            label == LONG_ARROW)
          qWarning ().noquote () << "Unmigrated node"
                                 << QString::fromStdString (where)
                                 << QString::fromUtf8 (as_string (label).data ());
        for (int i=0; i<N (value); ++i)
          scan (value[i], where + "/" + std::to_string (i));
      };
    scan (converted.document, "");
    const std::string written= write_xml_v3 (converted.document);
    QVERIFY (read_xml_v3 (written) == converted.document);
    QCOMPARE (write_xml_v3 (read_xml_v3 (written)), written);
  }

  void optionalReadOnlyCorpusAudit () {
    namespace fs= std::filesystem;
    const QByteArray supplied= qgetenv ("ATHENA_V3_READ_ONLY_CORPUS");
    if (supplied.isEmpty ()) QSKIP ("No read-only XML v2 corpus configured");
    const fs::path corpus (supplied.constData ());
    QVERIFY2 (fs::is_directory (corpus), "Corpus must be an existing directory");
    std::size_t files= 0, skipped= 0, errors= 0;
    std::size_t pairs= 0, script_count= 0, block_count= 0, warning_count= 0;
    std::map<std::string,std::size_t> warning_types;
    for (const auto& entry: fs::recursive_directory_iterator (corpus)) {
      if (!entry.is_regular_file () || entry.path ().extension () != ".ath")
        continue;
      const fs::path relative= entry.path ().lexically_relative (corpus);
      bool internal= false;
      for (const auto& component: relative)
        if (component == ".backup" || component == ".git" ||
            component == ".athena") { internal= true; break; }
      if (internal) continue;
      QFile source (QString::fromStdString (entry.path ().string ()));
      if (!source.open (QIODevice::ReadOnly)) {
        ++errors;
        qWarning ().noquote () << "Corpus read failure:"
                                 << QString::fromStdString (relative.string ());
        continue;
      }
      const QByteArray bytes= source.readAll ();
      if (!bytes.startsWith ("<?xml") && !bytes.startsWith ("<athena-document")) {
        ++skipped;
        continue;
      }
      try {
        const std::string xml (bytes.constData (), std::size_t (bytes.size ()));
        const tree previous= read_xml_v2 (xml);
        const auto result= migrate_math_v2_tree (previous);
        validate_cold_v3_migration (result.document);
        const std::string output= write_xml_v3 (result.document);
        const tree restored= read_xml_v3 (output);
        if (restored != result.document ||
            !athena::node::equal_metadata (restored, result.document))
          throw std::runtime_error ("V3 document/metadata round trip mismatch");
        pairs+= result.delimiters;
        script_count+= result.scripts;
        block_count+= result.math_blocks;
        warning_count+= result.warnings.size ();
        for (const auto& warning: result.warnings) {
          ++warning_types[warning.code];
          QStringList location;
          for (const auto index: warning.old_path) location << QString::number (index);
          qInfo ().noquote () << "Corpus migration warning:"
            << QString::fromStdString (relative.string ())
            << location.join ('/') << QString::fromStdString (warning.code)
            << QString::fromStdString (warning.detail);
        }
        ++files;
      }
      catch (const std::exception& error) {
        ++errors;
        qWarning ().noquote () << "Corpus migration failure:"
          << QString::fromStdString (relative.string ()) << error.what ();
      }
    }
    qInfo ().noquote () << "Read-only corpus audit:"
      << files << "v3 roundtrips;" << skipped << "non-v2 files;"
      << errors << "failures;" << pairs << "delimiters;"
      << script_count << "scripts;" << block_count << "blocks;"
      << warning_count << "warnings";
    for (const auto& [code, count]: warning_types)
      qInfo ().noquote () << "Corpus warning class:"
        << QString::fromStdString (code) << qsizetype (count);
    QVERIFY2 (files > 0, "Corpus contains no validated XML v2 document");
    QCOMPARE (errors, std::size_t (0));
  }

  void historyVersionsAndProtectedEvidence () {
    QTemporaryDir temp;
    QVERIFY (temp.isValid ());
    namespace fs= std::filesystem;
    const fs::path source= fs::path (temp.path ().toStdString ()) / "source";
    const fs::path staged= fs::path (temp.path ().toStdString ()) / "staged";
    fs::create_directories (source);
    fs::create_directories (staged / ".athena");
    std::int64_t first= 0, second= 0, protected_id= 0;
    std::string content_a, content_b;
    athena::history::version_entry old_version;
    {
      athena::history::document_history_store history;
      std::string error;
      QVERIFY2 (history.open (source, error), error.c_str ());
      tree body_a (DOCUMENT, compound ("body", tree (DOCUMENT,
        compound ("math", tree (CONCAT, tree ("x"), tree (RSUB, "i"))))));
      tree body_b (DOCUMENT, compound ("body", tree (DOCUMENT,
        compound ("math", tree (CONCAT, tree ("y"), tree (RSUB, "i"))))));
      content_a= write_xml_v2 (body_a);
      content_b= write_xml_v2 (body_b);
      bool inserted= false;
      QVERIFY2 (history.capture ("Math.ath", content_a, "edit", std::nullopt,
                                 inserted, first, error), error.c_str ());
      QVERIFY (inserted);
      QVERIFY2 (history.capture ("Math.ath", content_b, "edit", std::nullopt,
                                 inserted, second, error), error.c_str ());
      QVERIFY (inserted);
      athena::history::version_entry second_version;
      QVERIFY2 (history.get (second, second_version, error), error.c_str ());
      QVERIFY (second_version.delta);
      QVERIFY2 (history.protect ("Math.ath", content_b, "hodarium-sync-1",
                                 protected_id, error), error.c_str ());
      QVERIFY2 (history.get (protected_id, old_version, error), error.c_str ());
      QVERIFY (old_version.protected_snapshot);
      QVERIFY (first < second && second < protected_id);
    }
    const fs::path relative= fs::path (".athena") / "document-history.sqlite";
    fs::copy_file (source / relative, staged / relative);
    const auto original_database_before= fs::file_size (source / relative);
    const auto preview= migrate_vault_history_v3 (source, nullptr, true);
    QCOMPARE (preview.versions_converted, std::size_t (3));
    QCOMPARE (preview.protected_evidence, std::size_t (1));
    QCOMPARE (fs::file_size (source / relative), original_database_before);
    const auto converted= migrate_vault_history_v3 (source, &staged, true);
    QCOMPARE (converted.versions_converted, std::size_t (3));
    QCOMPARE (converted.protected_evidence, std::size_t (1));
    QCOMPARE (fs::file_size (source / relative), original_database_before);

    athena::history::document_history_store history;
    std::string error;
    QVERIFY2 (history.open (staged, error), error.c_str ());
    for (const auto id: {first,second,protected_id}) {
      athena::history::version_entry version;
      QVERIFY2 (history.get (id, version, error), error.c_str ());
      QCOMPARE (version.id, id);
      QCOMPARE (version.protected_snapshot, id == protected_id);
      std::string bytes;
      QVERIFY2 (history.reconstruct (id, bytes, error), error.c_str ());
      const tree result= read_xml_v3 (bytes);
      QVERIFY (is_func (result, DOCUMENT));
      QVERIFY (is_func (result[0][0][0][0][0], MATH_SCRIPTS, 5));
      if (id == protected_id)
        QCOMPARE (version.created_at_ms, old_version.created_at_ms);
    }

    sqlite3* db= nullptr;
    const auto file= (staged / relative).string ();
    QVERIFY (sqlite3_open_v2 (file.c_str (), &db, SQLITE_OPEN_READONLY, nullptr) == SQLITE_OK);
    sqlite3_stmt* query= nullptr;
    QVERIFY (sqlite3_prepare_v2 (db,
      "SELECT original_payload,original_content_hash,original_size "
      "FROM xml_v3_protected_evidence WHERE version_id=?1",
      -1, &query, nullptr) == SQLITE_OK);
    QVERIFY (sqlite3_bind_int64 (query, 1, protected_id) == SQLITE_OK);
    QVERIFY (sqlite3_step (query) == SQLITE_ROW);
    const auto* raw= static_cast<const char*> (sqlite3_column_blob (query, 0));
    const int raw_size= sqlite3_column_bytes (query, 0);
    QCOMPARE (std::string (raw, std::size_t (raw_size)), content_b);
    QCOMPARE (std::string (reinterpret_cast<const char*> (sqlite3_column_text (query,1))),
              storage_bytes_fingerprint (content_b));
    QCOMPARE (sqlite3_column_int64 (query,2), std::int64_t (content_b.size ()));
    sqlite3_finalize (query);
    sqlite3_close (db);
  }

  void vaultPublicationRejectsConcurrentWrite () {
    namespace fs= std::filesystem;
    QTemporaryDir temp;
    QVERIFY (temp.isValid ());
    const fs::path root= fs::path (temp.path ().toStdString ()) / "vault";
    fs::create_directory (root);
    const auto put= [&] (const char* name, const std::string& bytes) {
      QFile file (QString::fromStdString ((root / name).string ()));
      if (!file.open (QIODevice::WriteOnly) ||
          file.write (bytes.data (), qsizetype (bytes.size ())) != qsizetype (bytes.size ()))
        throw std::runtime_error ("Cannot write private publication fixture");
    };
    const std::string info= "{\"name\":\"Publication fixture\",\"node_model_version\":1}";
    const auto first= write_xml_v2 (tree (DOCUMENT, compound ("body", tree (DOCUMENT, "first"))));
    const auto changed= write_xml_v2 (tree (DOCUMENT, compound ("body", tree (DOCUMENT, "external edit"))));
    put ("Vaultfile.json", info); put ("one.ath", first);
    bool changed_before_commit= false;
    const auto concurrent= [&] (const char* phase, std::size_t, std::size_t,
                                const std::string&) {
      if (std::string_view (phase) == "Commit XML v3") {
        put ("one.ath", changed);
        changed_before_commit= true;
      }
    };
    QVERIFY_THROWS_EXCEPTION (std::runtime_error,
      upgrade_vault_xml_v3 (root, {}, concurrent));
    QVERIFY (changed_before_commit);
    QFile document (QString::fromStdString ((root / "one.ath").string ()));
    QVERIFY (document.open (QIODevice::ReadOnly));
    QCOMPARE (document.readAll ().toStdString (), changed);
    QFile vaultfile (QString::fromStdString ((root / "Vaultfile.json").string ()));
    QVERIFY (vaultfile.open (QIODevice::ReadOnly));
    QCOMPARE (vaultfile.readAll ().toStdString (), info);
    QCOMPARE (std::distance (fs::directory_iterator (root.parent_path ()),
                             fs::directory_iterator ()), 1);
  }

  void vaultPublicationRecoversPrivateWal () {
    namespace fs= std::filesystem;
    QTemporaryDir temp;
    QVERIFY (temp.isValid ());
    const fs::path root= fs::path (temp.path ().toStdString ()) / "vault";
    fs::create_directory (root);
    const auto put= [&] (const char* name, const std::string& bytes) {
      QFile file (QString::fromStdString ((root / name).string ()));
      if (!file.open (QIODevice::WriteOnly) ||
          file.write (bytes.data (), qsizetype (bytes.size ())) != qsizetype (bytes.size ()))
        throw std::runtime_error ("Cannot write private WAL fixture");
    };
    const auto read= [] (const fs::path& path) {
      QFile file (QString::fromStdString (path.string ()));
      if (!file.open (QIODevice::ReadOnly)) throw std::runtime_error ("Cannot read WAL fixture");
      return file.readAll ();
    };
    put ("Vaultfile.json", "{\"name\":\"WAL fixture\",\"node_model_version\":1}");
    put ("one.ath", write_xml_v2 (tree (DOCUMENT, compound ("body", tree (DOCUMENT, "one")))));
    sqlite3* raw= nullptr;
    QCOMPARE (sqlite3_open ((root / "data.sqlite").c_str (), &raw), SQLITE_OK);
    const std::unique_ptr<sqlite3, decltype (&sqlite3_close)> database (raw, sqlite3_close);
    QCOMPARE (sqlite3_exec (raw, "PRAGMA journal_mode=WAL; PRAGMA wal_autocheckpoint=0;"
      "CREATE TABLE latest(value TEXT); INSERT INTO latest VALUES('committed in WAL');",
      nullptr, nullptr, nullptr), SQLITE_OK);
    const auto db_before= read (root / "data.sqlite");
    const auto wal_before= read (root / "data.sqlite-wal");
    QVERIFY (!wal_before.isEmpty ());
    const auto result= upgrade_vault_xml_v3 (root, {});
    QVERIFY (result.durable);
    QCOMPARE (read (result.backup / "data.sqlite"), db_before);
    QCOMPARE (read (result.backup / "data.sqlite-wal"), wal_before);
    sqlite3* published= nullptr;
    QCOMPARE (sqlite3_open_v2 ((root / "data.sqlite").c_str (), &published,
                              SQLITE_OPEN_READONLY, nullptr), SQLITE_OK);
    const std::unique_ptr<sqlite3, decltype (&sqlite3_close)> target (published, sqlite3_close);
    sqlite3_stmt* query= nullptr;
    QCOMPARE (sqlite3_prepare_v2 (published, "SELECT value FROM latest", -1,
                                 &query, nullptr), SQLITE_OK);
    QCOMPARE (sqlite3_step (query), SQLITE_ROW);
    QCOMPARE (std::string (reinterpret_cast<const char*> (sqlite3_column_text (query, 0))),
              std::string ("committed in WAL"));
    sqlite3_finalize (query);
    QVERIFY (!fs::exists (root / "data.sqlite-wal") || fs::file_size (root / "data.sqlite-wal") == 0);
    QVERIFY (!fs::exists (result.backup.parent_path () / "input"));
  }

  void vaultPreflightDoesNotWriteOrPublish () {
    namespace fs= std::filesystem;
    QTemporaryDir temp;
    QVERIFY (temp.isValid ());
    const fs::path root= fs::path (temp.path ().toStdString ()) / "vault";
    fs::create_directories (root);
    const fs::path vaultfile= root / "Vaultfile.json";
    const fs::path sourcefile= root / "Math.ath";
    const std::string info= "{\"name\":\"V3 fixture\",\"node_model_version\":1}\n";
    tree expression (CONCAT,
      tree ("x"), tree (RSUB, "i"), tree ("+"), tree ("y"));
    tree document (DOCUMENT,
      compound ("body", tree (DOCUMENT, compound ("math", expression))));
    const std::string original= write_xml_v2 (document);
    auto write= [] (const fs::path& path, const std::string& bytes) {
      QFile file (QString::fromStdString (path.string ()));
      if (!file.open (QIODevice::WriteOnly | QIODevice::Truncate))
        throw std::runtime_error ("Cannot construct v3 test fixture");
      if (file.write (bytes.data (), qsizetype (bytes.size ())) !=
          qint64 (bytes.size ()))
        throw std::runtime_error ("Cannot write v3 test fixture");
      file.close ();
    };
    write (vaultfile, info);
    write (sourcefile, original);
    // Maintenance snapshots are pre-replacement evidence, including XML v1.
    // User summaries and unrecognized hidden documents remain live sources.
    const std::string archived= write_xml (document);
    const fs::path evidence= fs::path (".athena/maintenance/enunciations") /
      (storage_bytes_fingerprint (archived) + ".ath");
    fs::create_directories ((root / evidence).parent_path ());
    write (root / evidence, archived);
    fs::create_directories (root / "maintenance");
    write (root / "maintenance/summary.ath", original);
    write (root / ".athena/user-note.ath", original);
    fs::create_directories (root / ".backup/manual-save");
    write (root / ".backup/manual-save/Math.ath", archived);
    fs::create_directories (root / ".athena/rag-backup-fixture");
    write (root / ".athena/rag-backup-fixture/rag.sqlite", "archived RAG");
    fs::create_directories (root / ".athena/node-locations");
    const std::vector<fs::path> caches {
      "rag.sqlite", "rag.sqlite-wal", "rag.sqlite-shm",
      ".athena/reference-graph.sqlite", ".athena/reference-graph.sqlite-wal",
      ".athena/reference-graph.sqlite-shm", ".athena/node-locations/data.mdb",
      ".athena/node-locations/lock.mdb"};
    for (const auto& path: caches) write (root / path, "obsolete derived cache");
    write (root / ".athena/node-locations/keep.txt", "unknown file: preserve");
    const auto make_index= [&] (const fs::path& path,
                               const char* create_sql,
                               const char* insert_sql,
                               const std::string& fragment) {
      sqlite3* db= nullptr;
      if (sqlite3_open_v2 (path.c_str (), &db,
                           SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK)
        throw std::runtime_error ("Cannot construct v3 indexed fragment fixture");
      char* error= nullptr;
      if (sqlite3_exec (db, create_sql, nullptr, nullptr, &error) != SQLITE_OK) {
        sqlite3_free (error); sqlite3_close (db);
        throw std::runtime_error ("Cannot construct v3 index schema");
      }
      sqlite3_stmt* insert= nullptr;
      if (sqlite3_prepare_v2 (db, insert_sql, -1, &insert, nullptr) != SQLITE_OK) {
        sqlite3_close (db);
        throw std::runtime_error ("Cannot prepare v3 indexed fragment fixture");
      }
      sqlite3_bind_text (insert, 1, fragment.data (), int (fragment.size ()),
                         SQLITE_TRANSIENT);
      const int status= sqlite3_step (insert);
      sqlite3_finalize (insert);
      sqlite3_close (db);
      if (status != SQLITE_DONE)
        throw std::runtime_error ("Cannot insert v3 indexed fragment fixture");
    };
    const auto indexed_xml= write_xml (tree ("name"), xml_kind::fragment);
    make_index (root / "artifacts.db",
      "CREATE TABLE artifact_names(artifact_uuid TEXT,name_tree TEXT);",
      "INSERT INTO artifact_names VALUES('artifact-1',?1)", indexed_xml);
    const QByteArray old_bytes (indexed_xml.data (), qsizetype (indexed_xml.size ()));
    const QByteArray encoded= old_bytes.toBase64 ();
    const std::string old_bold= "base64-v1:" +
      std::string (encoded.constData (), std::size_t (encoded.size ()));
    make_index (root / "bold-text.db",
      "CREATE TABLE entries(uuid TEXT,keyword_tree TEXT);",
      "INSERT INTO entries VALUES('bold-1',?1)", old_bold);
    make_index (root / "enunciations.db",
      "CREATE TABLE entries(uuid TEXT,display_text TEXT);",
      "INSERT INTO entries VALUES('enun-1',?1)", "Theorem");
    std::int64_t protected_id= 0;
    {
      athena::history::document_history_store history;
      std::string error;
      QVERIFY2 (history.open (root, error), error.c_str ());
      bool inserted= false;
      std::int64_t version= 0;
      QVERIFY2 (history.capture ("Math.ath", original, "save", std::nullopt,
                                 inserted, version, error), error.c_str ());
      QVERIFY (inserted);
      QVERIFY2 (history.protect ("Math.ath", original, "hodarium-test",
                                 protected_id, error), error.c_str ());
    }
    auto snapshot_bytes= [] (const fs::path& vault) {
      std::map<std::string,std::pair<std::string,fs::file_time_type>> state;
      for (const auto& entry: fs::recursive_directory_iterator (vault)) {
        if (!entry.is_regular_file ()) continue;
        QFile file (QString::fromStdString (entry.path ().string ()));
        if (!file.open (QIODevice::ReadOnly))
          throw std::runtime_error ("Could not snapshot test Vault bytes");
        const auto data= file.readAll ();
        state.emplace (entry.path ().lexically_relative (vault).generic_string (),
          std::make_pair (std::string (data.constData (),
                                      std::size_t (data.size ())),
                          fs::last_write_time (entry.path ())));
      }
      return state;
    };
    const auto untouched_source= snapshot_bytes (root);
    vault_xml_v3_upgrade_options options;
    options.check_only= true;
    options.strict= true;
    qInfo () << "v3 staging fixture: checking read-only source";
    const auto result= upgrade_vault_xml_v3 (root, options);
    QCOMPARE (result.documents, std::size_t (3));
    QCOMPARE (result.warnings, std::size_t (0));
    QCOMPARE (result.scripts, std::size_t (3));
    QCOMPARE (result.index_tree_fragments, std::size_t (2));
    QVERIFY (snapshot_bytes (root) == untouched_source);
    QFile verify_source (QString::fromStdString (sourcefile.string ()));
    QVERIFY (verify_source.open (QIODevice::ReadOnly));
    const auto bytes= verify_source.readAll ();
    QCOMPARE (std::string (bytes.data (), std::size_t (bytes.size ())), original);
    QFile verify_info (QString::fromStdString (vaultfile.string ()));
    QVERIFY (verify_info.open (QIODevice::ReadOnly));
    const auto json= verify_info.readAll ();
    QCOMPARE (std::string (json.data (), std::size_t (json.size ())), info);
    QCOMPARE (std::distance (fs::directory_iterator (root.parent_path ()),
                             fs::directory_iterator ()), 1);
    options.check_only= false;
    bool reached_commit= false;
    const auto cancel= [&] (const char* phase, std::size_t, std::size_t,
                            const std::string&) {
      if (std::string_view (phase) == "Commit XML v3") {
        reached_commit= true;
        throw std::runtime_error ("Test cancellation before exchange");
      }
    };
    QVERIFY_THROWS_EXCEPTION (std::runtime_error,
      upgrade_vault_xml_v3 (root, options, cancel));
    QVERIFY (reached_commit);
    QVERIFY (snapshot_bytes (root) == untouched_source);
    QCOMPARE (std::distance (fs::directory_iterator (root.parent_path ()),
                             fs::directory_iterator ()), 1);

    const fs::path staged= root.parent_path () / "disconnected-v3-candidate";
    qInfo () << "v3 staging fixture: building disconnected candidate";
    const auto candidate= stage_vault_xml_v3_candidate (root, staged, true);
    QCOMPARE (candidate.candidate, staged);
    QCOMPARE (candidate.checked.documents, std::size_t (3));
    QCOMPARE (candidate.migrated_index_trees, std::size_t (2));
    QVERIFY (candidate.migrated_history_versions >= std::size_t (2));
    QVERIFY (is_func (read_xml_v3 ([&] {
      QFile document (QString::fromStdString ((staged / "Math.ath").string ()));
      if (!document.open (QIODevice::ReadOnly)) return std::string ();
      const QByteArray bytes= document.readAll ();
      return std::string (bytes.constData (), std::size_t (bytes.size ()));
    } ()), DOCUMENT));
    QFile manifest (QString::fromStdString (
      (staged / ".athena/xml-v3-candidate-manifest.json").string ()));
    QVERIFY (manifest.open (QIODevice::ReadOnly));
    const auto receipt= QJsonDocument::fromJson (manifest.readAll ()).object ();
    QVERIFY (!receipt.value ("eligible_for_atomic_publication").toBool (true));
    QVERIFY (!receipt.value ("hodarium_protocol_cutover_verified").toBool (true));
    const auto preserved= receipt.value ("preserved_maintenance_sources").toArray ();
    QCOMPARE (preserved.size (), 1);
    QCOMPARE (preserved[0].toObject ().value ("path").toString ().toStdString (),
              evidence.generic_string ());
    QCOMPARE (preserved[0].toObject ().value ("sha256").toString ().toStdString (),
              storage_bytes_fingerprint (archived));
    QCOMPARE (receipt.value ("invalidated_cache_files").toArray ().size (),
              qsizetype (caches.size ()));
    for (const auto& path: caches) QVERIFY (!fs::exists (staged / path));
    const auto staged_files= snapshot_bytes (staged);
    for (const auto& path: {evidence,
         fs::path (".backup/manual-save/Math.ath"),
         fs::path (".athena/rag-backup-fixture/rag.sqlite"),
         fs::path (".athena/node-locations/keep.txt")})
      QCOMPARE (staged_files.at (path.generic_string ()).first,
                untouched_source.at (path.generic_string ()).first);
    for (const char* path: {"maintenance/summary.ath", ".athena/user-note.ath"})
      QVERIFY (is_func (read_xml_v3 (staged_files.at (path).first), DOCUMENT));
    const string refused= vault_load (
      url_system (string (staged.string ().c_str ())),
      "XML v3 candidate", "map.sqlite", "ns.sqlite");
    const std::string message (refused.data (), std::size_t (N (refused)));
    QVERIFY (message.find ("Unpublished XML v3 migration candidates cannot be opened") !=
             std::string::npos);
    athena::history::document_history_store staged_history;
    std::string history_error;
    QVERIFY2 (staged_history.open (staged, history_error), history_error.c_str ());
    std::string version;
    QVERIFY2 (staged_history.reconstruct (protected_id, version, history_error),
              history_error.c_str ());
    QVERIFY (is_func (read_xml_v3 (version), DOCUMENT));

    sqlite3* index_db= nullptr;
    QVERIFY (sqlite3_open_v2 ((staged / "artifacts.db").c_str (), &index_db,
                             SQLITE_OPEN_READONLY, nullptr) == SQLITE_OK);
    sqlite3_stmt* name_query= nullptr;
    QVERIFY (sqlite3_prepare_v2 (index_db, "SELECT name_tree FROM artifact_names",
                                -1, &name_query, nullptr) == SQLITE_OK);
    QCOMPARE (sqlite3_step (name_query), SQLITE_ROW);
    const auto* name_bytes= sqlite3_column_text (name_query, 0);
    const std::string staged_name ((const char*) name_bytes,
      std::size_t (sqlite3_column_bytes (name_query, 0)));
    QVERIFY (is_atomic (read_xml_v3 (staged_name, xml_kind::fragment)));
    sqlite3_finalize (name_query);
    sqlite3_close (index_db);

    QFile original_document (QString::fromStdString (sourcefile.string ()));
    QVERIFY (original_document.open (QIODevice::ReadOnly));
    QCOMPARE (original_document.readAll ().toStdString (), original);
    QFile original_vaultfile (QString::fromStdString (vaultfile.string ()));
    QVERIFY (original_vaultfile.open (QIODevice::ReadOnly));
    QCOMPARE (original_vaultfile.readAll ().toStdString (), info);
    QVERIFY (snapshot_bytes (root) == untouched_source);
    const auto published= upgrade_vault_xml_v3 (root, options);
    QVERIFY (published.durable);
    QVERIFY (snapshot_bytes (published.backup) == untouched_source);
    QVERIFY (!fs::exists (root / ".athena/xml-v3-candidate-manifest.json"));
    const auto published_files= snapshot_bytes (root);
    QVERIFY (read_xml_v3 (published_files.at ("Math.ath").first) ==
             read_xml_v3 (staged_files.at ("Math.ath").first));
    QVERIFY (published_files.at ("Vaultfile.json").first.find (
      "\"node_model_version\": 2") != std::string::npos);
  }

  void vaultStageRejectsWarmOnlyLabelsBeforeCreatingCandidate () {
    namespace fs= std::filesystem;
    QTemporaryDir temp;
    QVERIFY (temp.isValid ());
    const fs::path root= fs::path (temp.path ().toStdString ()) / "source";
    const fs::path candidate= root.parent_path () / "candidate";
    fs::create_directories (root);
    const auto save= [&] (const char* name, const std::string& bytes) {
      QFile file (QString::fromStdString ((root / name).string ()));
      if (!file.open (QIODevice::WriteOnly) ||
          file.write (bytes.data (), qsizetype (bytes.size ())) != qint64 (bytes.size ()))
        throw std::runtime_error ("Cannot write cold declaration fixture");
    };
    save ("Vaultfile.json", "{\"name\":\"Cold labels\",\"node_model_version\":1}");
    const std::string bytes= write_xml_v2 (tree (DOCUMENT,
      compound ("body", tree (DOCUMENT, compound ("test-v3-warm-only-vault-label", "x")))));
    save ("Source.ath", bytes);
    try {
      stage_vault_xml_v3_candidate (root, candidate, false);
      QFAIL ("Warm-only label must not produce a v3 candidate");
    }
    catch (const std::runtime_error& failure) {
      const std::string message= failure.what ();
      QVERIFY2 (message.find ("No cold-load declaration") != std::string::npos,
                message.c_str ());
      QVERIFY (message.find ("Source.ath") != std::string::npos);
    }
    QVERIFY (!fs::exists (candidate));
    QFile source (QString::fromStdString ((root / "Source.ath").string ()));
    QVERIFY (source.open (QIODevice::ReadOnly));
    QCOMPARE (source.readAll (), QByteArray::fromStdString (bytes));
  }

  void vaultInventoryRejectsUnrecognizedSources_data () {
    QTest::addColumn<QString> ("relative");
    QTest::addColumn<QString> ("kind");
    QTest::newRow ("ordinary-v1") << QString ("User.ath") << QString ("v1");
    QTest::newRow ("hidden-v1") << QString (".athena/User.ath") << QString ("v1");
    QTest::newRow ("maintenance-summary-v1")
      << QString ("maintenance/Summary.ath") << QString ("v1");
    QTest::newRow ("nested-snapshot-lookalike")
      << QString (".athena/maintenance/enunciations/nested/User.ath") << QString ("v1");
    QTest::newRow ("snapshot-name-not-hash")
      << QString (".athena/maintenance/enunciations/User.ath") << QString ("v1");
    QTest::newRow ("snapshot-hash-mismatch") << QString ("snapshot") << QString ("changed");
    QTest::newRow ("snapshot-symlink") << QString ("snapshot") << QString ("symlink");
    QTest::newRow ("snapshot-hardlink") << QString ("snapshot") << QString ("hardlink");
  }

  void vaultInventoryRejectsUnrecognizedSources () {
    QFETCH (QString, relative);
    QFETCH (QString, kind);
    namespace fs= std::filesystem;
    QTemporaryDir temp;
    QVERIFY (temp.isValid ());
    const fs::path root= fs::path (temp.path ().toStdString ()) / "source";
    const fs::path staged= root.parent_path () / "candidate";
    const auto save= [&] (const fs::path& path, const std::string& bytes) {
      fs::create_directories (path.parent_path ());
      QFile file (QString::fromStdString (path.string ()));
      if (!file.open (QIODevice::WriteOnly) ||
          file.write (bytes.data (), qsizetype (bytes.size ())) != qint64 (bytes.size ()))
        throw std::runtime_error ("Cannot write inventory fixture");
    };
    save (root / "Vaultfile.json", "{\"name\":\"Inventory\",\"node_model_version\":1}");
    const std::string original= write_xml (tree (DOCUMENT, tree ("evidence")));
    fs::path path (relative.toStdString ());
    if (relative == "snapshot")
      path= fs::path (".athena/maintenance/enunciations") /
        (storage_bytes_fingerprint (original) + ".ath");
    fs::create_directories ((root / path).parent_path ());
    if (kind == "symlink" || kind == "hardlink") {
      save (root / "evidence.txt", original);
      if (kind == "symlink") fs::create_symlink (root / "evidence.txt", root / path);
      else fs::create_hard_link (root / "evidence.txt", root / path);
    }
    else save (root / path, kind == "changed" ? original + "\n" : original);
    vault_xml_v3_upgrade_options options;
    options.check_only= true;
    QVERIFY_THROWS_EXCEPTION (std::runtime_error, upgrade_vault_xml_v3 (root, options));
    QVERIFY_THROWS_EXCEPTION (std::runtime_error,
                              stage_vault_xml_v3_candidate (root, staged, false));
    QVERIFY (!fs::exists (staged));
    QVERIFY (fs::exists (root / path));
  }

  void stagedIndexesInvalidateRangeFallbackAndPreserveIdentity () {
    namespace fs= std::filesystem;
    QTemporaryDir temp;
    QVERIFY (temp.isValid ());
    const fs::path root (temp.path ().toStdString ());
    AthenaVaultfileInfo info;
    sqlite3* db= nullptr;
    QCOMPARE (sqlite3_open_v2 ((root / info.artifacts_path).c_str (), &db,
      SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr), SQLITE_OK);
    const char* schema=
      "CREATE TABLE artifact_metadata(key TEXT PRIMARY KEY,value TEXT NOT NULL);"
      "CREATE TABLE documents(path TEXT PRIMARY KEY,semantic_hash TEXT,storage_hash TEXT,"
      "content_hash TEXT,locator_contract TEXT);"
      "INSERT INTO documents VALUES('Math.ath','semantic','storage','content','locator');"
      "CREATE TABLE artifacts(uuid TEXT PRIMARY KEY,source_uuid TEXT,source_nodes TEXT,"
      "identity_decision TEXT,input_hash TEXT);"
      "INSERT INTO artifacts VALUES('artifact','source','[source]','user-approved','input');"
      "CREATE TABLE artifact_identity_history(sequence INTEGER PRIMARY KEY,evidence TEXT);"
      "INSERT INTO artifact_identity_history VALUES(1,'keep original identity decision');"
      "CREATE TABLE artifact_range_cache(path TEXT,request_hash TEXT,semantic_hash TEXT,"
      "input_hash TEXT,paragraph_offsets TEXT);"
      "INSERT INTO artifact_range_cache VALUES('Math.ath','old-request','semantic','input','[7]');"
      "INSERT INTO artifact_range_cache VALUES('Math.ath','old-fallback','','','[9]');";
    QCOMPARE (sqlite3_exec (db, schema, nullptr, nullptr, nullptr), SQLITE_OK);
    sqlite3_close (db);
    const auto stats= migrate_staged_vault_indexes_v3 (root, info, true);
    QCOMPARE (stats.invalidated_range_rows, std::size_t (2));
    QCOMPARE (stats.invalidated_hashes, std::size_t (5));
    QCOMPARE (sqlite3_open_v2 ((root / info.artifacts_path).c_str (), &db,
      SQLITE_OPEN_READONLY, nullptr), SQLITE_OK);
    const auto scalar= [&] (const char* sql) {
      sqlite3_stmt* query= nullptr;
      if (sqlite3_prepare_v2 (db, sql, -1, &query, nullptr) != SQLITE_OK)
        throw std::runtime_error (sqlite3_errmsg (db));
      const int status= sqlite3_step (query);
      const auto* value= status == SQLITE_ROW ? sqlite3_column_text (query, 0) : nullptr;
      const std::string result= value ? reinterpret_cast<const char*> (value) : "";
      sqlite3_finalize (query);
      return result;
    };
    QCOMPARE (scalar ("SELECT count(*) FROM artifact_range_cache"), std::string ("0"));
    QCOMPARE (scalar ("SELECT source_uuid||':'||source_nodes||':'||identity_decision "
                      "FROM artifacts WHERE uuid='artifact'"),
              std::string ("source:[source]:user-approved"));
    QCOMPARE (scalar ("SELECT evidence FROM artifact_identity_history WHERE sequence=1"),
              std::string ("keep original identity decision"));
    QCOMPARE (scalar ("SELECT semantic_hash||storage_hash||content_hash||locator_contract "
                      "FROM documents"), std::string ());
    QCOMPARE (scalar ("SELECT input_hash FROM artifacts"), std::string ());
    sqlite3_close (db);
  }

  void derivedCacheInventoryRejectsAuthoritativeAndLinkedPaths () {
    namespace fs= std::filesystem;
    QTemporaryDir temp;
    QVERIFY (temp.isValid ());
    const fs::path root (temp.path ().toStdString ());
    AthenaVaultfileInfo info;
    for (const char* path: {"User.ath", "Vaultfile.json", "artifacts.db",
         "artifacts.db-wal", ".athena/document-history.sqlite",
         ".backup/rag.sqlite", ".athena/maintenance/rag.sqlite",
         "../rag.sqlite", "/tmp/rag.sqlite"}) {
      info.rag_index_path= path;
      QVERIFY_THROWS_EXCEPTION (std::exception,
        vault_xml_v3_derived_cache_files (root, info));
    }
    info.rag_index_path= "rag.sqlite";
    fs::create_symlink (root / "missing", root / info.rag_index_path);
    QVERIFY_THROWS_EXCEPTION (std::runtime_error,
      vault_xml_v3_derived_cache_files (root, info));
    fs::remove (root / info.rag_index_path);
    fs::create_directories (root / "outside");
    QFile file (QString::fromStdString ((root / "outside/reference-graph.sqlite").string ()));
    QVERIFY (file.open (QIODevice::WriteOnly));
    file.write ("must not delete");
    file.close ();
    fs::create_directory_symlink (root / "outside", root / ".athena");
    QVERIFY_THROWS_EXCEPTION (std::runtime_error,
      vault_xml_v3_derived_cache_files (root, info));
    QVERIFY (fs::exists (root / "outside/reference-graph.sqlite"));
  }

  void historyPreflightRejectsNonemptyWalWithoutWriting () {
    namespace fs= std::filesystem;
    QTemporaryDir temp;
    QVERIFY (temp.isValid ());
    const fs::path root (temp.path ().toStdString ());
    fs::create_directory (root / ".athena");
    const fs::path database= root / ".athena/document-history.sqlite";
    const fs::path wal (database.string () + "-wal");
    for (const auto& path: {database, wal}) {
      QFile file (QString::fromStdString (path.string ()));
      QVERIFY (file.open (QIODevice::WriteOnly));
      QCOMPARE (file.write ("untouched evidence"), qint64 (18));
    }
    const auto modified= fs::last_write_time (wal);
    try {
      (void) migrate_vault_history_v3 (root, nullptr, true);
      QFAIL ("Nonempty history WAL must block immutable preflight");
    }
    catch (const std::runtime_error& error) {
      QVERIFY (std::string (error.what ()).find ("WAL checkpoint") != std::string::npos);
    }
    for (const auto& path: {database, wal}) {
      QFile file (QString::fromStdString (path.string ()));
      QVERIFY (file.open (QIODevice::ReadOnly));
      QCOMPARE (file.readAll (), QByteArray ("untouched evidence"));
    }
    QCOMPARE (fs::last_write_time (wal), modified);
    QVERIFY (!fs::exists (fs::path (database.string () + "-shm")));
  }

  void eliminatedDocumentIdentityPrunesOnlyGeneratedReference () {
    const std::string obsolete= athena::node::new_id ();
    tree wrapper (DOCUMENT, tree (CONCAT, tree ("x")));
    athena::node::metadata meta;
    meta.id= obsolete;
    athena::node::set (wrapper, meta);
    const std::string derived= "athena-node-" + obsolete;
    tree references= compound ("references", tree (COLLECTION,
      tree (ASSOCIATE,
        tree (string (derived.data (), int (derived.size ()))),
        tree (TUPLE, tree ("uninit"), tree ("4"))),
      tree (ASSOCIATE, tree ("user-label"), tree (TUPLE, tree ("2")))));
    tree root (DOCUMENT,
      compound ("body", tree (DOCUMENT,
        compound ("equation*", wrapper))), references);
    auto migrated= migrate_math_v2_tree (root, true);
    QCOMPARE (migrated.removed_identities.size (), std::size_t (1));
    QCOMPARE (migrated.removed_identities[0], obsolete);
    QCOMPARE (N (migrated.document[1][0]), 1);
    QCOMPARE (migrated.document[1][0][0][0], tree ("user-label"));
    QVERIFY (is_func (migrated.document[0][0][0], MATH_BLOCK, 1));
    const auto xml= write_xml_v3 (migrated.document);
    QVERIFY (read_xml_v3 (xml) == migrated.document);
  }

  void vaultPreflightRejectsArtifactBindingToEliminatedSourceUuid () {
    namespace fs= std::filesystem;
    QTemporaryDir temporary;
    QVERIFY (temporary.isValid ());
    const fs::path vault= fs::path (temporary.path ().toStdString ()) / "fixture";
    fs::create_directory (vault);
    auto save= [] (const fs::path& path, const std::string& content) {
      QFile file (QString::fromStdString (path.string ()));
      if (!file.open (QIODevice::WriteOnly | QIODevice::Truncate))
        throw std::runtime_error ("Cannot save temporary XML v3 fixture");
      if (file.write (content.data (), qsizetype (content.size ())) !=
          qint64 (content.size ()))
        throw std::runtime_error ("Truncated fixture write");
    };
    const std::string old_id= athena::node::new_id ();
    tree obsolete (DOCUMENT, tree (CONCAT, tree ("x")));
    athena::node::metadata meta;
    meta.id= old_id;
    athena::node::set (obsolete, meta);
    tree document (DOCUMENT, compound ("body", tree (DOCUMENT,
      compound ("equation*", obsolete))));
    const std::string original= write_xml_v2 (document);
    save (vault / "Math.ath", original);
    save (vault / "Vaultfile.json",
      "{\"name\":\"Source UUID fixture\",\"node_model_version\":1}\n");
    sqlite3* db= nullptr;
    const fs::path database= vault / "artifacts.db";
    QCOMPARE (sqlite3_open_v2 (database.c_str (), &db,
      SQLITE_OPEN_CREATE | SQLITE_OPEN_READWRITE, nullptr), SQLITE_OK);
    QCOMPARE (sqlite3_exec (db,
      "CREATE TABLE artifacts(uuid TEXT,source_uuid TEXT,source_nodes TEXT)",
      nullptr, nullptr, nullptr), SQLITE_OK);
    sqlite3_stmt* insert= nullptr;
    QCOMPARE (sqlite3_prepare_v2 (db,
      "INSERT INTO artifacts VALUES('artifact-1',?1,?2)",
      -1, &insert, nullptr), SQLITE_OK);
    sqlite3_bind_text (insert, 1, old_id.data (), int (old_id.size ()),
                       SQLITE_TRANSIENT);
    const std::string nodes= "[\"" + old_id + "\"]";
    sqlite3_bind_text (insert, 2, nodes.data (), int (nodes.size ()),
                       SQLITE_TRANSIENT);
    QCOMPARE (sqlite3_step (insert), SQLITE_DONE);
    sqlite3_finalize (insert);
    sqlite3_close (db);

    vault_xml_v3_upgrade_options options;
    options.check_only= true;
    options.strict= true;
    QVERIFY_THROWS_EXCEPTION (std::runtime_error,
      upgrade_vault_xml_v3 (vault, options));
    // Both active binding columns are protected, not merely the simple UUID.
    QCOMPARE (sqlite3_open_v2 (database.c_str (), &db,
      SQLITE_OPEN_READWRITE, nullptr), SQLITE_OK);
    QCOMPARE (sqlite3_exec (db,
      "UPDATE artifacts SET source_uuid='',source_nodes='[]'",
      nullptr, nullptr, nullptr), SQLITE_OK);
    sqlite3_close (db);
    const auto accepted= upgrade_vault_xml_v3 (vault, options);
    QCOMPARE (accepted.documents, std::size_t (1));
    QFile verify (QString::fromStdString ((vault / "Math.ath").string ()));
    QVERIFY (verify.open (QIODevice::ReadOnly));
    QCOMPARE (verify.readAll ().toStdString (), original);
  }

  void immutableExternalHodariumAuditRejectsAlteredSignedRevision () {
    namespace fs= std::filesystem;
    QTemporaryDir temp;
    QVERIFY (temp.isValid ());
    const fs::path base= fs::path (temp.path ().toStdString ());
    const fs::path vault= base / "vault";
    const fs::path state= base / "hodarium-state";
    fs::create_directory (vault);
    fs::create_directory (vault / ".athena");
    fs::create_directory (state);
    const auto write_source= [] (const fs::path& path,
                                const std::string& content) {
      QFile file (QString::fromStdString (path.string ()));
      if (!file.open (QIODevice::WriteOnly | QIODevice::Truncate) ||
          file.write (content.data (), qsizetype (content.size ())) !=
            qint64 (content.size ()))
        throw std::runtime_error ("Cannot prepare v3 external audit Vault fixture");
    };
    write_source (vault / "Vaultfile.json",
      "{\"name\":\"External Hodarium fixture\",\"node_model_version\":1}\n");
    const std::string original_document= write_xml_v2 (
      tree (DOCUMENT, tree ("unchanged legacy source")));
    write_source (vault / "Math.ath", original_document);
    const std::string group (43, 'A');
    const std::string vault_id= "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa";
    sqlite3* settings= nullptr;
    QCOMPARE (sqlite3_open_v2 ((state / "device.sqlite").c_str (), &settings,
      SQLITE_OPEN_CREATE | SQLITE_OPEN_READWRITE, nullptr), SQLITE_OK);
    QCOMPARE (sqlite3_exec (settings,
      "CREATE TABLE vault_bindings(hodarium TEXT,vault TEXT,root TEXT,"
      "enabled INTEGER)", nullptr, nullptr, nullptr), SQLITE_OK);
    sqlite3_stmt* binding= nullptr;
    QCOMPARE (sqlite3_prepare_v2 (settings,
      "INSERT INTO vault_bindings VALUES(?1,?2,?3,1)", -1,
      &binding, nullptr), SQLITE_OK);
    const std::string path= fs::canonical (vault).generic_string ();
    for (const auto& item: std::vector<std::pair<int,std::string>> {
          {1,group}, {2,vault_id}, {3,path}})
      sqlite3_bind_text (binding, item.first, item.second.data (),
                         int (item.second.size ()), SQLITE_TRANSIENT);
    QCOMPARE (sqlite3_step (binding), SQLITE_DONE);
    sqlite3_finalize (binding);
    sqlite3_close (settings);

    const fs::path journal= state / ("revisions-" + group + ".sqlite");
    {
      athena::hodarium::revision_store store (journal);
      athena::hodarium::revision snapshot;
      snapshot.vault= vault_id;
      snapshot.object= "99999999-9999-4999-8999-999999999999";
      snapshot.origin_member= "member-1";
      snapshot.format= "ath-xml-v2";
      snapshot.semantic_version= 3;
      snapshot.relative_path= "Math.ath";
      tree source (DOCUMENT, compound ("body", tree (DOCUMENT, "authenticated old math")));
      athena::node::set (source[0][0], {snapshot.object, {}});
      snapshot.payload= write_xml_v2 (source);
      QVERIFY (store.capture_saved (snapshot, std::nullopt).has_value ());
    }

    auto snapshot_state= [&] {
      std::map<std::string,std::pair<QByteArray,fs::file_time_type>> files;
      for (const auto& file: fs::directory_iterator (state)) {
        if (!file.is_regular_file ()) continue;
        QFile input (QString::fromStdString (file.path ().string ()));
        if (!input.open (QIODevice::ReadOnly))
          throw std::runtime_error ("Could not snapshot external audit fixture");
        files[file.path ().filename ().string ()]=
          {input.readAll (), fs::last_write_time (file.path ())};
      }
      return files;
    };
    const auto untouched= snapshot_state ();
    const auto audit= audit_external_hodarium_v3 (vault, state);
    QCOMPARE (audit.bindings, std::size_t (1));
    QCOMPARE (audit.enabled_bindings, std::size_t (1));
    QCOMPARE (audit.revision_stores, std::size_t (1));
    QCOMPARE (audit.revisions_verified, std::size_t (1));
    QCOMPARE (audit.v2_document_revisions, std::size_t (1));
    QCOMPARE (audit.applied_revisions, std::size_t (1));
    QCOMPARE (audit.pending_applications, std::size_t (0));
    QCOMPARE (audit.incomplete_receives, std::size_t (0));
    QVERIFY (audit.coordinated_cutover_required);
    QCOMPARE (audit.state_fingerprint.size (), std::size_t (64));
    QCOMPARE (audit_external_hodarium_v3 (vault, state).state_fingerprint,
              audit.state_fingerprint);
    QVERIFY (snapshot_state () == untouched);
    vault_xml_v3_upgrade_options checked_options;
    checked_options.check_only= true;
    checked_options.strict= true;
    checked_options.hodarium_state= state;
    const auto checked_vault= upgrade_vault_xml_v3 (vault, checked_options);
    QCOMPARE (checked_vault.documents, std::size_t (1));
    QVERIFY (checked_vault.hodarium_external.has_value ());
    QCOMPARE (checked_vault.hodarium_external->revisions_verified,
              std::size_t (1));
    QVERIFY (snapshot_state () == untouched);
    QFile unchanged (QString::fromStdString ((vault / "Math.ath").string ()));
    QVERIFY (unchanged.open (QIODevice::ReadOnly));
    QCOMPARE (unchanged.readAll ().toStdString (), original_document);
    const fs::path candidate= base / "disconnected-v3-candidate";
    const auto staged= stage_vault_xml_v3_candidate (
      vault, candidate, true, {}, state);
    QCOMPARE (staged.candidate, candidate);
    QVERIFY (staged.checked.hodarium_external.has_value ());
    QVERIFY (staged.checked.hodarium_external->coordinated_cutover_required);
    QFile receipt (QString::fromStdString (
      (candidate / ".athena/xml-v3-candidate-manifest.json").string ()));
    QVERIFY (receipt.open (QIODevice::ReadOnly));
    const QByteArray manifest= receipt.readAll ();
    QVERIFY (manifest.contains ("\"hodarium_external_state_audited\": true"));
    QVERIFY (manifest.contains (audit.state_fingerprint.c_str ()));
    QVERIFY (manifest.contains ("\"hodarium_existing_bindings\": 1"));
    QVERIFY (manifest.contains (
      "\"hodarium_coordinated_cutover_required\": true"));
    QVERIFY (manifest.contains ("\"eligible_for_atomic_publication\": false"));
    QVERIFY (snapshot_state () == untouched);

    // A separate synchronization process can mutate device.sqlite *after*
    // the first read-only audit but before an isolated candidate completes.
    // The second fingerprint audit must reject this with the same number of
    // bindings/revisions; counts and file mtimes alone are not enough.
    bool changed_during_staging= false;
    const auto mutate_device= [&] (const char* phase, std::size_t,
                                  std::size_t, const std::string&) {
      if (changed_during_staging ||
          std::string_view (phase) != "Write private v3 candidate") return;
      sqlite3* writable= nullptr;
      if (sqlite3_open_v2 ((state / "device.sqlite").c_str (), &writable,
                           SQLITE_OPEN_READWRITE, nullptr) != SQLITE_OK)
        throw std::runtime_error ("Cannot alter isolated device fixture");
      const int result= sqlite3_exec (writable,
        "UPDATE vault_bindings SET enabled=0", nullptr, nullptr, nullptr);
      sqlite3_close (writable);
      if (result != SQLITE_OK)
        throw std::runtime_error ("Cannot write isolated device fixture");
      changed_during_staging= true;
    };
    const fs::path stale_candidate= base / "changed-device-candidate";
    QVERIFY_THROWS_EXCEPTION (std::runtime_error,
      stage_vault_xml_v3_candidate (
        vault, stale_candidate, true, mutate_device, state));
    QVERIFY (changed_during_staging);
    QVERIFY (!fs::exists (stale_candidate));
    QFile original_after_change (QString::fromStdString (
      (vault / "Math.ath").string ()));
    QVERIFY (original_after_change.open (QIODevice::ReadOnly));
    QCOMPARE (original_after_change.readAll ().toStdString (), original_document);

    // A writer may have committed a revision to SQLite WAL without
    // checkpointing it. An immutable connection would skip those pages;
    // accepting it as a complete snapshot would corrupt the migration plan.
    sqlite3* uncheckpointed= nullptr;
    QCOMPARE (sqlite3_open_v2 (journal.c_str (), &uncheckpointed,
      SQLITE_OPEN_READWRITE, nullptr), SQLITE_OK);
    QCOMPARE (sqlite3_exec (uncheckpointed,
      "INSERT INTO incoming VALUES('partial-revision','',x'',0)",
      nullptr, nullptr, nullptr), SQLITE_OK);
    QVERIFY_THROWS_EXCEPTION (std::runtime_error,
      audit_external_hodarium_v3 (vault, state));
    sqlite3_close (uncheckpointed); // Last writer performs the checkpoint.

    sqlite3* modified= nullptr;
    QCOMPARE (sqlite3_open_v2 (journal.c_str (), &modified,
      SQLITE_OPEN_READWRITE, nullptr), SQLITE_OK);
    QCOMPARE (sqlite3_exec (modified,
      "UPDATE revisions SET payload='tampered'", nullptr, nullptr, nullptr),
      SQLITE_OK);
    sqlite3_close (modified);
    QVERIFY_THROWS_EXCEPTION (std::runtime_error,
      audit_external_hodarium_v3 (vault, state));
  }

  void externalHodariumSharedGroupJournalsCountOnce () {
    namespace fs= std::filesystem;
    QTemporaryDir temp;
    QVERIFY (temp.isValid ());
    const fs::path base (temp.path ().toStdString ());
    const fs::path vault= base / "vault", state= base / "external";
    const fs::path other_vault= base / "other-vault";
    fs::create_directory (vault);
    fs::create_directory (state);
    fs::create_directory (other_vault);
    const std::string group (43, 'G');
    const std::string bound_root= fs::canonical (vault).generic_string ();
    const std::string first_vault= "10000000-0000-4000-8000-000000000001";
    const std::string second_vault= "20000000-0000-4000-8000-000000000002";
    sqlite3* settings= nullptr;
    QCOMPARE (sqlite3_open_v2 ((state / "device.sqlite").c_str (), &settings,
      SQLITE_OPEN_CREATE | SQLITE_OPEN_READWRITE, nullptr), SQLITE_OK);
    QCOMPARE (sqlite3_exec (settings,
      "CREATE TABLE vault_bindings(hodarium TEXT,vault TEXT,root TEXT,enabled INTEGER)",
      nullptr, nullptr, nullptr), SQLITE_OK);
    sqlite3_stmt* binding= nullptr;
    QCOMPARE (sqlite3_prepare_v2 (settings,
      "INSERT INTO vault_bindings VALUES(?1,?2,?3,1)", -1,
      &binding, nullptr), SQLITE_OK);
    for (const auto& [vault_id, root_path]:
         std::vector<std::pair<std::string,std::string>> {
           {first_vault, bound_root},
           {second_vault, fs::canonical (other_vault).generic_string ()}}) {
      for (const auto& item: std::vector<std::pair<int,std::string>> {
             {1, group}, {2, vault_id}, {3, root_path}})
        QCOMPARE (sqlite3_bind_text (binding, item.first, item.second.data (),
          int (item.second.size ()), SQLITE_TRANSIENT), SQLITE_OK);
      QCOMPARE (sqlite3_step (binding), SQLITE_DONE);
      QCOMPARE (sqlite3_reset (binding), SQLITE_OK);
    }
    sqlite3_finalize (binding);
    sqlite3_close (settings);

    const fs::path journal_path= state / ("revisions-" + group + ".sqlite");
    std::vector<std::string> saved_ids;
    {
      athena::hodarium::revision_store journal (journal_path);
      for (const auto& vault_id: {first_vault, second_vault}) {
        athena::hodarium::revision value;
        value.vault= vault_id;
        value.object= athena::node::new_id ();
        value.origin_member= "fixture-member";
        value.format= "ath-xml-v2";
        value.semantic_version= 3;
        value.relative_path= "Math.ath";
        tree source (DOCUMENT, compound ("body", tree (DOCUMENT, "legacy")));
        athena::node::set (source[0][0], {value.object, {}});
        value.payload= write_xml_v2 (source);
        const auto saved= journal.capture_saved (value, std::nullopt);
        QVERIFY (saved.has_value ());
        saved_ids.push_back (*saved);
      }
    }
    const auto audited= audit_external_hodarium_v3 (vault, state);
    QCOMPARE (audited.bindings, std::size_t (1));
    QCOMPARE (audited.enabled_bindings, std::size_t (1));
    QCOMPARE (audited.revision_stores, std::size_t (1));
    QCOMPARE (audited.revisions_verified, std::size_t (2));
    QCOMPARE (audited.v2_document_revisions, std::size_t (1));
    QCOMPARE (audited.applied_revisions, std::size_t (1));
    QCOMPARE (audited.incomplete_receives, std::size_t (0));
    QCOMPARE (audited.state_fingerprint,
              audit_external_hodarium_v3 (vault, state).state_fingerprint);
    const auto other_audit= audit_external_hodarium_v3 (other_vault, state);
    QCOMPARE (other_audit.bindings, std::size_t (1));
    QCOMPARE (other_audit.revision_stores, std::size_t (1));
    QCOMPARE (other_audit.revisions_verified, std::size_t (2));
    QCOMPARE (other_audit.v2_document_revisions, std::size_t (1));

    // FK integrity alone cannot detect a fabricated parent index edge: the
    // referenced revision exists but its signed descriptor has no such edge.
    sqlite3* altered= nullptr;
    QCOMPARE (sqlite3_open_v2 (journal_path.c_str (), &altered,
      SQLITE_OPEN_READWRITE, nullptr), SQLITE_OK);
    const std::string extra_edge= "INSERT INTO parents VALUES('" +
      saved_ids[0] + "','" + saved_ids[1] + "')";
    QCOMPARE (sqlite3_exec (altered, extra_edge.c_str (), nullptr, nullptr,
                           nullptr), SQLITE_OK);
    sqlite3_close (altered);
    QVERIFY_THROWS_EXCEPTION (std::runtime_error,
      audit_external_hodarium_v3 (vault, state));
    QCOMPARE (sqlite3_open_v2 (journal_path.c_str (), &altered,
      SQLITE_OPEN_READWRITE, nullptr), SQLITE_OK);
    const std::string undo_edge= "DELETE FROM parents WHERE child='" +
      saved_ids[0] + "' AND parent='" + saved_ids[1] + "'";
    QCOMPARE (sqlite3_exec (altered, undo_edge.c_str (), nullptr, nullptr,
                           nullptr), SQLITE_OK);
    sqlite3_close (altered);
    QCOMPARE (audit_external_hodarium_v3 (vault, state).revisions_verified,
              std::size_t (2));

    // The 'applied' pointer can also reference a genuine revision belonging
    // to another Vault; that is not a valid application history.
    QCOMPARE (sqlite3_open_v2 (journal_path.c_str (), &altered,
      SQLITE_OPEN_READWRITE, nullptr), SQLITE_OK);
    const std::string foreign_applied= "UPDATE applied SET revision='" +
      saved_ids[1] + "' WHERE vault='" + first_vault + "'";
    QCOMPARE (sqlite3_exec (altered, foreign_applied.c_str (), nullptr, nullptr,
                           nullptr), SQLITE_OK);
    sqlite3_close (altered);
    QVERIFY_THROWS_EXCEPTION (std::runtime_error,
      audit_external_hodarium_v3 (vault, state));
    QCOMPARE (sqlite3_open_v2 (journal_path.c_str (), &altered,
      SQLITE_OPEN_READWRITE, nullptr), SQLITE_OK);
    const std::string restore_applied= "UPDATE applied SET revision='" +
      saved_ids[0] + "' WHERE vault='" + first_vault + "'";
    QCOMPARE (sqlite3_exec (altered, restore_applied.c_str (), nullptr, nullptr,
                           nullptr), SQLITE_OK);
    sqlite3_close (altered);
    QCOMPARE (audit_external_hodarium_v3 (vault, state).revisions_verified,
              std::size_t (2));

    // A signed, content-addressed revision is not proof of a correct format
    // label. A second fixture revision can carry canonical XML v3 bytes under
    // the old v2 format tag; immutable preflight must refuse the entire state.
    {
      athena::hodarium::revision_store journal (journal_path);
      athena::hodarium::revision wrongly_labelled;
      wrongly_labelled.vault= first_vault;
      wrongly_labelled.object= athena::node::new_id ();
      wrongly_labelled.origin_member= "fixture-member";
      wrongly_labelled.format= "ath-xml-v2";
      wrongly_labelled.semantic_version= 3;
      wrongly_labelled.relative_path= "WrongEnvelope.ath";
      wrongly_labelled.payload= write_xml_v3 (tree (DOCUMENT, "wrong format"));
      QVERIFY (journal.receive (athena::hodarium::seal_revision (wrongly_labelled)));
    }
    QVERIFY_THROWS_EXCEPTION (std::runtime_error,
      audit_external_hodarium_v3 (vault, state));
  }

  void stagingRefusesExternalModificationAfterPreflight () {
    namespace fs= std::filesystem;
    QTemporaryDir temp;
    QVERIFY (temp.isValid ());
    const fs::path root= fs::path (temp.path ().toStdString ()) / "vault";
    const fs::path candidate= root.parent_path () / "v3-candidate";
    fs::create_directory (root);
    auto write= [] (const fs::path& target, const std::string& bytes) {
      QFile file (QString::fromStdString (target.string ()));
      if (!file.open (QIODevice::WriteOnly | QIODevice::Truncate) ||
          file.write (bytes.data (), qsizetype (bytes.size ())) !=
            qint64 (bytes.size ()))
        throw std::runtime_error ("Cannot write isolated external-change fixture");
    };
    write (root / "Vaultfile.json",
      "{\"name\":\"External change fixture\",\"node_model_version\":1}\n");
    write (root / "Input.ath", write_xml_v2 (tree (DOCUMENT, tree ("original"))));
    bool changed= false;
    const vault_upgrade_progress external_change= [&] (
        const char* phase, std::size_t, std::size_t, const std::string&) {
      if (!changed && std::string_view (phase) == "Stage inventory") {
        changed= true;
        // Simulate a different, uncooperative process changing source bytes
        // after the check's directory lease was released.
        write (root / "Input.ath",
          write_xml_v2 (tree (DOCUMENT, tree ("external modification"))));
      }
    };
    QVERIFY_THROWS_EXCEPTION (std::runtime_error,
      stage_vault_xml_v3_candidate (root, candidate, false, external_change));
    QVERIFY (changed);
    QVERIFY (!fs::exists (candidate));
  }
};

QTEST_GUILESS_MAIN (TestXmlV3)
#include "xml_v3_test.moc"
