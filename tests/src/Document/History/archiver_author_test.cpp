/******************************************************************************
* MODULE     : archiver_author_test.cpp
* DESCRIPTION: Author handoff and cursor-only undo transaction regressions
* COPYRIGHT  : (C) 2026 ATHENA contributors
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
******************************************************************************/

#include <QtTest/QtTest>
#include "archiver.hpp"
#include "new_document.hpp"

bool headless_mode= true;
bool is_headless () { return true; }

namespace {
class restore_author {
  double previous= get_author ();
public:
  ~restore_author () { set_author (previous); }
};

void record_cursor (tree& document, double editor_author, int offset= 0) {
  ::apply (document, mod_set_cursor (path (0, 0), offset,
    compound ("cursor-clear", as_string (editor_author))));
}

void insert_quote_pair (tree& document) {
  // insert_tree with an interior cursor takes this structural path, even for
  // an atomic quote pair. One undo must remove the wrapper as well as text.
  const string quotes= "\xc2\xab  \xc2\xbb";
  ::apply (document, mod_insert_node (path (0, 0), 0, tree (CONCAT)));
  ::apply (document, mod_insert (path (0, 0), 0, tree (CONCAT, tree (quotes))));
  ::apply (document, mod_join (path (0, 0), 0));
  ::apply (document, mod_remove_node (path (0, 0), 0));
}
} // namespace

class ArchiverAuthorTest: public QObject {
  Q_OBJECT
private slots:
  void consecutiveEditorsUndoWholeStructuralInsertion () {
    restore_author restore;
    set_author (new_author ());
    for (int fixture= 0; fixture < 3; ++fixture) {
      tree document= make_document_tree ();
      with_document_tree context (&document);
      set_document (document, path (0), tree (DOCUMENT, ""));
      const tree before= copy (document[0]);
      const double author= new_author ();
      archiver history (author, path (0));

      // archive_state precedes start_editing: the cursor still has the last
      // editor's thread-local author, while its payload names this editor.
      record_cursor (document, author);
      set_author (author);
      insert_quote_pair (document);
      global_confirm ();
      const tree inserted= copy (document[0]);
      QCOMPARE (inserted, tree (DOCUMENT, "\xc2\xab  \xc2\xbb"));
      QCOMPARE (history->undo_possibilities (), 1);
      history->undo ();
      QCOMPARE (document[0], before);
      QCOMPARE (history->undo_possibilities (), 0);
      QCOMPARE (history->redo_possibilities (), 1);
      history->redo ();
      QCOMPARE (document[0], inserted);
    }
  }

  void pendingContentHandoffKeepsEarlierAuthorEdit () {
    restore_author restore;
    tree document= make_document_tree ();
    with_document_tree context (&document);
    set_document (document, path (0), tree (DOCUMENT, ""));
    const double earlier= new_author (), current= new_author ();
    archiver history (current, path (0));
    set_author (earlier);
    ::apply (document, mod_insert (path (0, 0), 0, tree ("a")));
    set_author (current);
    ::apply (document, mod_insert (path (0, 0), 1, tree ("b")));
    global_confirm ();

    QCOMPARE (document[0], tree (DOCUMENT, "ab"));
    history->undo ();
    QCOMPARE (document[0], tree (DOCUMENT, "a"));
    QCOMPARE (history->undo_possibilities (), 1);
    history->redo ();
    QCOMPARE (document[0], tree (DOCUMENT, "ab"));
  }

  void cursorOnlyConfirmationKeepsRedoAndSavepoints () {
    restore_author restore;
    tree document= make_document_tree ();
    with_document_tree context (&document);
    set_document (document, path (0), tree (DOCUMENT, ""));
    const double author= new_author (), other= new_author ();
    archiver history (author, path (0));

    set_author (other);
    record_cursor (document, author);
    global_confirm ();
    QVERIFY (!history->active ());
    QCOMPARE (history->undo_possibilities (), 0);
    QCOMPARE (history->redo_possibilities (), 0);
    QVERIFY (history->conform_save ());
    QVERIFY (history->conform_autosave ());

    set_author (author);
    ::apply (document, mod_insert (path (0, 0), 0, tree ("saved")));
    global_confirm ();
    history->notify_save ();
    history->notify_autosave ();
    history->undo ();
    QCOMPARE (document[0], tree (DOCUMENT, ""));
    QVERIFY (!history->conform_save ());
    const int redo_count= history->redo_possibilities ();

    set_author (other);
    record_cursor (document, author);
    global_confirm ();
    QVERIFY (!history->active ());
    QCOMPARE (history->undo_possibilities (), 0);
    QCOMPARE (history->redo_possibilities (), redo_count);
    set_author (author);
    history->redo ();
    QCOMPARE (document[0], tree (DOCUMENT, "saved"));
    QVERIFY (history->conform_save ());
    QVERIFY (history->conform_autosave ());
  }

  void slaveHandoffRetainsMarkerWithContent () {
    restore_author restore;
    tree document= make_document_tree ();
    with_document_tree context (&document);
    set_document (document, path (0), tree (DOCUMENT, ""));
    const double earlier= new_author (), current= new_author ();
    const double marker= new_marker ();
    archiver history (current, path (0));

    set_author (earlier);
    record_cursor (document, current);
    set_author (current);
    history->start_slave (marker);
    ::apply (document, mod_insert (path (0, 0), 0, tree ("x")));
    global_confirm ();
    QCOMPARE (history->undo_move_marker (), marker);
    history->undo ();
    QCOMPARE (document[0], tree (DOCUMENT, ""));
    QCOMPARE (history->undo_possibilities (), 0);
    QCOMPARE (history->redo_move_marker (), marker);
    history->redo ();
    QCOMPARE (document[0], tree (DOCUMENT, "x"));
    QCOMPARE (history->undo_move_marker (), marker);
  }

  void slaveHandoffAfterConfirmation_data () {
    QTest::addColumn<bool> ("pending_content");
    QTest::newRow ("cursor-only") << false;
    QTest::newRow ("content") << true;
  }
  void slaveHandoffAfterConfirmation () {
    QFETCH (bool, pending_content);
    restore_author restore;
    tree document= make_document_tree ();
    with_document_tree context (&document);
    set_document (document, path (0), tree (DOCUMENT, ""));
    const double earlier= new_author (), current= new_author ();
    const double marker= new_marker ();
    archiver history (current, path (0));
    set_author (earlier);
    if (pending_content)
      ::apply (document, mod_insert (path (0, 0), 0, tree ("a")));
    else record_cursor (document, current);
    const tree before= copy (document[0]);

    set_author (current);
    history->start_slave (marker);
    history->confirm ();
    QCOMPARE (history->undo_move_marker (), marker);
    // Retraction only succeeds for this archiver's author. A marker assigned
    // to the preceding author or to zero cannot be reopened by its owner.
    QVERIFY (history->retract ());
    ::apply (document, mod_insert (path (0, 0), pending_content ? 1 : 0, tree ("b")));
    global_confirm ();
    const tree changed= copy (document[0]);
    QCOMPARE (history->undo_move_marker (), marker);
    history->undo ();
    QCOMPARE (document[0], before);
    QCOMPARE (history->redo_move_marker (), marker);
    history->redo ();
    QCOMPARE (document[0], changed);
  }
};

QTEST_GUILESS_MAIN (ArchiverAuthorTest)
#include "archiver_author_test.moc"
