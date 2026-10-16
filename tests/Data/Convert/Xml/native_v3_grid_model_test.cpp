/******************************************************************************
* MODULE     : native_v3_grid_model_test.cpp
* DESCRIPTION: Canonical MathGrid row policy, rich tags and persistent identity
* COPYRIGHT  : (C) 2026 ATHENA contributors
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
******************************************************************************/
#include <QtTest/QtTest>
#include "Xml/athena_document_xml.hpp"
#include "Xml/math_v2_to_v3.hpp"
#include "drd_std.hpp"
#include "math_tree_v3.hpp"
#include "node_metadata.hpp"

using namespace athena::document;
using namespace athena::math_v3;
using athena::node::property;

namespace {

tree grid (const tree& row, const char* kind= "align") {
  tree result (MATH_GRID, row);
  athena::node::metadata meta;
  meta.properties.emplace ("math:grid-kind", property (std::string (kind)));
  athena::node::set (result, meta);
  return result;
}

std::string row_xml (const std::string& properties= "",
                     const std::string& attributes= "") {
  return "<n t='row'" + attributes + ">" +
    (properties.empty () ? "" : "<x>" + properties + "</x>") +
    "<n t='cell'><t><v>x</v></t></n></n>";
}

std::string grid_xml (const std::string& row, const std::string& kind= "align") {
  return "<a v='3' k='t'><n t='math-grid' k='" + kind + "'>" + row + "</n></a>";
}

} // namespace

class TestNativeV3GridModel: public QObject {
  Q_OBJECT
  using property= athena::node::property;
private slots:
  void initTestCase () { init_std_drd (); }

  void missingPolicyIsCanonicalInherit () {
    const tree source= read_xml_v3 (grid_xml (row_xml ()), xml_kind::fragment);
    QVERIFY (valid_native_math (source));
    QVERIFY (valid_grid_row (source[0]));
    QVERIFY (get_row_numbering (source[0]) == row_numbering::inherit);
    QVERIFY (!get_row_tag (source[0]).has_value ());
    QVERIFY (athena::node::get (source[0]) == nullptr);
    const auto xml= write_xml_v3 (source, xml_kind::fragment);
    QVERIFY (xml.find (row_numbering_key) == std::string::npos);
    QVERIFY (xml.find (row_tag_key) == std::string::npos);
    QCOMPARE (write_xml_v3 (read_xml_v3 (xml, xml_kind::fragment),
                            xml_kind::fragment), xml);
    tree row= copy (source[0]);
    set_row_numbering (row, row_numbering::numbered);
    set_row_numbering (row, row_numbering::inherit);
    QVERIFY (athena::node::get (row) == nullptr);
    QCOMPARE (write_xml_v3 (grid (row), xml_kind::fragment), xml);
  }

  void explicitPoliciesUseGenericMetadata () {
    for (const auto policy: {row_numbering::numbered, row_numbering::unnumbered}) {
      tree row (ROW, tree (CELL, tree ("x")));
      set_row_numbering (row, policy);
      const std::string spelling=
        policy == row_numbering::numbered ? "numbered" : "unnumbered";
      const auto xml= write_xml_v3 (grid (row), xml_kind::fragment);
      QVERIFY (xml.find ("<n t=\"row\"><x><p n=\"math:row-numbering\" t=\"s\">" +
                          spelling + "</p></x>") != std::string::npos);
      const auto restored= read_xml_v3 (xml, xml_kind::fragment);
      QVERIFY (get_row_numbering (restored[0]) == policy);
      QVERIFY (athena::node::equal_metadata (restored[0], row));
      QCOMPARE (write_xml_v3 (restored, xml_kind::fragment), xml);
    }
  }

  void gridKindControlsRowOverrides () {
    for (const char* kind: {"eqnarray", "align", "stack", "matrix", "cases", "generic"}) {
      const bool allows_overrides= std::string (kind) == "eqnarray" ||
                                  std::string (kind) == "align" ||
                                  std::string (kind) == "stack";
      tree row (ROW, tree (CELL, tree ("x")));
      QVERIFY (valid_native_math (grid (row, kind)));
      const auto plain= write_xml_v3 (grid (row, kind), xml_kind::fragment);
      QVERIFY (read_xml_v3 (plain, xml_kind::fragment) == grid (row, kind));
      for (int override= 0; override < 3; ++override) {
        set_row_numbering (row, row_numbering::inherit);
        clear_row_tag (row);
        std::string properties;
        if (override == 2) {
          set_row_tag (row, tree ("A"));
          properties= "<p n='math:row-tag' t='h'><t><v>A</v></t></p>";
        }
        else {
          const auto policy= override == 0 ? row_numbering::numbered :
                                            row_numbering::unnumbered;
          set_row_numbering (row, policy);
          properties= std::string ("<p n='math:row-numbering' t='s'>") +
            (override == 0 ? "numbered" : "unnumbered") + "</p>";
        }
        const tree source= grid (row, kind);
        QCOMPARE (valid_native_math (source), allows_overrides);
        if (allows_overrides) {
          const auto xml= write_xml_v3 (source, xml_kind::fragment);
          QVERIFY (read_xml_v3 (xml, xml_kind::fragment) == source);
        }
        else {
          QVERIFY_EXCEPTION_THROWN (write_xml_v3 (source, xml_kind::fragment), codec_exception);
          QVERIFY_EXCEPTION_THROWN (read_xml_v3 (grid_xml (row_xml (properties), kind),
                                                 xml_kind::fragment), codec_exception);
        }
      }
    }
  }

