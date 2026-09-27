/******************************************************************************
* MODULE     : document_node_copy_test.cpp
* DESCRIPTION: Source selection, clipboard metadata and reference-safe duplication
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#include <QtTest/QtTest>
#include "ATHENA/Data/document_node_copy.hpp"
#include "Xml/clipboard_xml.hpp"
#include "tree_analyze.hpp"
#include "tree_select.hpp"
#include "Scheme/Scheme/native_interfaces.hpp"

bool headless_mode= true;
bool is_headless () { return true; }

namespace node= athena::node;
using NodeProperty= node::property;
using athena::document_node::duplicate_source_nodes;
namespace {
const std::string first= "11111111-1111-4111-8111-111111111111";
const std::string second= "22222222-2222-4222-8222-222222222222";
const std::string external= "33333333-3333-4333-8333-333333333333";

string native_text (const std::string& value) {
  return string (value.data (), static_cast<int> (value.size ()));
}

tree identified (tree t, const std::string& id) {
  node::set (t, {id, {}});
  return t;
}
tree link (const std::string& id) {
  return tree (HLINK, "target", native_text ("tmfs://wikilink/" + id));
}
}

class TestDocumentNodeCopy: public QObject {
  Q_OBJECT
private slots:
  void initTestCase () {
    make_tree_label (DOCUMENT, "document");
    make_tree_label (CONCAT, "concat");
    make_tree_label (TUPLE, "tuple");
    make_tree_label (HLINK, "hlink");
    make_tree_label (TRANSCLUDE, "transclude");
    make_tree_label (RAW_DATA, "raw-data");
  }

  void fullSelectionsRetainMetadataPartialSelectionsDoNotClaimIdentity () {
    tree atom= identified (tree ("abc"), first);
    tree selected= selection_compute (atom, path (0), path (3));
    QVERIFY (selected == atom);
    QVERIFY (!strong_equal (selected, atom));
    tree partial= selection_compute (atom, path (1), path (3));
    QVERIFY (partial == tree ("bc"));
    QVERIFY (!node::contains_metadata (partial));
    tree paragraph= identified (tree (CONCAT, "a", link (external)), second);
    selected= selection_compute (paragraph, start (paragraph), end (paragraph));
    QVERIFY (selected == paragraph);
  }

  void normalizationKeepsIndependentObjects () {
    tree atom= identified (tree ("text"), first);
    tree empty= identified (tree (""), second);
    tree paragraph (CONCAT, "before", atom, empty, "after");
    tree result= concat_recompose (concat_decompose (paragraph));
    QVERIFY (result == paragraph);
    node::set (paragraph, {external, {}});
    result= concat_recompose (concat_decompose (paragraph));
    QVERIFY (result == paragraph);
    QVERIFY (concat_recompose (concat_decompose (
      tree (CONCAT, "a", tree (CONCAT, "b", "")))) == tree ("ab"));
  }

  void nativeRebuildKeepsParentMetadata () {
    tree source= identified (tree (DOCUMENT, "old"), first);
    node::metadata metadata= *node::get (source);
    metadata.properties.emplace ("example:caption", NodeProperty (
      node::rich_text {tree (CONCAT, "caption")}));
    node::set (source, metadata);
    array<tree> children;
    children << identified (tree ("new"), second);
    tree result= tree_rebuild (source, children);
    QVERIFY (node::equal_metadata (source, result));
    QVERIFY (result[0] == children[0]);
    QVERIFY (source[0] == tree ("old"));
    QVERIFY (tree_node_id (result) == native_text (first));
    tree result_caption= std::get<node::rich_text> (
      node::get (result)->properties.at ("example:caption").data).content;
    result_caption[0]= "changed";
    QVERIFY (std::get<node::rich_text> (
      node::get (source)->properties.at ("example:caption").data).content[0] == "caption");
  }

  void clipboardVersionsAreLosslessSnapshots () {
    using namespace athena::document;
    tree selection= tuple ("texmacs", "abc", "text", "english");
    auto v1= write_clipboard_xml (selection);
    QVERIFY (v1.find ("version=\"1\"") != std::string::npos);
    QVERIFY (read_clipboard_xml (v1) == selection);
    selection[1]= identified (tree ("abc"), first);
    node::metadata metadata= *node::get (selection[1]);
    metadata.properties.emplace ("example:note", NodeProperty (
      node::rich_text {tree (CONCAT, "hello", link (first))}));
    node::set (selection[1], metadata);
    auto v2= write_clipboard_xml (selection);
    QVERIFY (v2.find ("version=\"2\"") != std::string::npos);
    QVERIFY (read_clipboard_xml (v2) == selection);
    QVERIFY_EXCEPTION_THROWN (write_clipboard_xml (tree ("not an envelope")),
                              codec_exception);
    QVERIFY_EXCEPTION_THROWN (read_clipboard_xml (
      write_xml_v2 (tree ("not an envelope"), xml_kind::fragment)), codec_exception);
  }

  void duplicationRemapsOnlySemanticReferences () {
    tree paragraph= identified (tree (CONCAT, link (second), link (external),
      native_text ("tmfs://wikilink/" + second)), first);
    node::metadata metadata= *node::get (paragraph);
    metadata.properties.emplace ("example:target", NodeProperty (node::reference {second}));
    metadata.properties.emplace ("example:rich", NodeProperty (node::rich_text {link (second)}));
    metadata.properties.emplace (athena::document_node::artifact_bindings_property,
      NodeProperty (NodeProperty::dictionary {{"statement", NodeProperty (external)}}));
    node::set (paragraph, metadata);
    tree source (DOCUMENT, paragraph, identified (tree ("target"), second),
      tree (TRANSCLUDE, tree (TUPLE, native_text (second), native_text (external))));
    node::identity_map mapping;
    tree result= duplicate_source_nodes (source, &mapping);
    QVERIFY (mapping.size () == 2);
    QVERIFY (node::id (result[0]) == mapping.at (first));
    QVERIFY (node::id (result[1]) == mapping.at (second));
    QVERIFY (result[0][0] == link (mapping.at (second)));
    QVERIFY (result[0][1] == link (external));
    QVERIFY (result[0][2] == source[0][2]);
    QVERIFY (result[2][0][0] == tree (native_text (mapping.at (second))));
    QVERIFY (result[2][0][1] == tree (native_text (external)));
    const auto& props= node::get (result[0])->properties;
    QVERIFY (props.count (athena::document_node::artifact_bindings_property) == 0);
    QVERIFY (std::get<node::reference> (props.at ("example:target").data).id == mapping.at (second));
    QVERIFY (std::get<node::rich_text> (props.at ("example:rich").data).content == link (mapping.at (second)));
    QVERIFY (node::id (source[0]) == first);
    QVERIFY (node::get (source[0])->properties.count (
      athena::document_node::artifact_bindings_property) == 1);
    tree another= duplicate_source_nodes (source);
    QVERIFY (node::id (another[0]) != node::id (result[0]));
  }

  void externalLinksAndLegacyAnchorTransclusionsAreUntouched () {
    tree links (DOCUMENT,
      identified (tree ("target"), first),
      tree (HLINK, "external", native_text ("https://example.com/" + first)),
      tree (HLINK, "source", native_text ("tmfs://transclude/" + first + "/x%2Fy?q=a%20b#part")),
      tree (TRANSCLUDE, native_text (first), "old.ath", "begin", "end"));
    node::identity_map mapping;
    tree result= duplicate_source_nodes (links, &mapping);
    QVERIFY (result[1] == links[1]);
    QVERIFY (result[2][1] == tree (native_text (
      "tmfs://transclude/" + mapping.at (first) + "/x%2Fy?q=a%20b#part")));
    QVERIFY (result[3] == links[3]);
  }
};
QTEST_APPLESS_MAIN (TestDocumentNodeCopy)
#include "document_node_copy_test.moc"
