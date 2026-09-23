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
#include "Concat/concater.hpp"
#include "drd_std.hpp"
#include "Stack/stacker.hpp"
#include "Boxes/utf8_line.hpp"

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
  void textTypesetting () {
    drd_info drd ("utf8-editor-text", std_drd);
    hashmap<string,tree> h1 (UNINIT), h2 (UNINIT), h3 (UNINIT);
    hashmap<string,tree> h4 (UNINIT), h5 (UNINIT), h6 (UNINIT);
    edit_env env (drd, url_none (), h1, h2, h3, h4, h5, h6);
    env->write_default_env ();
    env->write (FONT, "typewriter=JetBrains Mono,TeX Gyre Pagella");
    env->write ("athena-radioactive-links-suppressed", "true");
    env->update ();
    const string source= "A\xe4\xb8\xad" "e\xcc\x81 <alpha> \xce\xb1";
    box result= typeset_as_concat (env, tree (source), path (0));
    QCOMPARE (N(result), 1);
    box text= result[0];
    QCOMPARE (text->get_leaf_string (), source);
    QVERIFY (text->w () > 0);
    bool found= false;
    text->find_box_path (path (5), found);
    QVERIFY (!found); // Interior of e + combining acute.
    const path caret= text->find_box_path (path (7), found);
    QVERIFY (found);
    QVERIFY (text->find_tree_path (caret) == path (0, 7));
    // Literal angle brackets survive into actual layout, not a Greek glyph.
    const path angle= text->find_box_path (path (9), found);
    QVERIFY (found);
    QVERIFY (text->find_cursor (angle)->ox > text->find_cursor (caret)->ox);

    array<line_item> items= typeset_concat (env, tree (source), path (0));
    QVERIFY (N(items) > 1);
    bool literal= false;
    for (int i=0; i<N(items); ++i) {
      QVERIFY (items[i]->type != STRING_ITEM);
      const string fragment= items[i]->b->get_leaf_string ();
      if (fragment == "<alpha>") literal= true;
      const int first= items[i]->b->get_leaf_left_pos ();
      const int last= items[i]->b->get_leaf_right_pos ();
      QVERIFY (utf8_grapheme_boundary (source, first));
      QVERIFY (utf8_grapheme_boundary (source, last));
    }
    QVERIFY (literal);

    items= typeset_concat (env, tree ("x  y"), path (0));
    QCOMPARE (N(items), 2);
    QCOMPARE (items[0]->spc->def, 2 * env->fn->spc->def);
    const string rtl= "\xd7\x90\xd7\x91 \xd7\x92\xd7\x93";
    result= typeset_as_concat (env, tree (rtl), path (0));
    QCOMPARE (N(result), 1);
    text= result[0];
    const auto left= text->find_box_path (path (N(rtl)), found);
    QVERIFY (found);
    const auto right= text->find_box_path (path (0), found);
    QVERIFY (found);
    QVERIFY (text->find_cursor (right)->ox > text->find_cursor (left)->ox);

    athena::text::physical_font_source physical;
    QVERIFY (env->fn->physical_source (physical));
    auto request= athena::text::font_request_from_source (physical);
    QCOMPARE (request.point_size, physical.point_size);
    QCOMPARE (request.vertical_dpi, physical.vertical_dpi);
    env->write_update (FONT_FAMILY, "tt");
    QVERIFY (env->fn->physical_source (physical));
    request= athena::text::font_request_from_source (physical);
    QVERIFY (request.description_utf8.find ("JetBrains Mono") != std::string::npos);
    QCOMPARE (request.vertical_dpi, physical.vertical_dpi);
    result= typeset_as_concat (env, tree (source), path (0));
    QCOMPARE (result[0]->get_leaf_string (), source);
  }
  void inlineSourceMapping () {
    drd_info drd ("utf8-inline-source", std_drd);
    hashmap<string,tree> h1 (UNINIT), h2 (UNINIT), h3 (UNINIT);
    hashmap<string,tree> h4 (UNINIT), h5 (UNINIT), h6 (UNINIT);
    edit_env env (drd, url_none (), h1, h2, h3, h4, h5, h6);
    env->write_default_env ();
    env->write (FONT, "TeX Gyre Pagella");
    env->write ("athena-radioactive-links-suppressed", "true");
    env->update ();

    const string first= "\xd7\x90\xd7\x91 ", second= "\xd7\x92\xd7\x93";
    auto whole= typeset_as_concat (env, tree (first * second), path (0));
    box joined= typeset_as_concat (env, tree (CONCAT, first, second), path (0));
    QCOMPARE (N(joined), 1);
    QCOMPARE (joined->w (), whole->w ());
    bool found= false;
    const path start= joined->find_box_path (path (0, 0, 0), found);
    QVERIFY (found);
    const path end= joined->find_box_path (path (0, 1, N(second)), found);
    QVERIFY (found);
    QVERIFY (joined->find_cursor (start)->ox > joined->find_cursor (end)->ox);
    for (const path source: {path (0, 0, N(first)), path (0, 1, 0), path (0, 1, 2)}) {
      const path bp= joined->find_box_path (source, found);
      QVERIFY (found);
      QVERIFY (joined->find_tree_path (bp) == source);
      const auto other= joined->with_cursor_affinity (bp, athena::text::caret_affinity::upstream);
      QVERIFY (joined->find_tree_path (other) == source);
    }
    const path selection_start= joined->find_box_path (path (0, 0, 0), found);
    QVERIFY (found);
    const auto selection= joined->find_selection (selection_start, end);
    QVERIFY (selection->start == path (0, 0, 0));
    QVERIFY (selection->end == path (0, 1, N(second)));
    QVERIFY (!is_nil (selection->rs));

    box ligature= typeset_as_concat (env, tree (CONCAT, "of", "fice"), path (0));
    auto reference= typeset_as_concat (env, tree ("office"), path (0));
    QCOMPARE (N(ligature), 1);
    QCOMPARE (ligature->w (), reference->w ());
    auto expanded= ligature->expand_glyphs (0, 0.1);
    const path bp= expanded->find_box_path (path (0, 1, 2), found);
    QVERIFY (found);
    QVERIFY (expanded->find_tree_path (bp) == path (0, 1, 2));

    tree styled (CONCAT, first, tree (WITH, FONT_SERIES, "bold", second), "!");
    joined= typeset_as_concat (env, styled, path (0));
    QCOMPARE (N(joined), 1);
    for (const path source: {path (0, 0, 2), path (0, 1, 0),
                            path (0, path (1, path (2, 2))), path (0, 1, 1), path (0, 2, 1)}) {
      const auto at= joined->find_box_path (source, found);
      QVERIFY (found);
      QVERIFY (joined->find_tree_path (at) == source);
    }
    joined->find_box_path (path (0, path (1, path (2, 1))), found);
    QVERIFY (!found);

    athena::text::physical_font_source physical;
    QVERIFY (env->fn->physical_source (physical));
    const auto regular= athena::text::font_request_from_source (physical);
    env->write_update (FONT_SERIES, "bold");
    QVERIFY (env->fn->physical_source (physical));
    const auto bold= athena::text::font_request_from_source (physical);
    env->write_update (FONT_SERIES, "medium");
    athena::text::font_paragraph expected ("x WWW y", regular, {{2, 5, bold}});
    joined= typeset_as_concat (env, tree (CONCAT, "x ",
      tree (WITH, FONT_SERIES, "bold", "WWW"), " y"), path (0));
    QCOMPARE (N(joined), 1);
    QCOMPARE (joined->w (), expected.line (0, 7).advance);

    joined= typeset_as_concat (env, tree (CONCAT, "e", "\xcc\x81"), path (0));
    QCOMPARE (N(joined), 1);
    joined->find_box_path (path (0, 0, 1), found);
    QVERIFY (!found); // A node boundary must not introduce a grapheme stop.
    const auto accent_end= joined->find_box_path (path (0, 1, 2), found);
    QVERIFY (found);
    QVERIFY (joined->find_tree_path (accent_end) == path (0, 1, 2));

    // Paint changes and explicit layout spacing are not silently flattened.
    joined= typeset_as_concat (env, tree (CONCAT, "x",
      tree (WITH, COLOR, "red", "y"), "z"), path (0));
    QVERIFY (N(joined) > 1);
    const auto color_path= joined->find_box_path (path (0, path (1, path (2, 1))), found);
    QVERIFY (found);
    QVERIFY (joined->find_tree_path (color_path) == path (0, path (1, path (2, 1))));
  }
  void wrappedText () {
    drd_info drd ("utf8-wrapped-text", std_drd);
    hashmap<string,tree> h1 (UNINIT), h2 (UNINIT), h3 (UNINIT);
    hashmap<string,tree> h4 (UNINIT), h5 (UNINIT), h6 (UNINIT);
    edit_env env (drd, url_none (), h1, h2, h3, h4, h5, h6);
    env->write_default_env ();
    env->write (FONT, "TeX Gyre Pagella");
    env->write (PAR_MODE, "justify");
    env->write (PAR_FIRST, "0cm");
    env->write (PAR_LEFT, "0cm");
    env->write (PAR_RIGHT, "0cm");
    env->write ("athena-radioactive-links-suppressed", "true");
    env->update ();
    const string word= "\xd7\x90\xd7\x91";
    const string source= word * " " * word * " " * word * " " * word * " " * word * " " * word;
    auto atoms= typeset_concat (env, tree (source), path (0));
    const SI width= 3 * atoms[0]->b->w () + 2 * atoms[0]->spc->def + env->fn->spc->def / 2;
    stack_border border;
    auto pages= typeset_stack (env, tree (source), path (0), width,
                               array<line_item> (), array<line_item> (), border);
    int lines= 0;
    for (int i=0; i<N(pages); ++i) if (pages[i]->type == PAGE_LINE_ITEM) {
      auto row= pages[i]->b;
      QVERIFY (N(row) == 1);
      auto leaf= row[0];
      QVERIFY (is_utf8_line_box (leaf));
      const int first= leaf->get_leaf_left_pos (), last= leaf->get_leaf_right_pos ();
      bool found= false;
      const auto start= row->find_box_path (path (0, first), found);
      QVERIFY (found);
      const auto end= row->find_box_path (path (0, last), found);
      QVERIFY (found);
      QVERIFY (row->find_cursor (start)->ox > row->find_cursor (end)->ox);
      if (lines == 0) QCOMPARE (row->w (), width);
      for (int at=first; at<=last; ++at) if (utf8_grapheme_boundary (source, at)) {
        const auto bp= row->find_box_path (path (0, at), found);
        QVERIFY (found);
        QVERIFY (row->find_tree_path (bp) == path (0, at));
      }
      ++lines;
    }
    QVERIFY (lines >= 2);
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
