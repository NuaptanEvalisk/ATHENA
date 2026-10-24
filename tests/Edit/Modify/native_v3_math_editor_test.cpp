/******************************************************************************
* MODULE     : native_v3_math_editor_test.cpp
* DESCRIPTION: Native XML v3 mathematical editing regression tests
* COPYRIGHT  : (C) 2026 ATHENA contributors
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
******************************************************************************/
#include <QApplication>
#include <QtTest/QtTest>
#include <cstdlib>
#include <unistd.h>

#include "ATHENA/server.hpp"
#include "Editor/edit_main.hpp"
#include "Interface/auto_close.hpp"
#include "Interface/generic_keyboard_commands.hpp"
#include "Graphics/Gui/gui.hpp"
#include "boot.hpp"
#include "buffer_state.hpp"
#include "data_cache.hpp"
#include "math_tree_v3.hpp"
#include "tree_cursor.hpp"
#include "drd_std.hpp"
#include "node_metadata.hpp"
#include "document_node_model.hpp"
#include "scheme.hpp"
#include "scheme_execution_context.hpp"
#include "sys_utils.hpp"
#include "Xml/athena_document_xml.hpp"

bool headless_mode= true;
bool is_headless () { return true; }
static server_rep* test_server= nullptr;

class NativeMathEditor: public edit_main_rep {
public:
  using editor_rep::drd;
  using editor_rep::eb;
  NativeMathEditor (server_rep* server, buffer_document_state* buffer):
    editor_rep (server, buffer), edit_main_rep (server, buffer) {}
  void* derived_this () override { return this; }
};

class NativeV3MathEditorTest: public QObject {
  Q_OBJECT
  buffer_document_state* buffer= nullptr;
  NativeMathEditor* editor= nullptr;
  ::editor editor_owner;
  string previous_brackets;

  path content_path () const { return buffer->root_path * path (0, 0); }
  tree content () const {
    return subtree (current_document_tree (), content_path ());
  }
  void reset (tree value) {
    buffer->storage_version= athena::document::xml_storage_version::v3;
    tree block (MATH_BLOCK, value);
    athena::node::metadata meta;
    meta.properties["math:block-numbered"]= athena::node::property (false);
    athena::node::set (block, meta);
    set_document (buffer->document, buffer->root_path, tree (DOCUMENT, block));
    editor->go_to_end (content_path ());
  }
  void reset_inline (tree value, bool native= true) {
    buffer->storage_version= native ? athena::document::xml_storage_version::v3 :
                                      athena::document::xml_storage_version::v2;
    set_document (buffer->document, buffer->root_path,
                  tree (DOCUMENT, compound ("math", value)));
    editor->go_to_end (content_path ());
  }
  void verify_roundtrip () {
    const tree source= subtree (current_document_tree (), buffer->root_path);
    QVERIFY (athena::document::read_xml_v3 (
      athena::document::write_xml_v3 (source)) == source);
  }

  void verify_display_focus (path block) {
    SchemeExecutionContext context (nullptr, editor, &editor->drd,
      &buffer->document, ATHENA_NO_ACTOR, ATHENA_NO_VIEW, 0,
      SCHEME_CAPABILITY_BUFFER);
    SchemeExecutionScope execution (context);
    editor->go_to_end (block * 0);
    editor->typeset_forced ();
    for (int side: {0, 1}) {
      bool found= false;
      editor->eb->find_box_path (block * side, found);
      QVERIFY2 (found, "MathBlock boundary has no rendered box");
    }
    selection formula= editor->eb->find_check_selection (
      start (current_document_tree (), block * 0),
      end (current_document_tree (), block * 0));
    QVERIFY (formula->valid);
    QVERIFY (N(formula->rs) > 0);
    rectangle bounds= least_upper_bound (formula->rs);
    rectangles focus;
    editor->compute_env_rects (block, focus, false, PIXEL);
    QVERIFY (N(focus) > 0);
    rectangle outline= least_upper_bound (focus);
    QVERIFY (outline->y1 >= bounds->y1 - 4*PIXEL);
    QVERIFY (outline->y2 <= bounds->y2 + 4*PIXEL);
    QVERIFY (outline->x1 >= bounds->x1 - 4*PIXEL);
    QVERIFY (outline->x2 <= bounds->x2 + 4*PIXEL);
  }

private slots:
  void nativeGridRowSelectionKeepsNativeOwnership () {
    SchemeExecutionContext context (nullptr, editor, &editor->drd,
      &buffer->document, 0, 0, 0, SCHEME_CAPABILITY_BUFFER);
    SchemeExecutionScope scope (context);
    tree grid (MATH_GRID,
      tree (ROW, tree (CELL, "a"), tree (CELL, "="), tree (CELL, "b")),
      tree (ROW, tree (CELL, "c"), tree (CELL, "="), tree (CELL, "def")));
    athena::node::metadata meta;
    meta.properties["math:grid-kind"]= athena::node::property (std::string ("eqnarray"));
    athena::node::set (grid, meta);
    meta= athena::node::metadata ();
    meta.id= athena::node::new_id ();
    athena::node::set (grid[1], meta);
    reset (grid);
    editor->typeset_forced ();
    const path row= content_path () * 1;
    // The core dump crossed cells 1 and 2 within a native row. It was
    // misclassified as a legacy table and used uninitialized coordinates.
    editor->selection_set_paths (row * path (1, 0, 1), row * path (2, 0, 2));
    QVERIFY (!editor->selection_active_table ());
    selection selected;
    editor->selection_get (selected);
    QVERIFY (selected->valid);
    QVERIFY (!is_nil (selected->rs));
    path first, last;
    editor->selection_get (first, last);
    QCOMPARE (common (first, last), row);
    const tree copied= editor->selection_get ();
    QVERIFY (is_func (copied, MATH_BLOCK, 1));
    QVERIFY (is_func (copied[0], MATH_GRID, 1));
    QCOMPARE (copied[0][0], grid[1]);
    const tree copied_document (DOCUMENT, copied);
    QCOMPARE (athena::document::read_xml_v3 (
      athena::document::write_xml_v3 (copied_document)), copied_document);

    editor->clear_undo_history ();
    editor->archive_state ();
    editor->start_editing ();
    editor->selection_cut ("none");
    editor->end_editing ();
    QCOMPARE (N(content ()), 1);
    QCOMPARE (content ()[0], grid[0]);
    verify_roundtrip ();
    editor->undo (0);
    QCOMPARE (content (), grid);

  }

