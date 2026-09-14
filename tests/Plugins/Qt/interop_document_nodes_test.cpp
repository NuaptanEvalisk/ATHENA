/******************************************************************************
* MODULE     : interop_document_nodes_test.cpp
* DESCRIPTION: Node identity, mutation and owner lifetime tests for AUDM documents
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#include <QtTest/QtTest>
#include "ATHENA/Data/interop_document_nodes.hpp"
#include "ATHENA/Data/interop_document_codec.hpp"
#include "converter.hpp"
#include "drd_std.hpp"
#include "tree.hpp"
#include <thread>

using namespace athena::interop;

class TestDocumentNodes: public QObject {
  Q_OBJECT
private slots:
  void initTestCase () { init_std_drd (); }

  void structuredValuesRoundTrip () {
    tree source (DOCUMENT,
      compound ("TeXmacs", "2.1.4"),
      compound ("style", tree (TUPLE, "generic")),
      compound ("body", tree (DOCUMENT, compound ("math", tree (CONCAT,
        "<alpha>", tree (RSUB, "1"))))),
      compound ("initial", compound ("collection", compound ("associate", "font", "pagella"))));
    auto structured= document_node_to_value (source);
    QCOMPARE (structured.at ("tag").get<std::string> (), std::string ("document"));
    QCOMPARE (document_node_from_value (structured), source);
    auto decoded= value::from_msgpack (value::to_msgpack (structured));
    QCOMPARE (document_node_from_value (decoded), source);
    QCOMPARE (document_node_from_value (value {{"tag", "empty-custom-tag"},
      {"children", value::array ()}}), compound ("empty-custom-tag"));
    QCOMPARE (document_node_from_value (value {{"text", ""}}), tree (""));
  }

  void corkAndUtf8RemainLossless () {
    string bytes;
    for (int i= 0; i < 256; ++i) bytes << char (i);
    tree source (CONCAT, bytes, "<alpha>", "<#1F600>", "<not-a-known-symbol>");
    const auto encoded= document_node_to_value (source);
    QCOMPARE (document_node_from_value (encoded), source);
    QCOMPARE (document_node_from_value (value::from_msgpack (value::to_msgpack (encoded))), source);
    const std::string unicode= "\xce\xb1 \xe4\xb8\xad\xe6\x96\x87 \xf0\x9f\x98\x80";
    auto node= document_node_from_value (value {{"text", unicode}});
    auto utf8= cork_to_utf8 (node->label);
    QCOMPARE (std::string (utf8.data (), N (utf8)), unicode);
    auto raw= document_node_from_value (value {{"cork", value::binary ({0, 128, 255})}});
    QCOMPARE (N (raw->label), 3);
    QCOMPARE (static_cast<unsigned char> (raw->label[2]), 255);
    QVERIFY_THROWS_EXCEPTION (std::invalid_argument,
      document_node_from_value (value {{"text", std::string ("\xff", 1)}}));
    QVERIFY_THROWS_EXCEPTION (std::invalid_argument,
      document_node_from_value (value {{"text", std::string ("\xe4\xb8", 2)}}));
  }

  void structuredValuesRejectMalformedOrOversizeInput () {
    for (const auto& invalid: std::vector<value> {
      nullptr, value::array (), value {{"text", 1}}, value {{"text", "x"}, {"ignored", 1}},
      value {{"cork", "not binary"}}, value {{"tag", ""}, {"children", value::array ()}},
      value {{"tag", "string"}, {"children", value::array ()}},
      value {{"tag", "math"}, {"children", "x"}},
      value {{"tag", "math"}, {"tag_cork", value::binary ({1})}, {"children", value::array ()}}
    }) QVERIFY_THROWS_EXCEPTION (std::invalid_argument, document_node_from_value (invalid));
    document_codec_limits limits;
    limits.bytes= 2;
    QVERIFY_THROWS_EXCEPTION (std::length_error, document_node_to_value (tree ("abc"), limits));
    QVERIFY_THROWS_EXCEPTION (std::length_error,
      document_node_from_value (value {{"text", "abc"}}, limits));
    limits= {}; limits.nodes= 1;
    tree source (DOCUMENT, "x");
    auto encoded= document_node_to_value (source);
    QVERIFY_THROWS_EXCEPTION (std::length_error, document_node_to_value (source, limits));
    QVERIFY_THROWS_EXCEPTION (std::length_error, document_node_from_value (encoded, limits));
    limits= {}; limits.depth= 0;
    QVERIFY_THROWS_EXCEPTION (std::length_error, document_node_to_value (source, limits));
    QVERIFY_THROWS_EXCEPTION (std::length_error, document_node_from_value (encoded, limits));
  }

  void relativeInsertionFollowsIdentity () {
    tree root (DOCUMENT, "first", "last");
    document_nodes nodes;
    auto target= nodes.track (root, {1});
    insert (root, 0, tree (TUPLE, "prefix"));
    nodes.insert_siblings (root, target, false, value::array ({value {{"text", "before"}}}));
    nodes.insert_siblings (root, target, true, value::array ({value {{"text", "hello world"}}}));
    QCOMPARE (root, tree (DOCUMENT, "prefix", "first", "before", "last", "hello world"));
    QCOMPARE (nodes.read (root, target), value ({{"text", "last"}}));
    auto original= copy (root);
    QVERIFY_THROWS_EXCEPTION (std::invalid_argument,
      nodes.insert_siblings (root, nodes.track (root, {}), false, value::array ()));
    QVERIFY_THROWS_EXCEPTION (std::invalid_argument,
      nodes.insert_siblings (root, target, true,
        value::array ({value {{"text", "valid"}}, value {{"bad", "invalid"}}})));
    QCOMPARE (root, original);
    nodes.erase (root, target);
    QVERIFY_THROWS_EXCEPTION (std::runtime_error,
      nodes.insert_siblings (root, target, true, value::array ()));
  }

  void mutationsPreserveOnlySurvivingNodes () {
    tree root (DOCUMENT, tree (CONCAT, "first", "second"), "last");
    document_nodes registry;
    auto first= registry.track (root, {0, 0});
    auto last= registry.track (root, {1});
    auto batch= registry.track_many (root, {{1}, {0, 0}, {1}});
    QVERIFY (batch == std::vector<document_node> ({last, first, last}));
    QCOMPARE (registry.track (root, {0, 0}), first);
    insert (root[0], 0, tree (TUPLE, "prefix"));
    QVERIFY ((registry.locate (root, first) == document_node_path {0, 1}));
    insert (root[0][1], 5, "!");
    QCOMPARE (root[0][1], tree ("first!"));
    QVERIFY ((registry.locate (root, first) == document_node_path {0, 1}));
    remove (root[0], 1, 1);
    insert (root[0], 1, tree (TUPLE, "first!"));
    QVERIFY_THROWS_EXCEPTION (std::runtime_error, registry.locate (root, first));
    auto replacement= registry.track (root, {0, 1});
    QVERIFY (replacement->id != first->id);
    assign (root[1], tree ("last"));
    QVERIFY_THROWS_EXCEPTION (std::runtime_error, registry.locate (root, last));
  }

  void ownerOperationsExposeFullSourceAndPreserveSurvivors () {
    tree source (DOCUMENT,
      compound ("style", tree (TUPLE, "generic")),
      compound ("body", tree (DOCUMENT, compound ("math", "x"))),
      compound ("initial", tree (COLLECTION, compound ("associate", "font", "pagella"))));
    document_nodes registry;
    auto root= registry.track (source, {});
    auto fields= registry.children (source, root);
    QCOMPARE (fields.size (), std::size_t (3));
    const auto attributes= registry.properties (source, fields[0]);
    QCOMPARE (attributes.at ("name").get<std::string> (), std::string ("style"));
    QCOMPARE (attributes.at ("type").get<std::string> (), std::string ("compound"));
    QCOMPARE (attributes.at ("arity").get<int> (), 1);
    QVERIFY (!attributes.contains ("children"));
    QCOMPARE (document_node_from_value (registry.read (source, root)), source);
    auto font= registry.track (source, {2, 0, 0, 1});
    QCOMPARE (registry.properties (source, font).at ("text").get<std::string> (), std::string ("pagella"));
    QVERIFY (registry.children (source, font).empty ());
    auto body= registry.track (source, {1, 0});
    auto math= registry.track (source, {1, 0, 0});
    auto x= registry.track (source, {1, 0, 0, 0});
    const auto inserted= registry.insert_children (source, body, 0,
      value::array ({value {{"text", "before"}}, value {{"text", "between"}}}));
    QCOMPARE (inserted.size (), std::size_t (2));
    QVERIFY ((registry.locate (source, math) == document_node_path {1, 0, 2}));
    registry.set_tag (source, math, "strong");
    QVERIFY (is_compound (source[1][0][2], "strong", 1));
    QCOMPARE (registry.track (source, {1, 0, 2}), math);
    QCOMPARE (registry.track (source, {1, 0, 2, 0}), x);
    auto replacement= registry.replace (source, inserted[0], value {{"text", "new"}});
    QVERIFY (replacement != inserted[0]);
    QVERIFY_THROWS_EXCEPTION (std::runtime_error, registry.read (source, inserted[0]));
    registry.erase (source, inserted[1]);
    QVERIFY_THROWS_EXCEPTION (std::runtime_error, registry.read (source, inserted[1]));
    QVERIFY ((registry.locate (source, math) == document_node_path {1, 0, 1}));
    registry.move (source, math, body, 0);
    QCOMPARE (registry.track (source, {1, 0, 0, 0}), x);
    QCOMPARE (registry.track (source, {2, 0, 0, 1}), font);
    auto new_root= registry.replace (source, root,
      value {{"tag", "document"}, {"children", value::array ({value {{"text", "replacement"}}})}});
    QVERIFY (new_root != root);
    QCOMPARE (source, tree (DOCUMENT, "replacement"));
    QVERIFY_THROWS_EXCEPTION (std::runtime_error, registry.read (source, root));
    QVERIFY_THROWS_EXCEPTION (std::runtime_error, registry.read (source, font));
  }

  void invalidOperationsLeaveSourceUnchanged () {
    tree source (DOCUMENT, "first", "last");
    tree expected= copy (source);
    document_nodes registry;
    auto root= registry.track (source, {});
    auto child= registry.track (source, {0});
    QVERIFY_THROWS_EXCEPTION (std::invalid_argument, registry.erase (source, root));
    QVERIFY_THROWS_EXCEPTION (std::invalid_argument, registry.set_tag (source, child, "math"));
    QVERIFY_THROWS_EXCEPTION (std::invalid_argument, registry.set_tag (source, root, ""));
    QVERIFY_THROWS_EXCEPTION (std::invalid_argument, registry.replace (source, child, nullptr));
    QVERIFY_THROWS_EXCEPTION (std::invalid_argument,
      registry.insert_children (source, root, 0,
        value::array ({value {{"text", "valid"}}, value {{"text", 3}}})));
    QVERIFY_THROWS_EXCEPTION (std::invalid_argument,
      registry.insert_children (source, root, 3, value::array ()));
    QVERIFY_THROWS_EXCEPTION (std::invalid_argument,
      registry.insert_children (source, child, 0, value::array ({value {{"text", "bad"}}})));
    QCOMPARE (source, expected);
    QCOMPARE (registry.track (source, {}), root);
    QCOMPARE (registry.track (source, {0}), child);
    QVERIFY (registry.insert_children (source, root, 2, value::array ()).empty ());
    QCOMPARE (source, expected);
  }

  void wrappersAndDescendants () {
    tree root (DOCUMENT, tree (CONCAT, "a", "b"));
    document_nodes registry;
    auto compound= registry.track (root, {0});
    auto child= registry.track (root, {0, 1});
    assign_node (root[0], TUPLE);
    QVERIFY ((registry.locate (root, compound) == document_node_path {0}));
    insert_node (root[0], 0, tree (CONCAT));
    QVERIFY ((registry.locate (root, compound) == document_node_path {0, 0}));
    QVERIFY ((registry.locate (root, child) == document_node_path {0, 0, 1}));
    auto wrapper= registry.track (root, {0});
    remove_node (root[0], 0);
    QVERIFY_THROWS_EXCEPTION (std::runtime_error, registry.locate (root, wrapper));
    QVERIFY ((registry.locate (root, child) == document_node_path {0, 1}));
    assign (root[0], tree (CONCAT, "a", "b"));
    QVERIFY_THROWS_EXCEPTION (std::runtime_error, registry.locate (root, compound));
    QVERIFY_THROWS_EXCEPTION (std::runtime_error, registry.locate (root, child));
  }

  void splitAndJoinDoNotGuessIdentity () {
    tree root (DOCUMENT, tree (CONCAT, "a", "b"));
    document_nodes registry;
    auto parent= registry.track (root, {0});
    auto child= registry.track (root, {0, 1});
    split (root, 0, 1);
    QVERIFY_THROWS_EXCEPTION (std::runtime_error, registry.locate (root, parent));
    QVERIFY ((registry.locate (root, child) == document_node_path {1, 0}));
    auto left= registry.track (root, {0});
    auto right= registry.track (root, {1});
    join (root, 0);
    QVERIFY_THROWS_EXCEPTION (std::runtime_error, registry.locate (root, left));
    QVERIFY_THROWS_EXCEPTION (std::runtime_error, registry.locate (root, right));
    QVERIFY ((registry.locate (root, child) == document_node_path {0, 1}));
  }

  void moveFollowsNodeAndDescendants () {
    tree root (DOCUMENT, tree (CONCAT, "a", "b"), tree (CONCAT, "c"), "last");
    document_nodes registry;
    auto parent= registry.track (root, {});
    auto moving= registry.track (root, {0});
    auto child= registry.track (root, {0, 1});
    auto destination= registry.track (root, {1});
    registry.move (root, moving, parent, 3);
    QVERIFY ((registry.locate (root, moving) == document_node_path {2}));
    QVERIFY ((registry.locate (root, child) == document_node_path {2, 1}));
    registry.move (root, moving, destination, 0);
    QVERIFY ((registry.locate (root, moving) == document_node_path {0, 0}));
    QVERIFY ((registry.locate (root, child) == document_node_path {0, 0, 1}));
    QVERIFY_THROWS_EXCEPTION (std::invalid_argument, registry.move (root, destination, moving, 0));
    QVERIFY_THROWS_EXCEPTION (std::invalid_argument, registry.move (root, moving, destination, 9));
    QVERIFY_THROWS_EXCEPTION (std::invalid_argument, registry.move (root, parent, destination, 0));
    remove (root[0], 0, 1);
    QVERIFY_THROWS_EXCEPTION (std::runtime_error, registry.locate (root, child));
  }

  void removedNodesDoNotRevive () {
    tree original ("same");
    tree root (DOCUMENT, original);
    document_nodes registry;
    auto node= registry.track (root, {0});
    remove (root, 0, 1);
    insert (root, 0, tree (TUPLE, original));
    QVERIFY_THROWS_EXCEPTION (std::runtime_error, registry.locate (root, node));
    QVERIFY (registry.track (root, {0})->id != node->id);
  }

  void leasesHaveNoNativeOwnership () {
    tree root (DOCUMENT, "node");
    document_nodes registry;
    auto node= registry.track (root, {0});
    QVERIFY (!is_nil (inside (root[0])->obs));
    bool rejected= false;
    std::thread worker ([&, lease= std::move (node)] () mutable {
      try { registry.collect (); }
      catch (const std::logic_error&) { rejected= true; }
      lease.reset ();
    });
    worker.join ();
    QVERIFY (rejected);
    registry.collect ();
    QVERIFY (is_nil (inside (root[0])->obs));
    document_node surviving;
    {
      document_nodes transient;
      surviving= transient.track (root, {0});
    }
    QVERIFY (is_nil (inside (root[0])->obs));
    std::thread releaser ([lease= std::move (surviving)] () mutable { lease.reset (); });
    releaser.join ();
  }

  void ambiguousOrUnrelatedNodesAreRejected () {
    tree same ("shared");
    tree root (DOCUMENT, same, same);
    document_nodes registry;
    QVERIFY_THROWS_EXCEPTION (std::runtime_error, registry.track (root, {0}));
    tree unique (DOCUMENT, "node");
    auto node= registry.track (unique, {0});
    document_nodes unrelated;
    QVERIFY_THROWS_EXCEPTION (std::runtime_error, unrelated.locate (unique, node));
    QVERIFY_THROWS_EXCEPTION (std::invalid_argument, registry.track (unique, {1}));
    QVERIFY_THROWS_EXCEPTION (std::invalid_argument, registry.track (unique, {-1}));
    QVERIFY_THROWS_EXCEPTION (std::invalid_argument, registry.track (unique, {0, 0}));
    auto replacement= copy (unique);
    QVERIFY_THROWS_EXCEPTION (std::runtime_error, registry.locate (replacement, node));
  }

  void identityHandoffCrossesOwnersWithoutNativeObjects () {
    document_node_transfer handoff;
    document_node root_handle, child, deleted;
    {
      tree root (DOCUMENT, compound ("style", tree (TUPLE, "generic")),
        compound ("body", tree (DOCUMENT, "first", "second", "deleted")));
      document_nodes registry;
      root_handle= registry.track (root, {});
      child= registry.track (root, {1, 0, 1});
      deleted= registry.track (root, {1, 0, 2});
      remove (root[1][0], 2, 1);
      insert (root[1][0], 0, tree (TUPLE, "prefix"));
      handoff= registry.export_nodes (root);
    }
    // The previous registry and tree are gone before the next owner starts.
    std::exception_ptr failure;
    bool root_preserved= false, child_preserved= false, deletion_preserved= false;
    std::thread worker ([&] {
      try {
        tree root= document_node_from_value (handoff.source_value ());
        document_nodes registry;
        registry.import_nodes (root, handoff);
        root_preserved= registry.track (root, {}) == root_handle;
        child_preserved= registry.track (root, {1, 0, 2}) == child;
        insert (root[1][0][2], 6, "!");
        child_preserved= child_preserved &&
          registry.locate (root, child) == document_node_path {1, 0, 2} &&
          root[1][0][2] == "second!";
        try { registry.locate (root, deleted); }
        catch (const std::runtime_error&) { deletion_preserved= true; }
      }
      catch (...) { failure= std::current_exception (); }
    });
    worker.join ();
    if (failure) std::rethrow_exception (failure);
    QVERIFY (root_preserved);
    QVERIFY (child_preserved);
    QVERIFY (deletion_preserved);
  }

  void identityHandoffRequiresExactSourceAndUniqueOccurrences () {
    tree source (DOCUMENT, compound ("style", "generic"),
      compound ("body", tree (DOCUMENT, "same", "same")));
    document_nodes owner;
    auto child= owner.track (source, {1, 0, 0});
    const auto handoff= owner.export_nodes (source);
    document_nodes recipient;
    tree changed= document_node_from_value (handoff.source_value ());
    assign (changed[0][0], "different-style");
    QVERIFY_THROWS_EXCEPTION (std::runtime_error, recipient.import_nodes (changed, handoff));
    QVERIFY_THROWS_EXCEPTION (std::runtime_error, recipient.locate (changed, child));
    tree shared ("same");
    tree aliased (DOCUMENT, compound ("style", "generic"),
      compound ("body", tree (DOCUMENT, shared, shared)));
    QCOMPARE (aliased, source);
    QVERIFY_THROWS_EXCEPTION (std::runtime_error, recipient.import_nodes (aliased, handoff));
    tree exact= document_node_from_value (handoff.source_value ());
    recipient.import_nodes (exact, handoff);
    QCOMPARE (recipient.track (exact, {1, 0, 0}), child);
    QVERIFY_THROWS_EXCEPTION (std::logic_error, recipient.import_nodes (exact, handoff));
  }

  void wireSnapshotCanAcquireIdentitiesBeforeNativeImport () {
    document_node_transfer snapshot;
    {
      tree source (DOCUMENT, compound ("body", tree (DOCUMENT, "first", "second")));
      document_nodes original;
      snapshot= original.export_nodes (source);
    }
    document_node selected;
    std::thread reader ([&] { selected= snapshot.track ({0, 0, 1}); });
    reader.join ();
    QCOMPARE (snapshot.track ({0, 0, 1}), selected);
    QVERIFY (snapshot.locate (selected) == document_node_path ({0, 0, 1}));
    QVERIFY_THROWS_EXCEPTION (std::invalid_argument, snapshot.track ({-1}));
    QVERIFY_THROWS_EXCEPTION (std::invalid_argument, snapshot.track ({0, 0, 2}));
    QVERIFY_THROWS_EXCEPTION (std::invalid_argument, snapshot.track ({0, 0, 1, 0}));
    tree source= document_node_from_value (snapshot.source_value ());
    document_nodes recipient;
    recipient.import_nodes (source, snapshot);
    QCOMPARE (recipient.track (source, {0, 0, 1}), selected);
    insert (source[0][0], 0, tree (TUPLE, "prefix"));
    QVERIFY (recipient.locate (source, selected) == document_node_path ({0, 0, 2}));
  }
};

QTEST_GUILESS_MAIN (TestDocumentNodes)
#include "interop_document_nodes_test.moc"
