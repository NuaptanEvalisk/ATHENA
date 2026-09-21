/******************************************************************************
* MODULE     : commutative_diagram_native_editor_test.cpp
* DESCRIPTION: Native commutative diagram editor and undo regressions
*******************************************************************************
*/

#include <QApplication>
#include <QtTest/QtTest>
#include <cstdlib>
#include <functional>
#include <unistd.h>

#include "ATHENA/Math/commutative_diagram_native.hpp"
#include "ATHENA/server.hpp"
#include "Editor/edit_main.hpp"
#include "Graphics/Gui/gui.hpp"
#include "boot.hpp"
#include "buffer_state.hpp"
#include "data_cache.hpp"
#include "observer.hpp"
#include "scheme.hpp"
#include "sys_utils.hpp"

bool headless_mode= true;
bool is_headless () { return true; }

static server_rep* test_server= nullptr;

class DiagramTestEditorRep: public edit_main_rep {
public:
  DiagramTestEditorRep (server_rep* server, buffer_document_state* buffer):
    editor_rep (server, buffer), edit_main_rep (server, buffer) {}
  inline void* derived_this () override { return (DiagramTestEditorRep*) this; }
};

static tree
diagram_vertex (const char* id, const char* x, const char* y, const char* label) {
  tree result (make_tree_label ("cd-vertex"), 4);
  result[0]= id;
  result[1]= x;
  result[2]= y;
  result[3]= compound ("math", tree (label));
  return result;
}

static tree
diagram_arrow (const char* id, const char* source, const char* target) {
  tree result (make_tree_label ("cd-arrow"), 5);
  result[0]= id;
  result[1]= source;
  result[2]= target;
  result[3]= compound ("math", tree ("f"));
  result[4]= cd_default_arrow_options ();
  return result;
}

static tree
diagram_document () {
  tree body (make_tree_label ("cd-body"), 4);
  body[0]= "";
  body[1]= diagram_vertex ("a", "-2", "-1", "A");
  body[2]= diagram_vertex ("b", "2", "1", "B");
  body[3]= diagram_arrow ("f", "a", "b");
  tree diagram (COMMUTATIVE_DIAGRAM);
  diagram << "8" << "5" << body;
  return tree (DOCUMENT, compound ("math", diagram));
}

class TestNativeCommutativeDiagramEditor: public QObject {
  Q_OBJECT
private slots:
  void init ();
  void cleanup ();
  void optionAndReverseAreSingleUndoTransactions ();
  void keyboardDeleteIsNativeAndUndoable ();
  void trimRecentersAndUndoes ();
  void insertCommandCreatesNativeDiagram ();

private:
  buffer_document_state* buffer= nullptr;
  DiagramTestEditorRep* editor= nullptr;
  actor_ui_endpoint* endpoint= nullptr;
  path diagram_path;
  path body_path;

  void focusBody ();
  commutative_diagram_session_ptr selectArrow ();
};

void
TestNativeCommutativeDiagramEditor::init () {
  buffer= tm_new<buffer_document_state> (
    nullptr, "commutative-native-editor-test.ath", "",
    "commutative-native-editor-test", false, 0);
  swap_current_document_tree (&buffer->document);
  set_document (buffer->document, buffer->root_path, diagram_document ());
  editor= tm_new<DiagramTestEditorRep> (test_server, buffer);
  endpoint= register_actor_ui_endpoint (991021);
  editor->runtime_view_id= 991021;
  editor->ui_endpoint= endpoint;
  editor->init_style ("generic");
  diagram_path= buffer->root_path * 0 * 0;
  body_path= diagram_path * 2;
  focusBody ();
  editor->clear_undo_history ();
}

void
TestNativeCommutativeDiagramEditor::cleanup () {
  tm_delete<editor_rep> (editor);
  editor= nullptr;
  unregister_actor_ui_endpoint (991021);
  endpoint= nullptr;
  swap_current_document_tree (nullptr);
  tm_delete (buffer);
  buffer= nullptr;
}

void
TestNativeCommutativeDiagramEditor::focusBody () {
  editor->go_to (body_path * 0 * 0);
}

commutative_diagram_session_ptr
TestNativeCommutativeDiagramEditor::selectArrow () {
  tree body= subtree (current_document_tree (), body_path);
  auto session= cd_begin_session (body, body_path);
  cd_select (session, "arrow", "f");
  return session;
}

