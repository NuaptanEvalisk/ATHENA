/******************************************************************************
* MODULE     : native_v3_grid_migration_test.cpp
* DESCRIPTION: Offline conversion of shipped equation-row markers to XML v3
* COPYRIGHT  : (C) 2026 ATHENA contributors
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
******************************************************************************/
#include <QtTest/QtTest>
#include "Xml/athena_document_xml.hpp"
#include "Xml/math_v2_to_v3.hpp"
#include "Data/NodeContracts/execution_state.hpp"
#include "math_tree_v3.hpp"
#include "node_metadata.hpp"

using namespace athena::document;
using namespace athena::math_v3;
using athena::node::property;

namespace {

tree row_with (const tree& content) {
  return tree (ROW, tree (CELL, tree ("x")), tree (CELL, tree ("=")),
                    tree (CELL, content));
}

tree display (const char* name, const tree& table) {
  return compound (name, tree (DOCUMENT, tree (TFORMAT, table)));
}

tree source_with (const tree& row, const char* name= "eqnarray") {
  return tree (DOCUMENT, display (name, tree (TABLE, row)));
}

bool has_marker (const tree& value) {
  if (is_compound (value, "eq-number") || is_compound (value, "no-number") ||
      is_compound (value, "eq-lab")) return true;
  if (is_compound (value))
    for (int i=0; i<N (value); ++i) if (has_marker (value[i])) return true;
  return false;
}

const tree* labeled_node (const tree& value, const std::string& id) {
  if (athena::node::id (value) == id) return &value;
  if (is_compound (value))
    for (int i=0; i<N (value); ++i)
      if (const auto* found= labeled_node (value[i], id)) return found;
  return nullptr;
}

} // namespace

class TestNativeV3GridMigration: public QObject {
  Q_OBJECT
  using property= athena::node::property;
private slots:
  void initTestCase () { athena::contracts::initialize_builtins (); }

  void shippedGridNamesDoNotImplicitlyNumber () {
    // env-math.ats aliases both unstarred names to starred environments.
    // table_commands.cpp inserts eq-number explicitly at the final cell.
    for (const char* name: {"eqnarray", "eqnarray*", "align", "align*"}) {
      tree table (TABLE,
        row_with (tree ("y")),
        row_with (tree (CONCAT, tree ("y"), compound ("eq-number"))),
        row_with (tree (CONCAT, tree ("y"), compound ("no-number"))),
        row_with (tree (CONCAT, tree ("y"), compound ("eq-lab", tree ("")))));
      const tree source (DOCUMENT, display (name, table));
      const auto result= migrate_math_v2_tree (read_xml_v2 (write_xml_v2 (source)), true);
      QVERIFY (result.warnings.empty ());
      QVERIFY (result.removed_identities.empty ());
      const tree& block= result.document[0];
      const tree& grid= block[0];
      QVERIFY (is_func (block, MATH_BLOCK, 1));
      QVERIFY (valid_native_math (grid));
      QVERIFY (!std::get<bool> (athena::node::get (block)->properties.at (
        "math:block-numbered").data));
      QVERIFY (get_row_numbering (grid[0]) == row_numbering::inherit);
      QVERIFY (get_row_numbering (grid[1]) == row_numbering::numbered);
      QVERIFY (get_row_numbering (grid[2]) == row_numbering::unnumbered);
      QVERIFY (get_row_numbering (grid[3]) == row_numbering::inherit);
      QVERIFY (get_row_tag (grid[3]).has_value ());
      QVERIFY (*get_row_tag (grid[3]) == tree (""));
      QVERIFY (!has_marker (result.document));
      const auto xml= write_xml_v3 (result.document);
      QVERIFY (read_xml_v3 (xml) == result.document);
    }
  }

