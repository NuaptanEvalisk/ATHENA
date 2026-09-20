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
with_property (tree object, string name) {
  if (!is_func (object, WITH) || N(object) < 3) return tree (UNINIT);
  for (int i=0; i+1<N(object)-1; i+=2)
    if (is_atomic (object[i]) && object[i]->label == name)
      return object[i+1];
  return tree (UNINIT);
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
  void highlighterPersistsTransparentWideStroke ();
  void objectEraserRemovesWholeStroke ();
  void objectEraserRemovesVectorShape ();
  void segmentEraserSplitsStrokeInOneTransaction ();
  void lassoSelectsWithoutChangingDocument ();
  void lassoMoveCommitsOneTransform ();
  void lassoScaleCommitsOneTransform ();
  void lassoRotateCommitsOneTransform ();
  void transformedStrokeRemainsSegmentErasable ();

private:
  buffer_document_state* buffer= nullptr;
  NativeInkTestEditorRep* editor= nullptr;
  actor_ui_endpoint* endpoint= nullptr;
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
  endpoint= register_actor_ui_endpoint (991001);
  editor->runtime_view_id= 991001;
  editor->ui_endpoint= endpoint;
  editor->init_style ("generic");
}

void
TestNativeInkEditor::cleanup () {
  tm_delete<editor_rep> (editor);
  editor= nullptr;
  unregister_actor_ui_endpoint (991001);
  endpoint= nullptr;
  swap_current_document_tree (nullptr);
  tm_delete (buffer);
  buffer= nullptr;
}

static void
prepare_graphics_region (NativeInkTestEditorRep* editor,
                         buffer_document_state* buffer,
                         path& graphics_path,
                         SI& left, SI& bottom, SI& right, SI& top) {
  graphics_path= buffer->root_path * 0 * 10;
  editor->go_to (graphics_path * 0 * 0);
  SI tx1= 0, ty1= 0, tx2= 0, ty2= 0;
  editor->typeset (tx1, ty1, tx2, ty2);
  SI gx1= 0, gy1= 0, gx2= 0, gy2= 0;
  QVERIFY (editor->find_graphical_region (gx1, gy1, gx2, gy2));
  left= min (gx1, gx2);
  right= max (gx1, gx2);
  bottom= min (gy1, gy2);
  top= max (gy1, gy2);
  QVERIFY (right > left);
  QVERIFY (top > bottom);
}

static std::vector<native_ink_sample>
horizontal_samples (SI left, SI right, SI y, int count= 7) {
  std::vector<native_ink_sample> result ((std::size_t) count);
  for (int i=0; i<count; ++i) {
    result[(std::size_t) i].x=
      left + ((i + 1) * (right - left)) / (count + 1);
    result[(std::size_t) i].y= y;
    result[(std::size_t) i].time= 100.0 + i;
    result[(std::size_t) i].pressure= 0.5 + 0.05 * i;
  }
  return result;
}

