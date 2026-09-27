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
#include "node_metadata.hpp"
#include "tree.hpp"
#include <cmath>
#include <limits>
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

  void utf8AndRawDataRemainLossless () {
    string bytes;
    for (int i= 0; i < 256; ++i) bytes << char (i);
    tree source (CONCAT, "α", "中文", "😀", tree (RAW_DATA, bytes));
    const auto encoded= document_node_to_value (source);
    QCOMPARE (document_node_from_value (encoded), source);
    QCOMPARE (document_node_from_value (value::from_msgpack (value::to_msgpack (encoded))), source);
    const std::string unicode= "\xce\xb1 \xe4\xb8\xad\xe6\x96\x87 \xf0\x9f\x98\x80";
    auto node= document_node_from_value (value {{"text", unicode}});
    QCOMPARE (std::string (node->label.data (), N(node->label)), unicode);
    auto raw= document_node_from_value (value {{"tag", "raw-data"}, {"children",
      value::array ({value {{"raw", value::binary ({0, 128, 255})}}})}});
    QVERIFY (is_func (raw, RAW_DATA, 1));
    QCOMPARE (N (raw[0]->label), 3);
    QCOMPARE (static_cast<unsigned char> (raw[0]->label[2]), 255);
    QVERIFY_THROWS_EXCEPTION (std::invalid_argument,
      document_node_from_value (value {{"text", std::string ("\xff", 1)}}));
    QVERIFY_THROWS_EXCEPTION (std::invalid_argument,
      document_node_from_value (value {{"text", std::string ("\xe4\xb8", 2)}}));
  }

  void structuredValuesRejectMalformedOrOversizeInput () {
    for (const auto& invalid: std::vector<value> {
      nullptr, value::array (), value {{"text", 1}}, value {{"text", "x"}, {"ignored", 1}},
      value {{"raw", value::binary ({1})}}, value {{"tag", ""}, {"children", value::array ()}},
      value {{"tag", "string"}, {"children", value::array ()}},
      value {{"tag", "math"}, {"children", "x"}},
      value {{"tag", "raw-data"}, {"children", value::array ()}}
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

  void v3RawDataKeepsMessagePackBin () {
    string bytes;
    for (int i= 0; i < 256; ++i) bytes << char (i);
    tree source (RAW_DATA, tree (bytes));
    auto plain= document_node_to_value_v3 (source);
    QVERIFY (plain == document_node_to_value (source));
    athena::node::metadata metadata;
    metadata.id= athena::node::new_id ();
    metadata.properties.emplace ("format", athena::node::property (std::string ("opaque")));
    athena::node::set (source[0], metadata);
    metadata.id= athena::node::new_id ();
    athena::node::set (source, metadata);
    auto encoded= document_node_to_value_v3 (source);
    const auto& payload= encoded.at ("children")[0];
    QVERIFY (payload.at ("raw").is_binary ());
    QVERIFY (!payload.contains ("encoding"));
    QCOMPARE (payload.at ("raw").get_binary ().size (), std::size_t (256));
    for (int i= 0; i < 256; ++i)
      QCOMPARE (payload.at ("raw").get_binary ()[i], std::uint8_t (i));
    tree decoded= document_node_from_value_v3 (
      value::from_msgpack (value::to_msgpack (encoded)));
    QVERIFY (decoded == source);
    QVERIFY (athena::node::equal_metadata (decoded, source));
    QVERIFY (athena::node::equal_metadata (decoded[0], source[0]));
    QVERIFY (document_node_to_value_v3 (decoded) == encoded);
    QVERIFY_THROWS_EXCEPTION (std::invalid_argument, document_node_to_value (source));
    for (const auto& bad: std::vector<value> {
      value {{"raw", "AA=="}},
      value {{"raw", "AA=="}, {"encoding", "base64"}},
      value {{"raw", value::binary ({0})}, {"encoding", "base64"}},
      value {{"raw", value::array ({0})}},
      value {{"raw", value::binary ({0})}, {"text", ""}}
    }) QVERIFY_THROWS_EXCEPTION (std::invalid_argument,
      document_node_from_value_v3 (value {{"tag", "raw-data"},
        {"children", value::array ({bad})}}));
    QVERIFY_THROWS_EXCEPTION (std::invalid_argument,
      document_node_from_value_v3 (value {{"raw", value::binary ({0})}}));
    tree empty (RAW_DATA, tree (""));
    auto empty_encoded= document_node_to_value_v3 (empty);
    QVERIFY (empty_encoded["children"][0]["raw"].is_binary ());
    QVERIFY (document_node_from_value_v3 (
      value::from_msgpack (value::to_msgpack (empty_encoded))) == empty);
  }

  void v3TypedPropertiesRoundTrip () {
    using athena::node::property;
    tree rich (CONCAT, tree (u8"\u4e2d"), tree (" text"));
    athena::node::metadata rich_metadata;
    rich_metadata.id= athena::node::new_id ();
    rich_metadata.properties.emplace ("nested", property (true));
    athena::node::set (rich, rich_metadata);
    athena::node::metadata metadata;
    metadata.id= athena::node::new_id ();
    metadata.properties= {
      {"string", property (std::string ("42"))},
      {"boolean", property (false)},
      {"integer", property (std::int64_t (42))},
      {"minimum", property (std::numeric_limits<std::int64_t>::min ())},
      {"maximum", property (std::numeric_limits<std::int64_t>::max ())},
      {"double", property (42.0)},
      {"negative-zero", property (-0.0)},
      {"tiny", property (std::numeric_limits<double>::denorm_min ())},
      {"large", property (std::numeric_limits<double>::max ())},
      {"reference", property (athena::node::reference {metadata.id})},
      {"rich", property (athena::node::rich_text {rich})},
      {"list", property (property::list {property (true),
        property (property::dictionary {{std::string ("key\0", 4),
          property (std::string ("value\0\r", 7))}})})},
      {"dictionary", property (property::dictionary {
        {"empty-list", property (property::list {})},
        {"empty-map", property (property::dictionary {})}})}
    };
    tree source (string ("text\0\r", 6));
    athena::node::set (source, metadata);
    auto encoded= document_node_to_value_v3 (source);
    for (const auto& entry: encoded.at ("properties")) {
      const auto& typed= entry.at ("value");
      if (typed.at ("type") == "int64" || typed.at ("type") == "double")
        QVERIFY (typed.at ("value").is_string ());
    }
    for (const auto& transported: std::vector<value> {
      value::parse (encoded.dump ()),
      value::from_msgpack (value::to_msgpack (encoded))
    }) {
      tree decoded= document_node_from_value_v3 (transported);
      QVERIFY (decoded == source);
      QVERIFY (athena::node::equal_metadata (decoded, source));
      const auto* result= athena::node::get (decoded);
      QVERIFY (result != nullptr);
      QCOMPARE (std::get<std::int64_t> (result->properties.at ("maximum").data),
                std::numeric_limits<std::int64_t>::max ());
      QVERIFY (std::holds_alternative<std::string> (result->properties.at ("string").data));
      QVERIFY (std::holds_alternative<std::int64_t> (result->properties.at ("integer").data));
      QVERIFY (std::holds_alternative<double> (result->properties.at ("double").data));
      QVERIFY (std::signbit (std::get<double> (result->properties.at ("negative-zero").data)));
      QVERIFY (athena::node::equal_metadata (
        std::get<athena::node::rich_text> (result->properties.at ("rich").data).content, rich));
      QVERIFY (document_node_to_value_v3 (decoded) == encoded);
    }
  }

  void v3RejectsMalformedProperties () {
    auto wrap= [] (value typed) {
      return value {{"text", ""}, {"properties", value::array ({
        value {{"name", "p"}, {"value", std::move (typed)}}})}};
    };
    for (const auto& invalid: std::vector<value> {
      nullptr, value::array (),
      value {{"type", "unknown"}, {"value", ""}},
      value {{"type", "string"}},
      value {{"type", "string"}, {"value", ""}, {"extra", true}},
      value {{"type", "string"}, {"value", std::string ("\xff", 1)}},
      value {{"type", "boolean"}, {"value", 1}},
      value {{"type", "int64"}, {"value", 42}},
      value {{"type", "int64"}, {"value", "01"}},
      value {{"type", "int64"}, {"value", "9223372036854775808"}},
      value {{"type", "int64"}, {"value", "-9223372036854775809"}},
      value {{"type", "double"}, {"value", 42.0}},
      value {{"type", "double"}, {"value", "nan"}},
      value {{"type", "double"}, {"value", "inf"}},
      value {{"type", "double"}, {"value", "1e9999"}},
      value {{"type", "double"}, {"value", "1tail"}},
      value {{"type", "reference"}, {"value", "not-a-uuid"}},
      value {{"type", "list"}, {"value", value::object ()}},
      value {{"type", "dictionary"}, {"value", value::object ()}},
      value {{"type", "rich_tree"}, {"value", value::array ()}},
      value {{"type", "rich_tree"}, {"value", value {
        {"tag", "raw-data"}, {"children", value::array ({value {{"raw", value::binary ({0})}}})}}}}
    }) QVERIFY_THROWS_EXCEPTION (std::invalid_argument,
      document_node_from_value_v3 (wrap (invalid)));
    const value entry= {{"name", "p"}, {"value", {{"type", "string"}, {"value", ""}}}};
    for (const auto& properties: std::vector<value> {
      value::object (), nullptr, value::array ({entry, entry}),
      value::array ({value {{"name", ""}, {"value", entry["value"]}}}),
      value::array ({value {{"name", std::string ("\xff", 1)}, {"value", entry["value"]}}}),
      value::array ({value {{"name", "p"}, {"value", entry["value"]}, {"extra", 1}}})
    }) QVERIFY_THROWS_EXCEPTION (std::invalid_argument,
      document_node_from_value_v3 (value {{"text", ""}, {"properties", properties}}));
    QVERIFY_THROWS_EXCEPTION (std::invalid_argument,
      document_node_from_value_v3 (wrap (value {{"type", "dictionary"},
        {"value", value::array ({entry, entry})}})));
    for (const auto& malformed: std::vector<value> {
      value {{"text", ""}, {"id", ""}},
      value {{"text", ""}, {"id", 1}},
      value {{"text", ""}, {"id", "00000000-0000-4000-8000-00000000000A"}},
      value {{"text", ""}, {"children", value::array ()}},
      value {{"tag", "x"}, {"children", value::array ()}, {"extra", true}},
      value {{"tag", std::string ("\xff", 1)}, {"children", value::array ()}},
      value {{"text", std::string ("\xff", 1)}}
    }) QVERIFY_THROWS_EXCEPTION (std::invalid_argument, document_node_from_value_v3 (malformed));
  }

  void v3RejectsDuplicateIdentities () {
    using athena::node::property;
    tree child ("child");
    athena::node::metadata metadata;
    metadata.id= athena::node::new_id ();
    athena::node::set (child, metadata);
    tree source (DOCUMENT, child, copy (child));
    QVERIFY_THROWS_EXCEPTION (std::invalid_argument, document_node_to_value_v3 (source));
    const auto encoded_child= document_node_to_value_v3 (child);
    QVERIFY_THROWS_EXCEPTION (std::invalid_argument,
      document_node_from_value_v3 (value {{"tag", "document"},
        {"children", value::array ({encoded_child, encoded_child})}}));
    source= tree (DOCUMENT, child);
    metadata.id.clear ();
    metadata.properties.emplace ("rich", property (athena::node::rich_text {child}));
    athena::node::set (source, metadata);
    QVERIFY_THROWS_EXCEPTION (std::invalid_argument, document_node_to_value_v3 (source));
    QVERIFY_THROWS_EXCEPTION (std::invalid_argument,
      document_node_from_value_v3 (value {{"tag", "document"},
        {"properties", value::array ({value {{"name", "rich"}, {"value",
          {{"type", "rich_tree"}, {"value", encoded_child}}}}})},
        {"children", value::array ({encoded_child})}}));
  }

  void v3MetadataSharesAllLimits () {
    using athena::node::property;
    tree source ("");
    athena::node::metadata metadata;
    metadata.properties.emplace ("p", property (property::list {
      property (std::int64_t (7)), property (property::dictionary {
        {"k", property (athena::node::rich_text {tree ("x")})}})}));
    athena::node::set (source, metadata);
    document_codec_limits exact;
    exact.nodes= 6;
    exact.depth= 4;
    exact.bytes= 4;
    auto encoded= document_node_to_value_v3 (source, exact);
    QVERIFY (document_node_from_value_v3 (encoded, exact) == source);
    for (int field= 0; field < 3; ++field) {
      auto limit= exact;
      if (field == 0) --limit.nodes;
      if (field == 1) --limit.depth;
      if (field == 2) --limit.bytes;
      QVERIFY_THROWS_EXCEPTION (std::length_error, document_node_to_value_v3 (source, limit));
      QVERIFY_THROWS_EXCEPTION (std::length_error, document_node_from_value_v3 (encoded, limit));
    }
    metadata.properties.clear ();
    metadata.id= athena::node::new_id ();
    metadata.properties.emplace ("r", property (athena::node::reference {metadata.id}));
    athena::node::set (source, metadata);
    exact= {};
    exact.bytes= 2 * metadata.id.size () + 1;
    encoded= document_node_to_value_v3 (source, exact);
    QVERIFY (document_node_from_value_v3 (encoded, exact) == source);
    --exact.bytes;
    QVERIFY_THROWS_EXCEPTION (std::length_error, document_node_to_value_v3 (source, exact));
    QVERIFY_THROWS_EXCEPTION (std::length_error, document_node_from_value_v3 (encoded, exact));
    source= tree (RAW_DATA, tree (string ("\0\xff", 2)));
    exact= {};
    exact.nodes= 2;
    exact.depth= 1;
    exact.bytes= 10; // "raw-data" plus two binary bytes, without Base64 inflation.
    encoded= document_node_to_value_v3 (source, exact);
    QVERIFY (document_node_from_value_v3 (encoded, exact) == source);
    --exact.bytes;
    QVERIFY_THROWS_EXCEPTION (std::length_error, document_node_to_value_v3 (source, exact));
    QVERIFY_THROWS_EXCEPTION (std::length_error, document_node_from_value_v3 (encoded, exact));
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
