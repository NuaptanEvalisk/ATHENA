/******************************************************************************
* MODULE     : clipboard_origin_version_test.cpp
* DESCRIPTION: Explicit clipboard origin and lossless cross-version paste checks
* COPYRIGHT  : (C) 2026 ATHENA contributors
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
******************************************************************************/
#include <QtTest/QtTest>
#include "Xml/clipboard_xml.hpp"
#include "Xml/document_file_codec.hpp"
#include "math_tree_v3.hpp"
#include "node_metadata.hpp"
#include "tree_label.hpp"

namespace xml= athena::document;
namespace node= athena::node;
namespace math= athena::math_v3;
using version= xml::xml_format_version;

namespace {
tree selection (const tree& content) { return tuple ("texmacs", content, "math", "english"); }

tree header_property (const tree& source, const node::property& marker) {
  tree result= copy (source);
  node::metadata metadata;
  metadata.properties.emplace (xml::clipboard_source_version_key, marker);
  node::set (result, metadata);
  return result;
}

tree rich_property (const tree& rich) {
  tree result ("owner");
  node::metadata metadata;
  metadata.id= "11111111-1111-4111-8111-111111111111";
  metadata.properties.emplace ("test:rich", node::property (node::property::dictionary {
    {"nested", node::property (node::property::list {
      node::property (node::rich_text {rich})})}}));
  metadata.properties.emplace ("test:target", node::property (node::reference {
    "22222222-2222-4222-8222-222222222222"}));
  node::set (result, metadata);
  return result;
}
} // namespace

class ClipboardOriginVersionTest: public QObject {
  Q_OBJECT
private slots:
  void initTestCase () { init_standard_tree_labels (); }

  void explicitVersionsPreservePlainAndBinaryPayloads () {
    const tree raw (RAW_DATA, tree (string ("\0\xff\xc0\x80", 4)));
    const tree source= selection (tree (CONCAT, "plain", raw));
    for (auto origin: {version::v1, version::v2, version::v3}) {
      const tree stamped= xml::clipboard_with_version (source, origin);
      QVERIFY (xml::clipboard_version (stamped) == origin);
      QVERIFY (!xml::clipboard_version (source));
      QCOMPARE (xml::clipboard_without_version (stamped), source);
      for (const auto& bytes: {xml::write_clipboard_xml (source, origin),
                               xml::write_clipboard_xml (stamped)}) {
        QCOMPARE (xml::inspect_xml_version (bytes), origin);
        QVERIFY (bytes.find (xml::clipboard_source_version_key) == std::string::npos);
        QCOMPARE (xml::read_clipboard_xml (bytes), stamped);
        for (auto target: {version::v1, version::v2, version::v3})
          xml::validate_clipboard_paste (xml::read_clipboard_xml (bytes), target);
      }
    }
    QCOMPARE (xml::inspect_xml_version (xml::write_clipboard_xml (source)), version::v1);
  }

  void markerHelpersAreDetachedAndKeepMoveShape () {
    tree source= selection (tree (FRAC, "x", "2"));
    source << tuple ("athena-move-v1", "33333333-3333-4333-8333-333333333333");
    tree stamped= xml::clipboard_with_version (source, version::v3);
    QCOMPARE (N(stamped), 5);
    QCOMPARE (stamped[4], source[4]);
    QCOMPARE (xml::read_clipboard_xml (xml::write_clipboard_xml (stamped)), stamped);
    xml::validate_clipboard_paste (stamped, version::v2);
    tree stripped= xml::clipboard_without_version (stamped);
    QCOMPARE (stripped, source);
    stripped[1][0]= "changed";
    QCOMPARE (source[1][0], tree ("x"));
    QCOMPARE (stamped[1][0], tree ("x"));
  }

