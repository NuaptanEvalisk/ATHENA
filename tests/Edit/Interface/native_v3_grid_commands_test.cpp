/******************************************************************************
* MODULE     : native_v3_grid_commands_test.cpp
* DESCRIPTION: Actor-owned native MathGrid numbering commands and undo
* COPYRIGHT  : (C) 2026 ATHENA contributors
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
******************************************************************************/
#include <QApplication>
#include <QTemporaryDir>
#include <QtTest/QtTest>
#include <cstdlib>

#include "ATHENA/server.hpp"
#include "Editor/edit_main.hpp"
#include "Graphics/Gui/gui.hpp"
#include "Interface/table_commands.hpp"
#include "boot.hpp"
#include "buffer_state.hpp"
#include "data_cache.hpp"
#include "math_tree_v3.hpp"
#include "node_metadata.hpp"
#include "scheme.hpp"
#include "sys_utils.hpp"
#include "Xml/athena_document_xml.hpp"

bool headless_mode= true;
bool is_headless () { return true; }
static server_rep* test_server= nullptr;

namespace {
namespace math= athena::math_v3;
namespace commands= athena::table_commands;
namespace node= athena::node;

class GridEditor: public edit_main_rep {
public:
  GridEditor (server_rep* server, buffer_document_state* buffer):
    editor_rep (server, buffer), edit_main_rep (server, buffer) {}
  void* derived_this () override { return this; }
};

tree native_grid (const char* kind) {
  tree result (MATH_GRID, tree (ROW, tree (CELL, "x")),
                          tree (ROW, tree (CELL, "y")));
  node::metadata metadata;
  metadata.properties["math:grid-kind"]= node::property (std::string (kind));
  node::set (result, metadata);
  for (int i= 0; i < N(result); ++i) {
    node::metadata identity;
    identity.id= node::new_id ();
    node::set (result[i], identity);
  }
  return result;
}

tree block (tree body, bool numbered) {
  tree result (MATH_BLOCK, body);
  node::metadata metadata;
  metadata.properties["math:block-numbered"]= node::property (numbered);
  node::set (result, metadata);
  return result;
}

std::string serialized (tree source) {
  return athena::document::write_xml_v3 (source);
}
}

