/******************************************************************************
* MODULE     : read_only_edit_test.cpp
* DESCRIPTION: Regression tests for read-only editor transactions
* COPYRIGHT  : (C) 2026  Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include <QApplication>
#include <QtTest/QtTest>
#include <cstdlib>
#include <unistd.h>

#include "ATHENA/server.hpp"
#include "Editor/edit_main.hpp"
#include "Graphics/Gui/gui.hpp"
#include "boot.hpp"
#include "data_cache.hpp"
#include "observer.hpp"
#include "scheme.hpp"
#include "sys_utils.hpp"
#include "tm_buffer.hpp"

bool headless_mode= true;
bool is_headless () { return true; }

static server_rep* test_server= nullptr;

class ReadOnlyTestEditorRep: public edit_main_rep {
public:
  ReadOnlyTestEditorRep (server_rep* server, tm_buffer buffer):
    editor_rep (server, buffer),
    edit_main_rep (server, buffer) {}

  inline void* derived_this () override { return (ReadOnlyTestEditorRep*) this; }
};

class TestReadOnlyEdit: public QObject {
  Q_OBJECT

private slots:
  void init ();
  void cleanup ();
  void contentMutationIsRolledBack ();

private:
  tm_buffer buffer= nullptr;
  ReadOnlyTestEditorRep* editor= nullptr;
};

void
TestReadOnlyEdit::init () {
  buffer     = tm_new<tm_buffer_rep> (url ("tmfs://ns/Test"));
  swap_current_document_tree (&buffer->document);
  buffer->data->init ("no-zoom")= "true";
  buffer->data->init (ZOOM_FACTOR)= "1";
  set_document (buffer->document, buffer->rp,
                tree (DOCUMENT, "original"));
  buffer->buf->read_only= true;
  editor= tm_new<ReadOnlyTestEditorRep> (test_server, buffer);
}

void
TestReadOnlyEdit::cleanup () {
  tm_delete<editor_rep> (editor);
  editor= nullptr;
  swap_current_document_tree (nullptr);
  tm_delete (buffer);
  buffer= nullptr;
}

void
TestReadOnlyEdit::contentMutationIsRolledBack () {
  const tree original= copy (subtree (current_document_tree (), buffer->rp));
  editor->go_to (buffer->rp * 0 * N (as_string (original[0])));

  editor->start_editing ();
  editor->insert_tree (" changed");
  editor->end_editing ();

  QVERIFY (subtree (current_document_tree (), buffer->rp) == original);
  QVERIFY (!editor->need_save ());
}

static int test_status= 1;

static void
run_tests (int argc, char** argv) {
  init_plugins ();
  gui_open (argc, argv);
  {
    server sv;
    test_server= sv->get_server ();
    eval ("(begin "
          "  (tm-define (notify-cursor-moved status) #f) "
          "  (tm-define (in-commutative-diagram?) #f) "
          "  (tm-define (like-emacs?) #f))");
    TestReadOnlyEdit test;
    test_status= QTest::qExec (&test, argc, argv);
    test_server= nullptr;
  }
  gui_close ();
  release_boot_lock ();
  std::exit (test_status);
}

int
main (int argc, char** argv) {
  qputenv ("QT_QPA_PLATFORM", "offscreen");
  QApplication app (argc, argv);
  const string test_home= "/tmp/athena-read-only-edit-test-" *
                          as_string ((int) getpid ());
  set_env ("ATHENA_HOME_PATH", test_home);
  cache_initialize ();
  reset_document_tree (current_document_tree ());
  init_athena ();
  start_scheme (argc, argv, run_tests);
  return test_status;
}

#include "read_only_edit_test.moc"
