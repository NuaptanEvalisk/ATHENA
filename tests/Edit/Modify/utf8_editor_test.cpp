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
#include <algorithm>
#include <cstdlib>
#include <stdexcept>
#include <thread>
#include <unistd.h>

#include "ATHENA/server.hpp"
#include "Editor/edit_main.hpp"
#include "Edit/Interface/native_math_keyboard.hpp"
#include "Edit/Interface/native_latex_commands.hpp"
#include "Graphics/Gui/gui.hpp"
#include "boot.hpp"
#include "buffer_state.hpp"
#include "data_cache.hpp"
#include "drd_mode.hpp"
#include "scheme.hpp"
#include "sys_utils.hpp"
#include "tree_select.hpp"
#include "tree_traverse.hpp"
#include "utf8_edit.hpp"
#include "Concat/concater.hpp"
#include "drd_std.hpp"
#include "Stack/stacker.hpp"
#include "Boxes/utf8_line.hpp"
#include "Qt/qt_renderer.hpp"
#include "Qt/QTMKeyboardEvent.hpp"
#include "named_symbol.hpp"
#include "tree_analyze.hpp"
#include "Xml/legacy_document_import.hpp"
#include "Xml/clipboard_xml.hpp"
#include "packrat_parser.hpp"
#include "math_token.hpp"
#include "math_font.hpp"
#include "ATHENA/Data/new_buffer.hpp"
#include "convert.hpp"
#include "file.hpp"
#include "web_files.hpp"

bool headless_mode= true;
bool is_headless () { return true; }
static server_rep* test_server= nullptr;

class InlineRenderProbe: public qt_renderer_rep {
public:
  struct Draw { std::string text; color pen; SI x, y; };
  std::vector<Draw> draws;
  struct Link { string target; SI x1, y1, x2, y2; };
  std::vector<Link> references, anchors;
  InlineRenderProbe (QPainter* painter): qt_renderer_rep (painter, 1.0, 400, 120) {
    set_zoom_factor (1.0);
    set_clipping (0, -120*std_shrinkf*PIXEL, 400*std_shrinkf*PIXEL, 0);
    move_origin (10*std_shrinkf*PIXEL, -60*std_shrinkf*PIXEL);
  }
  void draw_utf8 (const athena::text::shaped_text& run, std::string_view source,
                  SI x, SI y) override {
    draws.push_back ({std::string (source), get_pencil ()->get_color (), x, y});
    qt_renderer_rep::draw_utf8 (run, source, x, y);
  }
  void href (string target, SI x1, SI y1, SI x2, SI y2) override {
    references.push_back ({target, x1, y1, x2, y2});
  }
  void anchor (string target, SI x1, SI y1, SI x2, SI y2) override {
    anchors.push_back ({target, x1, y1, x2, y2});
  }
};

class Utf8TestEditor: public edit_main_rep {
public:
  Utf8TestEditor (server_rep* server, buffer_document_state* buffer):
    editor_rep (server, buffer), edit_main_rep (server, buffer) {}
  void* derived_this () override { return this; }
  bool border_jump_skips_accessible_child (
    path old_path, path new_path, bool forwards) {
    return physical_border_jump_skips_accessible_child (
      old_path, new_path, forwards);
  }
  void set_test_child_accessibility (tree_label tag, int child, bool accessible) {
    drd->set_arity (tag, 1, 0, ARITY_NORMAL, CHILD_DETAILED);
    drd->set_accessible (
      tag, child, accessible ? ACCESSIBLE_ALWAYS : ACCESSIBLE_NEVER);
  }
  path spellable_range (path p, string language) {
    search_lan= language;
    return test_spellable (p);
  }
};