void
TestNativeInkEditor::strokePersistsAndUndoesAsOneTransaction () {
  path graphics_path;
  SI left= 0, bottom= 0, right= 0, top= 0;
  prepare_graphics_region (
    editor, buffer, graphics_path, left, bottom, right, top);

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
  tree graphics= subtree (current_document_tree (), graphics_path);
  QCOMPARE (N(graphics), 2);
  QVERIFY (is_func (graphics[1], WITH));
  QVERIFY (is_func (graphics[1][N(graphics[1])-1], PENSCRIPT));
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

void
TestNativeInkEditor::highlighterPersistsTransparentWideStroke () {
  path graphics_path;
  SI left= 0, bottom= 0, right= 0, top= 0;
  prepare_graphics_region (
    editor, buffer, graphics_path, left, bottom, right, top);
  auto samples= horizontal_samples (left, right, (bottom + top) / 2, 5);
  editor->set_native_drawing_tool (native_drawing_tool::highlighter);
  editor->commit_native_drawing_gesture (
    native_drawing_tool::highlighter, samples.data (), samples.size ());

  tree after= copy (subtree (current_document_tree (), buffer->root_path));
  QCOMPARE (count_label (after, PENSCRIPT), 1);
  string serialized= tree_to_texmacs (after);
  QVERIFY (occurs ("highlighter", serialized));
  tree graphics= subtree (current_document_tree (), graphics_path);
  QCOMPARE (N(graphics), 2);
  tree object= graphics[1];
  QVERIFY (is_func (object, WITH));
  tree color_value= with_property (object, "color");
  tree width_value= with_property (object, "line-width");
  QVERIFY (is_atomic (color_value));
  QVERIFY (is_atomic (width_value));
  int r= 0, g= 0, b= 0, a= 0;
  get_rgb_color (named_color (color_value->label), r, g, b, a);
  QCOMPARE (a, 96);
  QCOMPARE (width_value->label, string ("10ln"));
}

void
TestNativeInkEditor::objectEraserRemovesWholeStroke () {
  path graphics_path;
  SI left= 0, bottom= 0, right= 0, top= 0;
  prepare_graphics_region (
    editor, buffer, graphics_path, left, bottom, right, top);
  auto first= horizontal_samples (left, right, bottom + (top-bottom)/3, 7);
  auto second= horizontal_samples (left, right, bottom + 2*(top-bottom)/3, 7);
  editor->commit_native_ink_stroke (first.data (), first.size ());
  editor->commit_native_ink_stroke (second.data (), second.size ());
  QCOMPARE (count_label (
    subtree (current_document_tree (), buffer->root_path), PENSCRIPT), 2);

  native_ink_sample erase[2];
  erase[0]= first[1];
  erase[1]= first[first.size () - 2];
  editor->commit_native_drawing_gesture (
    native_drawing_tool::object_eraser, erase, 2);
  QCOMPARE (count_label (
    subtree (current_document_tree (), buffer->root_path), PENSCRIPT), 1);
}

void
TestNativeInkEditor::objectEraserRemovesVectorShape () {
  path graphics_path;
  SI left= 0, bottom= 0, right= 0, top= 0;
  prepare_graphics_region (
    editor, buffer, graphics_path, left, bottom, right, top);
  tree shape= compound (
    "line", tree (_POINT, "-2", "0"), tree (_POINT, "2", "0"));
  tree graphics= subtree (current_document_tree (), graphics_path);
  editor->start_editing ();
  insert (graphics_path * N(graphics), tree (TUPLE, shape));
  editor->end_editing ();
  SI tx1= 0, ty1= 0, tx2= 0, ty2= 0;
  editor->typeset (tx1, ty1, tx2, ty2);
  frame f= editor->find_frame ();
  QVERIFY (!is_nil (f));
  point a= f (point (-1.0, 0.0));
  point b= f (point (1.0, 0.0));
  QVERIFY (N(a) >= 2 && N(b) >= 2);
  native_ink_sample erase[2];
  erase[0].x= (SI) a[0]; erase[0].y= (SI) a[1];
  erase[1].x= (SI) b[0]; erase[1].y= (SI) b[1];
  editor->commit_native_drawing_gesture (
    native_drawing_tool::object_eraser, erase, 2);
  tree after= subtree (current_document_tree (), graphics_path);
  QCOMPARE (N(after), 1);
  QVERIFY (is_empty (after[0]));
}

void
TestNativeInkEditor::segmentEraserSplitsStrokeInOneTransaction () {
  path graphics_path;
  SI left= 0, bottom= 0, right= 0, top= 0;
  prepare_graphics_region (
    editor, buffer, graphics_path, left, bottom, right, top);
  SI y= (bottom + top) / 2;
  auto stroke= horizontal_samples (left, right, y, 9);
  editor->commit_native_ink_stroke (stroke.data (), stroke.size ());
  QCOMPARE (count_label (
    subtree (current_document_tree (), buffer->root_path), PENSCRIPT), 1);

  native_ink_sample erase[2];
  erase[0].x= stroke[4].x;
  erase[0].y= y - (top-bottom)/8;
  erase[1].x= stroke[4].x;
  erase[1].y= y + (top-bottom)/8;
  editor->commit_native_drawing_gesture (
    native_drawing_tool::segment_eraser, erase, 2);
  QCOMPARE (count_label (
    subtree (current_document_tree (), buffer->root_path), PENSCRIPT), 2);

  editor->go_to (buffer->root_path * 1 * 0);
  QVERIFY (editor->undo_possibilities () >= 1);
  editor->undo (0);
  QCOMPARE (count_label (
    subtree (current_document_tree (), buffer->root_path), PENSCRIPT), 1);
}

void
TestNativeInkEditor::lassoSelectsWithoutChangingDocument () {
  path graphics_path;
  SI left= 0, bottom= 0, right= 0, top= 0;
  prepare_graphics_region (
    editor, buffer, graphics_path, left, bottom, right, top);
  SI y1= bottom + (top-bottom)/3;
  SI y2= bottom + 2*(top-bottom)/3;
  auto first= horizontal_samples (left, (left+right)/2, y1, 5);
  auto second= horizontal_samples ((left+right)/2, right, y2, 5);
  editor->commit_native_ink_stroke (first.data (), first.size ());
  editor->commit_native_ink_stroke (second.data (), second.size ());
  tree before= copy (subtree (current_document_tree (), buffer->root_path));
  int undo_before= editor->undo_possibilities ();

  SI margin_x= (right-left)/20;
  SI margin_y= (top-bottom)/12;
  native_ink_sample lasso[5];
  SI lx1= first.front ().x - margin_x;
  SI lx2= first.back ().x + margin_x;
  SI ly1= y1 - margin_y;
  SI ly2= y1 + margin_y;
  lasso[0].x= lx1; lasso[0].y= ly1;
  lasso[1].x= lx2; lasso[1].y= ly1;
  lasso[2].x= lx2; lasso[2].y= ly2;
  lasso[3].x= lx1; lasso[3].y= ly2;
  lasso[4]= lasso[0];
  editor->commit_native_drawing_gesture (
    native_drawing_tool::lasso, lasso, 5);


  QCOMPARE (subtree (current_document_tree (), buffer->root_path), before);
  QCOMPARE (editor->undo_possibilities (), undo_before);
  std::vector<native_drawing_selection_box> selected=
    endpoint->native_drawing_selection ();
  QCOMPARE ((int) selected.size (), 1);
}

static native_drawing_selection_box
select_one_native_stroke (NativeInkTestEditorRep* editor,
                          actor_ui_endpoint* endpoint,
                          SI left, SI bottom, SI right, SI top) {
  SI y= (bottom + top) / 2;
  auto stroke= horizontal_samples (left, right, y, 7);
  editor->commit_native_ink_stroke (stroke.data (), stroke.size ());
  SI mx= (right-left)/20;
  SI my= (top-bottom)/10;
  native_ink_sample lasso[5];
  lasso[0].x= stroke.front ().x - mx; lasso[0].y= y - my;
  lasso[1].x= stroke.back ().x + mx; lasso[1].y= y - my;
  lasso[2].x= stroke.back ().x + mx; lasso[2].y= y + my;
  lasso[3].x= stroke.front ().x - mx; lasso[3].y= y + my;
  lasso[4]= lasso[0];
  editor->commit_native_drawing_gesture (
    native_drawing_tool::lasso, lasso, 5);
  std::vector<native_drawing_selection_box> selected=
    endpoint->native_drawing_selection ();
  if (selected.size () != 1) return native_drawing_selection_box ();
  return selected[0];
}

void
TestNativeInkEditor::lassoMoveCommitsOneTransform () {
  path graphics_path;
  SI left= 0, bottom= 0, right= 0, top= 0;
  prepare_graphics_region (
    editor, buffer, graphics_path, left, bottom, right, top);
  native_drawing_selection_box before=
    select_one_native_stroke (editor, endpoint, left, bottom, right, top);
  QVERIFY (before.x2 > before.x1);

  SI dx= (right-left)/10;
  SI dy= (top-bottom)/12;
  native_ink_sample drag[2];
  drag[0].x= (before.x1 + before.x2) / 2;
  drag[0].y= (before.y1 + before.y2) / 2;
  drag[1].x= drag[0].x + dx;
  drag[1].y= drag[0].y + dy;
  editor->commit_native_drawing_transform (
    native_drawing_transform::move, drag, 2);
  QVERIFY (editor->undo_possibilities () >= 1);

  tree object= subtree (current_document_tree (), graphics_path * 1);
  QVERIFY (is_func (object, GR_TRANSFORM, 2));
  QVERIFY (is_tuple (object[1], "translation", 2));

  SI tx1= 0, ty1= 0, tx2= 0, ty2= 0;
  editor->typeset (tx1, ty1, tx2, ty2);
  editor->refresh_native_ink_interaction ();
  auto moved= endpoint->native_drawing_selection ();
  QCOMPARE ((int) moved.size (), 1);
  QVERIFY (std::abs ((moved[0].x1 - before.x1) - dx) <= 2 * PIXEL);
  QVERIFY (std::abs ((moved[0].y1 - before.y1) - dy) <= 2 * PIXEL);

  editor->go_to (buffer->root_path * 1 * 0);
  editor->undo (0);
  QCOMPARE (count_label (
    subtree (current_document_tree (), buffer->root_path), GR_TRANSFORM), 0);
  QCOMPARE (count_label (
    subtree (current_document_tree (), buffer->root_path), PENSCRIPT), 1);
}

void
TestNativeInkEditor::lassoScaleCommitsOneTransform () {
  path graphics_path;
  SI left= 0, bottom= 0, right= 0, top= 0;
  prepare_graphics_region (
    editor, buffer, graphics_path, left, bottom, right, top);
  native_drawing_selection_box before=
    select_one_native_stroke (editor, endpoint, left, bottom, right, top);
  QVERIFY (before.x2 > before.x1);

  native_ink_sample drag[2];
  drag[0].x= before.x2; drag[0].y= before.y2;
  drag[1].x= before.x1 + (SI) std::llround (1.5 * (before.x2-before.x1));
  drag[1].y= before.y1 + (SI) std::llround (1.5 * (before.y2-before.y1));
  editor->commit_native_drawing_transform (
    native_drawing_transform::scale, drag, 2);
  tree object= subtree (current_document_tree (), graphics_path * 1);
  QVERIFY (is_func (object, GR_TRANSFORM, 2));
  QVERIFY (is_tuple (object[1], "scaling", 3));

  SI tx1= 0, ty1= 0, tx2= 0, ty2= 0;
  editor->typeset (tx1, ty1, tx2, ty2);
  editor->refresh_native_ink_interaction ();
  auto scaled= endpoint->native_drawing_selection ();
  QCOMPARE ((int) scaled.size (), 1);
  QVERIFY ((scaled[0].x2 - scaled[0].x1) >
           (SI) (1.35 * (before.x2 - before.x1)));

  editor->go_to (buffer->root_path * 1 * 0);
  editor->undo (0);
  QCOMPARE (count_label (
    subtree (current_document_tree (), buffer->root_path), GR_TRANSFORM), 0);
  QCOMPARE (count_label (
    subtree (current_document_tree (), buffer->root_path), PENSCRIPT), 1);
}

void
TestNativeInkEditor::lassoRotateCommitsOneTransform () {
  path graphics_path;
  SI left= 0, bottom= 0, right= 0, top= 0;
  prepare_graphics_region (
    editor, buffer, graphics_path, left, bottom, right, top);
  native_drawing_selection_box before=
    select_one_native_stroke (editor, endpoint, left, bottom, right, top);
  SI width= before.x2 - before.x1;
  SI height= before.y2 - before.y1;
  QVERIFY (width > height);
  SI cx= (before.x1 + before.x2) / 2;
  SI cy= (before.y1 + before.y2) / 2;
  SI radius= max (width, height) / 2;
  native_ink_sample drag[2];
  drag[0].x= cx; drag[0].y= cy + radius;
  drag[1].x= cx + radius; drag[1].y= cy;
  editor->commit_native_drawing_transform (
    native_drawing_transform::rotate, drag, 2);
  tree object= subtree (current_document_tree (), graphics_path * 1);
  QVERIFY (is_func (object, GR_TRANSFORM, 2));
  QVERIFY (is_tuple (object[1], "rotation", 2));

  SI tx1= 0, ty1= 0, tx2= 0, ty2= 0;
  editor->typeset (tx1, ty1, tx2, ty2);
  editor->refresh_native_ink_interaction ();
  auto rotated= endpoint->native_drawing_selection ();
  QCOMPARE ((int) rotated.size (), 1);
  QVERIFY ((rotated[0].y2 - rotated[0].y1) >
           (rotated[0].x2 - rotated[0].x1));

  editor->go_to (buffer->root_path * 1 * 0);
  editor->undo (0);
  QCOMPARE (count_label (
    subtree (current_document_tree (), buffer->root_path), GR_TRANSFORM), 0);
  QCOMPARE (count_label (
    subtree (current_document_tree (), buffer->root_path), PENSCRIPT), 1);
}

void
TestNativeInkEditor::transformedStrokeRemainsSegmentErasable () {
  path graphics_path;
  SI left= 0, bottom= 0, right= 0, top= 0;
  prepare_graphics_region (
    editor, buffer, graphics_path, left, bottom, right, top);
  native_drawing_selection_box before=
    select_one_native_stroke (editor, endpoint, left, bottom, right, top);
  SI dx= (right-left)/12;
  native_ink_sample move[2];
  move[0].x= (before.x1 + before.x2) / 2;
  move[0].y= (before.y1 + before.y2) / 2;
  move[1].x= move[0].x + dx;
  move[1].y= move[0].y;
  editor->commit_native_drawing_transform (
    native_drawing_transform::move, move, 2);

  SI tx1= 0, ty1= 0, tx2= 0, ty2= 0;
  editor->typeset (tx1, ty1, tx2, ty2);
  editor->refresh_native_ink_interaction ();
  auto moved= endpoint->native_drawing_selection ();
  QCOMPARE ((int) moved.size (), 1);
  SI cx= (moved[0].x1 + moved[0].x2) / 2;
  SI cy= (moved[0].y1 + moved[0].y2) / 2;
  SI half= max ((SI) (2 * PIXEL), (moved[0].y2 - moved[0].y1) * 2);
  native_ink_sample erase[2];
  erase[0].x= cx; erase[0].y= cy - half;
  erase[1].x= cx; erase[1].y= cy + half;
  editor->commit_native_drawing_gesture (
    native_drawing_tool::segment_eraser, erase, 2);
  QCOMPARE (count_label (
    subtree (current_document_tree (), buffer->root_path), PENSCRIPT), 2);
  QCOMPARE (count_label (
    subtree (current_document_tree (), buffer->root_path), GR_TRANSFORM), 2);
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
