/******************************************************************************
* MODULE     : table_resize_test.cpp
* DESCRIPTION: Table resize geometry, incremental layout and math exclusions
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* See the file LICENSE in the root directory.
******************************************************************************/

#include <QtTest/QtTest>
#include "table_resize.hpp"
#include "ATHENA/server.hpp"
#include "Editor/edit_main.hpp"
#include "Graphics/Gui/gui.hpp"
#include "boot.hpp"
#include "buffer_state.hpp"
#include "data_cache.hpp"
#include "scheme.hpp"
#include "sys_utils.hpp"
#include <cstdlib>
#include <unistd.h>

bool headless_mode= true;
bool is_headless () { return true; }
static server_rep* test_server= nullptr;

class TableTestEditor: public edit_main_rep {
public:
  TableTestEditor (buffer_document_state* buffer):
    editor_rep (test_server, buffer), edit_main_rep (test_server, buffer) {}
  void* derived_this () override { return this; }
  using edit_interface_rep::table_cell_bounds;
  using edit_interface_rep::table_resize_start;
  using edit_interface_rep::table_resize_update;
  using edit_interface_rep::table_resize_finish;
  using edit_interface_rep::hanging_reanchor_pending;
};

class TableResizeTest: public QObject {
  Q_OBJECT
private slots:
  void resizeOutsideCursorFreezesViewportThroughRelease () {
    auto* buffer= tm_new<buffer_document_state> (
      nullptr, "tmfs://table-resize-scroll-test", "", "UTF-8", false, 0);
    swap_current_document_tree (&buffer->document);
    tree table (TABLE, tree (ROW, tree (CELL, "Column"), tree (CELL, "Value")));
    set_document (buffer->document, buffer->root_path,
      tree (DOCUMENT, "Before", compound ("tabular", tree (TFORMAT, table)),
            "After"));
    auto* editor= tm_new<TableTestEditor> (buffer);
    editor->init_style ("generic");
    path fp= buffer->root_path * 1 * 0;
    for (int paragraph: {0, 2}) {
      editor->go_to_start (buffer->root_path * paragraph);
      editor->typeset_forced ();
      rectangle cell;
      QVERIFY (editor->table_cell_bounds (fp, 0, 0, cell));
      SI x= (cell->x1 + cell->x2) / 2, y= cell->y1;
      SI delta= 20 * editor->get_pixel_size ();
      editor->hanging_reanchor_pending= true;
      QVERIFY (editor->table_resize_start (x, y));
      QVERIFY (!editor->hanging_reanchor_pending);
      QVERIFY (editor->has_changed (THE_FREEZE));
      for (int step= 1; step <= 3; ++step) {
        editor->notify_change (0, THE_FREEZE);
        QVERIFY (editor->table_resize_update (x, y - step * delta));
        QVERIFY (editor->has_changed (THE_FREEZE));
        editor->typeset_forced ();
      }
      // Release requests reanchoring before it dispatches to the resize handler.
      editor->hanging_reanchor_pending= true;
      editor->notify_change (0, THE_FREEZE);
      editor->table_resize_finish ();
      QVERIFY (editor->has_changed (THE_FREEZE));
      QVERIFY (!editor->hanging_reanchor_pending);
    }
    tm_delete<editor_rep> (editor);
    swap_current_document_tree (nullptr);
    tm_delete (buffer);
  }