class TestUtf8Editor: public QObject {
  Q_OBJECT
private slots:
  void tmfsNativeDocument () {
    const tree expected (DOCUMENT, compound ("style", "generic"),
      compound ("body", tree (DOCUMENT, "caf\xc3\xa9 \xe4\xb8\xad \xf0\x9f\x91\xa9 <literal> \\\\xE9",
                               compound ("named-symbol", "texmacs:mathD"))));
    object handler= call (eval ("(lambda (doc) (lambda (name) "
      "(if (equal? name \"tree\") doc (tree->stree doc))))"), object (expected));
    call ("tmfs-handler", object ("utf8-native-test"), eval ("'load"), handler);
    for (const char* kind: {"tree", "stree"}) {
      url source (string ("tmfs://utf8-native-test/") * kind);
      QVERIFY (import_tree (source, "stm") == expected);
    }
    call ("tmfs-handler", object ("utf8-raw-test"), eval ("'load"),
          eval ("(lambda (name) \"raw caf\xc3\xa9\")"));
    url raw= get_from_server (url ("tmfs://utf8-raw-test/file"));
    string data;
    QVERIFY (!load_string (raw, data, false));
    QCOMPARE (data, string ("raw caf\xc3\xa9"));
    remove (raw);
  }
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
  void namedSymbols () {
    using namespace athena::text;
    using namespace athena::document;
    const auto& registry= standard_named_symbols ();
    QCOMPARE (registry.size (), std::size_t (869));
    QVERIFY (!registry.lookup ("unknown:symbol"));
    QVERIFY (!registry.lookup ("<mathD>"));
    const auto* native_mathd= registry.lookup ("texmacs:mathD");
    QVERIFY (native_mathd && native_mathd->glyph_utf8 == "D" &&
             native_mathd->virtual_font.empty ());
    const auto* virtual_backassign= registry.lookup ("texmacs:backassign");
    QVERIFY (virtual_backassign && virtual_backassign->glyph_utf8.empty () &&
             virtual_backassign->virtual_font == "emu-operators" &&
             virtual_backassign->virtual_symbol == "backassign");
    const auto* idotsint= registry.lookup ("texmacs:idotsint");
    QVERIFY (idotsint && idotsint->op_type == OP_UNARY &&
             idotsint->virtual_font == "tradi-long");
    const auto* native_recipe= registry.lookup ("texmacs:Yleft");
    QVERIFY (native_recipe && native_recipe->glyph_utf8.empty () &&
             native_recipe->virtual_font.empty () && native_recipe->recipe &&
             native_recipe->recipe->kind == named_symbol_recipe_kind::rotate);
    QVERIFY_THROWS_EXCEPTION (std::invalid_argument, named_symbol_registry ("{}"));
    const std::string entry= R"({"identity":"test:symbol","glyph":"x","math_class":"symbol","slant":"upright"})";
    QVERIFY_THROWS_EXCEPTION (std::invalid_argument,
      named_symbol_registry ("{\"version\":1,\"symbols\":[" + entry + "," + entry + "]}"));
    QVERIFY_THROWS_EXCEPTION (std::invalid_argument, named_symbol_registry (
      R"({"version":1,"symbols":[{"identity":"test:symbol","glyph":"x","math_class":"typo","slant":"upright"}]})"));
    QVERIFY_THROWS_EXCEPTION (std::invalid_argument, named_symbol_registry (
      R"({"version":1,"symbols":[{"identity":"test:symbol","glyph":"x","virtual_font":"v","virtual_symbol":"s","math_class":"symbol","slant":"upright"}]})"));
    QVERIFY_THROWS_EXCEPTION (std::invalid_argument, named_symbol_registry (
      R"({"version":1,"symbols":[{"identity":"test:symbol","virtual_font":"v","math_class":"symbol","slant":"upright"}]})"));
    QVERIFY_THROWS_EXCEPTION (std::invalid_argument, named_symbol_registry (
      R"({"version":1,"symbols":[{"identity":"test:symbol","glyph":"x","recipe":["rotate",90,"x"],"math_class":"symbol","slant":"upright"}]})"));
    QVERIFY_THROWS_EXCEPTION (std::invalid_argument, named_symbol_registry (
      R"({"version":1,"symbols":[{"identity":"test:symbol","recipe":["rotate",900,"x"],"math_class":"symbol","slant":"upright"}]})"));

    drd_info drd ("utf8-symbols", std_drd);
    hashmap<string,tree> h1 (UNINIT), h2 (UNINIT), h3 (UNINIT);
    hashmap<string,tree> h4 (UNINIT), h5 (UNINIT), h6 (UNINIT);
    edit_env env (drd, url_none (), h1, h2, h3, h4, h5, h6);
    env->write_default_env ();
    env->write (FONT, "pagella");
    env->write (FONT_SHAPE, "italic");
    env->update ();
    physical_font_source physical;
    QVERIFY (env->fn->physical_source (physical));
    const auto base= font_request_from_source (physical);
    const legacy_cork_table legacy ((qEnvironmentVariable ("ATHENA_PATH") + "/langs/encoding").toStdString ());
    for (const char* name: {"mathD", "mathd", "mathe", "mathi", "mathcatalan",
                            "mathGamma", "mathLaplace", "matheuler", "mathlambda", "mathpi"}) {
      const string identity= string ("texmacs:") * name;
      const auto* definition= registry.lookup (std::string_view (identity.data (), N(identity)));
      QVERIFY (definition);
      const tree symbol (NAMED_SYMBOL, identity);
      int semantic_position= 0;
      QCOMPARE (math_language ("std-math")->advance (
        symbol, semantic_position)->op_type,
        definition->op_type);
      const auto imported= import_legacy_document (
        tree (DOCUMENT, string ("<") * name * ">"), legacy).document;
      QVERIFY (imported == tree (DOCUMENT, tree (CONCAT, symbol)));
      QVERIFY (read_xml (write_xml (imported)) == imported);
      QVERIFY (env->exec (symbol) == symbol);
      QVERIFY (!is_accessible_cursor (symbol, path (0, 0)));
      QVERIFY (valid_cursor (symbol, path (0)));
      QVERIFY (valid_cursor (symbol, path (1)));
      QVERIFY (next_accessible (symbol, path (0)) == path (1));
      QVERIFY (previous_accessible (symbol, path (1)) == path (0));
      const int old_access= set_access_mode (DRD_ACCESS_SOURCE);
      const bool source_access= is_accessible_cursor (symbol, path (0, 0));
      set_access_mode (old_access);
      QVERIFY (source_access);

      array<line_item> items= typeset_concat (env, symbol, path (0));
      QCOMPARE (N(items), 1);
      QCOMPARE (items[0]->type, STD_ITEM);
      QCOMPARE (items[0]->op_type, definition->op_type);
      box rendered= items[0]->b;
      QVERIFY (rendered->w () > 0);
      QVERIFY (is_utf8_line_box (rendered[0]));
      auto expected_request= font_request_with_italic (
        base, definition->italic);
      auto expected= utf8_line_box (decorate (path (0)),
        std::make_shared<font_paragraph> (definition->glyph_utf8,
          expected_request),
        0, definition->glyph_utf8.size (), env->fn, env->pen);
      QCOMPARE (rendered->w (), expected->w ());
      QCOMPARE (rendered->y1, expected->y1);
      QCOMPARE (rendered->y2, expected->y2);
      for (int endpoint: {0, 1}) {
        bool found= false;
        const auto caret= rendered->find_box_path (path (endpoint), found);
        QVERIFY (found);
        QVERIFY (rendered->find_tree_path (caret) == path (0, endpoint));
        auto expanded= rendered->expand_glyphs (0, 0.1);
        const auto expanded_caret= expanded->find_box_path (path (endpoint), found);
        QVERIFY (found && expanded->find_tree_path (expanded_caret) == path (0, endpoint));
      }
    }
    for (const char* identity: {"texmacs:Yleft", "texmacs:Yright",
                                "texmacs:curlywedgeuparrow",
                                "texmacs:curlywedgedownarrow",
                                "texmacs:curlyveeuparrow",
                                "texmacs:curlyveedownarrow",
                                "texmacs:leftrightarroweq",
                                "texmacs:subsetpluseq",
                                "texmacs:supsetpluseq",
                                "texmacs:longequivlim"}) {
      const auto* definition= registry.lookup (identity);
      QVERIFY (definition && definition->recipe);
      const tree symbol (NAMED_SYMBOL, string (identity));
      auto items= typeset_concat (env, symbol, path (0));
      QCOMPARE (N(items), 1);
      QCOMPARE (items[0]->op_type, definition->op_type);
      QVERIFY (items[0]->b->w () > 0);
      QVERIFY (items[0]->b->h () > 0);
    }
    const tree unknown (NAMED_SYMBOL, "unregistered:symbol");
    QVERIFY (env->exec (unknown) == unknown);
    auto missing= typeset_concat (env, unknown, path (0));
    QCOMPARE (N(missing), 1);
    QCOMPARE (missing[0]->b[0]->get_leaf_string (), string ("[missing symbol]"));
    QCOMPARE (symbol_type (tree (NAMED_SYMBOL, "texmacs:mathD")), SYMBOL_BASIC);
    QCOMPARE (symbol_type (tree (NAMED_SYMBOL, "texmacs:mathcatalan")), SYMBOL_BASIC);
  }
  void symbolDeletion_data () {
    QTest::addColumn<bool> ("forward");
    QTest::newRow ("delete-symbol") << true;
    QTest::newRow ("backspace-symbol") << false;
  }
  void symbolDeletion () {
    QFETCH (bool, forward);
    const tree symbol (NAMED_SYMBOL, "texmacs:mathD");
    const tree source (CONCAT, "a", symbol, "b");
    const path paragraph= buffer->root_path * 0;
    editor->go_to (paragraph * 0);
    editor->start_editing ();
    editor->insert_tree (source);
    editor->end_editing ();
    editor->go_to (paragraph * path (1, forward ? 0 : 1));
    editor->archive_state ();
    editor->start_editing ();
    editor->remove_text (forward);
    editor->end_editing ();
    QVERIFY (subtree (current_document_tree (), paragraph) == tree ("ab"));
    editor->undo (0);
    QVERIFY (subtree (current_document_tree (), paragraph) == source);
    editor->redo (0);
    QVERIFY (subtree (current_document_tree (), paragraph) == tree ("ab"));
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
    QCOMPARE (request.primary.point_size, physical.point_size);
    QCOMPARE (request.primary.vertical_dpi, physical.vertical_dpi);
    const auto regular= request;
    env->write_update (FONT_FAMILY, "tt");
    QVERIFY (env->fn->physical_source (physical));
    request= athena::text::font_request_from_source (physical);
    QVERIFY (request.primary.file.file_utf8.find ("JetBrains") != std::string::npos);
    QCOMPARE (request.primary.vertical_dpi, physical.vertical_dpi);
    result= typeset_as_concat (env, tree (source), path (0));
    QCOMPARE (result[0]->get_leaf_string (), source);
    const auto monospace= request;
    env->write_update (FONT_FAMILY, "rm");
    athena::text::font_paragraph mixed ("x MMM y", regular, {{2, 5, monospace}});
    result= typeset_as_concat (env, tree (CONCAT, "x ",
      tree (WITH, FONT_FAMILY, "tt", "MMM"), " y"), path (0));
    QCOMPARE (N(result), 1);
    QCOMPARE (result->w (), mixed.line (0, 7).advance);
    bool restored= false;
    const auto mono_cursor= result->find_box_path (path (0, path (1, path (2, 2))), restored);
    QVERIFY (restored);
    QVERIFY (result->find_tree_path (mono_cursor) == path (0, path (1, path (2, 2))));
  }
  void unicodeKeyboard () {
    QTMKeyboard keyboard;
    for (const string text: {string ("\xc3\xa9"), string ("\xe4\xb8\xad"),
         string ("\xf0\x9f\x98\x80"), string ("e\xcc\x81"),
         string ("\xc2\xa8"), string ("\xcc\x81"), string ("<"), string (">")}) {
      const QKeyEvent event (QEvent::KeyPress, Qt::Key_unknown, Qt::NoModifier,
                             QString::fromUtf8 (text.data (), N(text)));
      QCOMPARE (QTMKeyboardEvent (keyboard, event).texmacsKeyCombination (), text);
    }
    const QKeyEvent dead (QEvent::KeyPress, Qt::Key_Dead_Acute, Qt::NoModifier);
    QCOMPARE (QTMKeyboardEvent (keyboard, dead).texmacsKeyCombination (), string ("acute"));
    const QKeyEvent shortcut (QEvent::KeyPress, Qt::Key_F, Qt::ControlModifier, "\x06");
    QCOMPARE (QTMKeyboardEvent (keyboard, shortcut).texmacsKeyCombination (), string ("C-f"));
    const string composed= "\xc3\x89";
    keyboard.setShiftPreference (0x1234, composed);
    QVERIFY (keyboard.hasShiftPreference (0x1234));
    QCOMPARE (keyboard.getShiftPreference (0x1234), composed);
    const QKeyEvent shifted (QEvent::KeyPress, Qt::Key_1,
      Qt::ControlModifier | Qt::ShiftModifier, 0, 0x1234, 0, "\x01");
    QCOMPARE (QTMKeyboardEvent (keyboard, shifted).texmacsKeyCombination (), "C-" * composed);
  }
  void mathTypesetting () {
    using namespace athena::text;
    QCOMPARE (native_math_keyboard_binding_count (), 2375);
    QCOMPARE (native_latex_command_count (), 736);
    string latex_help;
    command latex_command;
    QVERIFY (native_latex_get_command ("alpha", latex_help, latex_command));
    QCOMPARE (latex_help, string ("Insert α"));
    QVERIFY (native_latex_get_command ("acute", latex_help, latex_command));
    QCOMPARE (latex_help, string ("Make acute"));
    drd_info drd ("utf8-math-typesetting", std_drd);
    hashmap<string,tree> h1 (UNINIT), h2 (UNINIT), h3 (UNINIT);
    hashmap<string,tree> h4 (UNINIT), h5 (UNINIT), h6 (UNINIT);
    edit_env env (drd, url_none (), h1, h2, h3, h4, h5, h6);
    env->write_default_env ();
    env->write (FONT, "cal=TeX Gyre Termes,frak=TeX Gyre Pagella,bbb=TeX Gyre Bonum,TeX Gyre Pagella");
    env->write (MODE, "math");
    env->update ();
    for (auto test: {std::pair<string, math_alphabet> {"h", math_alphabet::italic},
          {"\xce\xb1", math_alphabet::italic}, {"e\xcc\x81", math_alphabet::italic},
          {"12", math_alphabet::normal}, {"xyz", math_alphabet::normal},
          {"\xe2\x84\xb0", math_alphabet::script},
          {"\xe2\x84\x9d", math_alphabet::double_struck}}) {
      auto items= typeset_concat (env, tree (test.first), path (0));
      QCOMPARE (N(items), 1);
      auto b= items[0]->b;
      QVERIFY (is_utf8_line_box (b));
      QCOMPARE (b->get_leaf_string (), test.first);
      QCOMPARE (b->get_leaf_left_pos (), 0);
      QCOMPARE (b->get_leaf_right_pos (), N(test.first));
      auto request= math_font_request (env->fn, test.second);
      auto expected= std::make_shared<font_paragraph> (
        std::string (test.first.data (), N(test.first)), request);
      shaping_options options;
      options.ligatures= false;
      const auto line= expected->line (0, N(test.first), options);
      QVERIFY (!line.missing_glyphs);
      QCOMPARE (b->w (), line.advance);
      auto cursor= b->find_cursor (path (N(test.first)));
      QCOMPARE (cursor->ox, line.advance);
      QVERIFY (b->find_tree_path (path (N(test.first))) == path (0, N(test.first)));
      if (test.second == math_alphabet::script)
        QVERIFY (request.primary.file.file_utf8.find ("termes") != std::string::npos);
      if (test.second == math_alphabet::double_struck)
        QVERIFY (request.primary.file.file_utf8.find ("bonum") != std::string::npos);
    }
    auto expression= typeset_concat (env, tree ("\xce\xb1=1"), path (0));
    QCOMPARE (N(expression), 3);
    QCOMPARE (expression[0]->b->get_leaf_right_pos (), 2);
    QCOMPARE (expression[1]->b->get_leaf_left_pos (), 2);
    QCOMPARE (expression[1]->op_type, OP_INFIX);
    QVERIFY (expression[0]->spc->def > 0);
    QVERIFY (expression[1]->spc->def > 0);
    auto product= typeset_concat (env, tree ("a*b"), path (0));
    QCOMPARE (N(product), 3);
    QCOMPARE (product[1]->b->get_leaf_string (), string ("*"));
    QCOMPARE (product[1]->b->get_leaf_left_pos (), 1);
    QCOMPARE (product[1]->b->get_leaf_right_pos (), 2);
    QCOMPARE (product[1]->b->w (), SI (0));
    QCOMPARE (product[1]->op_type, OP_INFIX);
    QVERIFY (product[0]->spc->def > 0);
    QVERIFY (product[1]->spc->def > 0);
    auto literal= typeset_concat (env, tree ("<alpha>"), path (0));
    string retained;
    for (int i=0; i<N(literal); ++i) retained << literal[i]->b->get_leaf_string ();
    QCOMPARE (retained, string ("<alpha>"));
    env->write (MATH_FONT, "cal");
    env->update ();
    auto calligraphic= typeset_concat (env, tree ("E"), path (0));
    auto req= math_font_request (env->fn, math_alphabet::script);
    font_paragraph reference ("E", req);
    QCOMPARE (calligraphic[0]->b->w (), reference.line (0, 1).advance);
    QCOMPARE (calligraphic[0]->b->get_leaf_string (), string ("E"));
  }
  void nativeMathAltTableShortcut () {
    QVERIFY (native_math_keyboard_has_registered_key ("A-t"));
    QCOMPARE (test_server->kbd_pre_rewrite ("math t"), string ("A-t"));
  }
  void nativeMathTabVariants () {
    QVERIFY (native_math_keyboard_has_registered_key ("A-t tab"));
    QCOMPARE (test_server->kbd_pre_rewrite ("math t var"),
              string ("A-t tab"));
    QCOMPARE (test_server->kbd_pre_rewrite ("- var"), string ("- tab"));
    QVERIFY (native_math_keyboard_has_registered_key ("- tab"));
  }

