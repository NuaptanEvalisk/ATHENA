/******************************************************************************
* MODULE     : modification_metadata_test.cpp
* DESCRIPTION: Node metadata notifications, structural replay and history
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include <QtTest/QtTest>
#include "archiver.hpp"
#include "new_document.hpp"
#include "node_metadata.hpp"
#include "ATHENA/Data/document_node_model.hpp"

bool headless_mode= true;
bool is_headless () { return true; }

namespace {
const std::string first_id= "11111111-1111-4111-8111-111111111111";
const std::string second_id= "22222222-2222-4222-8222-222222222222";

tree annotated (tree t, const std::string& id= first_id) {
  athena::node::metadata value;
  value.id= id;
  value.properties.emplace ("caption", athena::node::property (std::string ("kept")));
  athena::node::set (t, value);
  return t;
}

class notification_probe: public observer_rep {
public:
  int announcements= 0, completions= 0, changes= 0, detachments= 0;
  bool headers_ready= true, metadata_was_old= true;
  modification forward= mod_assign (path (), "");
  modification backward= mod_assign (path (), "");
  tree old_header;

  void announce (tree& ref, modification mod) override {
    ++announcements;
    forward= copy (mod);
    backward= invert (mod, ref);
    old_header= node_header (ref);
  }
  void done (tree&, modification) override { ++completions; }
  void notify_set_metadata (tree& ref, tree) override {
    ++changes;
    metadata_was_old= metadata_was_old && athena::node::equal_metadata (ref, old_header);
  }
  void notify_split (tree& ref, int pos, tree) override {
    headers_ready= headers_ready &&
      node_header (ref[pos]) == forward->t[0] &&
      node_header (ref[pos+1]) == forward->t[1];
  }
  void notify_join (tree&, int, tree joined) override {
    headers_ready= headers_ready && node_header (joined) == single_node_header (forward);
  }
  void notify_detach (tree&, tree, bool) override { ++detachments; }
};
}

class ModificationMetadataTest: public QObject {
  Q_OBJECT
private slots:
  void metadataNotificationAndUndo () {
    tree source (DOCUMENT, tree (CONCAT, "text"));
    tree live= copy (source);
    tree child= live[0];
    auto* a= tm_new<notification_probe> ();
    auto* b= tm_new<notification_probe> ();
    observer oa (a), ob (b);
    attach_observer (live, oa);
    attach_observer (live, ob);
    observer pointer= tree_pointer_new (child);
    tree carrier= annotated (tree (TUPLE));
    modification m= mod_set_metadata (path (), carrier);
    tree clean= clean_apply (source, m);
    raw_apply (live, m);
    QVERIFY (live == clean);
    QVERIFY (!athena::node::get (source));
    QVERIFY (strong_equal (child, live[0]));
    QVERIFY (strong_equal (obtain_tree (pointer), live[0]));
    QCOMPARE (a->announcements, 1);
    QCOMPARE (b->announcements, 1);
    QCOMPARE (a->changes, 1);
    QCOMPARE (b->changes, 1);
    QCOMPARE (a->completions, 1);
    QVERIFY (a->metadata_was_old && b->metadata_was_old);
    modification undo= a->backward;
    raw_apply (live, m);
    QCOMPARE (a->announcements, 1);
    raw_apply (live, undo);
    QVERIFY (live == source);
    QCOMPARE (a->detachments, 0);
    tree_pointer_delete (pointer);
  }

  void splitIdentity_data () {
    QTest::addColumn<int> ("example");
    for (int i=0; i<10; ++i)
      QTest::newRow (qPrintable (QString::number (i))) << i;
  }

  void splitIdentity () {
    QFETCH (int, example);
    tree fragment ("ab");
    int at= 1;
    bool original_right= false;
    if (example == 1) { at= 0; original_right= true; }
    if (example == 2) at= 2;
    if (example == 3) { fragment= ""; at= 0; }
    if (example == 4) {
      fragment= tree (CONCAT, "", "abc"); original_right= true;
    }
    if (example == 5) {
      fragment= tree (DOCUMENT, tree (CONCAT, ""), "abc"); original_right= true;
    }
    if (example == 6) fragment= tree (CONCAT, "", tree (DOCUMENT, ""));
    if (example == 7) fragment= tree (CONCAT, tree (SPACE), "abc");
    if (example == 8) {
      fragment= tree (TUPLE, "abc"); at= 0; original_right= true;
    }
    if (example == 9) { fragment= tree (TUPLE); at= 0; }
    tree source (DOCUMENT, annotated (fragment));
    tree live= copy (source);
    auto* probe= tm_new<notification_probe> ();
    observer obs (probe);
    attach_observer (live, obs);
    modification m= mod_split (path (), 0, at);
    raw_apply (live, m);
    QVERIFY (has_node_headers (probe->forward));
    QVERIFY (probe->headers_ready);
    int original= original_right? 1: 0;
    QVERIFY (athena::node::id (live[original]) == first_id);
    QVERIFY (athena::node::valid_id (athena::node::id (live[1-original])));
    QVERIFY (athena::node::id (live[1-original]) != first_id);
    QVERIFY (athena::node::equal (
      athena::node::get (live[0])->properties.at ("caption"),
      athena::node::get (live[1])->properties.at ("caption")));
    tree expected= copy (live);
    modification forward= probe->forward, backward= probe->backward;
    QVERIFY (clean_apply (source, forward) == expected);
    raw_apply (live, backward);
    QVERIFY (live == source);
    raw_apply (live, forward);
    QVERIFY (live == expected);
    QVERIFY (probe->headers_ready);
  }

  void splitRichTextIdentities_data () {
    QTest::addColumn<bool> ("root_id");
    QTest::addColumn<bool> ("keep_right");
    QTest::newRow ("identified-left") << true << false;
    QTest::newRow ("identified-right") << true << true;
    QTest::newRow ("anonymous-left") << false << false;
    QTest::newRow ("anonymous-right") << false << true;
  }

  void splitRichTextIdentities () {
    QFETCH (bool, root_id);
    QFETCH (bool, keep_right);
    using namespace athena::node;
    const std::string body_id= "33333333-3333-4333-8333-333333333333";
    const std::string rich_child_id= "44444444-4444-4444-8444-444444444444";
    tree rich= annotated (tree (CONCAT, annotated (tree ("note"), rich_child_id)), second_id);
    metadata rich_attributes= *get (rich);
    rich_attributes.properties.emplace ("self", athena::node::property (reference {second_id}));
    if (root_id)
      rich_attributes.properties.emplace ("owner", athena::node::property (reference {first_id}));
    set (rich, rich_attributes);

    metadata attributes;
    if (root_id) attributes.id= first_id;
    attributes.properties.emplace ("note", athena::node::property (rich_text {rich}));
    attributes.properties.emplace ("note-ref", athena::node::property (reference {second_id}));
    attributes.properties.emplace ("child-ref", athena::node::property (reference {rich_child_id}));
    attributes.properties.emplace ("body-ref", athena::node::property (reference {body_id}));
    tree body= annotated (tree ("abc"), body_id);
    tree fragment= keep_right? tree (CONCAT, "", body): tree (CONCAT, body, "");
    set (fragment, attributes);
    tree source (DOCUMENT, fragment);
    tree live= copy (source);
    tree live_body= live[0][keep_right? 1: 0];
    modification forward= mod_split (path (), 0, 1);
    modification backward= invert (forward, source);
    raw_apply (live, forward);

    int retained= keep_right? 1: 0, fresh= 1-retained;
    QVERIFY (equal_metadata (live[retained], source[0]));
    QVERIFY (strong_equal (live[retained][0], live_body));
    const auto& fresh_attributes= *get (live[fresh]);
    tree fresh_rich= std::get<rich_text> (fresh_attributes.properties.at ("note").data).content;
    QVERIFY (valid_id (id (fresh_rich)) && id (fresh_rich) != second_id);
    QVERIFY (valid_id (id (fresh_rich[0])) && id (fresh_rich[0]) != rich_child_id);
    QVERIFY (std::get<reference> (fresh_attributes.properties.at ("note-ref").data).id == id (fresh_rich));
    QVERIFY (std::get<reference> (fresh_attributes.properties.at ("child-ref").data).id == id (fresh_rich[0]));
    QVERIFY (std::get<reference> (fresh_attributes.properties.at ("body-ref").data).id == body_id);
    QVERIFY (std::get<reference> (get (fresh_rich)->properties.at ("self").data).id == id (fresh_rich));
    if (root_id) {
      QVERIFY (valid_id (id (live[fresh])) && id (live[fresh]) != first_id);
      QVERIFY (std::get<reference> (get (fresh_rich)->properties.at ("owner").data).id == id (live[fresh]));
    }
    else QVERIFY (id (live[fresh]).empty ());

    tree expected= copy (live);
    QVERIFY (clean_apply (source, forward) == expected);
    raw_apply (live, backward);
    QVERIFY (live == source);
    raw_apply (live, forward);
    QVERIFY (live == expected);
  }

  void paragraphWrappingTransfersIdentityInOneEdit () {
    tree source= annotated (tree ("abc"));
    modification forward= mod_insert_node_preserving_identity (
      path (), 0, tree (CONCAT), source);
    modification backward= invert (forward, source);
    tree live= copy (source);
    raw_apply (live, forward);
    QVERIFY (is_concat (live));
    QVERIFY (athena::node::equal_metadata (live, source));
    QVERIFY (!athena::node::get (live[0]));
    QVERIFY (live[0] == tree ("abc"));
    tree expected= copy (live);
    QVERIFY (clean_apply (source, forward) == expected);
    raw_apply (live, backward);
    QVERIFY (live == source);
    raw_apply (live, forward);
    QVERIFY (live == expected);

    // Enter first splits the inner text, then the paragraph CONCAT. Only the
    // paragraph split allocates an identity; redoing either split reuses it.
    tree document (DOCUMENT, live);
    modification text_split= mod_split (path (0), 0, 1);
    raw_apply (document, text_split);
    QVERIFY (!athena::node::get (document[0][0]));
    QVERIFY (!athena::node::get (document[0][1]));
    modification paragraph_split= mod_split (path (), 0, 1);
    modification unsplit= invert (paragraph_split, document);
    tree before= copy (document);
    raw_apply (document, paragraph_split);
    QVERIFY (athena::node::id (document[0]) == first_id);
    QVERIFY (athena::node::valid_id (athena::node::id (document[1])));
    QVERIFY (athena::node::id (document[1]) != first_id);
    expected= copy (document);
    raw_apply (document, unsplit);
    QVERIFY (document == before);
    raw_apply (document, paragraph_split);
    QVERIFY (document == expected);
  }

  void joinRestoresLabelsAndProperties () {
    tree source (DOCUMENT, annotated (tree (CONCAT, "a")),
                 annotated (tree (DOCUMENT, "b"), second_id));
    tree live= copy (source);
    modification forward= mod_join (path (), 0);
    modification backward= invert (forward, source);
    raw_apply (live, forward);
    QVERIFY (L(live[0]) == CONCAT);
    QVERIFY (athena::node::id (live[0]) == first_id);
    tree expected= copy (live);
    raw_apply (live, backward);
    QVERIFY (live == source);
    raw_apply (live, forward);
    QVERIFY (live == expected);
    QVERIFY (clean_apply (expected, backward) == source);
  }

  void explicitEmptyHeadersClearMetadata () {
    tree source (DOCUMENT, "ab");
    tree headers (TUPLE, annotated (tree ("")), annotated (tree (""), second_id));
    modification split= mod_split (path (), 0, 1, headers);
    modification undo= invert (split, source);
    tree live= copy (source);
    raw_apply (live, split);
    raw_apply (live, undo);
    QVERIFY (live == source);
    QVERIFY (!athena::node::get (live[0]));
  }

  void legacyPayloadAndExplicitHeaderRoundTrip () {
    tree compounds (DOCUMENT, tree (CONCAT, "a"), tree (CONCAT, "b"));
    modification legacy_join= make_modification ("join", path (0), tree (""));
    QVERIFY (legacy_join == mod_join (path (), 0));
    QVERIFY (is_applicable (compounds, legacy_join));
    tree expected= clean_apply (compounds, legacy_join);
    tree live= copy (compounds);
    raw_apply (live, legacy_join);
    QVERIFY (live == expected);
    QVERIFY (get_tree (legacy_join) == tree (""));

    tree wrapper (CONCAT, tree (DOCUMENT, "a"));
    modification legacy_remove= make_modification ("remove-node", path (0), tree (""));
    QVERIFY (legacy_remove == mod_remove_node (path (), 0));
    QVERIFY (is_applicable (wrapper, legacy_remove));
    expected= clean_apply (wrapper, legacy_remove);
    live= copy (wrapper);
    raw_apply (live, legacy_remove);
    QVERIFY (live == expected);
    QVERIFY (get_tree (legacy_remove) == tree (""));

    tree atoms (DOCUMENT, annotated (tree ("a")), annotated (tree ("b"), second_id));
    modification clear_join= mod_join (path (), 0, tree (""));
    modification replay_join= make_modification (
      get_type (clear_join), get_path (clear_join), copy (get_tree (clear_join)));
    QVERIFY (replay_join == clear_join);
    QVERIFY (has_node_headers (replay_join));
    QVERIFY (single_node_header (replay_join) == tree (""));
    QVERIFY (get_tree (replay_join) == tree (TUPLE, tree ("")));
    live= copy (atoms);
    raw_apply (live, replay_join);
    QVERIFY (!athena::node::get (live[0]));
    QVERIFY (live == clean_apply (atoms, clear_join));

    tree outer= annotated (tree (CONCAT, "a"));
    modification clear_remove= mod_remove_node (path (), 0, tree (""));
    modification replay_remove= make_modification (
      get_type (clear_remove), get_path (clear_remove), copy (get_tree (clear_remove)));
    QVERIFY (replay_remove == clear_remove);
    QVERIFY (restores_child_header (replay_remove));
    QVERIFY (single_node_header (replay_remove) == tree (""));
    live= copy (outer);
    raw_apply (live, replay_remove);
    QVERIFY (!athena::node::get (live));
    QVERIFY (live == clean_apply (outer, clear_remove));
    tree transferred= clean_apply (outer, mod_remove_node (path (), 0));
    QVERIFY (athena::node::id (transferred) == first_id);
  }

  void unannotatedAndPropertiesOnly () {
    tree plain (DOCUMENT, "ab");
    modification split= mod_split (path (), 0, 1);
    tree live= clean_apply (plain, split);
    QVERIFY (!has_node_headers (split));
    QVERIFY (!athena::node::get (live[0]) && !athena::node::get (live[1]));
    modification join= mod_join (path (), 0);
    raw_apply (live, join);
    QVERIFY (!has_node_headers (join));
    QVERIFY (live == plain);
    tree properties (DOCUMENT, annotated (tree ("ab"), std::string ()));
    raw_apply (properties, mod_split (path (), 0, 1));
    QVERIFY (athena::node::get (properties[0]) && athena::node::get (properties[1]));
    QVERIFY (athena::node::id (properties[0]).empty ());
    QVERIFY (athena::node::id (properties[1]).empty ());
  }

  void wrapperTransferAndUndo () {
    tree source= annotated (tree (CONCAT, "text"));
    tree live= copy (source);
    modification remove= mod_remove_node (path (), 0);
    modification restore= invert (remove, source);
    tree expected= clean_apply (source, remove);
    raw_apply (live, remove);
    QVERIFY (live == expected);
    QVERIFY (athena::node::id (live) == first_id);
    raw_apply (live, restore);
    QVERIFY (live == source);
    QVERIFY (!athena::node::get (live[0]));
    QVERIFY (clean_apply (expected, restore) == source);
    tree conflict= annotated (tree (CONCAT, annotated (tree ("text"), second_id)));
    QVERIFY (!is_applicable (conflict, mod_remove_node (path (), 0)));

    tree original= annotated (tree ("text"), second_id);
    live= copy (original);
    modification insert= mod_insert_node (path (), 0, annotated (tree (CONCAT)));
    modification undo= invert (insert, original);
    raw_apply (live, insert);
    QVERIFY (athena::node::id (live) == first_id);
    QVERIFY (athena::node::id (live[0]) == second_id);
    raw_apply (live, undo);
    QVERIFY (live == original);
  }

  void cleanPreservesAncestors () {
    tree source= annotated (tree (DOCUMENT, annotated (tree ("abc"), second_id)));
    modification edit= mod_insert (path (0), 1, "x");
    tree clean= clean_apply (source, edit), live= copy (source);
    raw_apply (live, edit);
    QVERIFY (live == clean);
    QVERIFY (athena::node::equal_metadata (source, clean));
    QVERIFY (athena::node::equal_metadata (source[0], clean[0]));
    QVERIFY (source[0]->label == "abc");
    modification remove= mod_remove (path (0), 1, 1);
    raw_apply (live, remove);
    QVERIFY (live == source);
    QVERIFY (live == clean_apply (clean, remove));
    modification label= mod_assign_node (path (), CONCAT);
    raw_apply (live, label);
    QVERIFY (live == clean_apply (source, label));
  }

  void archivedReplayAndMetadataOnlyUndo () {
    tree document= make_document_tree ();
    with_document_tree context (&document);
    tree source (DOCUMENT, annotated (tree (CONCAT, "", "abc")));
    set_document (document, path (0), source);
    double author= new_author ();
    double previous= get_author ();
    set_author (author);
    {
      archiver history (author, path (0));
      split (document[0], 0, 1);
      history->confirm ();
      tree expected= copy (document[0]);
      QVERIFY (history->undo_possibilities () > 0);
      history->undo ();
      QVERIFY (document[0] == source);
      history->redo ();
      QVERIFY (document[0] == expected);
      set_metadata (document[0][0], tree (TUPLE));
      history->confirm ();
      QVERIFY (!athena::node::get (document[0][0]));
      history->undo ();
      QVERIFY (document[0] == expected);
      history->redo ();
      QVERIFY (!athena::node::get (document[0][0]));
    }
    set_author (previous);
  }

  void invalidPayloadAndCommutation () {
    tree source (DOCUMENT, "a", "b");
    QVERIFY (!is_applicable (source, mod_split (path (), 0, 0, tree (TUPLE, "bad"))));
    QVERIFY (!is_applicable (source, mod_join (path (), 0, tree (CONCAT))));
    QVERIFY (!is_applicable (source, make_modification ("join", path (0), tree (TUPLE))));
    QVERIFY (!is_applicable (tree (CONCAT, "a"),
      make_modification ("remove-node", path (0), tree (TUPLE))));
    modification a= mod_set_metadata (path (0), annotated (tree (TUPLE)));
    modification b= mod_insert (path (1), 0, "x");
    QVERIFY (commute (a, b));
    QVERIFY (!commute (a, mod_insert (path (0), 0, "x")));
    QVERIFY (is_nil (cursor_hint (patch (a, invert (a, source)), source)));
  }

  void preparedPropertyEditUsesOneUndoStep () {
    namespace model= athena::document_node;
    tree document= make_document_tree ();
    with_document_tree context (&document);
    set_document (document, path (0), tree (DOCUMENT, "Paragraph"));
    const tree before= copy (document[0]);
    double author= new_author (), previous= get_author ();
    set_author (author);
    {
      archiver history (author, path (0));
      model::property_edit edit;
      edit.ensure_id= true;
      edit.set["test:flag"]= athena::node::property (true);
      edit.set["test:title"]= athena::node::property (athena::node::rich_text {
        compound ("em", "Structured")});
      auto prepared= model::prepare_property_edit (document[0], {0}, edit);
      QVERIFY (prepared.ok () && prepared.change);
      ::apply (document[0], *prepared.change);
      history->confirm ();
      tree changed= copy (document[0]);
      QCOMPARE (athena::node::id (changed[0]), prepared.id);
      history->undo ();
      QCOMPARE (document[0], before);
      history->redo ();
      QCOMPARE (document[0], changed);
      QCOMPARE (athena::node::id (document[0][0]), prepared.id);
    }
    set_author (previous);
  }
};

QTEST_GUILESS_MAIN (ModificationMetadataTest)
#include "modification_metadata_test.moc"