  void codecDoesNotResolveBlockOrNestedInheritance () {
    for (const bool numbered: {false, true}) {
      const tree nested= grid (tree (ROW, tree (CELL, tree ("x"))));
      tree block (MATH_BLOCK, grid (tree (ROW, tree (CELL, nested))));
      athena::node::metadata meta;
      meta.properties.emplace ("math:block-numbered", property (numbered));
      athena::node::set (block, meta);
      const auto xml= write_xml_v3 (block, xml_kind::fragment);
      QVERIFY (xml.find (row_numbering_key) == std::string::npos);
      const auto restored= read_xml_v3 (xml, xml_kind::fragment);
      QVERIFY (get_row_numbering (restored[0][0]) == row_numbering::inherit);
      QVERIFY (get_row_numbering (restored[0][0][0][0][0]) == row_numbering::inherit);
      QVERIFY (restored == block);
    }
  }

  void richTagRetainsNativeTreeIdentityAndReferences () {
    tree row (ROW, tree (CELL, tree (CONCAT, tree ("x"), tree (LABEL, "eq:row"))));
    athena::node::metadata row_meta;
    row_meta.id= athena::node::new_id ();
    row_meta.properties.emplace ("reviewed", property (true));
    athena::node::set (row, row_meta);
    tree tag= scripts (tree ("A"), tree (""), tree (""), tree ("i"), tree ("2"));
    athena::node::metadata tag_meta= *athena::node::get (tag);
    tag_meta.id= athena::node::new_id ();
    tag_meta.properties.emplace ("row-ref", property (athena::node::reference {row_meta.id}));
    athena::node::set (tag, tag_meta);
    athena::node::metadata base_meta;
    base_meta.id= athena::node::new_id ();
    base_meta.properties.emplace ("annotation", property (std::string ("tag-base")));
    athena::node::set (tag[0], base_meta);
    set_row_tag (row, tag);
    set_row_numbering (row, row_numbering::numbered);
    tree source (DOCUMENT, grid (row));
    athena::node::metadata document_meta;
    document_meta.properties.emplace ("equation-ref",
      property (athena::node::reference {row_meta.id}));
    athena::node::set (source, document_meta);

    const auto xml= write_xml_v3 (source);
    QVERIFY (xml.find ("<p n=\"math:row-tag\" t=\"h\"><n t=\"scripts\" p=\"a\"") !=
             std::string::npos);
    const auto restored= read_xml_v3 (xml);
    const auto restored_tag= get_row_tag (restored[0][0]);
    QVERIFY (restored_tag.has_value ());
    QVERIFY (*restored_tag == tag);
    QVERIFY (athena::node::equal_metadata (*restored_tag, tag));
    QVERIFY (athena::node::equal_metadata ((*restored_tag)[0], tag[0]));
    QVERIFY (athena::node::equal_metadata (restored[0][0], row));
    QVERIFY (athena::node::equal_metadata (restored, source));
    QVERIFY (restored[0][0][0][0][1] == tree (LABEL, "eq:row"));
    QCOMPARE (athena::node::id (restored[0][0]), row_meta.id);
    QCOMPARE (write_xml_v3 (restored), xml);

    set_row_numbering (row, row_numbering::inherit);
    QVERIFY (get_row_tag (row).has_value ());
    QVERIFY (!athena::node::get (row)->properties.count (row_numbering_key));
    clear_row_tag (row);
    tree original= copy (row);
    athena::node::set (original, row_meta);
    QVERIFY (athena::node::equal_metadata (row, original));
    QCOMPARE (athena::node::id (row), row_meta.id);
  }