  void mathDelimiterFonts () {
    using namespace athena::text;
    drd_info drd ("utf8-math-delimiters", std_drd);
    hashmap<string,tree> h1 (UNINIT), h2 (UNINIT), h3 (UNINIT);
    hashmap<string,tree> h4 (UNINIT), h5 (UNINIT), h6 (UNINIT);
    edit_env env (drd, url_none (), h1, h2, h3, h4, h5, h6);
    env->write_default_env ();
    env->write (FONT, "TeX Gyre Pagella");
    env->write (MODE, "math");
    for (const char* weight: {"medium", "bold"}) {
      env->write (FONT_SERIES, weight);
      env->update ();
      for (tree delimiter: {tree (LEFT, "("), tree (RIGHT, ")"),
                             tree (MID, "|"), tree (LEFT, "\xe2\x9f\xa8")}) {
        auto items= typeset_concat (env, delimiter, path (0));
        QCOMPARE (N(items), 1);
        QVERIFY (items[0]->b->w () > 0);
        const string scalar= delimiter[0]->label;
        // Concatenation replaces delimiters with stretch wrappers; cursor
        // assertions belong to the editable text leaf before that step.
        box b= math_text_box (path (0), scalar, env->fn, env->pen);
        font_paragraph expected (std::string (scalar.data (), N(scalar)),
          math_font_request (env->fn, math_alphabet::normal));
        auto line= expected.line (0, N(scalar));
        QVERIFY (!line.missing_glyphs);
        QCOMPARE (b->w (), line.advance);
        QCOMPARE (b->get_leaf_string (), scalar);
        QCOMPARE (b->find_cursor (path (N(scalar)))->ox, line.advance);
      }
      tree nested (VAR_AROUND, "(",
        tree (CONCAT, "B", tree (RSUB, "E"), tree (VAR_AROUND, "(", "0,1", ")")), ")");
      QVERIFY (typeset_as_concat (env, nested, path (0))->w () > 0);
      // A glyph with no horizontal assembly exercises wide_box's fallback.
      SI width= 4 * env->fn->wfn;
      box accent= wide_box (path (0), "x", env->fn, env->pen, width);
      QVERIFY (accent->w () >= width - 1);
    }
  }

  void nativeListMarkers () {
    drd_info drd ("utf8-list-markers", std_drd);
    hashmap<string,tree> h1 (UNINIT), h2 (UNINIT), h3 (UNINIT);
    hashmap<string,tree> h4 (UNINIT), h5 (UNINIT), h6 (UNINIT);
    edit_env env (drd, url_none (), h1, h2, h3, h4, h5, h6);
    env->write_default_env ();
    env->write (FONT, "TeX Gyre Pagella");
    env->update ();
    env->exec (tree (USE_PACKAGE,
      string (std::getenv ("ATHENA_PATH")) * "/packages/standard/std-list.ts"));
    for (auto marker: {std::pair<const char*, const char*> {"item-1", "\xe2\x80\xa2"},
                       {"item-2", "\xe2\x88\x98"}}) {
      box b= typeset_as_concat (env, compound (marker.first), path (0));
      QImage image (400, 120, QImage::Format_ARGB32);
      image.fill (Qt::white);
      QPainter painter (&image);
      InlineRenderProbe probe (&painter);
      rectangles painted;
      b->redraw (&probe, path (), painted);
      std::string text;
      for (const auto& draw: probe.draws) text+= draw.text;
      QCOMPARE (text, std::string (marker.second));
    }
  }