void
TestNativeCommutativeDiagramEditor::optionAndReverseAreSingleUndoTransactions () {
  auto session= selectArrow ();
  tree before= copy (subtree (current_document_tree (), body_path));
  int undo_count= editor->undo_possibilities ();

  editor->commutative_diagram_action (
    native_cd_action::set_selected_option, "offset", "3");
  tree body= subtree (current_document_tree (), body_path);
  QCOMPARE (cd_option (body[3], "offset", ""), string ("3"));
  QCOMPARE (editor->undo_possibilities (), undo_count + 1);
  editor->undo (0);
  QVERIFY (subtree (current_document_tree (), body_path) == before);

  focusBody ();
  session= selectArrow ();
  undo_count= editor->undo_possibilities ();
  editor->commutative_diagram_action (native_cd_action::reverse_selected_arrow);
  body= subtree (current_document_tree (), body_path);
  QCOMPARE (cd_arrow_source (body[3]), string ("b"));
  QCOMPARE (cd_arrow_target (body[3]), string ("a"));
  QCOMPARE (cd_option (body[3], "label-alignment", ""), string ("right"));
  QCOMPARE (editor->undo_possibilities (), undo_count + 1);
}

void
TestNativeCommutativeDiagramEditor::keyboardDeleteIsNativeAndUndoable () {
  auto session= selectArrow ();
  int undo_count= editor->undo_possibilities ();
  QVERIFY (editor->commutative_diagram_keypress ("delete"));
  tree body= subtree (current_document_tree (), body_path);
  QCOMPARE (N(body), 3);
  QCOMPARE (editor->undo_possibilities (), undo_count + 1);
  editor->undo (0);
  QCOMPARE (N(subtree (current_document_tree (), body_path)), 4);
  (void) session;
}

void
TestNativeCommutativeDiagramEditor::trimRecentersAndUndoes () {
  int undo_count= editor->undo_possibilities ();
  tree before= copy (subtree (current_document_tree (), diagram_path));
  editor->commutative_diagram_action (native_cd_action::trim);
  tree diagram= subtree (current_document_tree (), diagram_path);
  tree body= diagram[2];
  QCOMPARE (cd_vertex_x (body[1]), -2.0);
  QCOMPARE (cd_vertex_y (body[1]), -1.0);
  QVERIFY (cd_number (diagram[0], 0.0) < 8.0);
  QVERIFY (cd_number (diagram[1], 0.0) < 5.0);
  QCOMPARE (editor->undo_possibilities (), undo_count + 1);
  editor->undo (0);
  QVERIFY (subtree (current_document_tree (), diagram_path) == before);
}

void
TestNativeCommutativeDiagramEditor::insertCommandCreatesNativeDiagram () {
  set_document (buffer->document, buffer->root_path, tree (DOCUMENT, ""));
  editor->go_to (buffer->root_path * 0 * 0);
  editor->clear_undo_history ();
  editor->commutative_diagram_action (native_cd_action::insert_diagram);
  tree document= subtree (current_document_tree (), buffer->root_path);
  bool found= false;
  std::function<void(tree)> visit= [&] (tree t) {
    if (is_func (t, COMMUTATIVE_DIAGRAM, 3)) found= true;
    if (!is_atomic (t)) for (int i=0; i<N(t); ++i) visit (t[i]);
  };
  visit (document);
  QVERIFY (found);
  QCOMPARE (editor->undo_possibilities (), 1);
}

static int test_status= 1;

static void
run_tests (int argc, char** argv) {
  init_system_state ();
  gui_open (argc, argv);
  {
    server sv;
    test_server= sv->get_server ();
    eval ("(begin (tm-define (notify-cursor-moved status) #f) "
          "(tm-define (like-emacs?) #f))");
    TestNativeCommutativeDiagramEditor test;
    test_status= QTest::qExec (&test, argc, argv);
    std::_Exit (test_status);
  }
}

int
main (int argc, char** argv) {
  qputenv ("QT_QPA_PLATFORM", "offscreen");
  QApplication app (argc, argv);
  const string test_home= "/tmp/athena-commutative-native-editor-test-" *
                          as_string ((int) getpid ());
  set_env ("ATHENA_HOME_PATH", test_home);
  cache_initialize ();
  reset_document_tree (current_document_tree ());
  init_athena ();
  start_scheme (argc, argv, run_tests);
  return test_status;
}

#include "commutative_diagram_native_editor_test.moc"
