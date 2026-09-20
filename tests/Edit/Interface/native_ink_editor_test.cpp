/******************************************************************************
* MODULE     : native_ink_editor_test.cpp
* DESCRIPTION: Native in-document ink persistence and undo regression
* COPYRIGHT  : (C) 2026  Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
******************************************************************************/

#include <QApplication>
#include <QtTest/QtTest>
#include <cstdlib>
#include <unistd.h>

#include "ATHENA/server.hpp"
#include "Editor/edit_main.hpp"
#include "Graphics/Gui/gui.hpp"
#include "boot.hpp"
#include "buffer_state.hpp"
#include "convert.hpp"
#include "data_cache.hpp"
#include "observer.hpp"
#include "scheme.hpp"
#include "sys_utils.hpp"

bool headless_mode= true;
bool is_headless () { return true; }

static server_rep* test_server= nullptr;

class NativeInkTestEditorRep: public edit_main_rep {
public:
  NativeInkTestEditorRep (server_rep* server, buffer_document_state* buffer):
    editor_rep (server, buffer), edit_main_rep (server, buffer) {}

  inline void* derived_this () override { return (NativeInkTestEditorRep*) this; }
};

static int
count_label (tree t, tree_label label) {
  if (is_atomic (t)) return 0;
  int result= L(t) == label ? 1 : 0;
  for (int i=0; i<N(t); ++i) result += count_label (t[i], label);
  return result;
}

static tree
ink_document () {
  tree mode (TUPLE);
  mode << "hand-edit" << "penscript";
  tree origin (TUPLE);
  origin << "0.5gw" << "0.5gh";
  tree frame (TUPLE);
  frame << "scale" << "1cm" << origin;
  tree geometry (TUPLE);
  geometry << "geometry" << "12cm" << "6cm" << "center";
  tree graphics (GRAPHICS);
  graphics << "";
  tree wrapped (WITH);
  wrapped << "gr-mode" << mode
          << "gr-frame" << frame
          << "gr-geometry" << geometry
          << "gr-color" << "#2040a0"
          << "gr-line-width" << "2ln"
          << graphics;
  return tree (DOCUMENT, wrapped, "outside graphics");
}

class TestNativeInkEditor: public QObject {
  Q_OBJECT

private slots:
  void init ();
  void cleanup ();
  void strokePersistsAndUndoesAsOneTransaction ();

private:
  buffer_document_state* buffer= nullptr;
  NativeInkTestEditorRep* editor= nullptr;
};

void
TestNativeInkEditor::init () {
  buffer= tm_new<buffer_document_state> (
    nullptr, "native-ink-editor-test.ath", "", "native-ink-editor-test",
    false, 0);
  swap_current_document_tree (&buffer->document);
  buffer->data->init ("no-zoom")= "true";
  buffer->data->init (ZOOM_FACTOR)= "1";
  set_document (buffer->document, buffer->root_path, ink_document ());
  editor= tm_new<NativeInkTestEditorRep> (test_server, buffer);
  editor->init_style ("generic");
}

void
TestNativeInkEditor::cleanup () {
  tm_delete<editor_rep> (editor);
  editor= nullptr;
  swap_current_document_tree (nullptr);
  tm_delete (buffer);
  buffer= nullptr;
}

void
TestNativeInkEditor::strokePersistsAndUndoesAsOneTransaction () {
  const path graphics_path= buffer->root_path * 0 * 10;
  editor->go_to (graphics_path * 0 * 0);

  SI tx1= 0, ty1= 0, tx2= 0, ty2= 0;
  editor->typeset (tx1, ty1, tx2, ty2);
  SI gx1= 0, gy1= 0, gx2= 0, gy2= 0;
  QVERIFY (editor->find_graphical_region (gx1, gy1, gx2, gy2));
  SI left= min (gx1, gx2), right= max (gx1, gx2);
  SI bottom= min (gy1, gy2), top= max (gy1, gy2);
  QVERIFY (right > left);
  QVERIFY (top > bottom);

  native_ink_sample samples[4];
  for (int i=0; i<4; ++i) {
    samples[i].x= left + ((i + 1) * (right - left)) / 5;
    samples[i].y= bottom + ((i + 2) * (top - bottom)) / 7;
    samples[i].time= 100.0 + i;
    samples[i].pressure= 0.4 + 0.15 * i;
  }
  editor->commit_native_ink_stroke (samples, 4);

  tree after= copy (subtree (current_document_tree (), buffer->root_path));
  QCOMPARE (count_label (after, PENSCRIPT), 1);
  string serialized= tree_to_texmacs (after);
  QVERIFY (occurs ("athena-ink-", serialized));
  tree reparsed= texmacs_to_tree (serialized);
  QCOMPARE (count_label (reparsed, PENSCRIPT), 1);

  editor->go_to (buffer->root_path * 1 * 0);
  QVERIFY (editor->undo_possibilities () >= 1);
  editor->undo (0);
  tree undone= subtree (current_document_tree (), buffer->root_path);
  QCOMPARE (count_label (undone, PENSCRIPT), 0);

  QVERIFY (editor->redo_possibilities () >= 1);
  editor->redo (0);
  tree redone= subtree (current_document_tree (), buffer->root_path);
  QCOMPARE (count_label (redone, PENSCRIPT), 1);
}

static int test_status= 1;

static void
run_tests (int argc, char** argv) {
  init_system_state ();
  gui_open (argc, argv);
  {
    server sv;
    test_server= sv->get_server ();
    eval ("(begin "
          "  (tm-define (notify-cursor-moved status) #f) "
          "  (tm-define (in-commutative-diagram?) #f) "
          "  (tm-define (like-emacs?) #f) "
          "  (tm-define (graphics-undo-enabled) #t) "
          "  (tm-define (graphics-reset-context . args) #f))");
    TestNativeInkEditor test;
    test_status= QTest::qExec (&test, argc, argv);
    // Several legacy full-editor Qt tests currently crash during global
    // process teardown after QTest has completed successfully.  This focused
    // regression owns and releases its editor/buffer in cleanup(), so avoid
    // entering that unrelated global teardown path here.
    std::_Exit (test_status);
  }
}

int
main (int argc, char** argv) {
  qputenv ("QT_QPA_PLATFORM", "offscreen");
  QApplication app (argc, argv);
  const string test_home= "/tmp/athena-native-ink-editor-test-" *
                          as_string ((int) getpid ());
  set_env ("ATHENA_HOME_PATH", test_home);
  cache_initialize ();
  reset_document_tree (current_document_tree ());
  init_athena ();
  start_scheme (argc, argv, run_tests);
  return test_status;
}

#include "native_ink_editor_test.moc"