  void preserveRichTagRowLabelsAndReferences () {
    tree label (LABEL, "eq:stable");
    athena::node::metadata label_meta;
    label_meta.id= athena::node::new_id ();
    athena::node::set (label, label_meta);
    tree tag (CONCAT, tree ("Appendix "), compound ("math",
      tree (CONCAT, tree ("x"), tree (RSUB, tree ("i")))));
    athena::node::metadata tag_meta;
    tag_meta.id= athena::node::new_id ();
    tag_meta.properties.emplace ("label-ref", property (athena::node::reference {label_meta.id}));
    athena::node::set (tag, tag_meta);
    tree row= row_with (tree (CONCAT, tree ("y"), compound ("eq-lab", tag), label));
    athena::node::metadata row_meta;
    row_meta.id= athena::node::new_id ();
    row_meta.properties.emplace ("reviewed", property (true));
    athena::node::set (row, row_meta);
    tree source= source_with (row);
    athena::node::metadata root_meta;
    root_meta.properties.emplace ("row-ref", property (athena::node::reference {row_meta.id}));
    athena::node::set (source, root_meta);
    const auto source_xml= write_xml_v2 (source);
    const auto result= migrate_math_v2_tree (source, true);
    QVERIFY (result.removed_identities.empty ());
    QCOMPARE (write_xml_v2 (source), source_xml);
    const tree& converted_row= result.document[0][0][0];
    QCOMPARE (athena::node::id (converted_row), row_meta.id);
    QVERIFY (athena::node::equal_metadata (result.document, source));
    const auto converted_tag= get_row_tag (converted_row);
    QVERIFY (converted_tag.has_value ());
    QCOMPARE (athena::node::id (*converted_tag), tag_meta.id);
    QVERIFY (athena::node::equal_metadata (*converted_tag, tag));
    const auto* kept_label= labeled_node (converted_row, label_meta.id);
    QVERIFY (kept_label != nullptr);
    QVERIFY (is_atomic ((*kept_label)[0]));
    QCOMPARE ((*kept_label)[0]->label, string ("eq:stable"));
    QVERIFY (*kept_label == label);
    const auto expected_tag= migrate_math_v2_tree (tag, true).document;
    QVERIFY (*converted_tag == expected_tag);
    const auto xml= write_xml_v3 (result.document);
    QVERIFY (read_xml_v3 (xml) == result.document);
  }

  void declaredIdentityStringAndUrlFieldsAreNotMath () {
    for (const tree source_node: {
      tree (LABEL, tree ("eq:stable")),
      tree (REFERENCE, tree ("eq:stable")),
      tree (PAGEREF, tree ("eq:stable")),
      tree (LABEL, tree (CONCAT, tree ("eq:"), tree ("stable"))),
      tree (HLINK, tree (CONCAT, tree ("x"), tree (RSUB, tree ("i"))),
                    tree ("https://example.test/equation#eq:stable")),
      tree (WRITE, tree ("fixture:channel"),
                   tree (CONCAT, tree ("x"), tree (RSUB, tree ("i"))))
    }) {
      tree node= copy (source_node);
      const int field= is_func (node, HLINK) ? 1 : 0;
      athena::node::metadata node_meta;
      node_meta.id= athena::node::new_id ();
      athena::node::set (node, node_meta);
      athena::node::metadata field_meta;
      field_meta.id= athena::node::new_id ();
      field_meta.properties.emplace ("fixture:origin", property (std::string ("literal")));
      athena::node::set (node[field], field_meta);
      const tree source= source_with (row_with (
        tree (CONCAT, node, compound ("eq-number"))));
      const auto before= write_xml_v2 (source);
      const auto result= migrate_math_v2_tree (source, true);
      QVERIFY (result.removed_identities.empty ());
      const auto* converted= labeled_node (result.document, node_meta.id);
      QVERIFY (converted != nullptr);
      QVERIFY ((*converted)[field] == node[field]);
      QVERIFY (athena::node::equal_metadata ((*converted)[field], node[field]));
      QVERIFY (athena::node::equal_metadata (*converted, node));
      if (is_func (node, HLINK) || is_func (node, WRITE)) {
        const tree& body= (*converted)[1 - field];
        QVERIFY (is_func (body, CONCAT, 1));
        QVERIFY (is_func (body[0], MATH_SCRIPTS, 5));
      }
      QCOMPARE (write_xml_v2 (source), before);
      QVERIFY (read_xml_v3 (write_xml_v3 (result.document)) == result.document);
    }
  }

  void markerIdentityRelocatesOnlyWithoutCollision () {
    for (const char* name: {"eq-number", "no-number", "eq-lab"}) {
      tree marker= std::string (name) == "eq-lab" ? compound (name, tree ("A")) :
                                                              compound (name);
      athena::node::metadata marker_meta;
      marker_meta.id= athena::node::new_id ();
      marker_meta.properties.emplace ("audit", property (std::string ("original")));
      athena::node::set (marker, marker_meta);
      tree source= source_with (row_with (tree (CONCAT, tree ("y"), marker)));
      athena::node::metadata root_meta;
      root_meta.properties.emplace ("marker-ref",
        property (athena::node::reference {marker_meta.id}));
      athena::node::set (source, root_meta);
      const auto result= migrate_math_v2_tree (source, true);
      QVERIFY (result.removed_identities.empty ());
      const tree& row= result.document[0][0][0];
      QCOMPARE (athena::node::id (row), marker_meta.id);
      QVERIFY (athena::node::equal (athena::node::get (row)->properties.at ("audit"),
                                    marker_meta.properties.at ("audit")));
      QVERIFY (athena::node::equal_metadata (result.document, source));
      QVERIFY (read_xml_v3 (write_xml_v3 (result.document)) == result.document);

      tree conflicting_row= row_with (marker);
      athena::node::metadata row_meta;
      row_meta.id= athena::node::new_id ();
      athena::node::set (conflicting_row, row_meta);
      for (const bool strict: {false, true})
        QVERIFY_EXCEPTION_THROWN (migrate_math_v2_tree (source_with (conflicting_row), strict),
                                  std::runtime_error);
      row_meta.id.clear ();
      row_meta.properties.emplace ("audit", property (std::string ("row")));
      athena::node::set (conflicting_row, row_meta);
      QVERIFY_EXCEPTION_THROWN (migrate_math_v2_tree (source_with (conflicting_row)),
                                std::runtime_error);
    }
  }