  void allNativeLabelsRequireNativeOriginAndTarget () {
    const tree_label labels[]= {DELIMITED, MATH_MIDDLE, BIG_OPERATOR, MATH_SCRIPTS,
      MATH_ACCENT, MATH_ANNOTATION, MATH_LONG_ARROW, MATH_GRID, MATH_BLOCK,
      MATH_VARIANT, MATH_SYMBOL};
    for (auto label: labels) {
      // Deliberately shape-agnostic: legacy generic XML can encode these names,
      // but doing so must not confer a legacy semantic origin on any of them.
      const tree source= selection (tree (label, 0));
      QVERIFY_THROWS_EXCEPTION (xml::codec_exception, xml::write_clipboard_xml (source));
      QVERIFY_THROWS_EXCEPTION (xml::codec_exception,
        xml::write_clipboard_xml (source, version::v2));
      QVERIFY_THROWS_EXCEPTION (xml::codec_exception,
        xml::validate_clipboard_paste (source, version::v3));
      QVERIFY_THROWS_EXCEPTION (xml::codec_exception,
        xml::read_clipboard_xml (xml::write_xml_v2 (source, xml::xml_kind::fragment)));
    }
    const tree source= selection (math::delimited ("(", ")", 'a',
      math::scripts ("x", "", "", "i", "", 'r')));
    const tree stamped= xml::clipboard_with_version (source, version::v3);
    QCOMPARE (xml::read_clipboard_xml (xml::write_clipboard_xml (stamped)), stamped);
    xml::validate_clipboard_paste (stamped, version::v3);
    for (auto target: {version::v1, version::v2})
      QVERIFY_THROWS_EXCEPTION (xml::codec_exception,
        xml::validate_clipboard_paste (stamped, target));
    QVERIFY_THROWS_EXCEPTION (xml::codec_exception,
      xml::write_clipboard_xml (xml::clipboard_without_version (stamped)));
    const tree old= xml::clipboard_with_version (selection (tree (AROUND, "(", "x", ")")), version::v2);
    xml::validate_clipboard_paste (old, version::v2);
    QVERIFY_THROWS_EXCEPTION (xml::codec_exception, xml::validate_clipboard_paste (old, version::v3));
  }

  void sharedTypedMetadataCrossesOnlyLosslessVersions () {
    const tree source= selection (rich_property (tree (FRAC, "a", "b")));
    QCOMPARE (xml::inspect_xml_version (xml::write_clipboard_xml (source)), version::v2);
    for (auto origin: {version::v2, version::v3}) {
      const tree stamped= xml::clipboard_with_version (source, origin);
      QCOMPARE (xml::clipboard_without_version (xml::read_clipboard_xml (
        xml::write_clipboard_xml (stamped))), source);
      xml::validate_clipboard_paste (stamped, version::v2);
      xml::validate_clipboard_paste (stamped, version::v3);
      QVERIFY_THROWS_EXCEPTION (xml::codec_exception,
        xml::validate_clipboard_paste (stamped, version::v1));
    }
    const tree false_origin= xml::clipboard_with_version (source, version::v1);
    QVERIFY_THROWS_EXCEPTION (xml::codec_exception,
      xml::validate_clipboard_paste (false_origin, version::v3));
    QCOMPARE (source[1], rich_property (tree (FRAC, "a", "b")));
  }

  void nativeContentInsideRichListsAndDictionariesIsChecked () {
    const tree source= selection (rich_property (math::scripts ("x", "", "", "i", "", 'r')));
    QVERIFY_THROWS_EXCEPTION (xml::codec_exception, xml::write_clipboard_xml (source));
    QVERIFY_THROWS_EXCEPTION (xml::codec_exception,
      xml::read_clipboard_xml (xml::write_xml_v2 (source, xml::xml_kind::fragment)));
    const tree stamped= xml::clipboard_with_version (source, version::v3);
    QCOMPARE (xml::read_clipboard_xml (xml::write_clipboard_xml (stamped)), stamped);
    xml::validate_clipboard_paste (stamped, version::v3);
    QVERIFY_THROWS_EXCEPTION (xml::codec_exception,
      xml::validate_clipboard_paste (stamped, version::v2));
  }

  void rowAndCellMathPropertiesCannotLeakIntoLegacy () {
    tree row (ROW, tree (CELL, "x"));
    math::set_row_numbering (row, math::row_numbering::numbered);
    math::set_row_tag (row, tree (""));
    tree cell (CELL, "x");
    node::metadata metadata;
    metadata.properties.emplace ("math:cell-format", node::property (node::property::dictionary {
      {"align", node::property (std::string ("c"))}}));
    node::set (cell, metadata);
    for (const tree& content: {row, cell, rich_property (row), rich_property (cell)}) {
      const tree source= selection (content);
      QVERIFY_THROWS_EXCEPTION (xml::codec_exception, xml::write_clipboard_xml (source));
      QVERIFY_THROWS_EXCEPTION (xml::codec_exception,
        xml::read_clipboard_xml (xml::write_xml_v2 (source, xml::xml_kind::fragment)));
      const tree stamped= xml::clipboard_with_version (source, version::v3);
      QCOMPARE (xml::read_clipboard_xml (xml::write_clipboard_xml (stamped)), stamped);
      xml::validate_clipboard_paste (stamped, version::v3);
      QVERIFY_THROWS_EXCEPTION (xml::codec_exception,
        xml::validate_clipboard_paste (stamped, version::v2));
    }
  }