  void tagIsOptionalIndependentAndDetached () {
    for (const auto policy: {row_numbering::inherit, row_numbering::numbered,
                             row_numbering::unnumbered}) {
      tree row (ROW, tree (CELL, tree ("x")));
      set_row_numbering (row, policy);
      set_row_tag (row, tree (""));
      QVERIFY (get_row_tag (row).has_value ());
      QVERIFY (*get_row_tag (row) == tree (""));
      const auto xml= write_xml_v3 (grid (row), xml_kind::fragment);
      const auto restored= read_xml_v3 (xml, xml_kind::fragment);
      QVERIFY (get_row_numbering (restored[0]) == policy);
      QVERIFY (get_row_tag (restored[0]).has_value ());
      clear_row_tag (row);
      QVERIFY (!get_row_tag (row).has_value ());
      QVERIFY (get_row_numbering (row) == policy);
    }
    tree row (ROW, tree (CELL, tree ("x")));
    tree tag (CONCAT, tree ("A"), tree ("B"));
    set_row_tag (row, tag);
    tag[0]= tree ("changed input");
    auto retrieved= get_row_tag (row);
    QVERIFY ((*retrieved)[0] == tree ("A"));
    (*retrieved)[0]= tree ("changed result");
    QVERIFY ((*get_row_tag (row))[0] == tree ("A"));
  }

  void invalidRowProperties_data () {
    QTest::addColumn<QString> ("properties");
    QTest::newRow ("explicit-inherit") << QString (
      "<p n='math:row-numbering' t='s'>inherit</p>");
    QTest::newRow ("unknown-policy") << QString (
      "<p n='math:row-numbering' t='s'>auto</p>");
    QTest::newRow ("empty-policy") << QString (
      "<p n='math:row-numbering' t='s'></p>");
    QTest::newRow ("case-sensitive") << QString (
      "<p n='math:row-numbering' t='s'>Numbered</p>");
    QTest::newRow ("boolean-policy") << QString (
      "<p n='math:row-numbering' t='b'>true</p>");
    QTest::newRow ("integer-policy") << QString (
      "<p n='math:row-numbering' t='i'>1</p>");
    QTest::newRow ("rich-policy") << QString (
      "<p n='math:row-numbering' t='h'><t><v>numbered</v></t></p>");
    QTest::newRow ("plaintext-tag") << QString ("<p n='math:row-tag' t='s'>A</p>");
    QTest::newRow ("boolean-tag") << QString ("<p n='math:row-tag' t='b'>false</p>");
    QTest::newRow ("list-tag") << QString ("<p n='math:row-tag' t='l'/>");
  }

  void invalidRowProperties () {
    QFETCH (QString, properties);
    const auto row_source= row_xml (properties.toStdString ());
    QVERIFY_EXCEPTION_THROWN (read_xml_v3 (grid_xml (row_source), xml_kind::fragment),
                              codec_exception);
    // The identical ROW is generic metadata in a text TABLE, not math policy.
    const auto table_xml= "<a v='3' k='t'><n t='table'>" + row_source + "</n></a>";
    const auto table= read_xml_v3 (table_xml, xml_kind::fragment);
    QVERIFY (valid_native_math (table));
    QVERIFY (valid_native_math (table[0]));
    QVERIFY (!valid_grid_row (table[0]));
    const auto text_xml= write_xml_v3 (table, xml_kind::fragment);
    const auto restored= read_xml_v3 (text_xml, xml_kind::fragment);
    QVERIFY (athena::node::equal_metadata (restored[0], table[0]));
    QVERIFY_EXCEPTION_THROWN (write_xml_v3 (grid (table[0]), xml_kind::fragment),
                              codec_exception);
    QVERIFY_EXCEPTION_THROWN (get_row_numbering (table[0]), std::invalid_argument);
    QVERIFY_EXCEPTION_THROWN (get_row_tag (table[0]), std::invalid_argument);
  }

  void rejectAlternateXmlSpellings () {
    for (const std::string properties: {
      "<p n='math:row-numbering' t='s'>numbered</p>"
      "<p n='math:row-numbering' t='s'>unnumbered</p>",
      "<p n='math:row-tag' t='h'><t><v>A</v></t></p>"
      "<p n='math:row-tag' t='h'><t><v>B</v></t></p>",
      "<p n='math:row-tag' t='h'/>",
      "<p n='math:row-tag' t='h'><t><v>A</v></t><t><v>B</v></t></p>"
    }) QVERIFY_EXCEPTION_THROWN (
      read_xml_v3 (grid_xml (row_xml (properties)), xml_kind::fragment), codec_exception);
    for (const std::string attribute: {" b='1'", " p='numbered'",
                                        " numbering='numbered'", " tag='A'"})
      QVERIFY_EXCEPTION_THROWN (read_xml_v3 (grid_xml (row_xml ("", attribute)),
                                             xml_kind::fragment), codec_exception);
  }