  void nativePagellaSmallCaps () {
    using namespace athena::text;
    drd_info drd ("utf8-pagella-smallcaps", std_drd);
    hashmap<string,tree> h1 (UNINIT), h2 (UNINIT), h3 (UNINIT);
    hashmap<string,tree> h4 (UNINIT), h5 (UNINIT), h6 (UNINIT);
    edit_env env (drd, url_none (), h1, h2, h3, h4, h5, h6);
    env->write_default_env ();
    env->write (FONT, "TeX Gyre Pagella");
    env->write (FONT_SHAPE, "small-caps");
    env->write ("athena-radioactive-links-suppressed", "true");
    env->update ();

    native_text_source source;
    QVERIFY (env->fn->native_text_source (source));
    constexpr auto smcp= open_type_tag ('s', 'm', 'c', 'p');
    QVERIFY (std::find_if (
      source.features.begin (), source.features.end (),
      [smcp] (const open_type_feature& feature) {
        return feature.tag == smcp && feature.value == 1;
      }) != source.features.end ());
    QVERIFY (open_type_has_substitution_feature (source.physical, smcp));

    const std::string text= "By Somebody";
    shaping_options smallcaps;
    smallcaps.features= source.features;
    const auto shaped= shape_freetype_utf8 (
      source.physical.file, source.physical.point_size,
      source.physical.horizontal_dpi, source.physical.vertical_dpi,
      text, 0, text.size (), smallcaps);
    const auto plain= shape_freetype_utf8 (
      source.physical.file, source.physical.point_size,
      source.physical.horizontal_dpi, source.physical.vertical_dpi,
      text, 0, text.size ());
    QVERIFY (!shaped.missing_glyphs);
    QVERIFY (shaped.glyphs.size () == plain.glyphs.size ());
    bool substituted= false;
    for (std::size_t i=0; i<shaped.glyphs.size (); ++i)
      if (shaped.glyphs[i].index != plain.glyphs[i].index)
        substituted= true;
    QVERIFY (substituted);

    box rendered= typeset_as_concat (env, tree ("By Somebody"), path (0));
    QVERIFY (rendered->w () > 0);
    QVERIFY (is_utf8_line_box (rendered[0]));
  }

  void nativeMathPackageDots () {
    drd_info drd ("utf8-math-package-dots", std_drd);
    hashmap<string,tree> h1 (UNINIT), h2 (UNINIT), h3 (UNINIT);
    hashmap<string,tree> h4 (UNINIT), h5 (UNINIT), h6 (UNINIT);
    edit_env env (drd, url_none (), h1, h2, h3, h4, h5, h6);
    env->write_default_env ();
    env->write (FONT, "TeX Gyre Pagella");
    env->write (MODE, "math");
    env->update ();
    env->exec (tree (USE_PACKAGE,
      string (std::getenv ("ATHENA_PATH")) * "/packages/standard/std-math.ts"));

    box b= typeset_as_concat (env, compound ("center-dots"), path (0));
    QImage image (500, 140, QImage::Format_ARGB32);
    image.fill (Qt::white);
    QPainter painter (&image);
    InlineRenderProbe probe (&painter);
    rectangles painted;
    b->redraw (&probe, path (), painted);
    std::string text;
    for (const auto& draw: probe.draws) text+= draw.text;
    QCOMPARE (text, std::string ("\xe2\x8b\x85\xe2\x8b\x85\xe2\x8b\x85"));
    QVERIFY (text.find ("<cdot>") == std::string::npos);

    for (const char* macro: {"high-dots", "tiny-box", "explicit-space"})
      QVERIFY (typeset_as_concat (env, compound (macro), path (0))->w () >= 0);
  }

  void nativeMathPrimeGlyphs () {
    drd_info drd ("utf8-math-primes", std_drd);
    hashmap<string,tree> h1 (UNINIT), h2 (UNINIT), h3 (UNINIT);
    hashmap<string,tree> h4 (UNINIT), h5 (UNINIT), h6 (UNINIT);
    edit_env env (drd, url_none (), h1, h2, h3, h4, h5, h6);
    env->write_default_env ();
    env->write (FONT, "TeX Gyre Pagella");
    env->write (MODE, "math");
    env->update ();

    tree value (CONCAT, tree (LPRIME, "`"), "x", tree (RPRIME, "''"));
    box b= typeset_as_concat (env, value, path (0));
    QImage image (500, 140, QImage::Format_ARGB32);
    image.fill (Qt::white);
    QPainter painter (&image);
    InlineRenderProbe probe (&painter);
    rectangles painted;
    b->redraw (&probe, path (), painted);
    std::string text;
    for (const auto& draw: probe.draws) text+= draw.text;
    QVERIFY (text.find ("‵") != std::string::npos);
    QVERIFY (text.find ("′′") != std::string::npos);
    QVERIFY (text.find ("ʹ") == std::string::npos);
    QVERIFY (text.find ("<prime>") == std::string::npos);
    QVERIFY (text.find ("<backprime>") == std::string::npos);

    box prime_expr= typeset_as_concat (
      env, tree (CONCAT, "e", tree (RPRIME, "'")), path (9));
    box superscript_expr= typeset_as_concat (
      env, tree (CONCAT, "e", tree (RSUP, "2")), path (10));
    // Equal baselines alone miss a text-style prime lifted as a superscript.
    // Pagella's ssty alternate must also keep the actual prime ink lower.
    QVERIFY (prime_expr->y2 < superscript_expr->y2);
    auto painted_baseline= [] (box expr, const std::string& glyph) {
      QImage image (300, 140, QImage::Format_ARGB32);
      image.fill (Qt::white);
      QPainter painter (&image);
      InlineRenderProbe probe (&painter);
      rectangles painted;
      expr->redraw (&probe, path (), painted);
      for (const auto& draw: probe.draws)
        if (draw.text == glyph) return draw.y;
      return MAX_SI;
    };
    SI prime_y= painted_baseline (prime_expr, "′");
    SI superscript_y= painted_baseline (superscript_expr, "2");
    QVERIFY (prime_y != MAX_SI);
    QVERIFY (superscript_y != MAX_SI);
    SI baseline_delta= prime_y > superscript_y ?
      prime_y - superscript_y : superscript_y - prime_y;
    QVERIFY (baseline_delta <= env->fn->yx / 2);
  }

  void smallLabelSelectionGeometry () {
    drd_info drd ("small-label-selection", std_drd);
    hashmap<string,tree> h1 (UNINIT), h2 (UNINIT), h3 (UNINIT);
    hashmap<string,tree> h4 (UNINIT), h5 (UNINIT), h6 (UNINIT);
    edit_env env (drd, url_none (), h1, h2, h3, h4, h5, h6);
    env->write_default_env ();
    env->write (FONT, "TeX Gyre Pagella");
    env->update ();

    const string previous= get_preference ("vault labels mode", "visible");
    set_preference ("vault labels mode", "small");
    box b= typeset_as_concat (env, tree (LABEL, "H2 §9.3 Supplementary Contents"),
                              path (0));
    set_preference ("vault labels mode", previous);

    selection sel= b->find_check_selection (path (0, 0), path (0, 1));
    QVERIFY (sel->valid);
    QVERIFY (!is_nil (sel->rs));
    rectangle bounds= least_upper_bound (sel->rs);
    QVERIFY (bounds->x2 - bounds->x1 > b->w () / 2);

    QImage image (600, 120, QImage::Format_ARGB32);
    image.fill (Qt::white);
    QPainter painter (&image);
    InlineRenderProbe probe (&painter);
    rectangles painted;
    b->redraw (&probe, path (), painted);
    QVERIFY (!probe.anchors.empty ());
  }