  void resizePreservesGeometryAndLocalInvalidation () {
    for (double zoom: {0.75, 1.0, 1.5, 2.0}) {
      auto* buffer= tm_new<buffer_document_state> (
        nullptr, "tmfs://table-resize-test", "", "UTF-8", false, 0);
      swap_current_document_tree (&buffer->document);
      tree nested= compound ("subtable", tree (TFORMAT, tree (TABLE,
        tree (ROW, tree (CELL, "Nested table with a longer line")),
        tree (ROW, tree (CELL, "Second nested row")))));
      tree table (TABLE,
        tree (ROW, tree (CELL, tree (WITH, "font-series", "bold", "Name")),
                   tree (CELL, "Value")),
        tree (ROW, tree (CELL, "Details"), tree (CELL, nested)));
      set_document (buffer->document, buffer->root_path,
        tree (DOCUMENT, compound ("tabular", tree (TFORMAT, table))));
      auto* editor= tm_new<TableTestEditor> (buffer);
      editor->init_style ("generic");
      editor->set_zoom_factor (zoom);
      path fp= buffer->root_path * 0 * 0;
      editor->go_to_start (fp * 0 * 0 * 0 * 0);
      editor->typeset_forced ();
      rectangle before;
      QVERIFY (editor->table_cell_bounds (fp, 0, 0, before));
      SI x= before->x2, y= (before->y1 + before->y2) / 2;
      SI delta= 24 * editor->get_pixel_size ();
      QVERIFY (editor->table_resize_start (x, y));
      editor->notify_change (0, THE_TREE | THE_ENVIRONMENT);
      QVERIFY (editor->table_resize_update (x, y));
      QVERIFY (!editor->has_changed (THE_TREE | THE_ENVIRONMENT));
      QVERIFY (editor->table_resize_update (x + delta, y));
      QVERIFY (!editor->has_changed (THE_ENVIRONMENT));
      editor->typeset_forced ();
      rectangle after;
      QVERIFY (editor->table_cell_bounds (fp, 0, 0, after));
      QCOMPARE (after->x2 - after->x1, before->x2 - before->x1 + delta);

      // The drag recognizer sends another start after the press. It must not
      // replace the original geometry or make a repeated move an edit.
      QVERIFY (editor->table_resize_start (x + delta, y));
      editor->notify_change (0, THE_TREE | THE_ENVIRONMENT);
      QVERIFY (editor->table_resize_update (x + delta, y));
      QVERIFY (!editor->has_changed (THE_TREE | THE_ENVIRONMENT));
      QVERIFY (editor->table_resize_update (x + 2 * delta, y));
      editor->typeset_forced ();
      QVERIFY (editor->table_cell_bounds (fp, 0, 0, after));
      QCOMPARE (after->x2 - after->x1, before->x2 - before->x1 + 2 * delta);
      editor->table_resize_finish ();

      SI rowY= after->y1;
      SI rowX= (after->x1 + after->x2) / 2;
      SI height= after->y2 - after->y1;
      QVERIFY (editor->table_resize_start (rowX, rowY));
      editor->notify_change (0, THE_TREE | THE_ENVIRONMENT);
      QVERIFY (editor->table_resize_update (rowX, rowY - delta));
      QVERIFY (!editor->has_changed (THE_ENVIRONMENT));
      editor->typeset_forced ();
      QVERIFY (editor->table_cell_bounds (fp, 0, 0, after));
      QCOMPARE (after->y2 - after->y1, height + delta);
      editor->table_resize_finish ();
      tm_delete<editor_rep> (editor);
      swap_current_document_tree (nullptr);
      tm_delete (buffer);
    }
  }
  void protectsEquationLayout () {
    tree table (TABLE, tree (ROW, tree (CELL, "x")));
    QVERIFY (table_mouse_resize_allowed (tree (DOCUMENT, table), path (0)));
    for (const char* name: {"eqnarray", "eqnarray*", "matrix", "matrix*",
                           "bmatrix", "Bmatrix", "det"}) {
      tree doc (DOCUMENT, compound (name, tree (TFORMAT, table)));
      QVERIFY (!table_mouse_resize_allowed (doc, path (0, 0)));
      QVERIFY (!table_mouse_resize_allowed (doc, path (0, 0, 0)));
      tree nested (DOCUMENT, compound (name, tree (TFORMAT,
        tree (TABLE, tree (ROW, tree (CELL, table))))));
      QVERIFY (table_mouse_resize_allowed (nested, path (0, 0, 0) * 0 * 0 * 0));
    }
  }

  void protectsDocumentWrappedEquationLayout () {
    tree table (TABLE, tree (ROW, tree (CELL, "x")));
    for (const char* name: {"eqnarray", "eqnarray*", "matrix", "matrix*",
                           "bmatrix", "Bmatrix", "det"}) {
      tree format (TFORMAT,
        tree (CWITH, "1", "-1", "1", "1", "cell-width", "100px"), table);
      // Source shape observed in the live editor, including an existing resize.
      tree doc (TUPLE, tree (DOCUMENT, tree (""),
        compound (name, tree (DOCUMENT, format))));
      QVERIFY (!table_mouse_resize_allowed (doc, path (0, 1, 0, 0)));
      QVERIFY (!table_mouse_resize_allowed (doc, path (0, 1, 0, 0) * 1));

      tree nested (DOCUMENT, compound (name, tree (DOCUMENT,
        tree (TFORMAT, tree (TABLE, tree (ROW,
          tree (CELL, tree (DOCUMENT, tree (TFORMAT, table)))))))));
      QVERIFY (table_mouse_resize_allowed (nested,
        path (0, 0, 0) * 0 * 0 * 0 * 0 * 0));
    }
  }
};

static int test_status= 1;
static void run_tests (int argc, char** argv) {
  init_system_state ();
  gui_open (argc, argv);
  server sv;
  test_server= sv->get_server ();
  eval ("(begin (tm-define (notify-cursor-moved status) #f) "
        "(tm-define (like-emacs?) #f))");
  TableResizeTest test;
  test_status= QTest::qExec (&test, argc, argv);
  std::_Exit (test_status);
}

int main (int argc, char** argv) {
  qputenv ("QT_QPA_PLATFORM", "offscreen");
  QApplication app (argc, argv);
  set_env ("ATHENA_HOME_PATH", "/tmp/athena-table-resize-test-" *
           as_string ((int) getpid ()));
  cache_initialize ();
  reset_document_tree (current_document_tree ());
  init_athena ();
  start_scheme (argc, argv, run_tests);
  return test_status;
}
#include "table_resize_test.moc"