  void nativeGridEnterAddsRow_data () {
    QTest::addColumn<QString> ("kind");
    QTest::addColumn<int> ("columns");
    QTest::addColumn<int> ("row");
    QTest::addColumn<int> ("column");
    for (const QString kind: {QString ("eqnarray"), QString ("align"),
                              QString ("matrix"), QString ("stack"),
                              QString ("cases"), QString ("generic")}) {
      const int columns= kind == "stack" ? 1 : kind == "eqnarray" ? 3 : 2;
      for (int row: {0, 1})
        for (int column= 0; column < columns; ++column) {
          const QByteArray name= (kind + "-" + QString::number (row) + "-" +
            QString::number (column)).toUtf8 ();
          QTest::newRow (name.constData ()) << kind << columns << row << column;
        }
    }
  }
  void nativeGridEnterAddsRow () {
    QFETCH (QString, kind);
    QFETCH (int, columns);
    QFETCH (int, row);
    QFETCH (int, column);
    SchemeExecutionContext context (nullptr, editor, &editor->drd,
      &buffer->document, 0, 0, 0, SCHEME_CAPABILITY_BUFFER);
    SchemeExecutionScope scope (context);
    tree grid (MATH_GRID, 2);
    for (int r= 0; r < 2; ++r) {
      grid[r]= tree (ROW, columns);
      for (int c= 0; c < columns; ++c) grid[r][c]= tree (CELL, "X");
      athena::node::metadata row_meta;
      row_meta.id= athena::node::new_id ();
      athena::node::set (grid[r], row_meta);
    }
    athena::node::metadata meta;
    meta.id= athena::node::new_id ();
    meta.properties["math:grid-kind"]= athena::node::property (kind.toStdString ());
    athena::node::set (grid, meta);
    reset (grid);
    const tree original= copy (content ());
    editor->go_to_end (content_path () * path (row, column, 0));
    editor->clear_undo_history ();
    editor->start_editing ();
    generic_kbd_return ();
    editor->end_editing ();
    QCOMPARE (N(content ()), 3);
    QCOMPARE (content ()[row], original[row]);
    QCOMPARE (content ()[row == 0 ? 2 : 0], original[row == 0 ? 1 : 0]);
    tree empty (ROW, columns);
    for (int c= 0; c < columns; ++c) empty[c]= tree (CELL, "");
    QCOMPARE (content ()[row + 1], empty);
    QVERIFY (athena::node::equal_metadata (content (), original));
    QCOMPARE (athena::node::id (content ()), meta.id);
    QVERIFY (athena::node::id (content ()[row + 1]).empty () ||
      (athena::node::id (content ()[row + 1]) != athena::node::id (original[0]) &&
       athena::node::id (content ()[row + 1]) != athena::node::id (original[1])));
    QVERIFY (editor->the_path () == content_path () * path (row + 1, 0, 0, 0));
    verify_roundtrip ();
    editor->typeset_forced ();
    QVERIFY (editor->eb->find_check_cursor (editor->the_path ())->valid);
    editor->undo (0);
    QCOMPARE (content (), original);
    editor->go_to_end (content_path () * path (row, column, 0));
    editor->start_editing ();
    generic_kbd_return ();
    generic_kbd_return ();
    editor->insert_tree ("Y");
    editor->end_editing ();
    QCOMPARE (N(content ()), 4);
    QCOMPARE (content ()[row + 2][0][0], tree ("Y"));
    verify_roundtrip ();
  }
  void eqnarrayInsertionUsesNativeGrid_data () {
    QTest::addColumn<QString> ("entry");
    QTest::addColumn<QString> ("name");
    QTest::addColumn<bool> ("selected");
    for (const QString entry: {QString ("compound"), QString ("hybrid"),
                               QString ("prepared"), QString ("curly")})
      for (const QString name: {QString ("eqnarray"), QString ("eqnarray*")})
        for (bool selected: {false, true}) {
          if (entry == "curly" && name != "eqnarray") continue;
          const QByteArray row= (entry + "-" + name +
            (selected ? "-selection" : "-empty")).toUtf8 ();
          QTest::newRow (row.constData ()) << entry << name << selected;
        }
  }
  void eqnarrayInsertionUsesNativeGrid () {
    QFETCH (QString, entry);
    QFETCH (QString, name);
    QFETCH (bool, selected);
    SchemeExecutionContext context (nullptr, editor, &editor->drd,
      &buffer->document, 0, 0, 0, SCHEME_CAPABILITY_BUFFER);
    SchemeExecutionScope scope (context);
    buffer->storage_version= athena::document::xml_storage_version::v3;
    const tree body= selected ? tree ("X") : tree ("");
    set_document (buffer->document, buffer->root_path, tree (DOCUMENT, body));
    const path paragraph= buffer->root_path * 0;
    editor->go_to_end (paragraph);
    if (selected)
      editor->selection_set_paths (start (current_document_tree (), paragraph),
                                    end (current_document_tree (), paragraph));
    const string command (name.toUtf8 ().constData ());
    editor->start_editing ();
    if (entry == "compound") editor->make_compound (make_tree_label (command), -1);
    else {
      editor->make_hybrid ();
      if (entry == "curly") generic_hybrid_kbd_curly_left ();
      else {
        editor->insert_tree (command);
        if (entry == "prepared") QVERIFY (editor->prepare_hybrid ());
        editor->activate_hybrid (false);
      }
    }
    editor->end_editing ();
    const tree block= subtree (current_document_tree (), paragraph);
    QVERIFY (is_func (block, MATH_BLOCK, 1));
    QVERIFY (athena::math_v3::valid_native_math (block));
    const tree grid= block[0];
    QVERIFY (is_func (grid, MATH_GRID, 1));
    QVERIFY (athena::math_v3::valid_native_math (grid));
    QCOMPARE (std::get<std::string> (athena::node::get (grid)->properties.at (
      "math:grid-kind").data), std::string ("eqnarray"));
    QVERIFY (!std::get<bool> (athena::node::get (block)->properties.at (
      "math:block-numbered").data));
    QCOMPARE (grid[0], tree (ROW, tree (CELL, body), tree (CELL, ""), tree (CELL, "")));
    QVERIFY (editor->the_path () == end (current_document_tree (),
      paragraph * path (0, 0, 0, 0)));
    verify_roundtrip ();
    editor->typeset_forced ();
    QVERIFY (editor->eb->find_check_cursor (editor->the_path ())->valid);
    editor->insert_tree ("Y");
    QCOMPARE (subtree (current_document_tree (), paragraph * path (0, 0, 0, 0)),
              tree (selected ? "XY" : "Y"));
    verify_roundtrip ();
    generic_kbd_return ();
    QVERIFY (is_func (subtree (current_document_tree (), paragraph * 0), MATH_GRID, 2));
    QVERIFY (editor->the_path () == paragraph * path (0, 1, 0, 0) * 0);
    verify_roundtrip ();
  }
  void escapePickerBracketsAreNative_data () {
    QTest::addColumn<QString> ("action");
    QTest::addColumn<QString> ("left");
    QTest::addColumn<QString> ("right");
    QTest::addColumn<QString> ("automatic");
    QTest::addColumn<bool> ("selected");
    for (const QString action: {QString ("angle"), QString ("norm")})
      for (const QString automatic: {QString ("on"), QString ("off"),
                                     QString ("mathematics")})
        for (bool selected: {false, true}) {
          const QByteArray name= (action + "-" + automatic +
            (selected ? "-selected" : "-empty")).toUtf8 ();
          QTest::newRow (name.constData ())
            << ("tree:" + action + "-brackets")
            << (action == "angle" ? QString::fromUtf8 ("⟨") : QString::fromUtf8 ("‖"))
            << (action == "angle" ? QString::fromUtf8 ("⟩") : QString::fromUtf8 ("‖"))
            << automatic << selected;
        }
  }
  void escapePickerBracketsAreNative () {
    QFETCH (QString, action);
    QFETCH (QString, left);
    QFETCH (QString, right);
    QFETCH (QString, automatic);
    QFETCH (bool, selected);
    eval ("(import-from (math math-edit))");
    SchemeExecutionContext context (nullptr, editor, &editor->drd,
      &buffer->document, ATHENA_NO_ACTOR, ATHENA_NO_VIEW, 0,
      SCHEME_CAPABILITY_BUFFER);
    SchemeExecutionScope execution (context);
    set_preference ("automatic brackets", string (automatic.toUtf8 ().constData ()));
    const tree body= selected ? tree (CONCAT, "x", tree (FRAC, "a", "b")) : tree ("");
    reset_inline (body);
    if (selected)
      editor->selection_set_paths (start (current_document_tree (), content_path ()),
                                   end (current_document_tree (), content_path ()));
    editor->start_editing ();
    generic_escape_symbol_insert (string (action.toUtf8 ().constData ()));
    editor->end_editing ();
    verify_roundtrip ();
    QCOMPARE (content (), athena::math_v3::delimited (
      string (left.toUtf8 ().constData ()), string (right.toUtf8 ().constData ()),
      'a', body));
    QCOMPARE (editor->the_path (), end (current_document_tree (), content_path () * 5));
  }
  void escapePickerBracketsInTextStaySerializable () {
    SchemeExecutionContext context (nullptr, editor, &editor->drd,
      &buffer->document, ATHENA_NO_ACTOR, ATHENA_NO_VIEW, 0,
      SCHEME_CAPABILITY_BUFFER);
    SchemeExecutionScope execution (context);
    for (const string preference: {string ("on"), string ("off"), string ("mathematics")})
      for (bool angle: {true, false}) {
        set_preference ("automatic brackets", preference);
        buffer->storage_version= athena::document::xml_storage_version::v3;
        set_document (buffer->document, buffer->root_path, tree (DOCUMENT, ""));
        editor->go_to_start (buffer->root_path * 0);
        QCOMPARE (editor->get_env_string (MODE), string ("text"));
        editor->start_editing ();
        generic_escape_symbol_insert (angle ? "tree:angle-brackets" : "tree:norm-brackets");
        editor->end_editing ();
        const string left= angle ? "⟨" : "‖";
        const string right= angle ? "⟩" : "‖";
        QCOMPARE (subtree (current_document_tree (), buffer->root_path),
                  tree (DOCUMENT, preference == "on" ? left * right : left));
        verify_roundtrip ();
      }
  }
  void selectedNativeFormulaAcceptsBrackets_data () {
    QTest::addColumn<bool> ("identified");
    QTest::addColumn<bool> ("partial");
    QTest::newRow ("whole") << false << false;
    QTest::newRow ("partial") << false << true;
    QTest::newRow ("identified-whole") << true << false;
    QTest::newRow ("identified-partial") << true << true;
  }
  void selectedNativeFormulaAcceptsBrackets () {
    QFETCH (bool, identified);
    QFETCH (bool, partial);
    with_borrowed_drd context (&editor->drd);
    namespace model= athena::document_node;
    tree accent (MATH_ACCENT, "\xcc\x87", "o", "\xce\xb3");
    athena::node::metadata accent_meta;
    accent_meta.properties["math:accent-stretch"]=
      athena::node::property (false);
    athena::node::set (accent, accent_meta);
    tree scripts= athena::math_v3::scripts ("\xe2\x88\x87", "", "", accent, "");
    tree expression (CONCAT, scripts, "*", "V");
    if (identified) {
      athena::node::metadata expression_meta;
      expression_meta.id= athena::node::new_id ();
      athena::node::set (expression, expression_meta);
      athena::node::edit (scripts)->id= athena::node::new_id ();
      athena::node::edit (accent)->id= athena::node::new_id ();
    }
    if (partial) {
      tree surrounding (CONCAT, "x", "+", scripts, "*", "V", "+", "y");
      athena::node::copy_metadata (expression, surrounding);
      expression= surrounding;
    }
    reset_inline (expression);
    const auto assigned= model::assign_detached_source_ids (
      subtree (current_document_tree (), buffer->root_path), editor->drd,
      model::standard_source_role, [] (const auto&) { return athena::node::new_id (); });
    QVERIFY2 (assigned.ok (), assigned.diagnostics.empty () ? "No source body" :
              assigned.diagnostics.front ().detail.c_str ());
    set_document (buffer->document, buffer->root_path, *assigned.body);
    editor->clear_undo_history ();
    buffer->node_identities= std::make_unique<model::source_identity_state> ();
    QVERIFY (buffer->node_identities->initialize_complete (
      *assigned.body, editor->drd, model::standard_source_role).empty ());
    const tree before= copy (*assigned.body);
    editor->archive_state ();
    editor->selection_set_paths (content_path () * path (partial ? 2 : 0, 0),
                                content_path () * path (partial ? 4 : 2, 1));
    QVERIFY (editor->selection_active_normal ());
    const tree selected= copy (editor->selection_get ());
    editor->start_editing ();
    editor->math_bracket_open ("(", ")", 1);
    const auto plan= buffer->node_identities->prepare (
      subtree (current_document_tree (), buffer->root_path), editor->drd,
      model::standard_source_role, [] (const auto&) { return athena::node::new_id (); });
    editor->end_editing ();
    QVERIFY2 (plan.ok (), plan.diagnostics.empty () ? "Invalid edit" :
              plan.diagnostics.front ().detail.c_str ());
    if (partial) QVERIFY (is_concat (content ()) && N(content ()) > 2);
    const tree enclosed= partial ? content ()[1] : content ();
    QVERIFY (athena::math_v3::valid_delimited (enclosed));
    QCOMPARE (enclosed[5], selected);
    verify_roundtrip ();
    const tree after= copy (subtree (current_document_tree (), buffer->root_path));
    editor->undo (0);
    QCOMPARE (subtree (current_document_tree (), buffer->root_path), before);
    editor->redo (0);
    QCOMPARE (subtree (current_document_tree (), buffer->root_path), after);
  }
  void cutIdentifiedFixedContentTransfersHeader () {
    with_borrowed_drd context (&editor->drd);
    for (tree value: {tree ("xy"), tree (CONCAT, "x", tree (FRAC, "a", "b")),
                      tree (DOCUMENT, "x", "y")}) {
      athena::node::metadata meta;
      meta.id= athena::node::new_id ();
      meta.properties["test:selection"]= athena::node::property (true);
      athena::node::set (value, meta);
      const tree before (DOCUMENT, tree (WITH, MODE, "text", value));
      set_document (buffer->document, buffer->root_path, copy (before));
      editor->clear_undo_history ();
      const path slot= buffer->root_path * path (0, 2);
      editor->selection_set_paths (start (current_document_tree (), slot),
                                   end (current_document_tree (), slot));
      editor->archive_state ();
      editor->start_editing ();
      const tree selected= editor->selection_get_cut ();
      editor->end_editing ();
      QCOMPARE (athena::node::id (selected), meta.id);
      QVERIFY (athena::node::equal_metadata (selected, value));
      const tree owner= subtree (current_document_tree (), path_up (slot));
      QVERIFY (is_func (owner, WITH, 3));
      QCOMPARE (owner[2], is_document (value) ? tree (DOCUMENT, "") : tree (""));
      QVERIFY (!athena::node::get (owner[2]));
      editor->undo (0);
      QCOMPARE (subtree (current_document_tree (), buffer->root_path), before);
    }
  }
  void displayFocusExcludesPrecedingParagraphs () {
    tree grid (MATH_GRID,
      tree (ROW, tree (CELL, "x"), tree (CELL, "="), tree (CELL, "y")),
      tree (ROW, tree (CELL, "u"), tree (CELL, "="), tree (CELL, "v")));
    athena::node::metadata meta;
    meta.properties["math:grid-kind"]= athena::node::property (std::string ("eqnarray"));
    athena::node::set (grid, meta);
    for (const tree& body: {tree ("X"), tree (CONCAT, "X", "+", "Y"),
                           tree (FRAC, "x", "y"), grid}) {
      reset (body);
      tree block= subtree (current_document_tree (), buffer->root_path * 0);
      set_document (buffer->document, buffer->root_path,
        tree (DOCUMENT, "Title", "First paragraph", "Second paragraph",
              block, "Following paragraph"));
      verify_display_focus (buffer->root_path * 3);
    }
  }

