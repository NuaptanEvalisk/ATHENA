/******************************************************************************
* MODULE     : hodge_recolor_test.cpp
* DESCRIPTION: Regression tests for recoloring structured mathematics
* COPYRIGHT  : (C) 2026  Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include <QtTest/QtTest>
#include <QApplication>
#include <cstdlib>
#include <unistd.h>

#include "boot.hpp"
#include "convert.hpp"
#include "drd_std.hpp"
#include "ATHENA/server.hpp"
#include "Editor/edit_main.hpp"
#include "Graphics/Gui/gui.hpp"
#include "observer.hpp"
#include "scheme.hpp"
#include "data_cache.hpp"
#include "sys_utils.hpp"
#include "tm_buffer.hpp"

bool headless_mode= true;
bool is_headless () { return true; }

static server_rep* testServer= nullptr;

class TestEditorRep: public edit_main_rep {
public:
  TestEditorRep (server_rep* server, tm_buffer buffer):
    editor_rep (server, buffer),
    edit_main_rep (server, buffer) {}

  void setRawSelection (path p1, path p2) {
    cur_sel= simple_range (p1, p2);
  }

  void correctedSelection (path& p1, path& p2) {
    selection_get (p1, p2);
  }

  tree selectedTree () {
    return selection_get ();
  }

  inline void* derived_this () override { return (TestEditorRep*) this; }
};

class TestHodgeRecolor: public QObject {
  Q_OBJECT

private:
  static tree hodgeFormula ();

private slots:
  void init ();
  void cleanup ();
  void crossNodeCutRestoresInsertionSplice ();
  void mixedMathTextFormattingKeepsPosition ();
  void wikilinkDeletionKeepsInsertionAtSplice ();
  void patternRecolorKeepsOperatorBeforeCoexactTerm ();

private:
  tm_buffer buffer= nullptr;
  TestEditorRep* ed= nullptr;
};

tree
TestHodgeRecolor::hodgeFormula () {
  tree lhs= texmacs_to_tree (
    R"(<with|mode|math|C<rsup|i>=<with|color|#ff007f|<below|ker \<Delta\><rsub|<with|font|cal|H>>|harmonic>\|<rsub|C<rsup|i>>>\<oplus\><with|color|#5500ff|<below|im \<mathd\>\|<rsub|C<rsup|i-1>>|exact>>\<oplus\><with|color|#c1940e|<below|im d<rsup|\<ast\>>\|<rsub|C<rsup|i+1>>|coexact>>>)");
  if (is_func (lhs, DOCUMENT, 1)) lhs= lhs[0];

  // The following compound gives the corrected right boundary a surviving
  // owner, as it has in the real document where the Hodge formula is embedded.
  lhs[2] << tree (WITH, "color", "#222222", "following");
  return tree (DOCUMENT, lhs);
}

void
TestHodgeRecolor::init () {
  buffer     = tm_new<tm_buffer_rep> (url ("hodge-recolor-test.ath"));
  swap_current_document_tree (&buffer->document);
  buffer->data->init ("no-zoom")= "true";
  buffer->data->init (ZOOM_FACTOR)= "1";
  set_document (buffer->document, buffer->rp, hodgeFormula ());
  ed= tm_new<TestEditorRep> (testServer, buffer);
}

void
TestHodgeRecolor::cleanup () {
  tm_delete<editor_rep> (ed);
  ed= nullptr;
  swap_current_document_tree (nullptr);
  tm_delete (buffer);
  buffer= nullptr;
}

void
TestHodgeRecolor::crossNodeCutRestoresInsertionSplice () {
  const tree initial= tree (
    DOCUMENT,
    tree (CONCAT, "ab", tree (WITH, "color", "#123456", "middle"), "cd"));
  set_document (buffer->document, buffer->rp, initial);

  const path concatPath= buffer->rp * 0;
  ed->setRawSelection (concatPath * 0 * 1, concatPath * 2 * 1);
  ed->selection_cut ("none");
  ed->insert_tree ("X");

  QVERIFY (subtree (current_document_tree (), buffer->rp) == tree (DOCUMENT, "aXd"));
}

void
TestHodgeRecolor::mixedMathTextFormattingKeepsPosition () {
  const tree initial= tree (
    DOCUMENT,
    tree (CONCAT, "Before ", compound ("math", "A"), "-forms after"));
  set_document (buffer->document, buffer->rp, initial);

  const path concatPath= buffer->rp * 0;
  ed->setRawSelection (concatPath * 1 * 0 * 0,
                       concatPath * 2 * 6);
  path p1, p2;
  ed->correctedSelection (p1, p2);
  const tree selected= ed->selectedTree ();
  QCOMPARE (p1, concatPath * 1 * 0);
  QCOMPARE (p2, concatPath * 2 * 6);
  QVERIFY (selected == tree (
    CONCAT, compound ("math", "A"), "-forms"));
  ed->selection_cut ("none");
  ed->insert_tree (
    tree (WITH, "font-series", "bold", selected),
    path (2, end (selected)));

  const tree expected= tree (
    DOCUMENT,
    tree (CONCAT,
          "Before ",
          tree (WITH, "font-series", "bold",
                tree (CONCAT, compound ("math", "A"), "-forms")),
          " after"));
  const tree result= subtree (current_document_tree (), buffer->rp);
  QVERIFY (result == expected);
}

void
TestHodgeRecolor::wikilinkDeletionKeepsInsertionAtSplice () {
  tree link (HLINK);
  link << "linked text" << "tmfs://wikilink/uuid/file/anchor";
  const tree initial= tree (
    DOCUMENT,
    tree (CONCAT, "Before ", link, " after"));
  set_document (buffer->document, buffer->rp, initial);

  const path concatPath= buffer->rp * 0;
  ed->setRawSelection (concatPath * 1 * 0 * 0,
                       concatPath * 2 * 0);
  path p1, p2;
  ed->correctedSelection (p1, p2);
  QCOMPARE (p1, concatPath * 1 * 0);
  QCOMPARE (p2, concatPath * 2 * 0);
  QVERIFY (ed->selectedTree () == link);

  ed->selection_cut ("none");
  ed->insert_tree ("X");
  QVERIFY (subtree (current_document_tree (), buffer->rp) ==
           tree (DOCUMENT, "Before X after"));
}

void
TestHodgeRecolor::patternRecolorKeepsOperatorBeforeCoexactTerm () {
  const path concatPath= buffer->rp * 0 * 2;
  const path coexactPath= concatPath * 7;
  const path followingPath= concatPath * 8;
  const path rawStart= coexactPath * 2 * 0 * 0 * 0;
  const path rawEnd= followingPath * 0;

  const tree original= copy (subtree (current_document_tree (), concatPath));
  const tree oldCoexact= copy (original[7]);
  ed->setRawSelection (rawStart, rawEnd);

  path p1, p2;
  ed->correctedSelection (p1, p2);
  QCOMPARE (p1, coexactPath * 0);
  QCOMPARE (p2, followingPath * 0);
  QVERIFY (ed->selectedTree () == oldCoexact);

  // Non-string color values follow make-with -> insert-go-to ->
  // selection_cut/selection_paste, unlike ordinary string colors.
  const tree pattern= compound (
    "pattern", "/tmp/wood-light.png", "", "");
  const string clipboardKey= "hodge-recolor-test";
  ed->selection_cut (clipboardKey);
  ed->insert_tree (tree (WITH, "color", pattern, ""), path (2, 0));
  ed->selection_paste (clipboardKey);
  ed->selection_clear (clipboardKey);

  tree expected= copy (original);
  expected[7]= tree (WITH, "color", pattern, oldCoexact);
  const tree result= subtree (current_document_tree (), concatPath);
  QCOMPARE (N(result), N(expected));
  QVERIFY (result[6] == "<oplus>");
  QVERIFY (result[7] == expected[7]);
  QVERIFY (result[8] == original[8]);
  QVERIFY (result == expected);
}

static int testStatus= 1;

static void
runTests (int argc, char** argv) {
  init_plugins ();
  gui_open (argc, argv);

  {
    server sv;
    testServer= sv->get_server ();
    eval ("(begin "
          "  (tm-define (notify-cursor-moved status) #f) "
          "  (tm-define (in-commutative-diagram?) #f) "
          "  (tm-define (like-emacs?) #f))");
    TestHodgeRecolor test;
    testStatus= QTest::qExec (&test, argc, argv);
    testServer= nullptr;
  }
  gui_close ();
  release_boot_lock ();
  std::exit (testStatus);
}

int
main (int argc, char** argv) {
  qputenv ("QT_QPA_PLATFORM", "offscreen");
  QApplication app (argc, argv);
  const string testHome= "/tmp/athena-hodge-recolor-test-" *
                         as_string ((int) getpid ());
  set_env ("ATHENA_HOME_PATH", testHome);
  cache_initialize ();
  reset_document_tree (current_document_tree ());
  init_athena ();
  start_scheme (argc, argv, runTests);
  return testStatus;
}

#include "hodge_recolor_test.moc"
