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
#include "Graphics/Gui/gui.hpp"
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

class NativeMathEditor: public edit_main_rep {
public:
  NativeMathEditor (server_rep* server, buffer_document_state* buffer):
    editor_rep (server, buffer), edit_main_rep (server, buffer) {}
  void* derived_this () override { return this; }
};

class NativeV3MathEditorTest: public QObject {
  Q_OBJECT
  buffer_document_state* buffer= nullptr;
  NativeMathEditor* editor= nullptr;
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

private slots:
  void init () {
    previous_brackets= get_preference ("automatic brackets", "mathematics");
    buffer= tm_new<buffer_document_state> (
      nullptr, "tmfs://native-v3-math-test", "", "UTF-8", false, 0);
    swap_current_document_tree (&buffer->document);
    buffer->data->init ("no-zoom")= "true";
    buffer->data->init (ZOOM_FACTOR)= "1";
    set_document (buffer->document, buffer->root_path, tree (DOCUMENT, ""));
    editor= tm_new<NativeMathEditor> (test_server, buffer);
  }
  void cleanup () {
    set_preference ("automatic brackets", previous_brackets);
    tm_delete<editor_rep> (editor);
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
  void nativeDelimiterDeletionHandlersPreservePair () {
    // Exercise the owned handlers directly. Public remove_text dispatch must
    // also route DELIMITED here before this is an end-to-end deletion test.
    for (bool forward: {false, true}) {
      reset (athena::math_v3::delimited ("(", ")", 'a', tree ("x")));
      const tree before= copy (content ());
      editor->back_around (content (), content_path (), forward);
      const path body= content_path () * 5;
      QVERIFY (editor->the_path () == (forward ?
        start (current_document_tree (), body) :
        end (current_document_tree (), body)));
      editor->back_in_around (content (), body, forward);
      QVERIFY (content () == before);
      verify_roundtrip ();
      reset (athena::math_v3::delimited ("(", ")", 'a', tree ("")));
      editor->start_editing ();
      editor->back_in_around (content (), content_path () * 5, forward);
      editor->end_editing ();
      QCOMPARE (content (), tree (""));
      verify_roundtrip ();
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