class NativeV3GridCommandsTest: public QObject {
  Q_OBJECT
  buffer_document_state* buffer= nullptr;
  GridEditor* editor= nullptr;
  path row_path () const { return buffer->root_path * path (0, 0, 0); }
  tree source () const { return editor->the_buffer (); }
  tree row () const { return editor->the_subtree (row_path ()); }
  tree target () const { return editor->the_subtree (path_up (row_path ())); }
  void reset (tree content) {
    set_document (buffer->document, buffer->root_path, tree (DOCUMENT, content));
    editor->go_to (row_path () * path (0, 0, 1));
  }

private slots:
  void init () {
    buffer= tm_new<buffer_document_state> (
      nullptr, "tmfs://native-v3-grid-test", "", "UTF-8", false, 0);
    swap_current_document_tree (&buffer->document);
    buffer->storage_version= athena::document::xml_storage_version::v3;
    buffer->data->init ("no-zoom")= "true";
    buffer->data->init (ZOOM_FACTOR)= "1";
    set_document (buffer->document, buffer->root_path, tree (DOCUMENT, ""));
    editor= tm_new<GridEditor> (test_server, buffer);
  }
  void cleanup () {
    tm_delete<editor_rep> (editor);
    swap_current_document_tree (nullptr);
    tm_delete (buffer);
  }
  void toggleCanonicalPolicyWithoutMovingCursor () {
    for (const char* kind: {"eqnarray", "align", "stack"})
      for (bool inherited: {false, true}) {
        tree content= native_grid (kind);
        math::set_row_tag (content[0], tree (CONCAT, "Tag ", compound ("math", "x")));
        reset (block (content, inherited));
        const std::string before= serialized (source ());
        const std::string id= node::id (row ());
        const path cursor= editor->the_path ();
        QVERIFY (commands::native_grid_numbering_available (editor, target ()));
        QCOMPARE (commands::eqnarray_numbered (editor, target ()), inherited);
        editor->start_editing ();
        QVERIFY (commands::eqnarray_toggle_numbering (editor, target ()));
        editor->end_editing ();
        QCOMPARE (commands::eqnarray_numbered (editor, target ()), !inherited);
        QCOMPARE (math::get_row_numbering (row ()), inherited ?
          math::row_numbering::unnumbered : math::row_numbering::numbered);
        QCOMPARE (node::id (row ()), id);
        QCOMPARE (editor->the_path (), cursor);
        QVERIFY (*math::get_row_tag (row ()) == *math::get_row_tag (content[0]));
        editor->start_editing ();
        QVERIFY (commands::eqnarray_toggle_numbering (editor, target ()));
        editor->end_editing ();
        QCOMPARE (math::get_row_numbering (row ()), math::row_numbering::inherit);
        QCOMPARE (serialized (source ()), before);
      }
  }
  void numberingUndoPreservesRowIdentity () {
    reset (block (native_grid ("align"), true));
    const std::string before= serialized (source ());
    editor->archive_state ();
    editor->start_editing ();
    QVERIFY (commands::eqnarray_toggle_numbering (editor, target ()));
    editor->end_editing ();
    const std::string after= serialized (source ());
    QVERIFY (before != after);
    QVERIFY (editor->undo_possibilities () > 0);
    editor->undo (0);
    QCOMPARE (serialized (source ()), before);
    editor->redo (0);
    QCOMPARE (serialized (source ()), after);
  }
  void unrelatedAndWholeBlockTargetsRejected () {
    reset (block (native_grid ("align"), true));
    const std::string before= serialized (source ());
    QVERIFY (!commands::native_grid_numbering_available (editor, copy (target ())));
    QVERIFY (!commands::eqnarray_toggle_numbering (editor, copy (target ())));
    QCOMPARE (serialized (source ()), before);
    for (const char* kind: {"matrix", "cases", "generic"}) {
      reset (block (native_grid (kind), true));
      const std::string original= serialized (source ());
      QVERIFY (!commands::native_grid_numbering_available (editor, target ()));
      QVERIFY (!commands::eqnarray_toggle_numbering (editor, target ()));
      QCOMPARE (serialized (source ()), original);
    }
  }
  void nestedGridDoesNotToggleOuterRow () {
    tree outer= native_grid ("align");
    outer[0][0][0]= native_grid ("stack");
    reset (block (outer, true));
    editor->go_to (row_path () * path (0, 0, 0) * path (0, 0, 1));
    const std::string original= serialized (source ());
    QVERIFY (!commands::native_grid_numbering_available (editor, target ()));
    QVERIFY (!commands::eqnarray_toggle_numbering (editor, target ()));
    QCOMPARE (serialized (source ()), original);
  }
  void readOnlyDoesNotMutate () {
    reset (block (native_grid ("align"), true));
    const std::string before= serialized (source ());
    buffer->read_only= true;
    QVERIFY (commands::eqnarray_toggle_numbering (editor, target ()));
    QCOMPARE (serialized (source ()), before);
  }
};

static void run_tests (int argc, char** argv) {
  init_system_state ();
  gui_open (argc, argv);
  int status;
  {
    server server;
    test_server= server.operator-> ();
    NativeV3GridCommandsTest test;
    status= QTest::qExec (&test, argc, argv);
    test_server= nullptr;
  }
  gui_close ();
  release_boot_lock ();
  std::exit (status);
}

int main (int argc, char** argv) {
  qputenv ("QT_QPA_PLATFORM", "offscreen");
  QApplication application (argc, argv);
  QTemporaryDir profile;
  if (!profile.isValid ()) return 1;
  set_env ("ATHENA_HOME_PATH", string (profile.path ().toUtf8 ().constData ()));
  cache_initialize ();
  reset_document_tree (current_document_tree ());
  init_athena ();
  start_scheme (argc, argv, run_tests);
  return 1;
}

#include "native_v3_grid_commands_test.moc"
