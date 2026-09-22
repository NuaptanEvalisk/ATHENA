/******************************************************************************
* MODULE     : utf8_editor_test.cpp
* DESCRIPTION: UTF-8 grapheme navigation, deletion and undo in native editors
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#include <QApplication>
#include <QtTest/QtTest>
#include <cstdlib>
#include <stdexcept>
#include <thread>
#include <unistd.h>

#include "ATHENA/server.hpp"
#include "Editor/edit_main.hpp"
#include "Graphics/Gui/gui.hpp"
#include "boot.hpp"
#include "buffer_state.hpp"
#include "data_cache.hpp"
#include "drd_mode.hpp"
#include "scheme.hpp"
#include "sys_utils.hpp"
#include "tree_traverse.hpp"
#include "utf8_edit.hpp"

bool headless_mode= true;
bool is_headless () { return true; }
static server_rep* test_server= nullptr;

class Utf8TestEditor: public edit_main_rep {
public:
  Utf8TestEditor (server_rep* server, buffer_document_state* buffer):
    editor_rep (server, buffer), edit_main_rep (server, buffer) {}
  void* derived_this () override { return this; }
};

class TestUtf8Editor: public QObject {
  Q_OBJECT
private slots:
  void init () {
    buffer= tm_new<buffer_document_state> (nullptr, "tmfs://utf8-test", "", "UTF-8", false, 0);
    swap_current_document_tree (&buffer->document);
    buffer->data->init ("no-zoom")= "true";
    buffer->data->init (ZOOM_FACTOR)= "1";
    set_document (buffer->document, buffer->root_path, tree (DOCUMENT, ""));
    editor= tm_new<Utf8TestEditor> (test_server, buffer);
  }
  void cleanup () {
    tm_delete<editor_rep> (editor);
    swap_current_document_tree (nullptr);
    tm_delete (buffer);
  }
  void treeNavigation () {
    const string text= "A\xe4\xb8\xad" "e\xcc\x81"
      "\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x92\xbb"
      "\xf0\x9f\x87\xa8\xf0\x9f\x87\xb3" "<alpha>";
    const tree value (DOCUMENT, text);
    const std::vector<int> stops {0, 1, 4, 7, 18, 26, 27, 28, 29, 30, 31, 32, 33};
    for (std::size_t i= 0; i < stops.size (); ++i) {
      const path at (0, stops[i]);
      QVERIFY (is_inside (value, at));
      QVERIFY (is_accessible_cursor (value, at));
      QVERIFY (valid_cursor (value, at));
      if (i + 1 < stops.size ())
        QVERIFY (next_valid (value, at) == path (0, stops[i + 1]));
      if (i > 0)
        QVERIFY (previous_valid (value, at) == path (0, stops[i - 1]));
    }
    for (int inside: {2, 3, 5, 6, 8, 11, 14, 17, 19, 22, 25}) {
      QVERIFY (!is_inside (value, path (0, inside)));
      QVERIFY (!is_accessible_cursor (value, path (0, inside)));
      QVERIFY (!valid_cursor (value, path (0, inside)));
      const auto left= correct_cursor (value, path (0, inside), false);
      const auto right= correct_cursor (value, path (0, inside), true);
      QVERIFY (is_inside (value, left) && is_inside (value, right));
      QVERIFY (last_item (left) < inside && last_item (right) > inside);
      QVERIFY (closest_inside (value, path (0, inside)) == right);
    }
    const tree binary (RAW_DATA, string ("\xff\x80\0", 3));
    QVERIFY (is_inside (binary, path (0, 0)));
    QVERIFY (!is_accessible_cursor (binary, path (0, 0)));
    const int old_access= set_access_mode (DRD_ACCESS_SOURCE);
    const bool source_access= is_accessible_cursor (binary, path (0, 0));
    set_access_mode (old_access);
    QVERIFY (source_access);
    QVERIFY (valid_cursor (binary, path (0, 0)));
    QVERIFY (!valid_cursor (binary, path (0, 1)));
    QVERIFY (closest_inside (binary, path (0, 2)) == path (0, 0));
    QVERIFY (next_valid (binary, path (0, 0)) == path (1));
  }
  void revisionsAndOwners () {
    string original= "long source: e\xcc\x81";
    string changed= original;
    const int size= N(original);
    QCOMPARE (utf8_grapheme_previous (original, size), size - 3);
    changed.set (size - 3, 'x');
    changed.resize (size - 2);
    QCOMPARE (utf8_grapheme_previous (changed, N(changed)), size - 3);
    QCOMPARE (utf8_grapheme_next (original, size - 3), size);
    for (int i= 0; i < 12; ++i) {
      const string other= as_string (i) * " e\xcc\x81";
      QCOMPARE (utf8_grapheme_previous (other, N(other)), N(other) - 3);
    }
    QCOMPARE (utf8_grapheme_next (original, size - 3), size);
    bool worker_ok= false;
    std::thread worker ([&] {
      worker_ok= utf8_grapheme_next (original, size - 3) == size;
    });
    worker.join ();
    QVERIFY (worker_ok);
    QVERIFY_EXCEPTION_THROWN (utf8_grapheme_next (string ("\xff"), 0), std::invalid_argument);
    QCOMPARE (utf8_grapheme_next (original, size - 3), size);
  }
  void deletionAndUndo_data () {
    QTest::addColumn<bool> ("forward");
    QTest::newRow ("delete-zwj") << true;
    QTest::newRow ("backspace-combining") << false;
  }
  void deletionAndUndo () {
    QFETCH (bool, forward);
    const string cluster= "\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x92\xbb";
    const string source= "A" * cluster * "e\xcc\x81";
    const path atom= buffer->root_path * 0;
    editor->go_to (atom * 0);
    editor->start_editing ();
    editor->insert_tree (source);
    editor->end_editing ();
    QVERIFY (editor->the_path () == atom * N(source));
    QVERIFY (subtree (current_document_tree (), atom) == tree (source));

    editor->selection_set_paths (atom * 2, atom * 8);
    QVERIFY (editor->selection_get_start () == atom * 1);
    QVERIFY (editor->selection_get_end () == atom * (1 + N(cluster)));
    QVERIFY (editor->selection_get () == tree (cluster));
    editor->selection_cancel ();

    const string expected= forward ? string ("Ae\xcc\x81") : "A" * cluster;
    const int cursor= forward ? 1 : 1 + N(cluster);
    editor->go_to (atom * (forward ? 1 : N(source)));
    editor->archive_state ();
    editor->start_editing ();
    editor->remove_text (forward);
    editor->end_editing ();
    QVERIFY (subtree (current_document_tree (), atom) == tree (expected));
    QVERIFY (editor->the_path () == atom * cursor);
    QVERIFY (editor->undo_possibilities () > 0);
    editor->undo (0);
    QVERIFY (subtree (current_document_tree (), atom) == tree (source));
    QVERIFY (editor->redo_possibilities () > 0);
    editor->redo (0);
    QVERIFY (subtree (current_document_tree (), atom) == tree (expected));
  }
private:
  buffer_document_state* buffer= nullptr;
  Utf8TestEditor* editor= nullptr;
};

static void run_tests (int argc, char** argv) {
  init_system_state ();
  gui_open (argc, argv);
  int status;
  {
    server sv;
    test_server= sv->get_server ();
    eval ("(begin (tm-define (notify-cursor-moved status) #f) "
          "(tm-define (like-emacs?) #f))");
    TestUtf8Editor test;
    status= QTest::qExec (&test, argc, argv);
    test_server= nullptr;
  }
  gui_close ();
  release_boot_lock ();
  std::exit (status);
}

int main (int argc, char** argv) {
  qputenv ("QT_QPA_PLATFORM", "offscreen");
  QApplication app (argc, argv);
  set_env ("ATHENA_HOME_PATH", "/tmp/athena-utf8-editor-test-" * as_string ((int) getpid ()));
  cache_initialize ();
  reset_document_tree (current_document_tree ());
  init_athena ();
  start_scheme (argc, argv, run_tests);
  return 1;
}

#include "utf8_editor_test.moc"