  void nativeDisplayEstablishesCursorMathEnvironment () {
    reset ("x");
    QCOMPARE (editor->get_env_value (MODE, content_path () * 0), tree ("math"));
    QCOMPARE (editor->get_env_value (MATH_DISPLAY, content_path () * 0), tree ("true"));
    reset (tree (WITH, MODE, "text", "word"));
    QCOMPARE (editor->get_env_value (MODE, content_path () * path (2, 0)), tree ("text"));
  }

  void emptyInlineScriptFocusStaysInsideEnunciationFormula_data () {
    QTest::addColumn<bool> ("sup");
    QTest::addColumn<bool> ("right");
    QTest::newRow ("right-subscript") << false << true;
    QTest::newRow ("right-superscript") << true << true;
    QTest::newRow ("left-subscript") << false << false;
    QTest::newRow ("left-superscript") << true << false;
  }
  void emptyInlineScriptFocusStaysInsideEnunciationFormula () {
    QFETCH (bool, sup);
    QFETCH (bool, right);
    buffer->storage_version= athena::document::xml_storage_version::v3;
    tree statement (make_tree_label ("enunciation"), tree (DOCUMENT,
      tree (CONCAT, "The law ", compound ("math", "P"))));
    athena::node::metadata meta;
    meta.id= athena::node::new_id ();
    meta.properties["kind"]= athena::node::property (std::string ("theorem"));
    meta.properties["name"]= athena::node::property (athena::node::rich_text {tree ("")});
    meta.properties["numbered"]= athena::node::property (false);
    athena::node::set (statement, meta);
    set_document (buffer->document, buffer->root_path, tree (DOCUMENT, statement));
    const path scripts= buffer->root_path * path (0, 0, 0) * path (1, 0);
    editor->go_to_end (scripts);
    editor->start_editing ();
    editor->make_script (sup, right);
    editor->end_editing ();
    QVERIFY (athena::math_v3::valid_scripts (subtree (current_document_tree (), scripts)));
    const int slot= right ? (sup ? 4 : 3) : (sup ? 2 : 1);
    QVERIFY (editor->the_path () == scripts * path (slot, 0));
    editor->typeset_forced ();
    cursor caret= editor->eb->find_check_cursor (editor->the_path ());
    QVERIFY2 (caret->valid, "Empty script slot has no valid rendered cursor");
    cursor base= editor->eb->find_check_cursor (right ?
      end (current_document_tree (), scripts * 0) :
      start (current_document_tree (), scripts * 0));
    QVERIFY (base->valid);
    QVERIFY (right ? caret->ox >= base->ox : caret->ox <= base->ox);
    QVERIFY (sup ? caret->oy > base->oy : caret->oy < base->oy);
    for (int side: {0, 1}) {
      bool found= false;
      editor->eb->find_box_path (scripts * side, found);
      QVERIFY2 (found, "Script owner boundary has no rendered box");
    }
    selection selected= editor->eb->find_check_selection (scripts * 0, scripts * 1);
    QVERIFY (selected->valid);
    QVERIFY (N(selected->rs) > 0);
    for (rectangles rs= selected->rs; !is_nil (rs); rs= rs->next)
      QVERIFY (rs->item->x2-rs->item->x1 < (editor->eb->x2-editor->eb->x1)/3);
    rectangles focus;
    editor->compute_env_rects (scripts, focus, false, PIXEL);
    QVERIFY (N(focus) > 0);
    for (rectangles rs= focus; !is_nil (rs); rs= rs->next)
      QVERIFY (rs->item->x2-rs->item->x1 < (editor->eb->x2-editor->eb->x1)/3);
    editor->insert_tree ("i");
    editor->typeset_forced ();
    QVERIFY (editor->eb->find_check_cursor (editor->the_path ())->valid);
    editor->remove_text (false);
    editor->typeset_forced ();
    QVERIFY (editor->the_path () == scripts * path (slot, 0));
    QVERIFY (editor->eb->find_check_cursor (editor->the_path ())->valid);
  }
  void inlineFormulaBackspaceTakesThreeSteps () {
    reset_inline ("X");
    const path formula= buffer->root_path * 0;
    editor->go_to_border (formula, false);
    editor->remove_text (false);
    QVERIFY (editor->the_path () == content_path () * 1);
    QCOMPARE (content (), tree ("X"));
    editor->remove_text (false);
    QCOMPARE (content (), tree (""));
    editor->remove_text (false);
    QCOMPARE (subtree (current_document_tree (), buffer->root_path), tree (DOCUMENT, ""));
  }
  void inlineFormulaBackspaceFromFollowingText () {
    set_document (buffer->document, buffer->root_path,
      tree (DOCUMENT, tree (CONCAT, "before", compound ("math", "X"), "after")));
    const path paragraph= buffer->root_path * 0;
    editor->go_to (paragraph * path (2, 0));
    editor->remove_text (false);
    QVERIFY (editor->the_path () == paragraph * path (1, 0, 1));
    editor->remove_text (false);
    QCOMPARE (subtree (current_document_tree (), paragraph * path (1, 0)), tree (""));
    editor->remove_text (false);
    QCOMPARE (subtree (current_document_tree (), buffer->root_path), tree (DOCUMENT, "beforeafter"));
  }
  void init () {
    previous_brackets= get_preference ("automatic brackets", "mathematics");
    buffer= tm_new<buffer_document_state> (
      nullptr, "tmfs://native-v3-math-test", "", "UTF-8", false, 0);
    swap_current_document_tree (&buffer->document);
    buffer->data->init ("no-zoom")= "true";
    buffer->data->init (ZOOM_FACTOR)= "1";
    set_document (buffer->document, buffer->root_path, tree (DOCUMENT, ""));
    editor= tm_new<NativeMathEditor> (test_server, buffer);
    editor_owner= ::editor (editor);
    editor->init_style ("generic");
    editor->typeset_preamble ();
  }
  void cleanup () {
    set_preference ("automatic brackets", previous_brackets);
    editor_owner= ::editor ();
    swap_current_document_tree (nullptr);
    tm_delete (buffer);
  }
  void scriptsAttachToNativeCompoundBases () {
    const tree bases[]= {
      athena::math_v3::big_operator ("\xe2\x88\x91"),
      athena::math_v3::delimited ("(", ")", 'a', tree ("x")),
      tree (FRAC, "a", "b"),
      tree (MATH_SYMBOL, "differential-d")
    };
    for (const tree& value: bases) {
      tree base= copy (value);
      athena::node::metadata meta;
      meta.id= athena::node::new_id ();
      meta.properties["test:base"]= athena::node::property (true);
      athena::node::set (base, meta);
      const tree expected_base= copy (base);
      reset (base);
      editor->start_editing ();
      editor->make_script (false, true);
      editor->insert_tree ("i");
      editor->go_to_end (content_path ());
      editor->make_script (true, true);
      editor->insert_tree ("n");
      editor->end_editing ();
      const tree result= content ();
      QVERIFY (athena::math_v3::valid_scripts (result));
      QVERIFY (result[0] == expected_base);
      QCOMPARE (athena::node::id (result[0]), meta.id);
      QCOMPARE (result[3], tree ("i"));
      QCOMPARE (result[4], tree ("n"));
      verify_roundtrip ();
    }
  }
  void scriptsReuseAdjacentNativeOwnerAndPolicy () {
    tree scripts= athena::math_v3::scripts (
      athena::math_v3::big_operator ("\xe2\x88\x91"),
      tree (""), tree (""), tree ("i"), tree (""), 'o');
    athena::node::edit (scripts)->id= athena::node::new_id ();
    const std::string identity= athena::node::id (scripts);
    const tree expected_scripts= copy (scripts);
    reset (tree (CONCAT, scripts, tree ("z")));
    editor->go_to (content_path () * path (1, 0));
    editor->start_editing ();
    editor->make_script (true, true);
    editor->insert_tree ("n");
    editor->end_editing ();
    const tree result= content ()[0];
    QCOMPARE (athena::node::id (result), identity);
    QVERIFY (athena::node::equal_metadata (result, expected_scripts));
    QCOMPARE (result[3], tree ("i"));
    QCOMPARE (result[4], tree ("n"));
    QCOMPARE (content ()[1], tree ("z"));
    verify_roundtrip ();
  }
  void emptyV3BufferDoesNotRequireNativeMathAncestor () {
    buffer->storage_version= athena::document::xml_storage_version::v3;
    set_document (buffer->document, buffer->root_path,
                  tree (DOCUMENT, compound ("math", "")));
    editor->go_to (content_path () * 0);
    editor->start_editing ();
    editor->math_bracket_open ("(", ")", 1);
    editor->insert_tree ("x");
    editor->math_bracket_close (")", "(", 1);
    editor->make_script (true, true);
    editor->insert_tree ("2");
    editor->end_editing ();
    const tree result= content ();
    QVERIFY (athena::math_v3::valid_scripts (result));
    QVERIFY (athena::math_v3::valid_delimited (result[0]));
    QCOMPARE (result[0][5], tree ("x"));
    QCOMPARE (result[4], tree ("2"));
    verify_roundtrip ();
  }
  void sourceVersionIsNotInferredFromNativeAncestors () {
    using athena::document::xml_storage_version;
    reset (athena::math_v3::delimited ("(", ")", 'a', "x"));
    editor->go_to_end (content_path () * 5);
    const tree before= copy (subtree (current_document_tree (), buffer->root_path));
    QVERIFY (editor->native_math_editing ());
    for (auto version: {xml_storage_version::v1, xml_storage_version::v2}) {
      buffer->storage_version= version;
      QVERIFY (!editor->native_math_editing ());
      QCOMPARE (buffer->storage_version, version);
      QCOMPARE (subtree (current_document_tree (), buffer->root_path), before);
    }
  }
  void keyboardBracketWrappersUseNativeV3Storage () {
    using namespace athena::auto_close;
    for (const char* preference: {"on", "off", "mathematics"})
      for (bool large: {false, true}) {
        set_preference ("automatic brackets", preference);
        reset_inline ("");
        QVERIFY (editor->native_math_editing ());
        editor->start_editing ();
        make_bracket_open (editor, "(", ")", large);
        editor->insert_tree ("x");
        make_separator (editor, "|", large);
        editor->insert_tree ("y");
        make_bracket_close (editor, ")", "(", large);
        editor->end_editing ();
        const tree pair= content ();
        QVERIFY (athena::math_v3::valid_delimited (pair));
        QCOMPARE (pair[5], tree (CONCAT, "x", tree (MATH_MIDDLE, "|"), "y"));
        QVERIFY (editor->the_path () == end (current_document_tree (), content_path ()));
        verify_roundtrip ();
      }
  }
  void keyboardTextBracketsRespectModeAndPreference () {
    using namespace athena::auto_close;
    for (bool embedded: {false, true})
      for (const char* preference: {"off", "mathematics", "on"}) {
        set_preference ("automatic brackets", preference);
        buffer->storage_version= athena::document::xml_storage_version::v3;
        tree text= embedded ? compound ("math", tree (WITH, MODE, "text", "")) :
                              tree ("");
        set_document (buffer->document, buffer->root_path, tree (DOCUMENT, text));
        const path atom= embedded ? buffer->root_path * path (0, 0, 2) :
                                    buffer->root_path * 0;
        editor->go_to_end (atom);
        QCOMPARE (editor->get_env_string (MODE), string ("text"));
        editor->start_editing ();
        make_bracket_open (editor, "(", ")");
        const bool paired= string (preference) == "on";
        QCOMPARE (subtree (current_document_tree (), atom), tree (paired ? "()" : "("));
        editor->insert_tree ("x");
        make_separator (editor, "|");
        if (paired) editor->go_to_end (atom);
        make_bracket_close (editor, ")", "(");
        editor->end_editing ();
        QCOMPARE (subtree (current_document_tree (), atom),
                  tree (paired ? "(x|))" : "(x|)"));
        verify_roundtrip ();
      }
  }
  void keyboardBracketWrapperPreservesSelectionAndUndo () {
    using namespace athena::auto_close;
    set_preference ("automatic brackets", "off");
    tree selected (FRAC, "a", "b");
    athena::node::metadata meta;
    meta.id= athena::node::new_id ();
    meta.properties["test:selected"]= athena::node::property (true);
    athena::node::set (selected, meta);
    reset_inline (selected);
    const tree before= copy (subtree (current_document_tree (), buffer->root_path));
    editor->selection_set_paths (start (current_document_tree (), content_path ()),
                                 end (current_document_tree (), content_path ()));
    QVERIFY (editor->selection_active_normal ());
    editor->archive_state ();
    editor->start_editing ();
    make_bracket_open (editor, "[", "]", true);
    make_bracket_close (editor, "]", "[", true);
    editor->end_editing ();
    QVERIFY (athena::math_v3::valid_delimited (content ()));
    QCOMPARE (content ()[5], selected);
    const tree after= copy (subtree (current_document_tree (), buffer->root_path));
    editor->undo (0);
    QCOMPARE (subtree (current_document_tree (), buffer->root_path), before);
    editor->redo (0);
    QCOMPARE (subtree (current_document_tree (), buffer->root_path), after);
    verify_roundtrip ();
  }
  void keyboardBigOperatorIsAnAtomWithoutClosingSentinel () {
    using namespace athena::auto_close;
    for (const char* preference: {"on", "off", "mathematics"}) {
      set_preference ("automatic brackets", preference);
      reset_inline ("");
      editor->start_editing ();
      make_big_operator (editor, "sum");
      QCOMPARE (content (), athena::math_v3::big_operator ("\xe2\x88\x91"));
      const path after_operator= editor->the_path ();
      make_big_operator (editor, ".");
      QCOMPARE (editor->the_path (), after_operator);
      QCOMPARE (content (), athena::math_v3::big_operator ("\xe2\x88\x91"));
      editor->make_script (false, true);
      editor->insert_tree ("i");
      editor->go_to_end (content_path ());
      editor->insert_tree ("x");
      editor->end_editing ();
      const tree result= content ();
      QVERIFY (is_func (result, CONCAT, 2));
      QVERIFY (athena::math_v3::valid_scripts (result[0]));
      QCOMPARE (result[0][0], athena::math_v3::big_operator ("\xe2\x88\x91"));
      QCOMPARE (result[0][3], tree ("i"));
      QCOMPARE (result[1], tree ("x"));
      verify_roundtrip ();
    }
  }
  void keyboardBigOperatorNamesUseNativeGlyphAndPolicies () {
    using namespace athena::auto_close;
    for (const char* input: {"cup", "upint", "upintlim", "<sum>", "\xe2\x88\x91"}) {
      reset_inline ("");
      editor->start_editing ();
      make_big_operator (editor, input);
      editor->end_editing ();
      tree atom= content ();
      if (string (input) == "upintlim") {
        QVERIFY (athena::math_v3::valid_scripts (atom));
        QCOMPARE (std::get<std::string> (athena::node::get (atom)->properties.at (
                    athena::math_v3::scripts_policy_key).data), std::string ("d"));
        atom= atom[0];
      }
      QVERIFY (athena::math_v3::valid_big_operator (atom));
      if (starts (string (input), "upint"))
        QCOMPARE (atom, athena::math_v3::big_operator ("\xe2\x88\xab", true));
      else if (string (input) == "cup")
        QCOMPARE (atom, athena::math_v3::big_operator ("\xe2\x8b\x83"));
      else QCOMPARE (atom, athena::math_v3::big_operator ("\xe2\x88\x91"));
      verify_roundtrip ();
    }
  }
  void keyboardBigOperatorSelectionRemainsAnIndependentSibling () {
    using namespace athena::auto_close;
    set_preference ("automatic brackets", "on");
    tree selected (FRAC, "a", "b");
    athena::node::metadata meta;
    meta.id= athena::node::new_id ();
    athena::node::set (selected, meta);
    reset_inline (selected);
    const tree before= copy (subtree (current_document_tree (), buffer->root_path));
    editor->selection_set_paths (start (current_document_tree (), content_path ()),
                                 end (current_document_tree (), content_path ()));
    QVERIFY (editor->selection_active_normal ());
    editor->archive_state ();
    editor->start_editing ();
    make_big_operator (editor, "\xe2\x88\xab");
    editor->end_editing ();
    QCOMPARE (content (), tree (CONCAT,
      athena::math_v3::big_operator ("\xe2\x88\xab"), selected));
    const tree after= copy (subtree (current_document_tree (), buffer->root_path));
    editor->undo (0);
    QCOMPARE (subtree (current_document_tree (), buffer->root_path), before);
    editor->redo (0);
    QCOMPARE (subtree (current_document_tree (), buffer->root_path), after);
    verify_roundtrip ();
  }
  void keyboardInvalidNativeOperatorDoesNotCutSelection () {
    using namespace athena::auto_close;
    reset_inline ("selected");
    editor->selection_set_paths (content_path () * 0, content_path () * 8);
    const tree before= copy (content ());
    for (const char* input: {".", "bignone", "unknown-operator"}) {
      make_big_operator (editor, input);
      QCOMPARE (content (), before);
      QVERIFY (editor->selection_active_normal ());
    }
  }
  void keyboardWrappersPreserveV2Trees () {
    using namespace athena::auto_close;
    for (const char* preference: {"on", "off", "mathematics"}) {
      const bool paired= string (preference) == "on";
      set_preference ("automatic brackets", preference);
      for (bool large: {false, true}) {
        reset_inline ("", false);
        QVERIFY (!editor->native_math_editing ());
        make_bracket_open (editor, "(", ")", large);
        if (large)
          QCOMPARE (content (), paired ?
            tree (CONCAT, tree (LEFT, "("), tree (RIGHT, ")")) : tree (LEFT, "("));
        else QCOMPARE (content (), tree (paired ? "()" : "("));
        reset_inline ("", false);
        make_separator (editor, "|", large);
        QCOMPARE (content (), large ? tree (MID, "|") : tree ("|"));
        reset_inline ("", false);
        make_bracket_close (editor, ")", "(", large);
        QCOMPARE (content (), large ? tree (RIGHT, ")") : tree (")"));
      }
      reset_inline ("", false);
      make_big_operator (editor, "sum");
      QCOMPARE (content (), paired ?
        tree (CONCAT, tree (BIG, "sum"), tree (BIG, ".")) : tree (BIG, "sum"));
    }
  }
  void repeatedScriptCommandNestsWithoutChangingArity () {
    reset (tree ("x"));
    editor->start_editing ();
    editor->make_script (true, true);
    editor->insert_tree ("n");
    editor->make_script (true, true);
    editor->insert_tree ("2");
    editor->end_editing ();
    const tree result= content ();
    QVERIFY (athena::math_v3::valid_scripts (result));
    QVERIFY (athena::math_v3::valid_scripts (result[4]));
    QCOMPARE (result[0], tree ("x"));
    QCOMPARE (result[4][0], tree ("n"));
    QCOMPARE (result[4][4], tree ("2"));
    verify_roundtrip ();
  }
  void repeatedPrimesShareOneScript_data () {
    QTest::addColumn<bool> ("right");
    QTest::addColumn<QString> ("input");
    QTest::newRow ("right-primes") << true << QString ("'");
    QTest::newRow ("left-primes") << false << QString ("'");
    QTest::newRow ("left-backprimes") << false << QString ("`");
  }
  void repeatedPrimesShareOneScript () {
    QFETCH (bool, right);
    QFETCH (QString, input);
    const string key= input.toStdString ().c_str ();
    const tree glyph= key == "'" ? tree ("\xe2\x80\xb2") : tree (key);
    const int slot= right ? 4 : 2;
    reset_inline ("r");
    string expected;
    for (int count=1; count<=3; ++count) {
      editor->start_editing ();
      if (right) editor->make_rprime (key);
      else editor->make_lprime (key);
      editor->end_editing ();
      const tree result= content ();
      QVERIFY (athena::math_v3::valid_scripts (result));
      QCOMPARE (result[0], tree ("r"));
      expected= expected * glyph->label;
      QCOMPARE (result[slot], tree (expected));
      editor->typeset_forced ();
      cursor previous;
      for (int i=0; i<count; ++i) {
        cursor caret= editor->eb->find_check_cursor (
          content_path () * path (slot, i*N(glyph->label)));
        QVERIFY (caret->valid);
        if (i > 0) {
          QCOMPARE (caret->oy, previous->oy);
          QVERIFY (caret->ox > previous->ox);
        }
        previous= caret;
      }
      verify_roundtrip ();
    }
  }
  void primeInsideOrdinaryExponentStillAttachesToExponent () {
    reset_inline ("r");
    editor->make_script (true, true);
    editor->insert_tree ("n");
    editor->make_rprime ("'");
    const tree result= content ();
    QVERIFY (athena::math_v3::valid_scripts (result));
    QVERIFY (athena::math_v3::valid_scripts (result[4]));
    QCOMPARE (result[4][0], tree ("n"));
    QCOMPARE (result[4][4], tree ("\xe2\x80\xb2"));
    verify_roundtrip ();
  }
  void primesInsideSubscriptStayOnSubscriptBase () {
    reset_inline ("A");
    editor->make_script (false, true);
    editor->insert_tree ("r");
    string primes;
    for (int count=1; count<=2; ++count) {
      editor->make_rprime ("'");
      primes= primes * "\xe2\x80\xb2";
      const tree result= content ();
      QVERIFY (athena::math_v3::valid_scripts (result));
      QCOMPARE (result[0], tree ("A"));
      QCOMPARE (result[4], tree (""));
      QVERIFY (athena::math_v3::valid_scripts (result[3]));
      QCOMPARE (result[3][0], tree ("r"));
      QCOMPARE (result[3][4], tree (primes));
      editor->typeset_forced ();
      QVERIFY (editor->eb->find_check_cursor (editor->the_path ())->valid);
      verify_roundtrip ();
    }
    // After leaving the scripts, a prime belongs to A rather than to r.
    editor->go_to_border (content_path (), false);
    editor->make_rprime ("'");
    QCOMPARE (content ()[4], tree ("\xe2\x80\xb2"));
    QCOMPARE (content ()[3][4], tree (primes));
    verify_roundtrip ();
  }
  void selectedScriptAttachesToPrecedingBase () {
    reset (tree ("xi"));
    editor->selection_set_paths (content_path () * 1, content_path () * 2);
    editor->start_editing ();
    editor->make_script (false, true);
    editor->end_editing ();
    const tree result= content ();
    QVERIFY (athena::math_v3::valid_scripts (result));
    QCOMPARE (result[0], tree ("x"));
    QCOMPARE (result[3], tree ("i"));
    verify_roundtrip ();
  }
  void compoundScriptUndoRestoresOriginalIdentity () {
    tree base= athena::math_v3::delimited ("(", ")", 'a', tree ("x"));
    athena::node::metadata meta;
    meta.id= athena::node::new_id ();
    athena::node::set (base, meta);
    reset (base);
    const tree before= copy (subtree (current_document_tree (), buffer->root_path));
    editor->archive_state ();
    editor->start_editing ();
    editor->make_script (true, true);
    editor->insert_tree ("2");
    editor->end_editing ();
    QVERIFY (athena::math_v3::valid_scripts (content ()));
    const tree after= copy (subtree (current_document_tree (), buffer->root_path));
    QVERIFY (editor->undo_possibilities () > 0);
    editor->undo (0);
    QVERIFY (subtree (current_document_tree (), buffer->root_path) == before);
    QCOMPARE (athena::node::id (content ()), meta.id);
    editor->redo (0);
    QVERIFY (subtree (current_document_tree (), buffer->root_path) == after);
    verify_roundtrip ();
  }
  void annotationsAndEvaluationBarStayNative () {
    for (bool above: {false, true}) {
      reset (tree ("x"));
      editor->start_editing ();
      if (above) editor->math_make_above ();
      else editor->math_make_below ();
      editor->insert_tree ("n");
      editor->end_editing ();
      const tree annotation= content ();
      QVERIFY (is_func (annotation, MATH_ANNOTATION, 3));
      QCOMPARE (annotation[0], tree ("x"));
      QCOMPARE (annotation[above ? 2 : 1], tree ("n"));
      verify_roundtrip ();
    }
    reset (tree ("x"));
    editor->start_editing ();
    editor->math_evaluation_bar ();
    editor->make_script (false, true);
    editor->insert_tree ("0");
    editor->end_editing ();
    const tree result= content ();
    QVERIFY (athena::math_v3::valid_scripts (result));
    QVERIFY (athena::math_v3::valid_delimited (result[0]));
    QCOMPARE (result[0][0], tree (""));
    QCOMPARE (result[0][1], tree ("|"));
    QCOMPARE (result[0][5], tree ("x"));
    QCOMPARE (result[3], tree ("0"));
    verify_roundtrip ();
  }
  void symmetricBracketClosesWithoutNesting () {
    reset (athena::math_v3::delimited ("|", "|", 'a', tree ("x")));
    editor->go_to_end (content_path () * 5);
    const tree before= copy (content ());
    editor->math_bracket_open ("|", "|", 1);
    QVERIFY (content () == before);
    QVERIFY (editor->the_path () == end (current_document_tree (), content_path ()));
  }
  void nestedBracketClosingMovesOutward () {
    reset (athena::math_v3::delimited ("(", ")", 'a',
      athena::math_v3::delimited ("[", "]", 'a', tree ("x"))));
    editor->go_to_end (content_path () * 5 * 5);
    const tree before= copy (content ());
    editor->math_bracket_close ("]", "[", 1);
    editor->math_bracket_close (")", "(", 1);
    QVERIFY (content () == before);
    QVERIFY (editor->the_path () == end (current_document_tree (), content_path ()));
    verify_roundtrip ();
  }
  void arrowKeysStopOutsideDelimitersInsideMath () {
    with_borrowed_drd context (&editor->drd);
    for (char sizing: {'a', 'f', 'm'})
      for (const tree& body: {tree (""), tree ("x"), tree (FRAC, "a", "b")}) {
        reset_inline (athena::math_v3::delimited ("(", ")", sizing, body,
          sizing == 'm' ? "1" : "", sizing == 'm' ? "2" : ""));
        editor->typeset_forced ();
        editor->notify_change (0, THE_TREE | THE_ENVIRONMENT);
        const tree before= copy (content ());
        editor->go_to_end (content_path () * 5);
        editor->go_right ();
        QVERIFY (editor->the_path () == end (current_document_tree (), content_path ()));
        QCOMPARE (editor->get_env_string (MODE), string ("math"));
        editor->go_right ();
        QCOMPARE (editor->get_env_string (MODE), string ("text"));
        editor->go_to_start (content_path () * 5);
        editor->go_left ();
        QVERIFY (editor->the_path () == start (current_document_tree (), content_path ()));
        QCOMPARE (editor->get_env_string (MODE), string ("math"));
        QCOMPARE (content (), before);
      }
    reset_inline (athena::math_v3::delimited ("(", ")", 'a',
      athena::math_v3::delimited ("[", "]", 'a', "x")));
    editor->typeset_forced ();
    editor->notify_change (0, THE_TREE | THE_ENVIRONMENT);
    editor->go_to_end (content_path () * path (5, 5));
    editor->go_right ();
    QVERIFY (editor->the_path () == end (current_document_tree (), content_path () * 5));
    editor->go_right ();
    QVERIFY (editor->the_path () == end (current_document_tree (), content_path ()));
  }
  void hybridBigOperatorsAcceptScriptsInV3 () {
    SchemeExecutionContext context (nullptr, editor, &editor->drd,
      &buffer->document, 0, 0, 0, SCHEME_CAPABILITY_BUFFER);
    SchemeExecutionScope scope (context);
    for (const char* name: {"prod", "sum", "int", "coprod"}) {
      reset_inline ("");
      editor->start_editing ();
      editor->make_hybrid ();
      editor->insert_tree (name);
      editor->activate_hybrid (false);
      QVERIFY (is_func (content (), BIG_OPERATOR, 2));
      editor->make_script (false, true);
      editor->insert_tree ("i");
      editor->end_editing ();
      QVERIFY (athena::math_v3::valid_scripts (content ()));
      QVERIFY (is_func (content ()[0], BIG_OPERATOR, 2));
      QCOMPARE (content ()[3], tree ("i"));
      verify_roundtrip ();
    }
  }
  void nativeDelimiterDeletionHandlersPreservePair () {
    for (bool forward: {false, true}) {
      reset (athena::math_v3::delimited ("(", ")", 'a', tree ("x")));
      const tree before= copy (content ());
      editor->go_to_border (content_path (), forward);
      editor->remove_text (forward);
      const path body= content_path () * 5;
      QVERIFY (editor->the_path () == (forward ?
        start (current_document_tree (), body) :
        end (current_document_tree (), body)));
      editor->go_to_border (body, !forward);
      editor->remove_text (forward);
      QVERIFY (content () == before);
      verify_roundtrip ();
      reset_inline ("");
      editor->archive_state ();
      editor->start_editing ();
      athena::auto_close::make_bracket_open (editor, "(", ")");
      editor->end_editing ();
      const tree empty_pair= copy (content ());
      QVERIFY (athena::math_v3::valid_delimited (empty_pair));
      editor->archive_state ();
      editor->start_editing ();
      editor->remove_text (forward);
      editor->end_editing ();
      QCOMPARE (content (), tree (""));
      verify_roundtrip ();
      editor->undo (0);
      QCOMPARE (content (), empty_pair);
      editor->redo (0);
      QCOMPARE (content (), tree (""));

      reset (athena::math_v3::delimited ("(", ")", 'a',
        athena::math_v3::delimited ("[", "]", 'a', "")));
      editor->go_to_start (content_path () * path (5, 5));
      editor->remove_text (forward);
      QVERIFY (athena::math_v3::valid_delimited (content ()));
      QCOMPARE (content ()[5], tree (""));
      editor->remove_text (forward);
      QCOMPARE (content (), tree (""));
      verify_roundtrip ();
    }
  }
  void nativeLongArrowDeletionKeepsThreeSlots () {
    // The legacy LONG_ARROW editing handler may remove an empty script child;
    // this is forbidden for the fixed MATH_LONG_ARROW arity in native XML v3.
    for (bool forward: {false, true})
      for (int slot: {1, 2}) {
        tree arrow (MATH_LONG_ARROW, "→", "", "");
        athena::node::metadata metadata;
        metadata.id= athena::node::new_id ();
        metadata.properties["test:arrow"]= athena::node::property (true);
        athena::node::set (arrow, metadata);
        reset (arrow);
        const tree before= copy (content ());
        editor->start_editing ();
        editor->back_in_long_arrow (content (), content_path () * slot, forward);
        editor->end_editing ();
        QCOMPARE (content (), before);
        QCOMPARE (N(content ()), 3);
        QCOMPARE (athena::node::id (content ()), metadata.id);
        QVERIFY (athena::math_v3::valid_native_math (content ()));
        verify_roundtrip ();
      }
  }
  void hybridEmptyHatCanBeDeleted () {
    SchemeExecutionContext context (nullptr, editor, &editor->drd,
      &buffer->document, 0, 0, 0, SCHEME_CAPABILITY_BUFFER);
    SchemeExecutionScope scope (context);
    reset_inline ("");
    editor->make_hybrid ();
    editor->insert_tree ("hat");
    editor->activate_hybrid (false);
    QVERIFY (is_func (content (), MATH_ACCENT, 3));
    QCOMPARE (content ()[2], tree (""));
    editor->remove_text (false);
    QCOMPARE (content (), tree (""));
    verify_roundtrip ();
  }
  void nativeAccentsDeleteThroughTheirBody () {
    for (bool forward: {false, true})
      for (bool below: {false, true})
        for (bool stretch: {false, true}) {
          reset_inline ("");
          if (below) editor->make_wide_under ("^", stretch);
          else editor->make_wide ("^", stretch);
          editor->insert_tree ("x");
          const tree accent= copy (content ());
          QVERIFY (is_func (accent, MATH_ACCENT, 3));
          editor->go_to_border (content_path (), forward);
          editor->remove_text (forward);
          QCOMPARE (content (), accent);
          QVERIFY (editor->the_path () == content_path () * path (2, forward ? 0 : 1));
          editor->remove_text (forward);
          QVERIFY (is_func (content (), MATH_ACCENT, 3));
          QCOMPARE (content ()[2], tree (""));
          editor->remove_text (forward);
          QCOMPARE (content (), tree (""));
          verify_roundtrip ();
        }
  }
  void longArrowKeyboardAndToolbarActionsUseNativeV3 () {
    for (bool below: {false, true}) {
      reset_inline ("");
      editor->start_editing ();
      athena::auto_close::insert_long_arrow (editor, tree ("→"), below);
      editor->end_editing ();
      QVERIFY (is_func (content (), MATH_LONG_ARROW, 3));
      QCOMPARE (content ()[0], tree ("→"));
      QCOMPARE (content ()[1], tree (""));
      QCOMPARE (content ()[2], tree (""));
      QVERIFY (editor->the_path () ==
        start (current_document_tree (), content_path () * (below ? 1 : 2)));
      verify_roundtrip ();

      reset_inline ("", false);
      editor->start_editing ();
      athena::auto_close::insert_long_arrow (editor, tree ("→"), below);
      editor->end_editing ();
      QCOMPARE (content (), below ?
        tree (LONG_ARROW, "→", "", "") : tree (LONG_ARROW, "→", ""));
    }
    reset_inline ("");
    editor->start_editing ();
    athena::auto_close::insert_long_arrow (
      editor, tree (NAMED_SYMBOL, "texmacs:longhookrightarrow"), false);
    editor->end_editing ();
    QCOMPARE (content (), tree (MATH_LONG_ARROW,
      tree (MATH_SYMBOL, "long-hookrightarrow"), "", ""));
    verify_roundtrip ();

    reset_inline ("x");
    editor->start_editing ();
    athena::auto_close::insert_long_arrow (
      editor, tree (NAMED_SYMBOL, "texmacs:nonexistent-arrow"), false);
    editor->end_editing ();
    QCOMPARE (content (), tree ("x"));
    verify_roundtrip ();
  }
  void bigOperatorKeyboardAndToolbarActionsKeepV3Policies () {
    reset_inline ("");
    editor->start_editing ();
    athena::auto_close::insert_big_operator (editor, tree ("∑"));
    editor->end_editing ();
    QCOMPARE (content (), athena::math_v3::big_operator ("∑"));
    verify_roundtrip ();

    const tree legacy_flags (WITH, "math-big-upright", "true",
                             "math-big-limits", "true", "∫");
    reset_inline ("");
    editor->start_editing ();
    athena::auto_close::insert_big_operator (editor, legacy_flags);
    editor->end_editing ();
    QVERIFY (athena::math_v3::valid_scripts (content ()));
    QCOMPARE (content ()[0], athena::math_v3::big_operator ("∫", true));
    QCOMPARE (std::get<std::string> (athena::node::get (content ())->properties.at (
      athena::math_v3::scripts_policy_key).data), std::string ("d"));
    verify_roundtrip ();

    reset_inline ("", false);
    editor->start_editing ();
    athena::auto_close::insert_big_operator (editor, legacy_flags);
    editor->end_editing ();
    QCOMPARE (content (), tree (BIG, legacy_flags));

    // A registry-only named symbol with no Unicode operator glyph has no
    // lossless BIG_OPERATOR encoding. Do not write a legacy BIG into XML v3.
    reset_inline ("x");
    editor->start_editing ();
    athena::auto_close::insert_big_operator (
      editor, tree (NAMED_SYMBOL, "texmacs:idotsint"));
    editor->end_editing ();
    QCOMPARE (content (), tree ("x"));
    verify_roundtrip ();
  }
  void escapeBlackboardFollowedByBracketUsesNativeV3 () {
    SchemeExecutionContext context (nullptr, editor, &editor->drd,
      &buffer->document, ATHENA_NO_ACTOR, ATHENA_NO_VIEW, 0,
      SCHEME_CAPABILITY_BUFFER);
    SchemeExecutionScope scope (context);
    reset_inline ("");
    editor->start_editing ();
    generic_escape_symbol_insert ("bbb-k");
    editor->math_bracket_open ("(", ")", 0);
    editor->end_editing ();
    QCOMPARE (content (), tree (CONCAT,
      tree (MATH_VARIANT, "bb", "k"),
      athena::math_v3::delimited ("(", ")", 'a', "")));
    verify_roundtrip ();
  }