  void malformedOrConflictingMarkersFailClosed () {
    const tree plain= selection ("plain");
    for (const node::property& marker: {
        node::property (std::string ("3")), node::property (true), node::property (3.0),
        node::property (std::int64_t (0)), node::property (std::int64_t (4))}) {
      const tree source= header_property (plain, marker);
      QVERIFY_THROWS_EXCEPTION (xml::codec_exception, xml::clipboard_version (source));
      QVERIFY_THROWS_EXCEPTION (xml::codec_exception, xml::clipboard_without_version (source));
      QVERIFY_THROWS_EXCEPTION (xml::codec_exception, xml::write_clipboard_xml (source));
      QVERIFY_THROWS_EXCEPTION (xml::codec_exception, xml::validate_clipboard_paste (source, version::v3));
      QVERIFY_THROWS_EXCEPTION (xml::codec_exception,
        xml::read_clipboard_xml (xml::write_xml_v2 (source, xml::xml_kind::fragment)));
    }
    const tree native= xml::clipboard_with_version (plain, version::v3);
    QVERIFY_THROWS_EXCEPTION (xml::codec_exception, xml::write_clipboard_xml (native, version::v2));
    QVERIFY_THROWS_EXCEPTION (xml::codec_exception, xml::clipboard_with_version (native, version::v2));
    QVERIFY_THROWS_EXCEPTION (xml::codec_exception,
      xml::read_clipboard_xml (xml::write_xml_v2 (native, xml::xml_kind::fragment)));
    QVERIFY_THROWS_EXCEPTION (xml::codec_exception,
      xml::read_clipboard_xml (xml::write_xml_v3 (native, xml::xml_kind::fragment)));
    QVERIFY_THROWS_EXCEPTION (xml::codec_exception,
      xml::write_clipboard_xml (plain, static_cast<version> (99)));
    QVERIFY_THROWS_EXCEPTION (xml::codec_exception,
      xml::validate_clipboard_paste (plain, static_cast<version> (99)));
    tree extra_header= copy (native);
    node::edit (extra_header)->properties.emplace ("test:unknown", node::property (true));
    QVERIFY_THROWS_EXCEPTION (xml::codec_exception, xml::clipboard_version (extra_header));
  }

  void headerIdentityAndUnrecognizedPropertiesAreRejected () {
    const tree plain= selection ("content");
    for (bool identity: {false, true}) {
      tree source= copy (plain);
      node::metadata metadata;
      if (identity) metadata.id= "11111111-1111-4111-8111-111111111111";
      else metadata.properties.emplace ("test:unknown", node::property (true));
      node::set (source, metadata);
      QVERIFY_THROWS_EXCEPTION (xml::codec_exception, xml::write_clipboard_xml (source));
      QVERIFY_THROWS_EXCEPTION (xml::codec_exception, xml::clipboard_without_version (source));
      QVERIFY_THROWS_EXCEPTION (xml::codec_exception,
        xml::read_clipboard_xml (xml::write_xml_v2 (source, xml::xml_kind::fragment)));
      source= copy (plain);
      node::set (source[2], metadata);
      QVERIFY_THROWS_EXCEPTION (xml::codec_exception, xml::write_clipboard_xml (source));
    }
    tree content ("origin is not a source property");
    node::metadata metadata;
    metadata.properties.emplace (xml::clipboard_source_version_key, node::property (std::int64_t (3)));
    node::set (content, metadata);
    const tree nested= selection (rich_property (content));
    QVERIFY_THROWS_EXCEPTION (xml::codec_exception, xml::write_clipboard_xml (nested, version::v3));
  }
};

QTEST_APPLESS_MAIN (ClipboardOriginVersionTest)
#include "clipboard_origin_version_test.moc"
