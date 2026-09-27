/******************************************************************************
* MODULE     : node_metadata_test.cpp
* DESCRIPTION: Detached node metadata, normalization and deterministic history
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#include <QtTest/QtTest>
#include "node_metadata.hpp"
#include "modification.hpp"
#include "patch.hpp"
#include <limits>
#include <set>
#include <stdexcept>

namespace node= athena::node;
using node::property;

bool headless_mode= true;
bool is_headless () { return true; }

namespace {
const std::string root_id= "00000000-0000-4000-8000-000000000001";
const std::string child_id= "00000000-0000-4000-8000-000000000002";
const std::string rich_id= "00000000-0000-4000-8000-000000000003";
const std::string external_id= "00000000-0000-4000-8000-000000000004";

node::metadata attributes (const std::string& id, const std::string& note) {
  return {id, {{"note", property (note)}}};
}

tree annotated (tree value, const std::string& id, const std::string& note) {
  node::set (value, attributes (id, note));
  return value;
}

tree rich_value (const tree& value, const std::string& key= "rich") {
  return std::get<node::rich_text> (node::get (value)->properties.at (key).data).content;
}
}

class TestNodeMetadata: public QObject {
  Q_OBJECT
  using property= node::property;
private slots:
  void initTestCase () {
    make_tree_label (DOCUMENT, "document");
    make_tree_label (CONCAT, "concat");
    make_tree_label (TUPLE, "tuple");
    make_tree_label (QUOTE, "quote");
    make_tree_label (RAW_DATA, "raw-data");
  }

  void defaultsAndIdentityValidation () {
    tree value ("plain");
    QVERIFY (node::get (value) == nullptr);
    QVERIFY (node::id (value).empty ());
    QVERIFY (node::metadata {}.empty ());
    node::set (value, {});
    QVERIFY (node::get (value) == nullptr);
    QVERIFY (node::valid_id (root_id));
    for (const std::string bad: {
      "", "not-a-uuid", "00000000-0000-0000-0000-000000000000",
      "00000000-0000-4000-8000-00000000000A",
      "00000000000040008000000000000001",
      "{00000000-0000-4000-8000-000000000001}"
    }) QVERIFY (!node::valid_id (bad));
    std::set<std::string> ids;
    for (int i= 0; i < 16; ++i) {
      auto id= node::new_id ();
      QVERIFY (node::valid_id (id));
      QVERIFY (ids.insert (id).second);
    }
    node::set (value, attributes (root_id, "first"));
    QCOMPARE (node::id (value), root_id);
    node::clear (value);
    QVERIFY (node::get (value) == nullptr);
    QVERIFY (value == tree ("plain"));
  }

  void validationIsAtomic () {
    tree value= annotated (tree ("original"), root_id, "keep");
    tree before= copy (value);
    auto bad= attributes ("invalid", "replace");
    QVERIFY_EXCEPTION_THROWN (node::set (value, bad), std::invalid_argument);
    QVERIFY (value == before);
    const property invalid_values[]= {
      property (std::string ("\xff", 1)),
      property (std::numeric_limits<double>::infinity ()),
      property (std::numeric_limits<double>::quiet_NaN ()),
      property (node::reference {"invalid"}),
      property (property::dictionary {{"", property (true)}}),
      property (property::dictionary {{std::string ("\xff", 1), property (true)}}),
      property (node::rich_text {tree (RAW_DATA, tree ("bytes"))}),
      property (node::rich_text {tree (UNINIT)})
    };
    for (const auto& invalid: invalid_values) {
      bad= attributes (child_id, "replace");
      bad.properties.emplace ("nested", property (property::list {invalid}));
      QVERIFY_EXCEPTION_THROWN (node::set (value, bad), std::invalid_argument);
      QVERIFY (value == before);
    }
    property deep (true);
    for (int i= 0; i < 258; ++i)
      deep= property (property::list {std::move (deep)});
    bad= attributes (child_id, "replace");
    bad.properties.emplace ("deep", std::move (deep));
    QVERIFY_EXCEPTION_THROWN (node::set (value, bad), std::length_error);
    QVERIFY (value == before);
    tree uninitialized (UNINIT);
    QVERIFY_EXCEPTION_THROWN (node::set (uninitialized, attributes (root_id, "bad")),
                              std::invalid_argument);
  }

  void setAndCopyAreDeep () {
    tree rich= annotated (tree (CONCAT, tree ("original")), rich_id, "rich");
    auto meta= attributes (root_id, "root");
    meta.properties.emplace ("rich", property (node::rich_text {rich}));
    meta.properties.emplace ("nested", property (property::list {
      property (property::dictionary {{"rich", property (node::rich_text {rich})}})}));
    tree source (DOCUMENT, annotated (tree ("child"), child_id, "child"));
    node::set (source, meta);
    tree snapshot= copy (source);
    QVERIFY (snapshot == source);
    QVERIFY (!strong_equal (snapshot, source));
    QVERIFY (!strong_equal (snapshot[0], source[0]));
    QVERIFY (node::get (snapshot) != node::get (source));
    QVERIFY (!strong_equal (rich_value (snapshot), rich_value (source)));
    QCOMPARE (hash (snapshot), hash (source));
    QCOMPARE (node::hash_metadata (snapshot), node::hash_metadata (source));

    rich[0]= tree ("changed caller tree");
    meta.id= external_id;
    meta.properties.at ("note")= property (std::string ("changed caller property"));
    QVERIFY (source == snapshot);
    QVERIFY (rich_value (source)[0] == tree ("original"));
    tree copied_rich= rich_value (snapshot);
    copied_rich[0]= tree ("changed copy");
    QVERIFY (rich_value (source)[0] == tree ("original"));
    node::clear (snapshot[0]);
    QCOMPARE (node::id (source[0]), child_id);
    QVERIFY (snapshot != source);

    tree target ("different content");
    node::copy_metadata (source, target);
    QVERIFY (node::equal_metadata (source, target));
    QVERIFY (!strong_equal (rich_value (source), rich_value (target)));
    node::copy_metadata (target, target);
    QVERIFY (node::equal_metadata (source, target));
    node::copy_metadata (tree ("plain"), target);
    QVERIFY (node::get (target) == nullptr);

    auto property_copy= node::copy_property (node::get (source)->properties.at ("nested"));
    auto& dict= std::get<property::dictionary> (
      std::get<property::list> (property_copy.data)[0].data);
    std::get<node::rich_text> (dict.at ("rich").data).content[0]= tree ("changed nested copy");
    QVERIFY (!node::equal (property_copy, node::get (source)->properties.at ("nested")));
    QVERIFY (rich_value (source)[0] == tree ("original"));
  }

  void equalityAndProjection () {
    const property values[]= {
      property (std::string ("1")), property (true), property (std::int64_t (1)),
      property (1.0), property (property::list {}), property (property::dictionary {}),
      property (node::reference {root_id}), property (node::rich_text {tree ("1")})
    };
    for (const auto& a: values) {
      QVERIFY (node::equal (a, node::copy_property (a)));
      for (const auto& b: values)
        if (a.data.index () != b.data.index ()) QVERIFY (!node::equal (a, b));
    }
    tree rich= annotated (tree ("rich"), rich_id, "rich");
    auto meta= attributes (root_id, "root");
    meta.properties.emplace ("rich", property (node::rich_text {rich}));
    meta.properties.emplace ("nested", property (property::list {
      property (property::dictionary {{"rich", property (node::rich_text {rich})}})}));
    meta.properties.emplace ("reference", property (node::reference {external_id}));
    tree source (DOCUMENT, annotated (tree ("child"), child_id, "child"));
    node::set (source, meta);
    tree projected= node::content_projection (source);
    QVERIFY (projected != source);
    QVERIFY (node::content_equal (source, projected));
    QVERIFY (node::id (projected).empty ());
    QVERIFY (node::id (projected[0]).empty ());
    QVERIFY (node::id (rich_value (projected)).empty ());
    const auto* properties= node::get (projected);
    QVERIFY (properties != nullptr);
    QVERIFY (node::equal (properties->properties.at ("note"), meta.properties.at ("note")));
    QCOMPARE (std::get<node::reference> (properties->properties.at ("reference").data).id,
              external_id);
    const auto& nested= std::get<property::dictionary> (
      std::get<property::list> (properties->properties.at ("nested").data)[0].data);
    QVERIFY (node::id (std::get<node::rich_text> (nested.at ("rich").data).content).empty ());
    QCOMPARE (node::id (source), root_id);
    QCOMPARE (node::id (rich_value (source)), rich_id);
    auto changed= *node::get (projected[0]);
    changed.properties.at ("note")= property (std::string ("changed"));
    node::set (projected[0], changed);
    QVERIFY (!node::content_equal (source, projected));
  }

  void duplicateRemapsInternalReferences () {
    auto rich_meta= attributes (rich_id, "rich");
    rich_meta.properties.emplace ("back", property (node::reference {root_id}));
    tree rich ("rich");
    node::set (rich, rich_meta);
    auto meta= attributes (root_id, "root");
    meta.properties.emplace ("rich", property (node::rich_text {rich}));
    meta.properties.emplace ("refs", property (property::list {
      property (node::reference {child_id}),
      property (property::dictionary {
        {"rich", property (node::reference {rich_id})},
        {"external", property (node::reference {external_id})}})}));
    tree source (DOCUMENT, annotated (tree ("child"), child_id, "child"), tree ("plain"));
    node::set (source, meta);
    tree before= copy (source);
    node::identity_map remap;
    tree duplicated= node::duplicate (source, &remap);
    QCOMPARE (remap.size (), std::size_t (3));
    for (const auto& entry: remap) {
      QVERIFY (node::valid_id (entry.second));
      QVERIFY (entry.first != entry.second);
    }
    QCOMPARE (node::id (duplicated), remap.at (root_id));
    QCOMPARE (node::id (duplicated[0]), remap.at (child_id));
    QVERIFY (node::get (duplicated[1]) == nullptr);
    tree duplicated_rich= rich_value (duplicated);
    QCOMPARE (node::id (duplicated_rich), remap.at (rich_id));
    QCOMPARE (std::get<node::reference> (
      node::get (duplicated_rich)->properties.at ("back").data).id, remap.at (root_id));
    const auto& refs= std::get<property::list> (node::get (duplicated)->properties.at ("refs").data);
    QCOMPARE (std::get<node::reference> (refs[0].data).id, remap.at (child_id));
    const auto& dict= std::get<property::dictionary> (refs[1].data);
    QCOMPARE (std::get<node::reference> (dict.at ("rich").data).id, remap.at (rich_id));
    QCOMPARE (std::get<node::reference> (dict.at ("external").data).id, external_id);
    QVERIFY (source == before);
    QVERIFY (duplicated != source);
    QVERIFY (node::id (node::duplicate (source)) != node::id (duplicated));
    node::clear (duplicated_rich);
    QCOMPARE (node::id (rich_value (source)), rich_id);

    tree conflicting (DOCUMENT, copy (source[0]), copy (source[0]));
    auto untouched= remap;
    QVERIFY_EXCEPTION_THROWN (node::duplicate (conflicting, &remap), std::invalid_argument);
    QVERIFY (remap == untouched);
    tree plain (DOCUMENT, tree ("plain"));
    tree plain_duplicate= node::duplicate (plain, &remap);
    QVERIFY (plain_duplicate == plain);
    QVERIFY (!strong_equal (plain_duplicate, plain));
    QVERIFY (remap.empty ());
  }

  void normalizationPreservesAnnotatedBoundaries () {
    QVERIFY (simplify_concat (tree (CONCAT, tree ("a"), tree (CONCAT, tree ("b")))) == tree ("ab"));
    tree text= annotated (tree ("b"), child_id, "text");
    tree empty= annotated (tree (""), rich_id, "empty");
    tree source (CONCAT, tree ("a"), text, empty, tree ("c"));
    tree before= copy (source);
    QVERIFY (simplify_concat (source) == source);
    QVERIFY (source == before);
    for (tree wrapper: {tree (CONCAT), tree (CONCAT, tree ("one")),
                        tree (CONCAT, tree ("a"), tree ("b"))}) {
      node::set (wrapper, attributes (root_id, "wrapper"));
      tree normalized= simplify_concat (wrapper);
      QVERIFY (node::equal_metadata (normalized, wrapper));
      QVERIFY (simplify_concat (normalized) == normalized);
    }
    tree nested= annotated (tree (CONCAT, tree ("inner")), child_id, "nested");
    tree outer (CONCAT, tree ("before"), nested, tree ("after"));
    QVERIFY (simplify_concat (outer) == outer);
    tree document= annotated (tree (DOCUMENT, tree ("inside")), child_id, "nested document");
    tree documents= annotated (tree (DOCUMENT, tree ("before"), document), root_id, "document");
    QVERIFY (simplify_document (documents) == documents);
    QVERIFY (simplify_document (tree (DOCUMENT, tree (DOCUMENT, tree ("plain")))) ==
             tree (DOCUMENT, tree ("plain")));
    tree quoted= annotated (tree (QUOTE, tree ("quoted")), rich_id, "quote");
    tree corrected= annotated (tree (DOCUMENT, quoted, outer), root_id, "root");
    tree expected= copy (corrected);
    // The recursive corrector may collapse a one-child CONCAT while moving
    // its metadata onto that unannotated child; the logical object survives.
    expected[1][1]= annotated (tree ("inner"), child_id, "nested");
    tree normalized= simplify_correct (corrected);
    QVERIFY (normalized == expected);
    QVERIFY (simplify_correct (normalized) == normalized);
    QVERIFY (is_concat (corrected[1][1]));
    QVERIFY (simplify_correct (tree (QUOTE, tree ("plain"))) == tree ("plain"));
  }

  // Pure detached-tree history coverage; observer and live-edit tests belong
  // to the separate modification suite.
  void splitUndoRedoIsDeterministic () {
    for (bool compound: {false, true}) {
      tree payload= compound ? tree (TUPLE, tree ("a"), tree ("b")) : tree ("ab");
      payload= annotated (payload, child_id, "payload");
      tree source= annotated (tree (DOCUMENT, payload), root_id, "parent");
      tree before= copy (source);
      for (int offset: {0, 1, 2}) {
        modification forward= mod_split (path (), 0, offset);
        QVERIFY (is_applicable (source, forward));
        modification backward= invert (forward, source);
        tree split= clean_apply (source, forward);
        QVERIFY (has_node_headers (forward));
        QVERIFY (node::equal_metadata (split, source));
        QCOMPARE (N(split), 2);
        const int survivor= offset == 0 ? 1 : 0;
        QCOMPARE (node::id (split[survivor]), child_id);
        QVERIFY (node::valid_id (node::id (split[1-survivor])));
        QVERIFY (node::id (split[1-survivor]) != child_id);
        for (int i= 0; i < 2; ++i)
          QVERIFY (node::equal (node::get (split[i])->properties.at ("note"),
                                node::get (payload)->properties.at ("note")));
        tree resolved_headers= copy (forward->t);
        tree replay= split;
        for (int cycle= 0; cycle < 3; ++cycle) {
          QVERIFY (is_applicable (replay, backward));
          replay= clean_apply (replay, backward);
          QVERIFY (replay == source);
          replay= clean_apply (replay, copy (forward));
          QVERIFY (replay == split);
          prepare_modification (source, forward);
          QVERIFY (forward->t == resolved_headers);
        }
        QVERIFY (source == before);
      }
    }
    tree empty= annotated (tree (""), child_id, "empty");
    tree source (DOCUMENT, empty);
    modification forward= mod_split (path (), 0, 0);
    auto backward= invert (forward, source);
    tree split= clean_apply (source, forward);
    QCOMPARE (node::id (split[0]), child_id);
    QVERIFY (node::id (split[1]) != child_id);
    QVERIFY (clean_apply (split, backward) == source);
    tree plain= clean_apply (tree (DOCUMENT, tree ("ab")), mod_split (path (), 0, 1));
    QVERIFY (node::get (plain[0]) == nullptr && node::get (plain[1]) == nullptr);
  }

  void joinUndoRestoresBothHeaders () {
    for (bool compound: {false, true}) {
      tree left= annotated (compound ? tree (TUPLE, tree ("a")) : tree ("a"), child_id, "left");
      tree right= annotated (compound ? tree (DOCUMENT, tree ("b")) : tree ("b"), rich_id, "right");
      tree source= annotated (tree (DOCUMENT, left, right), root_id, "parent");
      tree before= copy (source);
      modification forward= mod_join (path (), 0);
      QVERIFY (is_applicable (source, forward));
      modification backward= invert (forward, source);
      tree joined= clean_apply (source, forward);
      QCOMPARE (N(joined), 1);
      QCOMPARE (L(joined[0]), L(left));
      QVERIFY (node::equal_metadata (joined, source));
      QVERIFY (node::equal_metadata (joined[0], left));
      tree replay= joined;
      for (int cycle= 0; cycle < 3; ++cycle) {
        replay= clean_apply (replay, copy (backward));
        QVERIFY (replay == source);
        QCOMPARE (L(replay[1]), L(right));
        QVERIFY (node::equal_metadata (replay[1], right));
        replay= clean_apply (replay, copy (forward));
        QVERIFY (replay == joined);
      }
      QVERIFY (source == before);
    }
  }
};

QTEST_GUILESS_MAIN (TestNodeMetadata)
#include "node_metadata_test.moc"