  void emptyNativeVariantBackspaceRemovesWholeNode () {
    // Deleting the last glyph must never navigate into the non-editable "bb"
    // kind field or expose an inactive source macro.
    reset_inline (tree (MATH_VARIANT, "bb", "k"));
    editor->go_to_end (content_path () * 1);
    editor->start_editing ();
    editor->remove_text (false);
    editor->end_editing ();
    QCOMPARE (content (), tree (MATH_VARIANT, "bb", ""));
    editor->start_editing ();
    editor->remove_text (false);
    editor->end_editing ();
    QCOMPARE (content (), tree (""));
    verify_roundtrip ();

    // Repair an already-exposed macro left behind by the old Backspace path.
    reset_inline (tree (INACTIVE, tree (MATH_VARIANT, "bb", "")));
    editor->go_to_end (content_path () * path (0, 0));
    editor->start_editing ();
    editor->remove_text (false);
    editor->end_editing ();
    QCOMPARE (content (), tree (""));
    verify_roundtrip ();
  }

  void mathAlphabetToolbarUsesNativeV3Variants () {
    struct variant_fixture { const char* tag; const char* kind; };
    for (const auto& item: {
           variant_fixture {"math-alpha-bold", "mbf"},
           variant_fixture {"math-alpha-bbb", "bb"},
           variant_fixture {"math-alpha-cal", "cal"},
           variant_fixture {"math-alpha-frak", "frak"},
           variant_fixture {"math-alpha-bold-up", "mbfu"}}) {
      reset_inline ("");
      editor->start_editing ();
      athena::auto_close::insert_alphabet (editor, item.tag, "A");
      editor->end_editing ();
      QCOMPARE (content (), tree (MATH_VARIANT, item.kind, "A"));
      verify_roundtrip ();
    }
    for (const char* tag: {"math-alpha-does-not-exist"}) {
      reset_inline ("x");
      editor->start_editing ();
      athena::auto_close::insert_alphabet (editor, tag, "A");
      editor->end_editing ();
      QCOMPARE (content (), tree ("x"));
      verify_roundtrip ();
      reset_inline ("", false);
      editor->start_editing ();
      athena::auto_close::insert_alphabet (editor, tag, "A");
      editor->end_editing ();
      QCOMPARE (content (), tree (as_tree_label (string (tag)), "A"));
    }
  }
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
    NativeV3MathEditorTest test;
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
  set_env ("ATHENA_HOME_PATH", "/tmp/athena-native-v3-math-editor-test-" *
           as_string ((int) getpid ()));
  cache_initialize ();
  reset_document_tree (current_document_tree ());
  init_athena ();
  start_scheme (argc, argv, run_tests);
  return 1;
}

#include "native_v3_math_editor_test.moc"