  void rowShapeAndCellFormatRemainValidated () {
    for (tree row: {tree ("x"), tree (ROW), tree (ROW, tree ("x")),
                    tree (ROW, tree (CELL)),
                    tree (ROW, tree (CELL, tree ("x"), tree ("y")))}) {
      QVERIFY (!valid_grid_row (row));
      QVERIFY (!valid_native_math (grid (row)));
      QVERIFY_EXCEPTION_THROWN (set_row_numbering (row, row_numbering::numbered),
                                std::invalid_argument);
      QVERIFY_EXCEPTION_THROWN (set_row_tag (row, tree ("A")), std::invalid_argument);
      QVERIFY_EXCEPTION_THROWN (clear_row_tag (row), std::invalid_argument);
    }
    tree row (ROW, tree (CELL, tree ("x")));
    const auto before= write_xml_v3 (grid (row), xml_kind::fragment);
    QVERIFY_EXCEPTION_THROWN (set_row_numbering (row, static_cast<row_numbering> (42)),
                              std::invalid_argument);
    QCOMPARE (write_xml_v3 (grid (row), xml_kind::fragment), before);
    athena::node::metadata cell_meta;
    cell_meta.properties.emplace ("math:cell-format", property (property::dictionary {
      {"align", property (std::string ("r"))}
    }));
    athena::node::set (row[0], cell_meta);
    QVERIFY (valid_grid_row (row));
    cell_meta.properties["math:cell-format"]= property (property::dictionary {
      {"align", property (true)}
    });
    athena::node::set (row[0], cell_meta);
    QVERIFY (!valid_grid_row (row));
    QVERIFY_EXCEPTION_THROWN (write_xml_v3 (grid (row), xml_kind::fragment), codec_exception);
  }

  void richTagsObeyNativeGrammarIdentityAndLimits () {
    tree row (ROW, tree (CELL, tree ("x")));
    set_row_tag (row, tree (MATH_SCRIPTS, tree ("x")));
    QVERIFY_EXCEPTION_THROWN (write_xml_v3 (grid (row), xml_kind::fragment), codec_exception);
    QVERIFY_EXCEPTION_THROWN (read_xml_v3 (grid_xml (row_xml (
      "<p n='math:row-tag' t='h'><n t='scripts' p='a'><t><v>x</v></t></n></p>")),
      xml_kind::fragment), codec_exception);
    clear_row_tag (row);
    codec_limits limits;
    limits.nodes= 4;
    const auto plain= write_xml_v3 (grid (row), xml_kind::fragment, limits);
    QVERIFY (read_xml_v3 (plain, xml_kind::fragment, limits) == grid (row));
    set_row_tag (row, tree ("A"));
    const auto tagged= write_xml_v3 (grid (row), xml_kind::fragment);
    QVERIFY_EXCEPTION_THROWN (write_xml_v3 (grid (row), xml_kind::fragment, limits),
                              codec_exception);
    QVERIFY_EXCEPTION_THROWN (read_xml_v3 (tagged, xml_kind::fragment, limits),
                              codec_exception);

    athena::node::metadata meta;
    meta.id= athena::node::new_id ();
    athena::node::set (row, meta);
    tree tag ("A");
    athena::node::set (tag, meta);
    set_row_tag (row, tag);
    QVERIFY_EXCEPTION_THROWN (write_xml_v3 (grid (row), xml_kind::fragment), identity_conflict);
    QVERIFY_EXCEPTION_THROWN (read_xml_v3 (grid_xml (row_xml (
      "<p n='math:row-tag' t='h'><t i='" + meta.id + "'><v>A</v></t></p>",
      " i='" + meta.id + "'")), xml_kind::fragment), identity_conflict);
  }

  void migratedPropertyFreeRowsStayValid () {
    tree row (ROW, tree (CELL, tree ("x")), tree (CELL, tree ("=")),
              tree (CELL, tree ("y")));
    athena::node::metadata meta;
    meta.id= athena::node::new_id ();
    athena::node::set (row, meta);
    tree source (DOCUMENT, compound ("eqnarray", tree (DOCUMENT,
      tree (TFORMAT, tree (TABLE, row)))));
    const auto migrated= migrate_math_v2_tree (source, true);
    QVERIFY (migrated.warnings.empty ());
    const tree& math_grid= migrated.document[0][0];
    QVERIFY (is_func (math_grid, MATH_GRID));
    QVERIFY (valid_native_math (math_grid));
    QVERIFY (get_row_numbering (math_grid[0]) == row_numbering::inherit);
    QVERIFY (!get_row_tag (math_grid[0]).has_value ());
    QCOMPARE (athena::node::id (math_grid[0]), meta.id);
    const auto xml= write_xml_v3 (migrated.document);
    QVERIFY (xml.find (row_numbering_key) == std::string::npos);
    QVERIFY (read_xml_v3 (xml) == migrated.document);
  }
};

QTEST_GUILESS_MAIN (TestNativeV3GridModel)
#include "native_v3_grid_model_test.moc"