  void structuralNavigationSelections () {
    drd_info drd ("utf8-structural-navigation", std_drd);
    {
      with_drd use (drd);
      tree doc (DOCUMENT, tree (LABEL, "anchor"), "tail");
      path start_in (0, 0), end_in (0, 1), start_out, end_out;
      ::selection_correct (doc, start_in, end_in, start_out, end_out);
      QVERIFY (start_out == start_in);
      QVERIFY (end_out == end_in);
      QCOMPARE (selection_compute (doc, start_out, end_out),
                tree (LABEL, "anchor"));
    }

    hashmap<string,tree> h1 (UNINIT), h2 (UNINIT), h3 (UNINIT);
    hashmap<string,tree> h4 (UNINIT), h5 (UNINIT), h6 (UNINIT);
    edit_env env (drd, url_none (), h1, h2, h3, h4, h5, h6);
    env->write_default_env ();
    env->update ();
    env->exec (tree (USE_PACKAGE,
      string (std::getenv ("ATHENA_PATH")) * "/packages/section/section-base.ts"));
    env->update ();
    QVERIFY (drd->is_accessible_child (
      compound ("heading-fold-title", "Heading"), 0));
    tree heading= compound ("section", "Heading");
    QVERIFY (drd->is_accessible_child (heading, 0));
    {
      with_drd use (drd);
      path entered= next_accessible (heading, path (0));
      QVERIFY (entered != path (1));
      QVERIFY (!is_atom (entered) && entered->item == 0);
    }

    const tree_label test_heading= make_tree_label ("test-heading-navigation");
    const tree_label test_anchor= make_tree_label ("test-anchor-navigation");
    tree navigation (DOCUMENT,
      tree (test_heading, "Heading"), tree (test_anchor, "anchor"));
    set_document (buffer->document, buffer->root_path, navigation);
    editor->set_test_child_accessibility (test_heading, 0, true);
    editor->set_test_child_accessibility (test_anchor, 0, false);
    const path heading_path= buffer->root_path * 0;
    const path anchor_path= buffer->root_path * 1;
    QVERIFY (editor->border_jump_skips_accessible_child (
      heading_path * 0, heading_path * 1, true));
    QVERIFY (editor->border_jump_skips_accessible_child (
      heading_path * 1, heading_path * 0, false));
    QVERIFY (!editor->border_jump_skips_accessible_child (
      anchor_path * 0, anchor_path * 1, true));
  }

  void nativeUtf8MetadataAndLegacyEncodingPreference () {
    tree doc (DOCUMENT,
      compound ("doc-data",
        compound ("doc-title", "Functional Analysis 中"),
        compound ("author-name", "Fran\xc3\xa7ois \xce\xb1")));
    QCOMPARE (search_metadata (doc, "title"),
              string ("Functional Analysis \xe4\xb8\xad"));
    QCOMPARE (search_metadata (doc, "author"),
              string ("Fran\xc3\xa7ois \xce\xb1"));

    eval ("(module-provide '(convert rewrite init-rewrite))");
    QCOMPARE (as_string (eval (
      "(texmacs->code (tm->tree \"caf\303\251 \344\270\255\") \"cork\")")),
      string ("caf\xc3\xa9 \xe4\xb8\xad"));
    QVERIFY (as_bool (eval (
      "(begin (verbatim->texmacs \"caf\303\251\" "
      " (acons \"verbatim->texmacs:encoding\" \"cork\" '())) #t)")));
  }