  void rejectAmbiguousOrMalformedMarkersInEitherMode () {
    for (const tree content: {
      tree (CONCAT, compound ("eq-number"), compound ("eq-number")),
      tree (CONCAT, compound ("eq-number"), compound ("no-number")),
      tree (CONCAT, compound ("eq-number"), compound ("eq-lab", tree ("A"))),
      tree (CONCAT, compound ("eq-lab", tree ("A")), compound ("eq-lab", tree ("B"))),
      compound ("eq-number", tree ("argument")), compound ("no-number", tree ("argument")),
      compound ("eq-lab"), compound ("eq-lab", tree ("A"), tree ("B")),
      compound ("custom-row-macro", compound ("eq-number")),
      tree (FRAC, compound ("eq-number"), tree ("x")),
      scripts (compound ("eq-number"), tree (""), tree (""), tree ("i"), tree ("")),
      tree (WITH, tree ("color"), tree ("red"), compound ("eq-lab", tree ("A")))
    }) {
      const tree source= source_with (row_with (content));
      for (const bool strict: {false, true})
        QVERIFY_EXCEPTION_THROWN (migrate_math_v2_tree (source, strict), std::runtime_error);
    }
    tree row= row_with (compound ("eq-number"));
    set_row_numbering (row, row_numbering::unnumbered);
    QVERIFY_EXCEPTION_THROWN (migrate_math_v2_tree (source_with (row)), std::runtime_error);
  }

  void rejectMarkersWithoutDisplayRowContext () {
    const tree row= row_with (compound ("eq-number"));
    for (const tree source: {
      tree (DOCUMENT, compound ("math", tree (TABLE, row))),
      tree (DOCUMENT, tree (TABLE, row)),
      tree (DOCUMENT, compound ("equation*", compound ("eq-number"))),
      source_with (row, "custom-equations")
    }) for (const bool strict: {false, true})
      QVERIFY_EXCEPTION_THROWN (migrate_math_v2_tree (source, strict), std::runtime_error);
  }

  void nestedGridsOwnTheirMarkers () {
    const tree nested= display ("align", tree (TABLE, row_with (compound ("no-number"))));
    const tree source= source_with (row_with (
      tree (CONCAT, nested, compound ("eq-number"))));
    const auto result= migrate_math_v2_tree (source, true);
    const tree& row= result.document[0][0][0];
    QVERIFY (get_row_numbering (row) == row_numbering::numbered);
    const tree& inner= row[2][0][0];
    QVERIFY (is_func (inner, MATH_BLOCK, 1));
    QVERIFY (get_row_numbering (inner[0][0]) == row_numbering::unnumbered);
    QVERIFY (!has_marker (result.document));
    QVERIFY (read_xml_v3 (write_xml_v3 (result.document)) == result.document);
  }

  void multilineDisplaysUseStackRowPolicies () {
    tree source (DOCUMENT, compound ("equation", tree (DOCUMENT,
      tree (CONCAT, tree ("x"), compound ("no-number")),
      tree (CONCAT, tree ("y"), compound ("eq-lab", tree ("A"))))));
    const auto result= migrate_math_v2_tree (source, true);
    const tree& block= result.document[0];
    QVERIFY (std::get<bool> (athena::node::get (block)->properties.at (
      "math:block-numbered").data));
    QVERIFY (get_row_numbering (block[0][0]) == row_numbering::unnumbered);
    QVERIFY (*get_row_tag (block[0][1]) == tree ("A"));
    QVERIFY (!has_marker (result.document));
    QVERIFY (read_xml_v3 (write_xml_v3 (result.document)) == result.document);
  }
};

QTEST_GUILESS_MAIN (TestNativeV3GridMigration)
#include "native_v3_grid_migration_test.moc"