  void unicodeMathLanguage () {
    const string alpha= "\316\261";
    const tree backassign (NAMED_SYMBOL, "texmacs:backassign");
    QCOMPARE (math_symbol_group (alpha), "Letter-symbol");
    QCOMPARE (math_symbol_group (backassign), "Assign-symbol");
    QCOMPARE (math_symbol_type (backassign), "infix");
    QCOMPARE (math_symbol_type ("\342\211\244"), "infix");
    QCOMPARE (math_symbol_group ("<leq>"), "symbol");
    bool found= false;
    const array<tree> members= math_group_members ("Assign-symbol");
    for (int i= 0; i < N(members); ++i)
      if (members[i] == backassign) found= true;
    QVERIFY (found);
    QCOMPARE (symbol_type (backassign), SYMBOL_INFIX);
    QCOMPARE (symbol_priority (backassign), symbol_priority (tree ("\342\211\224")));

    auto gr= find_packrat_grammar ("std-math");
    const auto rule= [] (const char* name) {
      return encode_symbol (compound ("symbol", name));
    };
    for (tree expression: {tree (alpha),
         tree (CONCAT, alpha, backassign, "1"),
         tree (CONCAT, alpha, "\342\211\244", "1"),
         tree (FRAC, alpha, "\360\235\224\270")}) {
      packrat_parser parser (gr, expression);
      QVERIFY (parser->parse (rule ("Main"), 0) == N(parser->current_input));
    }
    packrat_parser literal (gr, tree ("<alpha>"));
    QCOMPARE (literal->parse (rule ("Letter-symbol"), 0), PACKRAT_FAILED);

    const string tokens= "sin" * alpha * "e\314\201<alpha>12.3..";
    const int boundaries[]= {3, 5, 8, 9, 14, 15, 19, 20, 21};
    int pos= 0;
    for (int end: boundaries) {
      pos= math_word_end (tokens, pos);
      QCOMPARE (pos, end);
    }
    QCOMPARE (pos, N(tokens));
    QVERIFY (as_bool (eval ("(string=? (math-symbol-group '(named-symbol \"texmacs:backassign\")) \"Assign-symbol\")")));
    QVERIFY (as_bool (eval ("(string=? (math-symbol-type '(named-symbol \"texmacs:backassign\")) \"infix\")")));
    QVERIFY (as_bool (eval (
      "(let loop ((members (math-group-members \"Assign-symbol\"))) "
      "(and (pair? members) "
      "(or (equal? (tree->stree (car members)) '(named-symbol \"texmacs:backassign\")) "
      "(loop (cdr members)))))")));
  }
  void unicodePackrat () {
    packrat_grammar_rep grammar ("utf8-packrat-test");
    packrat_grammar gr (&grammar);
    const string text= "A\xe4\xb8\xad" "e\xcc\x81\xf0\x90\x90\x80" "<alpha>";
    grammar.define ("Text", text);
    grammar.define ("Node", compound ("concat",
      compound ("tm-node-open", "utf8-packrat-node"),
      "\xe4\xb8\xad", compound ("tm-node-separator"),
      "\xf0\x90\x90\x80", compound ("tm-node-close")));
    grammar.define ("Range", compound ("range", "\xf0\x90\x90\x80",
                                                     "\xf0\x90\x90\x82"));
    grammar.define ("Char", compound ("tm-char"));
    const tree symbol (NAMED_SYMBOL, "legacy:custom-symbol");
    grammar.define ("Symbol", symbol);
    const auto rule= [] (const char* name) {
      return encode_symbol (compound ("symbol", name));
    };

    packrat_parser plain (gr, tree (text));
    QCOMPARE (N(plain->current_input), 12);
    QCOMPARE (plain->parse (rule ("Text"), 0), 12);
    const int bytes[]= {0, 1, 4, 5, 7, 11, 12, 13, 14, 15, 16, 17, 18};
    for (int i= 0; i < 13; ++i) {
      QCOMPARE (plain->encode_tree_position (path (bytes[i])), i);
      QVERIFY (plain->decode_tree_position (i) == path (bytes[i]));
    }
    for (int i: {2, 3, 6, 8, 9, 10, 19})
      QCOMPARE (plain->encode_tree_position (path (i)), PACKRAT_FAILED);
    QCOMPARE (plain->decode_string_position (13), -1);
    QCOMPARE (plain->parse (rule ("Text"), -1), PACKRAT_FAILED);

    tree node= compound ("utf8-packrat-node", "\xe4\xb8\xad",
                                             "\xf0\x90\x90\x80");
    packrat_parser structured (gr, node);
    QCOMPARE (structured->parse (rule ("Node"), 0), 5);
    QCOMPARE (structured->parse (PACKRAT_TM_ANY, 0), 5);
    QCOMPARE (structured->encode_tree_position (path (0, 3)), 2);
    QCOMPARE (structured->encode_tree_position (path (1, 4)), 4);
    QVERIFY (structured->decode_tree_position (1) == path (0, 0));
    QVERIFY (structured->decode_tree_position (4) == path (1, 4));
    QCOMPARE (structured->encode_tree_position (path (2, 0)), PACKRAT_FAILED);

    // The same diagnostic bytes must never let text impersonate a tree.
    packrat_parser lookalike (gr, tree (structured->current_string));
    QCOMPARE (lookalike->parse (rule ("Node"), 0), PACKRAT_FAILED);
    QCOMPARE (lookalike->parse (PACKRAT_TM_OPEN, 0), PACKRAT_FAILED);
    QCOMPARE (lookalike->parse (PACKRAT_TM_LEAF, 0), N(lookalike->current_input));

    packrat_parser named (gr, symbol);
    QCOMPARE (named->parse (rule ("Symbol"), 0), 1);
    QCOMPARE (named->parse (rule ("Char"), 0), 1);
    QVERIFY (named->decode_tree_position (0) == path (0));
    QVERIFY (named->decode_tree_position (1) == path (1));
    QCOMPARE (named->encode_tree_position (path (0, 0)), PACKRAT_FAILED);
    packrat_parser name_text (gr, tree ("legacy:custom-symbol"));
    QCOMPARE (name_text->parse (rule ("Symbol"), 0), PACKRAT_FAILED);
    packrat_parser other_name (gr, tree (NAMED_SYMBOL, "legacy:other-symbol"));
    QCOMPARE (other_name->parse (rule ("Symbol"), 0), PACKRAT_FAILED);

    packrat_parser ranged (gr, tree ("\xf0\x90\x90\x81"));
    QCOMPARE (ranged->parse (rule ("Range"), 0), 1);
    QCOMPARE (grammar.decode_as_string (0x10401), "\xf0\x90\x90\x81");
    packrat_parser binary (gr, tree (RAW_DATA, string ("\xff\x80\0", 3)));
    QCOMPARE (N(binary->current_input), 2);
    QCOMPARE (binary->parse (PACKRAT_TM_ANY, 0), 2);
    packrat_parser nul (gr, tree (string ("\0", 1)));
    QCOMPARE (N(nul->current_input), 1);
    QCOMPARE (nul->current_input[0], 0);
    QVERIFY_EXCEPTION_THROWN (packrat_parser (gr, tree (string ("\xff", 1))),
                              std::invalid_argument);

    packrat_parser builtin (find_packrat_grammar ("std-math"),
                            tree (FRAC, "1", "2"));
    QVERIFY (builtin->parse (rule ("Main"), 0) == N(builtin->current_input));
    grammar.define ("Node", compound ("tm-any"));
    packrat_parser nested (gr, compound ("utf8-packrat-node", node, symbol));
    QCOMPARE (nested->parse (rule ("Node"), 0), N(nested->current_input));
  }
  void unicodeSearchAndReplace () {
    const string source= u8"Stra\u00dfe STRASSE <alpha> e\u0301";
    const path atom= buffer->root_path * 0;
    editor->go_to (atom * 0);
    editor->start_editing ();
    editor->insert_tree (source);
    editor->end_editing ();
    QCOMPARE (editor->document_search ("strasse", true), 2);
    QCOMPARE (editor->selection_get_start (), atom * 0);
    QCOMPARE (editor->selection_get_end (), atom * 7);
    editor->archive_state ();
    QCOMPARE (editor->document_replace (u8"\u4e2d", true), 2);
    QVERIFY (subtree (current_document_tree (), atom) == tree (u8"\u4e2d \u4e2d <alpha> e\u0301"));
    editor->undo (0);
    QVERIFY (subtree (current_document_tree (), atom) == tree (source));
    QCOMPARE (editor->document_search ("alpha", false), 1);
    QCOMPARE (editor->selection_get_start (), atom * 17);
    QCOMPARE (editor->selection_get_end (), atom * 22);
    QCOMPARE (editor->document_search (u8"\u0301", false), 0);
    QCOMPARE (editor->document_search (u8"e\u0301", false), 1);

    tree pattern (CONCAT, "STRASSE", compound ("wildcard", "rest"), "STRASSE");
    range_set hits= search (tree (u8"Stra\u00dfe + Stra\u00dfe"), pattern, path (0), true);
    QCOMPARE (N(hits), 2);
    QCOMPARE (hits[0], path (0, 0));
    QCOMPARE (hits[1], path (0, 17));
  }
  void unicodeSpellingRange () {
    const string source= u8"\u00e9cole e\u0301qq";
    const path atom= buffer->root_path * 0;
    editor->go_to (atom * 0);
    editor->start_editing ();
    editor->insert_tree (source);
    editor->end_editing ();
    QVERIFY (editor->spellable_range (atom * 0, "french") == atom * 6);
    QVERIFY (editor->spellable_range (atom * 1, "french") == atom * 1);
    QVERIFY (editor->spellable_range (atom * 7, "french") == atom * N(source));
    QVERIFY (editor->spellable_range (atom * 8, "french") == atom * 8);
  }
  void clipboardAndUndo () {
    const string source= u8"\u00e9\u4e2d e\u0301 <alpha> \U0001f600";
    const string key= "utf8-clipboard-test";
    const path atom= buffer->root_path * 0;
    QVERIFY (::set_selection (key, tuple ("extern-utf8", source),
                               "", "", "", "default"));
    editor->go_to (atom * 0);
    editor->archive_state ();
    editor->start_editing ();
    editor->selection_paste (key);
    editor->end_editing ();
    QVERIFY (subtree (current_document_tree (), atom) == tree (source));
    QVERIFY (editor->the_path () == atom * N(source));
    editor->undo (0);
    QVERIFY (subtree (current_document_tree (), atom) == "");
    editor->redo (0);
    QVERIFY (subtree (current_document_tree (), atom) == tree (source));

    editor->selection_set ("primary", tree (source));
    tree selection;
    string bytes;
    QVERIFY (::get_selection ("primary", selection, bytes, "default"));
    QVERIFY (athena::document::read_clipboard_xml (
      {bytes.data (), std::size_t (N(bytes))}) == selection);
    QVERIFY (selection[1] == tree (source));
    ::clear_selection (key);
    ::clear_selection ("primary");
  }
  void schemeTextBoundaries () {
    QVERIFY (as_bool (eval (
      "(let* ((s (string (integer->char #xe9) #\\e (integer->char #x301))) "
      "       (t (string->tree s))) "
      "  (and (= (string-length s) 3) (= (tm-length s) 5) "
      "       (= (tm-length t) 5) "
      "       (equal? (tm-range t 2 5) (substring s 1 3))))")));
    QVERIFY (as_bool (eval (
      "(let* ((b (decode-base64 \"AP8=\")) "
      "       (s (list 'raw-data b)) (t (stree->tree s))) "
      "  (and (tm-equal? t s) (equal? (tree->list t) s) "
      "       (equal? (tree->stree (tm->tree (tree->list t))) s)))")));
  }
  void programTypesetting () {
    drd_info drd ("utf8-editor-program", std_drd);
    hashmap<string,tree> h1 (UNINIT), h2 (UNINIT), h3 (UNINIT);
    hashmap<string,tree> h4 (UNINIT), h5 (UNINIT), h6 (UNINIT);
    edit_env env (drd, url_none (), h1, h2, h3, h4, h5, h6);
    env->write_default_env ();
    env->write (MODE, "prog");
    env->write (PROG_FONT, "JetBrains Mono");
    env->write (PROG_LANGUAGE, "cpp");
    env->update ();
    const string source= "int x = 1; // \xe4\xb8\xad "
      "e\xcc\x81 \xf0\x9f\x98\x80 <alpha>";
    tree program (source);
    env->lan->highlight (program);
    auto items= typeset_concat (env, program, path (0));
    QVERIFY (N(items) > 1);
    for (int i=0; i<N(items); ++i) {
      QVERIFY (is_utf8_line_box (items[i]->b));
      QVERIFY (items[i]->type != STRING_ITEM);
      QVERIFY (utf8_grapheme_boundary (source, items[i]->b->get_leaf_left_pos ()));
      QVERIFY (utf8_grapheme_boundary (source, items[i]->b->get_leaf_right_pos ()));
    }
    box result= typeset_as_concat (env, program, path (0));
    QCOMPARE (N(result), 1);
    QCOMPARE (result[0]->get_leaf_string (), source);
    bool found= false;
    auto caret= result->find_box_path (path (0, N(source)), found);
    QVERIFY (found);
    QVERIFY (result->find_tree_path (caret) == path (0, N(source)));
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

    // Paint changes share paragraph direction without losing their colors.
    joined= typeset_as_concat (env, tree (CONCAT, "x",
      tree (WITH, COLOR, "red", "y"), "z"), path (0));
    QCOMPARE (N(joined), 1);
    const auto color_path= joined->find_box_path (path (0, path (1, path (2, 1))), found);
    QVERIFY (found);
    QVERIFY (joined->find_tree_path (color_path) == path (0, path (1, path (2, 1))));

    const string left= "\xd7\x90", right= "\xd7\x91";
    joined= typeset_as_concat (env, tree (CONCAT, left,
      tree (WITH, COLOR, "red", "text-background-color", "blue", right)), path (0));
    QCOMPARE (N(joined), 1);
    QImage image (400, 120, QImage::Format_ARGB32_Premultiplied);
    image.fill (Qt::white);
    QPainter painter (&image);
    InlineRenderProbe probe (&painter);
    joined[0]->display (&probe);
    QCOMPARE (probe.draws.size (), std::size_t(2));
    QCOMPARE (probe.draws[0].text, std::string (right.data (), N(right)));
    QCOMPARE (probe.draws[0].pen, named_color ("red"));
    QCOMPARE (probe.draws[1].text, std::string (left.data (), N(left)));
    QCOMPARE (probe.draws[1].pen, env->pen->get_color ());
    QVERIFY (probe.draws[0].x < probe.draws[1].x);
    painter.end ();
    int red_pixels= 0, blue_pixels= 0;
    for (int y=0; y<image.height (); ++y) for (int x=0; x<image.width (); ++x) {
      const auto pixel= image.pixel (x, y);
      if (qRed (pixel) > 100 && qGreen (pixel) < 80 && qBlue (pixel) < 80) ++red_pixels;
      if (qBlue (pixel) > 100 && qRed (pixel) < 80 && qGreen (pixel) < 80) ++blue_pixels;
    }
    QVERIFY (red_pixels > 0 && blue_pixels > 0);
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
  void wrappedSourceMapping () {
    drd_info drd ("utf8-wrapped-source", std_drd);
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
    const string first= word * " " * word * " ";
    const string middle= word * " " * word * " " * word * " " * word;
    const string last= " " * word * " " * word;
    const tree source (CONCAT, first, tree (WITH, FONT_SERIES, "bold", middle), last);
    auto atoms= typeset_concat (env, tree (word), path (0));
    const SI width= 3 * atoms[0]->b->w () + 3 * env->fn->spc->def;
    stack_border border;
    auto pages= typeset_stack (env, source, path (0), width,
                               array<line_item> (), array<line_item> (), border);
    int lines= 0, mapped= 0;
    bool start_marker= false, end_marker= false;
    for (int i=0; i<N(pages); ++i) if (pages[i]->type == PAGE_LINE_ITEM) {
      auto row= pages[i]->b;
      QCOMPARE (N(row), 1);
      auto leaf= row[0];
      QVERIFY (is_utf8_line_box (leaf));
      const auto start= leaf->find_left_box_path (), end= leaf->find_right_box_path ();
      QVERIFY (leaf->find_cursor (start)->ox > leaf->find_cursor (end)->ox);
      if (lines == 0) QCOMPARE (row->w (), width);
      for (int marker=0; marker<2; ++marker) {
        const path source_marker (0, path (1, marker));
        bool found= false;
        const auto bp= leaf->find_box_path (source_marker, found);
        if (found) {
          QVERIFY (leaf->find_tree_path (bp) == source_marker);
          (marker == 0 ? start_marker : end_marker)= true;
        }
      }
      // Source offsets on later lines are relative to their own atomic node,
      // not relative to either the concatenated paragraph or the selected line.
      for (int node=0; node<3; ++node) {
        const string text= node == 0 ? first : node == 1 ? middle : last;
        const path parent= node == 1 ? path (0, path (1, 2)) : path (0, node);
        for (int at=0; at<=N(text); ++at) if (utf8_grapheme_boundary (text, at)) {
          bool found= false;
          const auto bp= leaf->find_box_path (parent * at, found);
          if (found) {
            QVERIFY (leaf->find_tree_path (bp) == parent * at);
            auto expanded= leaf->expand_glyphs (0, 0.05);
            const auto ep= expanded->find_box_path (parent * at, found);
            QVERIFY (found);
            QVERIFY (expanded->find_tree_path (ep) == parent * at);
            ++mapped;
          }
        }
      }
      ++lines;
    }
    QVERIFY (lines >= 2);
    QVERIFY (mapped >= 20);
    QVERIFY (start_marker && end_marker);

    // A later line containing only numbers/punctuation still uses the RTL
    // base direction established by a different source node on the first line.
    const string prefix= word * " ", neutral= "123 456.";
    auto numeric= typeset_concat (env, tree (CONCAT, prefix, neutral), path (0));
    array<box> numeric_pieces;
    array<SI> numeric_spaces;
    for (int i=0; i<N(numeric); ++i) if (numeric[i]->b->ip == path (1, 0)) {
      numeric_pieces << numeric[i]->b;
      numeric_spaces << (N(numeric_pieces) == 1 ? SI(0) : numeric[i-1]->spc->def);
    }
    reassemble_utf8_line (numeric_pieces, numeric_spaces);
    QCOMPARE (N(numeric_pieces), 1);
    athena::text::physical_font_source physical;
    QVERIFY (env->fn->physical_source (physical));
    const string combined= prefix * neutral;
    athena::text::font_paragraph reference (std::string (combined.data (), N(combined)),
      athena::text::font_request_from_source (physical));
    auto expected= reference.line (N(prefix), N(combined));
    expected.set_space_widths (reference.analysis ().source (),
      {{std::size_t(N(prefix)+3), std::size_t(N(prefix)+4), env->fn->spc->def}});
    for (int at=0; at<=N(neutral); ++at) {
      bool found= false;
      auto bp= numeric_pieces[0]->find_box_path (path (0, 1, at), found);
      QVERIFY (found);
      bp= numeric_pieces[0]->with_cursor_affinity (bp, athena::text::caret_affinity::downstream);
      QCOMPARE (numeric_pieces[0]->find_cursor (bp)->ox,
        expected.caret_x (N(prefix)+at, athena::text::caret_affinity::downstream));
    }

    // A source-node boundary inside a grapheme is not an emergency break.
    auto cluster= typeset_concat (env, tree (CONCAT, "e", "\xcc\x81"), path (0));
    QCOMPARE (N(cluster), 2);
    QCOMPARE (cluster[0]->penalty, HYPH_INVALID);
    array<box> pieces;
    array<SI> spaces;
    for (int i=0; i<N(cluster); ++i) { pieces << cluster[i]->b; spaces << SI(0); }
    reassemble_utf8_line (pieces, spaces);
    QCOMPARE (N(pieces), 1);
    bool found= false;
    pieces[0]->find_box_path (path (0, 0, 1), found);
    QVERIFY (!found);
    const auto end= pieces[0]->find_box_path (path (0, 1, 2), found);
    QVERIFY (found);
    QVERIFY (pieces[0]->find_tree_path (end) == path (0, 1, 2));
  }
  void sourceBoundaryBreaks () {
    drd_info drd ("utf8-source-breaks", std_drd);
    hashmap<string,tree> h1 (UNINIT), h2 (UNINIT), h3 (UNINIT);
    hashmap<string,tree> h4 (UNINIT), h5 (UNINIT), h6 (UNINIT);
    edit_env env (drd, url_none (), h1, h2, h3, h4, h5, h6);
    env->write_default_env ();
    env->write (FONT, "TeX Gyre Pagella");
    env->write (PAR_MODE, "left");
    env->write (PAR_FIRST, "0cm");
    env->write (PAR_LEFT, "0cm");
    env->write (PAR_RIGHT, "0cm");
    env->write ("athena-radioactive-links-suppressed", "true");
    env->update ();
    const string a= "\xe4\xb8\xad", b= "\xe6\x96\x87";
    tree text (CONCAT, a, b, a, b);
    auto items= typeset_concat (env, text, path (0));
    QCOMPARE (N(items), 4);
    for (int i=0; i<3; ++i) QCOMPARE (items[i]->penalty, 0);
    const SI width= items[0]->b->w () + items[1]->b->w ();
    stack_border border;
    auto pages= typeset_stack (env, text, path (0), width,
                               array<line_item> (), array<line_item> (), border);
    int lines= 0;
    for (int i=0; i<N(pages); ++i) if (pages[i]->type == PAGE_LINE_ITEM) {
      QVERIFY (pages[i]->b->w () <= width);
      ++lines;
    }
    QCOMPARE (lines, 2);

    items= typeset_concat (env, tree (CONCAT, "of",
      tree (WITH, FONT_SERIES, "bold", "fice")), path (0));
    for (int i=0; i<N(items)-1; ++i) QCOMPARE (items[i]->penalty, HYPH_INVALID);

    // A word joiner at the next source node also constrains the break
    // which the previous atom would otherwise allow after its ASCII space.
    items= typeset_concat (env, tree (CONCAT, "word ", "\xe2\x81\xa0" "word"), path (0));
    const std::string joined= "word \xe2\x81\xa0word";
    athena::text::unicode_paragraph paragraph (joined);
    bool break_at_five= false;
    for (const auto& point: paragraph.breaks ()) if (point.byte == 5) break_at_five= true;
    QVERIFY (!break_at_five);
    QCOMPARE (items[0]->penalty == 0, break_at_five);
  }
  void linkedText () {
    drd_info drd ("utf8-linked-text", std_drd);
    hashmap<string,tree> h1 (UNINIT), h2 (UNINIT), h3 (UNINIT);
    hashmap<string,tree> h4 (UNINIT), h5 (UNINIT), h6 (UNINIT);
    edit_env env (drd, url_none (), h1, h2, h3, h4, h5, h6);
    env->write_default_env ();
    env->write (FONT, "TeX Gyre Pagella");
    env->write ("athena-radioactive-links-suppressed", "true");
    env->update ();
    {
      auto prefix= typeset_marker (env, decorate_right (path (0)));
      auto first= typeset_concat (env, tree ("ab"), path (0, 0));
      auto second= typeset_concat (env, tree ("cd"), path (1, 0));
      auto suffix= typeset_marker (env, decorate_right (path (0)));
      QCOMPARE (N(prefix), 1);
      QCOMPARE (N(first), 1);
      QCOMPARE (N(second), 1);
      QCOMPARE (N(suffix), 1);
      array<line_item> flow_items;
      flow_items << prefix << first << second << suffix;
      prepare_utf8_paragraph (path (0), flow_items);
      array<box> flow_pieces;
      array<SI> flow_spaces;
      for (int i=0; i<N(flow_items); ++i) {
        flow_pieces << flow_items[i]->b;
        flow_spaces << (i == 0 ? SI(0) : flow_items[i-1]->spc->def);
      }
      reassemble_utf8_line (flow_pieces, flow_spaces);
      QCOMPARE (N(flow_pieces), 1);
      box mapped= flow_pieces[0];
      QCOMPARE (reverse (mapped->find_lip ()), path (0, 0, 0));
      QCOMPARE (reverse (mapped->find_rip ()), path (0, 1, 2));
      box phrase= phrase_box (decorate_right (path (0)), flow_pieces, flow_spaces);
      bool found= false;
      const path start= phrase->find_box_path (path (0, 0, 0), found);
      QVERIFY (found);
      QVERIFY (phrase->find_tree_path (start) == path (0, 0, 0));
      const path end= phrase->find_box_path (path (0, 1, 2), found);
      QVERIFY (found);
      QVERIFY (phrase->find_tree_path (end) == path (0, 1, 2));
    }
    {
      auto text= typeset_concat (env, tree (CONCAT, "left", "right"), path (0));
      box marker= locus_box (path (0), empty_box (path (0), 0, 0, 0, 0),
        list<string> ("marker"), 0, "", "#destination", false);
      array<box> pieces;
      pieces << text[0]->b << marker << text[N(text)-1]->b;
      array<bool> markers;
      markers << false << true << false;
      QVERIFY (is_nil (join_utf8_line_boxes (path (0), pieces, markers)));
    }
    const string word= "\xd7\x90\xd7\x91";
    const string source= word * " " * word * " 12";
    auto items= typeset_concat (env, tree (source), path (0));
    QCOMPARE (N(items), 3);
    const string target= "https://example.invalid/target";
    items[1]->b= direct_link_box (items[1]->b->ip, items[1]->b, target);
    box opaque= locus_box (items[2]->b->ip, items[2]->b, list<string> ("id"),
      PIXEL, "#destination", "#label", false);
    QVERIFY (!is_utf8_inline_box (opaque));
    items[2]->b= locus_box (items[2]->b->ip, items[2]->b, list<string> ("id"),
      PIXEL, "#destination", "#label", true);
    QVERIFY (is_utf8_inline_box (items[1]->b));
    QVERIFY (is_utf8_inline_box (items[2]->b));
    prepare_utf8_paragraph (path (0), items);
    array<box> pieces;
    array<SI> spaces;
    for (int i=0; i<N(items); ++i) {
      pieces << items[i]->b;
      spaces << (i == 0 ? SI(0) : items[i-1]->spc->def);
    }
    reassemble_utf8_line (pieces, spaces);
    QCOMPARE (N(pieces), 1);
    auto leaf= pieces[0];
    bool found= false;
    const auto first= leaf->find_box_path (path (0, 5), found);
    QVERIFY (found);
    const auto last= leaf->find_box_path (path (0, 9), found);
    QVERIFY (found);
    auto selected= leaf->find_selection (first, last);
    QVERIFY (!is_nil (selected->rs));
    auto region= selected->rs->item;
    const SI x= region->x1 + (region->x2-region->x1)/2, y= (leaf->y1+leaf->y2)/2;
    rectangles rs;
    QVERIFY (leaf->message ("link-target", x, y, rs) == tree (TUPLE, "link-target", target));
    QVERIFY (leaf->message ("select", x, y, rs) == tree (TUPLE, "direct-link", target));
    QVERIFY (leaf->message ("link-target", leaf->x2+PIXEL, y, rs) == tree (""));
    auto expanded= leaf->expand_glyphs (0, 0.1);
    const auto a= expanded->find_box_path (path (0, 5), found);
    QVERIFY (found);
    const auto b= expanded->find_box_path (path (0, 9), found);
    QVERIFY (found);
    auto changed= expanded->find_selection (a, b)->rs->item;
    QVERIFY (expanded->message ("select", changed->x1+(changed->x2-changed->x1)/2,
      y, rs) == tree (TUPLE, "direct-link", target));

    const auto label_start= leaf->find_box_path (path (0, 10), found);
    QVERIFY (found);
    const auto label_end= leaf->find_box_path (path (0, 12), found);
    QVERIFY (found);
    auto label_region= leaf->find_selection (label_start, label_end)->rs->item;
    list<string> ids;
    leaf->loci (label_region->x1+(label_region->x2-label_region->x1)/2, y, 0, ids, rs);
    QVERIFY (ids == list<string> ("id"));
    QVERIFY (!is_nil (rs));
    hashmap<string,tree> numbers (UNINIT);
    leaf->collect_page_numbers (numbers, tree ("7"));
    QVERIFY (numbers["label"] == tree ("7"));

    QImage image (400, 120, QImage::Format_ARGB32_Premultiplied);
    image.fill (Qt::white);
    QPainter painter (&image);
    InlineRenderProbe probe (&painter);
    renderer ren= &probe;
    leaf->display (ren);
    leaf->post_display (ren);
    QCOMPARE (probe.references.size (), std::size_t(2));
    QCOMPARE (probe.anchors.size (), std::size_t(1));
    QCOMPARE (probe.references[0].target, target);
    QCOMPARE (probe.references[0].x1, region->x1);
    QCOMPARE (probe.references[0].x2, region->x2);
    QCOMPARE (probe.references[1].target, string ("#destination"));
    QCOMPARE (probe.anchors[0].target, string ("#label"));
    painter.end ();
  }
  void tocNotification () {
    drd_info drd ("utf8-toc", std_drd);
    hashmap<string,tree> h1 (UNINIT), h2 (UNINIT), h3 (UNINIT);
    hashmap<string,tree> h4 (UNINIT), h5 (UNINIT), h6 (UNINIT);
    edit_env env (drd, url_none (), h1, h2, h3, h4, h5, h6);
    env->write_default_env ();
    env->update ();
    const string title= "caf\xc3\xa9 \xe4\xb8\xad \xf0\x9f\x91\xa9";
    auto items= typeset_concat (env, tree (TOC_NOTIFY, "toc-strong-2", title), path (0));
    bool found= false;
    for (int i= 0; i < N(items); ++i)
      if ((tree) items[i]->b == tuple ("toc", "toc-strong-2", title)) found= true;
    QVERIFY (found);
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
