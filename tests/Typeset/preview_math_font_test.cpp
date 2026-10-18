/******************************************************************************
* MODULE     : preview_math_font_test.cpp
* DESCRIPTION: Inline legacy math font scopes preserve the text font
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* See the file LICENSE in the root directory.
******************************************************************************/

#include <QApplication>
#include <QTemporaryDir>
#include <QDirIterator>
#include <QFile>
#include <QtTest/QtTest>
#include <functional>
#include "boot.hpp"
#include "converter.hpp"
#include "data_cache.hpp"
#include "drd_std.hpp"
#include "gui.hpp"
#include "ATHENA/tm_frame.hpp"
#include "Qt/qt_widget.hpp"
#include "scheme.hpp"
#include "server.hpp"
#include "typesetter.hpp"
#include "qt_renderer.hpp"
#include "Freetype/tt_file.hpp"
#include "named_symbol.hpp"
#include "math_font.hpp"
#include "Concat/concater.hpp"
#include "math_tree_v3.hpp"
#include "Data/Convert/Xml/math_v2_to_v3.hpp"
#include "Data/Convert/Xml/athena_document_xml.hpp"
#include "node_metadata.hpp"
#include "Boxes/construct.hpp"

bool headless_mode= true;
bool is_headless () { return true; }

static font
letter_font (box b) {
  if (b->get_type () == TEXT_BOX && b->get_leaf_string () == "E")
    return b->get_leaf_font ();
  for (int i=0; i<N(b); ++i) {
    font f= letter_font (b[i]);
    if (!is_nil (f)) return f;
  }
  return font ();
}

class PreviewMathFontTest: public QObject {
  Q_OBJECT
private slots:
  void migratedBigOperatorGlyphsAndScriptsRender () {
    drd_info drd ("migrated-big-operator-glyphs", std_drd);
    hashmap<string,tree> h1 (UNINIT), h2 (UNINIT), h3 (UNINIT);
    hashmap<string,tree> h4 (UNINIT), h5 (UNINIT), h6 (UNINIT);
    edit_env env (drd, url_none (), h1, h2, h3, h4, h5, h6);
    env->write_default_env ();
    env->write (FONT, "TeX Gyre Pagella");
    env->write (MODE, "math");
    const std::pair<const char*, const char*> cases[]= {
      {"cup", "⋃"}, {"cap", "⋂"}, {"wedge", "⋀"}, {"vee", "⋁"},
      {"oplus", "⨁"}, {"otimes", "⨂"}, {"sqcup", "⨆"},
      {"amalg", "∐"}, {"times", "×"}
    };
    for (const string display: {string ("false"), string ("true")}) {
      env->write (MATH_DISPLAY, display);
      env->update ();
      for (const auto& [name, glyph]: cases) {
        tree old (DOCUMENT, compound ("math", tree (CONCAT,
          tree (BIG, name), tree (RSUB, "n"), tree ("A"))));
        tree migrated= athena::document::migrate_math_v2_tree (
          old, true).document[0][0];
        tree expected (CONCAT,
          athena::math_v3::scripts (
            athena::math_v3::big_operator (glyph), "", "", "n", ""), "A");
        QCOMPARE (migrated, expected);
        box actual_box= typeset_as_concat (env, migrated, path (0));
        box expected_box= typeset_as_concat (env, expected, path (0));
        QVERIFY (actual_box->w () > 0);
        QVERIFY (actual_box->h () > 0);
        QCOMPARE (tree (actual_box), tree (expected_box));
        string description;
        tm_ostream out= string_ostream (description);
        out << tree (actual_box);
        out.flush ();
        QVERIFY (!occurs ("invalid rubber", description));
      }
    }
  }

  void copiedCorpusBigOperatorScriptsRender () {
    const QString corpus= qEnvironmentVariable ("ATHENA_V3_OPERATOR_CORPUS");
    if (corpus.isEmpty ()) QSKIP ("No private corpus supplied");
    drd_info drd ("corpus-big-operator-glyphs", std_drd);
    hashmap<string,tree> h1 (UNINIT), h2 (UNINIT), h3 (UNINIT);
    hashmap<string,tree> h4 (UNINIT), h5 (UNINIT), h6 (UNINIT);
    edit_env env (drd, url_none (), h1, h2, h3, h4, h5, h6);
    env->write_default_env ();
    env->write (FONT, "TeX Gyre Pagella");
    env->write (MODE, "math");
    env->update ();
    int operators= 0;
    std::function<void(tree)> visit= [&] (tree value) {
      if (is_atomic (value)) return;
      const bool scripted= is_func (value, MATH_SCRIPTS, 5) &&
        is_func (value[0], BIG_OPERATOR, 2);
      if (scripted || is_func (value, BIG_OPERATOR, 2)) {
        box result= typeset_as_concat (env, value, path (0));
        string description;
        tm_ostream out= string_ostream (description);
        out << tree (result);
        out.flush ();
        QVERIFY (!occurs ("invalid rubber", description));
        QVERIFY (result->w () > 0);
        QVERIFY (result->h () > 0);
        ++operators;
      }
      for (int i=0; i<N(value); ++i) visit (value[i]);
    };
    QDirIterator files (corpus, {"*.ath"}, QDir::Files,
                        QDirIterator::Subdirectories);
    while (files.hasNext ()) {
      QFile input (files.next ());
      QVERIFY (input.open (QIODevice::ReadOnly));
      visit (athena::document::read_xml_v3 (input.readAll ().toStdString ()));
    }
    QVERIFY (operators > 0);
    qInfo () << "Rendered corpus operators and scripted operators:" << operators;
  }

  void migratedHistoricalLayoutRetainsGeometry () {
    drd_info drd ("migrated-historical-layout", std_drd);
    hashmap<string,tree> h1 (UNINIT), h2 (UNINIT), h3 (UNINIT);
    hashmap<string,tree> h4 (UNINIT), h5 (UNINIT), h6 (UNINIT);
    edit_env env (drd, url_none (), h1, h2, h3, h4, h5, h6);
    env->write_default_env ();
    env->write (FONT, "TeX Gyre Pagella");
    env->write (MODE, "math");
    env->update ();
    auto converted= [] (const tree& expression) {
      return athena::document::migrate_math_v2_tree (
        tree (DOCUMENT, compound ("math", expression)), true).document[0][0];
    };
    box normal= typeset_as_concat (env, tree ("x"), path (0));
    box overlap= typeset_as_concat (env,
      converted (compound ("mathllap", "x")), path (0));
    QCOMPARE (overlap->w (), SI (0));
    QVERIFY (overlap->x3 < 0);
    QCOMPARE (overlap->x4 - overlap->x3, normal->x4 - normal->x3);
    box raised= typeset_as_concat (env,
      converted (compound ("raise", ".*4*p*t", "x")), path (0));
    QCOMPARE (raised->w (), normal->w ());
    QVERIFY (raised->y3 > normal->y3);
    QCOMPARE (raised->y4 - raised->y3, normal->y4 - normal->y3);
    box omega= typeset_as_concat (env, converted (compound ("ohm")), path (0));
    box expected= typeset_as_concat (env, tree ("Ω"), path (0));
    QVERIFY (tree (omega) == tree (expected));
    box integral= typeset_as_concat (env, converted (compound ("fint")), path (0));
    box expected_integral= typeset_as_concat (env, tree (BIG_OPERATOR, "⨍", "0"), path (0));
    QVERIFY (integral->w () > 0);
    QVERIFY (integral->h () > 0);
    QVERIFY (tree (integral) == tree (expected_integral));
  }

  void nativeV3MiddleStretchMatchesLegacyUnicodeMiddle () {
    drd_info drd ("native-v3-math-middle-stretch", std_drd);
    hashmap<string,tree> h1 (UNINIT), h2 (UNINIT), h3 (UNINIT);
    hashmap<string,tree> h4 (UNINIT), h5 (UNINIT), h6 (UNINIT);
    edit_env env (drd, url_none (), h1, h2, h3, h4, h5, h6);
    env->write_default_env ();
    env->write (FONT, "TeX Gyre Pagella");
    env->write (MODE, "math");
    env->update ();

    // A native middle separator must produce exactly the same final line
    // items as the legacy Unicode MID: the same spacing, glyph and stretching
    // geometry, without building a legacy MID adapter in the native path.
    for (const string glyph: {string ("|"), string ("‖")}) {
      auto legacy= typeset_concat (env, tree (MID, glyph), path (0));
      auto native= typeset_concat (env, tree (MATH_MIDDLE, glyph), path (0));
      QCOMPARE (N (legacy), N (native));
      QVERIFY (N (native) >= 1);
      for (int i=0; i<N (native); ++i) {
        QCOMPARE (native[i]->type, legacy[i]->type);
        QCOMPARE (native[i]->op_type, legacy[i]->op_type);
        // The matching stage replaces a text glyph by a composite
        // move_delimiter: get_leaf_string() is not valid on that box.
        QVERIFY (tree (native[i]->b) == tree (legacy[i]->b));
        QCOMPARE (native[i]->b->w (), legacy[i]->b->w ());
        QCOMPARE (native[i]->b->y1, legacy[i]->b->y1);
        QCOMPARE (native[i]->b->y2, legacy[i]->b->y2);
      }
    }

    const tree tall (FRAC, tree ("1"),
      tree (FRAC, tree ("1"), tree ("x+1")));
    const tree old_expression (CONCAT, tree ("x"), tree (MID, "|"), tall,
                               tree ("+y"));
    const tree new_expression (CONCAT, tree ("x"),
      tree (MATH_MIDDLE, "|"), tall, tree ("+y"));
    box old_box= typeset_as_concat (env, old_expression, path (0));
    box new_box= typeset_as_concat (env, new_expression, path (0));
    QVERIFY (old_box->w () > 0);
    QCOMPARE (new_box->w (), old_box->w ());
    QCOMPARE (new_box->y1, old_box->y1);
    QCOMPARE (new_box->y2, old_box->y2);
    QCOMPARE (new_box->x1, old_box->x1);
    QCOMPARE (new_box->x2, old_box->x2);
  }

  void nativeV3DelimitedAutoMatchesLegacyGeometry () {
    drd_info drd ("native-v3-delimited-geometry", std_drd);
    hashmap<string,tree> h1 (UNINIT), h2 (UNINIT), h3 (UNINIT);
    hashmap<string,tree> h4 (UNINIT), h5 (UNINIT), h6 (UNINIT);
    edit_env env (drd, url_none (), h1, h2, h3, h4, h5, h6);
    env->write_default_env ();
    env->write (FONT, "TeX Gyre Pagella");
    env->write (MODE, "math");
    env->write (MATH_DISPLAY, "true");
    env->update ();
    for (const auto& delimiters:
         {std::pair<string,string> {"(", ")"}, {"⟨", "⟩"},
          {"", "|"}, {"|", "|"}}) {
      for (const tree body:
           {tree ("x"), tree (FRAC, "1", tree (FRAC, "1", "x+1"))}) {
        tree legacy (VAR_AROUND,
          delimiters.first == "" ? tree (".") : tree (delimiters.first),
          body, tree (delimiters.second));
        tree native= athena::math_v3::delimited (
          delimiters.first, delimiters.second, 'a', body);
        box old_box= typeset_as_concat (env, legacy, path (0));
        box new_box= typeset_as_concat (env, native, path (0));
        QVERIFY (new_box->w () > 0);
        QVERIFY (new_box->h () > 0);
        QCOMPARE (new_box->w (), old_box->w ());
        QCOMPARE (new_box->x1, old_box->x1);
        QCOMPARE (new_box->x2, old_box->x2);
        QCOMPARE (new_box->y1, old_box->y1);
        QCOMPARE (new_box->y2, old_box->y2);
      }
    }
  }

  void nativeV3DelimitedFixedAndManualSize () {
    drd_info drd ("native-v3-delimited-manual", std_drd);
    hashmap<string,tree> h1 (UNINIT), h2 (UNINIT), h3 (UNINIT);
    hashmap<string,tree> h4 (UNINIT), h5 (UNINIT), h6 (UNINIT);
    edit_env env (drd, url_none (), h1, h2, h3, h4, h5, h6);
    env->write_default_env ();
    env->write (FONT, "TeX Gyre Pagella");
    env->write (MODE, "math");
    env->update ();
    const tree body ("x");
    box natural= typeset_as_concat (env,
      athena::math_v3::delimited ("⌈", "⌋", 'f', body), path (0));
    box manual= typeset_as_concat (env,
      athena::math_v3::delimited ("⌈", "⌋", 'm', body, "5", "1"), path (0));
    box reversed= typeset_as_concat (env,
      athena::math_v3::delimited ("⌈", "⌋", 'm', body, "1", "5"), path (0));
    QVERIFY (natural->w () > 0);
    QVERIFY (manual->w () > 0);
    QVERIFY (reversed->w () > 0);
    QVERIFY (manual->h () > natural->h ());
    QVERIFY (reversed->h () > natural->h ());
  }

  void nativeV3ScriptsMatchLegacyUnicodeGeometry () {
    drd_info drd ("native-v3-math-scripts", std_drd);
    hashmap<string,tree> h1 (UNINIT), h2 (UNINIT), h3 (UNINIT);
    hashmap<string,tree> h4 (UNINIT), h5 (UNINIT), h6 (UNINIT);
    edit_env env (drd, url_none (), h1, h2, h3, h4, h5, h6);
    env->write_default_env ();
    env->write (FONT, "TeX Gyre Pagella");
    env->write (MODE, "math");
    env->write (MATH_DISPLAY, "true");
    env->update ();

    const tree empty ("");
    const tree legacy_script (CONCAT, tree ("x"),
      tree (RSUB, "i"), tree (RSUP, "2"));
    const tree modern_script= athena::math_v3::scripts (
      tree ("x"), empty, empty, tree ("i"), tree ("2"));

    const tree legacy_nucleus (CONCAT, tree (LSUB, "p"),
      tree (LSUP, "q"), tree ("π"), tree (RSUB, "i"), tree (RSUP, "n"));
    const tree modern_nucleus= athena::math_v3::scripts (
      tree ("π"), tree ("p"), tree ("q"), tree ("i"), tree ("n"));

    const tree legacy_big_unicode (CONCAT, tree (BIG, "∑"),
      tree (RSUB, "i=1"), tree (RSUP, "n"), tree ("a"));
    const tree modern_big (CONCAT,
      athena::math_v3::scripts (athena::math_v3::big_operator ("∑"),
        empty, empty, tree ("i=1"), tree ("n")), tree ("a"));
    const tree legacy_integral (CONCAT, tree (BIG, "∫"),
      tree (RSUB, "0"), tree (RSUP, "1"), tree ("f"));
    const tree native_integral (CONCAT,
      athena::math_v3::scripts (athena::math_v3::big_operator ("∫"),
        empty, empty, tree ("0"), tree ("1")), tree ("f"));
    const tree legacy_upright (CONCAT,
      tree (BIG, tree (WITH, "math-big-upright", "true", "∫")),
      tree (RSUB, "0"), tree (RSUP, "1"), tree ("f"));
    const tree native_upright (CONCAT,
      athena::math_v3::scripts (athena::math_v3::big_operator ("∫", true),
        empty, empty, tree ("0"), tree ("1")), tree ("f"));

    // Same Unicode operator, same size/style and same script fields must
    // yield the same geometry. The historical big("sum") font recipe uses
    // a different glyph from canonical Unicode ∑, so it is intentionally
    // not a pixel-identical reference for the native operator.
    for (const char* display: {"true", "false"}) {
      env->write (MATH_DISPLAY, display);
      env->update ();
      for (auto pair: {std::pair<tree,tree> {legacy_script,modern_script},
                       {legacy_nucleus,modern_nucleus},
                       {legacy_big_unicode,modern_big},
                       {legacy_integral,native_integral},
                       {legacy_upright,native_upright}}) {
        box old_box= typeset_as_concat (env, pair.first, path (0));
        box new_box= typeset_as_concat (env, pair.second, path (0));
        QVERIFY (old_box->w () > 0 && new_box->w () > 0);
        QCOMPARE (new_box->w (), old_box->w ());
        QCOMPARE (new_box->x1, old_box->x1);
        QCOMPARE (new_box->x2, old_box->x2);
        QCOMPARE (new_box->y1, old_box->y1);
        QCOMPARE (new_box->y2, old_box->y2);
      }
    }
  }

  void nativeV3LeftScriptsRespectRightLimitsPolicy () {
    drd_info drd ("native-v3-combined-scripts-limits", std_drd);
    hashmap<string,tree> h1 (UNINIT), h2 (UNINIT), h3 (UNINIT);
    hashmap<string,tree> h4 (UNINIT), h5 (UNINIT), h6 (UNINIT);
    edit_env env (drd, url_none (), h1, h2, h3, h4, h5, h6);
    env->write_default_env ();
    env->write (FONT, "TeX Gyre Pagella");
    env->write (MODE, "math");
    env->update ();

    const auto contains_box_marker= [] (const tree& structure,
                                         const string& value) {
      std::function<bool(const tree&)> visit= [&] (const tree& node) {
        if (is_atomic (node)) return node->label == value;
        for (int i=0; i<N (node); ++i)
          if (visit (node[i])) return true;
        return false;
      };
      return visit (structure);
    };
    tree big= athena::math_v3::big_operator ("∑");
    const tree empty ("");
    for (const char* display: {"true", "false"}) {
      env->write (MATH_DISPLAY, display);
      env->update ();
      for (char placement: {'a', 'o', 'r', 'd'}) {
        tree source= athena::math_v3::scripts (big, "p", "q", "i=1", "n",
                                               placement);
        box result= typeset_as_concat (env, source, path (0));
        QVERIFY (result->w () > 0);
        tree shape= tree (result);
        QVERIFY (contains_box_marker (shape, "side"));
        const bool should_have_limits= placement == 'o' ||
          ((placement == 'a' || placement == 'd') &&
           std::string (display) == "true");
        QCOMPARE (contains_box_marker (shape, "lim"), should_have_limits);
      }
    }
  }

  void nativeV3MathGridAndUnnumberedBlockLayout () {
    drd_info drd ("native-v3-math-grid-display", std_drd);
    hashmap<string,tree> h1 (UNINIT), h2 (UNINIT), h3 (UNINIT);
    hashmap<string,tree> h4 (UNINIT), h5 (UNINIT), h6 (UNINIT);
    edit_env env (drd, url_none (), h1, h2, h3, h4, h5, h6);
    env->write_default_env ();
    env->write (FONT, "TeX Gyre Pagella");
    env->write (MODE, "math");
    env->update ();

    tree grid (MATH_GRID,
      tree (ROW, tree (CELL, "x"), tree (CELL, "="), tree (CELL, "y")),
      tree (ROW, tree (CELL, "u"), tree (CELL, "="), tree (CELL, "v")));
    athena::node::metadata grid_meta;
    grid_meta.properties["math:grid-kind"]= athena::node::property (
      std::string ("eqnarray"));
    athena::node::set (grid, grid_meta);
    QVERIFY (athena::math_v3::valid_native_math (grid));
    box layout= typeset_as_concat (env, grid, path (0));
    QVERIFY (layout->w () > 0);
    QVERIFY (layout->h () > 0);

    tree display (MATH_BLOCK, grid);
    athena::node::metadata meta;
    meta.properties["math:block-numbered"]= athena::node::property (false);
    athena::node::set (display, meta);
    QVERIFY (athena::math_v3::valid_native_math (display));
    box full= typeset_as_concat (env, display, path (0));
    QVERIFY (full->w () > 0);
    QVERIFY (full->h () > 0);
  }

  void nativeV3SingleEquationNumberUsesStyleCounterAndBinding () {
    drd_info drd ("native-v3-numbered-math-block", std_drd);
    hashmap<string,tree> h1 (UNINIT), h2 (UNINIT), h3 (UNINIT);
    hashmap<string,tree> h4 (UNINIT), h5 (UNINIT), h6 (UNINIT);
    edit_env env (drd, url_none (), h1, h2, h3, h4, h5, h6);
    env->write_default_env ();
    env->write (FONT, "TeX Gyre Pagella");
    env->write (MODE, "text"); // MathBlock itself must establish math mode.
    env->write ("test-equation-counter", "0");
    env->write ("next-equation",
      tree (MACRO, tree (ASSIGN, "test-equation-counter", "7")));
    env->write ("the-equation",
      tree (MACRO, tree (VALUE, "test-equation-counter")));
    env->write ("the-tags", tree (TUPLE, "eq:my-formula"));
    env->update ();

    tree numbered (MATH_BLOCK, tree (FRAC, "1", "2"));
    athena::node::metadata meta;
    meta.properties["math:block-numbered"]= athena::node::property (true);
    athena::node::set (numbered, meta);
    QVERIFY (athena::math_v3::valid_native_math (numbered));
    box result= typeset_as_concat (env, numbered, path (0));
    QVERIFY (result->w () > 0);
    QCOMPARE (env->read ("test-equation-counter"), tree ("7"));
    QCOMPARE (env->read ("the-label"), tree ("7"));
    QCOMPARE (env->read ("the-tags"), tree (TUPLE));
    QVERIFY (is_func (env->local_ref["eq:my-formula"], TUPLE));
    QCOMPARE (env->local_ref["eq:my-formula"][0], tree ("7"));
    QCOMPARE (env->read (MODE), tree ("text"));
    QCOMPARE (env->read (MATH_DISPLAY), tree ("false"));

    // A numbered multi-row grid cannot silently receive only one number.
    // Reject before touching the counter or the document reference store.
    tree grid (MATH_GRID,
      tree (ROW, tree (CELL, "a")), tree (ROW, tree (CELL, "b")));
    athena::node::metadata grid_meta;
    grid_meta.properties["math:grid-kind"]=
      athena::node::property (std::string ("eqnarray"));
    athena::node::set (grid, grid_meta);
    tree multi (MATH_BLOCK, grid);
    athena::node::set (multi, meta);
    const tree previous= copy (env->read ("test-equation-counter"));
    (void) typeset_as_concat (env, multi, path (0));
    QCOMPARE (env->read ("test-equation-counter"), previous);
  }

  void nativeV3AnnotationAndLongArrowMatchLegacyUnicodeGeometry () {
    drd_info drd ("native-v3-annotation-layout", std_drd);
    hashmap<string,tree> h1 (UNINIT), h2 (UNINIT), h3 (UNINIT);
    hashmap<string,tree> h4 (UNINIT), h5 (UNINIT), h6 (UNINIT);
    edit_env env (drd, url_none (), h1, h2, h3, h4, h5, h6);
    env->write_default_env ();
    env->write (FONT, "TeX Gyre Pagella");
    env->write (MODE, "math");
    env->update ();
    const tree empty ("");

    const tree native_above (MATH_ANNOTATION, tree ("x"),
                             empty, tree ("i=1"));
    const tree native_below (MATH_ANNOTATION, tree ("x"),
                             tree ("i=1"), empty);
    const tree native_arrow (MATH_LONG_ARROW, tree ("→"),
                             tree ("x"), tree ("f"));
    const tree native_above_arrow (MATH_LONG_ARROW, tree ("→"),
                                   empty, tree ("f"));
    for (const char* display: {"true", "false"}) {
      env->write (MATH_DISPLAY, display);
      env->update ();
      for (const auto& pair: {
           std::pair<tree,tree> {tree (ABOVE, "x", "i=1"), native_above},
           {tree (BELOW, "x", "i=1"), native_below},
           {tree (LONG_ARROW, "→", "f", "x"), native_arrow},
           {tree (LONG_ARROW, "→", "f"), native_above_arrow}}) {
        box before= typeset_as_concat (env, pair.first, path (0));
        box after= typeset_as_concat (env, pair.second, path (0));
        QVERIFY (before->w () > 0 && after->w () > 0);
        QCOMPARE (after->w (), before->w ());
        QCOMPARE (after->x1, before->x1);
        QCOMPARE (after->x2, before->x2);
        QCOMPARE (after->y1, before->y1);
        QCOMPARE (after->y2, before->y2);
      }
    }
  }

  void nativeV3AccentMatchesLegacyUnicodeGeometry () {
    drd_info drd ("native-v3-accent-variant", std_drd);
    hashmap<string,tree> h1 (UNINIT), h2 (UNINIT), h3 (UNINIT);
    hashmap<string,tree> h4 (UNINIT), h5 (UNINIT), h6 (UNINIT);
    edit_env env (drd, url_none (), h1, h2, h3, h4, h5, h6);
    env->write_default_env ();
    env->write (FONT, "TeX Gyre Pagella");
    env->write (MODE, "math");
    env->update ();
    for (auto spec: {std::pair<string,string> {"̃", "x"},
                     {"̅", "xy"}, {"̂", "f"}, {"⃗", "v"}}) {
      for (bool above: {true, false}) {
        tree before (above ? WIDE : VAR_WIDE, tree (spec.second),
                     tree (spec.first));
        tree after (MATH_ACCENT, tree (spec.first),
                    tree (above ? "o" : "u"), tree (spec.second));
        athena::node::metadata meta;
        meta.properties["math:accent-stretch"]= athena::node::property (false);
        athena::node::set (after, meta);
        box old_box= typeset_as_concat (env, before, path (0));
        box new_box= typeset_as_concat (env, after, path (0));
        QVERIFY (new_box->w () > 0);
        QCOMPARE (new_box->w (), old_box->w ());
        QCOMPARE (new_box->x1, old_box->x1);
        QCOMPARE (new_box->x2, old_box->x2);
        QCOMPARE (new_box->y1, old_box->y1);
        QCOMPARE (new_box->y2, old_box->y2);
      }
    }
  }

  void nativeV3MathVariantMatchesExpandedStyleSemantics () {
    drd_info drd ("native-v3-math-variants", std_drd);
    hashmap<string,tree> h1 (UNINIT), h2 (UNINIT), h3 (UNINIT);
    hashmap<string,tree> h4 (UNINIT), h5 (UNINIT), h6 (UNINIT);
    edit_env env (drd, url_none (), h1, h2, h3, h4, h5, h6);
    env->write_default_env ();
    env->write (FONT, "TeX Gyre Pagella");
    env->write (MODE, "math");
    env->update ();
    // The std-math macros are not loaded in a standalone typesetting test;
    // directly typesetting compound("math-bf",...) prints the literal tag.
    // Compare the native variant with the macro's *expanded* environment
    // semantics, not with an unexpanded historical macro name.
    for (const string kind: {string ("bb"), string ("cal"),
                             string ("rm"), string ("bf"),
                             string ("it"), string ("sl"),
                             string ("ss"), string ("tt")}) {
      const tree base ("R");
      tree expected;
      if (kind == "bb" || kind == "cal")
        expected= tree (WITH, MATH_FONT_SHAPE,
          kind == "bb" ? "bbb" : "cal", base);
      else {
        const string family= kind == "ss" ? "ss" :
                             kind == "tt" ? "tt" : "rm";
        const string shape= kind == "it" ? "italic" :
                            kind == "sl" ? "slanted" : "right";
        expected= tree (WITH);
        expected << tree (MODE) << tree ("text")
                 << tree (FONT_FAMILY) << tree (family)
                 << tree (FONT_SHAPE) << tree (shape);
        if (kind == "bf")
          expected << tree (FONT_SERIES) << tree ("bold");
        expected << base;
      }
      const tree source (MATH_VARIANT, tree (kind), base);
      box before= typeset_as_concat (env, expected, path (0));
      box after= typeset_as_concat (env, source, path (0));
      QVERIFY2 (after->w () > 0, as_charp (kind));
      QCOMPARE (after->w (), before->w ());
      QCOMPARE (after->x1, before->x1);
      QCOMPARE (after->x2, before->x2);
      QCOMPARE (after->y1, before->y1);
      QCOMPARE (after->y2, before->y2);
    }
  }

  void legacyColonVariantsUseModernUnicodeMapping () {
    QCOMPARE (strict_cork_to_utf8 ("<of>"), string (":"));
    QCOMPARE (strict_cork_to_utf8 ("<over>"), string (":"));
    QCOMPARE (strict_cork_to_utf8 ("<suchthat>"), string (":"));

    const auto* of= athena::text::standard_named_symbols ().lookup ("texmacs:of");
    QVERIFY (of != nullptr);
    QCOMPARE (of->glyph_utf8, std::string (":"));
    QCOMPARE (of->op_type, OP_INFIX);

    font math= smart_font ("roman", "rm", "medium", "mathitalic", 10, 600);
    QVERIFY (!is_nil (math));
    for (string token: {string ("<of>"), string ("<over>"),
                        string ("<suchthat>")}) {
      metric ex;
      math->get_extents (token, ex);
      QVERIFY2 (ex->x2 > ex->x1, as_charp (token));
    }

    metric unknown;
    math->get_extents ("<athena-unmapped-math-symbol>", unknown);
    QVERIFY (unknown->x2 >= unknown->x1);
  }

  void legacyMenuMathClassesMaterializePaletteGlyphs () {
    scheme_tree descriptor (TUPLE);
    for (string token: {
           string ("<big-int-2>"), string ("<oplus>"), string ("<leq>"),
           string ("<rightarrow>"), string ("<alpha>"), string ("<b-A>"),
           string ("<cal-A>"), string ("<frak-A>"), string ("<bbb-A>")}) {
      widget preview= box_widget (descriptor, token, black, true, false);
      QVERIFY2 (!is_nil (preview), as_charp (token));
      QAction* action= concrete (preview)->as_qaction ();
      QVERIFY2 (action != nullptr, as_charp (token));
      QVERIFY2 (!action->icon ().isNull (), as_charp (token));
      QImage image= action->icon ().pixmap (QSize (48, 48)).toImage ();
      bool ink= false;
      for (int y=0; y<image.height () && !ink; ++y)
        for (int x=0; x<image.width () && !ink; ++x)
          ink= qAlpha (image.pixel (x, y)) != 0;
      QVERIFY2 (ink, as_charp (token));
      delete action;
    }
  }

  void romanUsesLatinModernWhenAvailable () {
    if (!tt_font_exists ("Latin Modern Roman") ||
        !tt_font_exists ("Latin Modern Math"))
      QSKIP ("Latin Modern OpenType fonts are not available");

    font text= smart_font ("roman", "rm", "medium", "right", 12, 600);
    font_metric text_metric;
    font_glyphs text_glyphs;
    int text_index= text->index_glyph ("x", text_metric, text_glyphs);
    QVERIFY (text_index >= 0);
    QVERIFY (!is_nil (text_metric));
    QVERIFY2 (occurs ("lmroman", text_metric->res_name) ||
              occurs ("Latin Modern Roman", text_metric->res_name),
              as_charp (text_metric->res_name));
    QVERIFY (!occurs ("pagella", locase_all (text_metric->res_name)));

    font math= smart_font ("roman", "rm", "medium", "mathitalic", 12, 600);
    font_metric math_metric;
    font_glyphs math_glyphs;
    int math_index= math->index_glyph ("<sum>", math_metric, math_glyphs);
    QVERIFY (math_index >= 0);
    QVERIFY (!is_nil (math_metric));
    QVERIFY2 (occurs ("latinmodern-math", locase_all (math_metric->res_name)) ||
              occurs ("latin modern math", locase_all (math_metric->res_name)),
              as_charp (math_metric->res_name));
    QVERIFY (!occurs ("pagella", locase_all (math_metric->res_name)));
  }

  void logicalFontKeepsConcreteFamilyIdentity () {
    array<string> styles= font_database_styles ("DejaVu Sans");
    if (N(styles) == 0) QSKIP ("DejaVu Sans is not installed");
    array<string> logical= logical_font_exact ("DejaVu Sans", styles[0]);
    QVERIFY (N(logical) > 0);
    QCOMPARE (logical[0], string ("DejaVu Sans"));
  }

  void artificialFamilyTaxonomyIsIgnored () {
    array<string> logical=
      logical_font ("DejaVu Serif", "gothic-artpen", "medium", "right");
    QVERIFY (N(logical) > 0);
    QCOMPARE (logical[0], string ("DejaVu Serif"));
    QVERIFY (!contains (string ("gothic"), logical));
    QVERIFY (!contains (string ("artpen"), logical));
  }

  void explicitCalFrakAndBbbRolesUseConfiguredMathFaces () {
    if (!tt_font_exists ("texgyretermes-math") ||
        !tt_font_exists ("texgyrepagella-math") ||
        !tt_font_exists ("texgyrebonum-math"))
      QSKIP ("TeX Gyre Termes/Pagella/Bonum Math are not available");

    string profile=
      "cal=TeX Gyre Termes,bold-cal=TeX Gyre Termes,"
      "frak=TeX Gyre Pagella,bbb=TeX Gyre Bonum,DejaVu Serif";
    font math= smart_font (profile, "rm", "medium", "mathitalic", 12, 600);
    QVERIFY (!is_nil (math));

    for (auto expected: {
           std::pair<string,string> ("<cal-A>", "termes"),
           std::pair<string,string> ("<b-cal-A>", "termes"),
           std::pair<string,string> ("<frak-A>", "pagella"),
           std::pair<string,string> ("<bbb-A>", "bonum")}) {
      font_metric metric;
      font_glyphs glyphs;
      int index= math->index_glyph (expected.first, metric, glyphs);
      QVERIFY2 (index >= 0 && !is_nil (metric) && !is_nil (glyphs),
                as_charp (expected.first));
      string resource= locase_all (metric->res_name);
      QVERIFY2 (occurs (expected.second, resource), as_charp (metric->res_name));
      QVERIFY2 (occurs ("math", resource), as_charp (metric->res_name));
    }
  }

  void automaticCalFrakAndBbbUseDedicatedMathFallback () {
    if (N(font_database_styles ("DejaVu Serif")) == 0)
      QSKIP ("DejaVu Serif is not available");
    if (N(font_database_styles ("STIX Two Math")) == 0 &&
        N(font_database_styles ("Latin Modern Math")) == 0 &&
        N(font_database_styles ("STIX Math")) == 0 &&
        N(font_database_styles ("Asana Math")) == 0)
      QSKIP ("No dedicated OpenType math fallback is available");

    font math= smart_font ("DejaVu Serif", "rm", "medium", "mathitalic",
                           12, 600);
    QVERIFY (!is_nil (math));
    for (string token: {string ("<cal-A>"), string ("<frak-A>"),
                        string ("<bbb-A>")}) {
      font_metric metric;
      font_glyphs glyphs;
      int index= math->index_glyph (token, metric, glyphs);
      QVERIFY2 (index >= 0 && !is_nil (metric) && !is_nil (glyphs),
                as_charp (token));
      string resource= locase_all (metric->res_name);
      QVERIFY2 (!occurs ("dejavu", resource), as_charp (metric->res_name));
      QVERIFY2 (occurs ("math", resource), as_charp (metric->res_name));
    }
  }

  void pagellaUsesNativeMathDelimiterVariants () {
    for (auto family: {std::pair<string,string> ("TeX Gyre Pagella",
                                                  "texgyrepagella-math"),
                       std::pair<string,string> ("TeX Gyre Bonum",
                                                  "texgyrebonum-math")}) {
      drd_info drd ("native-delimiters", std_drd);
      hashmap<string,tree> h1 (UNINIT), h2 (UNINIT), h3 (UNINIT);
      hashmap<string,tree> h4 (UNINIT), h5 (UNINIT), h6 (UNINIT);
      edit_env env (drd, url_none (), h1, h2, h3, h4, h5, h6);
      env->write_default_env ();
      env->write (FONT, family.first);
      env->write (FONT_BASE_SIZE, "12");
      env->write (MODE, "math");
      env->update ();

      SI previous= 0;
      for (int n=1; n<=6; ++n) {
        string token= "<left-(-" * as_string (n) * ">";
        metric ex;
        env->fn->get_extents (token, ex);
        SI height= ex->y2 - ex->y1;
        QVERIFY2 (height > previous, as_charp (token));
        previous= height;

        font_metric metric;
        font_glyphs glyphs;
        int index= env->fn->index_glyph (token, metric, glyphs);
        QVERIFY2 (index >= 0 && !is_nil (metric) && !is_nil (glyphs),
                  as_charp (token));
        QVERIFY2 (occurs (family.second, metric->res_name),
                  as_charp (metric->res_name));
      }
    }
  }

  void pagellaMatrixDelimiterGrowsWithRows () {
    drd_info drd ("matrix-delimiters", std_drd);
    hashmap<string,tree> h1 (UNINIT), h2 (UNINIT), h3 (UNINIT);
    hashmap<string,tree> h4 (UNINIT), h5 (UNINIT), h6 (UNINIT);
    edit_env env (drd, url_none (), h1, h2, h3, h4, h5, h6);
    env->write_default_env ();
    env->write (FONT, "TeX Gyre Pagella");
    env->write (FONT_BASE_SIZE, "12");
    env->write (MODE, "math");
    env->update ();

    auto two= athena::text::shape_math_stretch (
      env->fn, "(", 2 * env->fn->yx, true);
    auto three= athena::text::shape_math_stretch (
      env->fn, "(", 4 * env->fn->yx, true);
    QVERIFY (two.has_value ());
    QVERIFY (three.has_value ());
    QVERIFY (two->extent > 0);
    QVERIFY (three->extent > two->extent);
  }

  void pagellaDelimitersFollowMathAxis () {
    drd_info drd ("braces", std_drd);
    hashmap<string,tree> h1 (UNINIT), h2 (UNINIT), h3 (UNINIT);
    hashmap<string,tree> h4 (UNINIT), h5 (UNINIT), h6 (UNINIT);
    edit_env env (drd, url_none (), h1, h2, h3, h4, h5, h6);
    env->write_default_env ();
    env->write (FONT, "TeX Gyre Pagella");
    env->write (MODE, "math");
    env->update ();
    for (string size: {string ("10"), string ("12")}) {
      env->write (FONT_BASE_SIZE, size);
      env->update ();
      const auto metrics= athena::text::math_layout_metrics (env->fn);
      QVERIFY (metrics.has_value ());
      const SI axis= metrics->axis_height;
      for (string symbol: {string ("{"), string ("}"),
                           string ("("), string (")")}) {
        box b= delimiter_box (
          path (), symbol, env->fn, pencil (black),
          axis - 2 * env->fn->yx, axis + 2 * env->fn->yx);
        QVERIFY2 (!is_nil (b), as_charp (symbol));
        const SI center= (b->y1 + b->y2) / 2;
        QVERIFY2 (abs (center-axis) <= PIXEL/2,
                  as_charp (as_string (center-axis)));
      }
    }
  }

  void configuredTypewriterMetrics () {
    string family= "typewriter=JetBrains Mono,TeX Gyre Pagella";
    if (N(font_database_styles ("JetBrains Mono")) == 0)
      QSKIP ("JetBrains Mono is not available");
    for (string series: {string ("medium"), string ("bold")}) {
      font text= smart_font (family, "rm", series, "right", 12, 600);
      font mono= smart_font (family, "tt", series, "right", 12, 600);
      metric text_x, mono_x;
      text->get_extents ("x", text_x);
      mono->get_extents ("x", mono_x);
      double ratio= (double) (mono_x->y2-mono_x->y1)/
                            (text_x->y2-text_x->y1);
      QVERIFY2 (ratio > 0.95 && ratio < 1.05, as_charp (as_string (ratio)));
      double reported= (double) (mono_x->y2-mono_x->y1)/mono->yx;
      QVERIFY2 (reported > 0.95 && reported < 1.05,
                as_charp (as_string (reported)));
      font_metric mono_metric;
      font_glyphs mono_glyphs;
      int mono_index= mono->index_glyph ("x", mono_metric, mono_glyphs);
      QVERIFY (mono_index >= 0 && !is_nil (mono_metric));
      QVERIFY2 (occurs ("jetbrains", locase_all (mono_metric->res_name)),
                as_charp (mono_metric->res_name));
    }
  }

  void automaticTypewriterUsesSystemMonospaceRole () {
    string generic= tt_font_match_family ("monospace");
    if (generic == "" || N(font_database_styles (generic)) == 0)
      QSKIP ("Fontconfig did not resolve a concrete monospace family");
    font mono= smart_font ("TeX Gyre Pagella", "tt", "medium", "right",
                           12, 600);
    QVERIFY (!is_nil (mono));
    font_metric metric;
    font_glyphs glyphs;
    int index= mono->index_glyph ("x", metric, glyphs);
    QVERIFY (index >= 0 && !is_nil (metric));
    QVERIFY2 (!occurs ("pagella", locase_all (metric->res_name)),
              as_charp (metric->res_name));
  }

  void legacyInlineCalPreservesFont () {
    drd_info drd ("preview-test", std_drd);
    hashmap<string,tree> h1 (UNINIT), h2 (UNINIT), h3 (UNINIT);
    hashmap<string,tree> h4 (UNINIT), h5 (UNINIT), h6 (UNINIT);
    edit_env env (drd, url_none (), h1, h2, h3, h4, h5, h6);
    env->write_default_env ();
    env->write (FONT, "TeX Gyre Pagella");
    env->write (FONT_SERIES, "bold");
    env->write (MODE, "math");
    env->update ();
    tree original_font= env->read (FONT);
    tree original_math_font= env->read (MATH_FONT);
    box explicit_math= typeset_as_concat (
      env, tree (WITH, MATH_FONT, "cal", "E"), path ());
    box legacy= typeset_as_concat (
      env, tree (WITH, FONT, "cal", "E"), path ());
    font expected= letter_font (explicit_math);
    font actual= letter_font (legacy);
    QVERIFY (!is_nil (expected));
    QVERIFY (!is_nil (actual));
    QCOMPARE (actual->res_name, expected->res_name);
    QCOMPARE (env->read (FONT), original_font);
    QCOMPARE (env->read (MATH_FONT), original_math_font);
    QVERIFY (!is_nil (actual->get_glyph ("E")));
    QImage image (128, 128, QImage::Format_ARGB32_Premultiplied);
    image.fill (Qt::white);
    QPainter painter (&image);
    qt_renderer_rep renderer (&painter, 1.0, 128, 128, true);
    renderer.set_clipping (0, -128*PIXEL, 128*PIXEL, 0);
    renderer.set_pencil (pencil ((color) qRgb (0, 0, 0)));
    actual->draw_fixed (&renderer, "E", 20*PIXEL, -90*PIXEL);
    painter.end ();
    bool ink= false;
    for (int y=0; y<image.height (); ++y)
      for (int x=0; x<image.width (); ++x) {
        QRgb pixel= image.pixel (x, y);
        ink= ink || qRed (pixel) < 240;
        QCOMPARE (qRed (pixel), qGreen (pixel));
        QCOMPARE (qGreen (pixel), qBlue (pixel));
      }
    QVERIFY (ink);
  }
};

static void
run_tests (int argc, char** argv) {
  init_system_state ();
  gui_open (argc, argv);
  int result;
  {
    server sv;
    PreviewMathFontTest test;
    result= QTest::qExec (&test, argc, argv);
  }
  // The full ATHENA Qt runtime may race during process-global teardown after
  // this focused test has already released its test-owned objects.  Match the
  // other focused editor regressions and leave immediately after QTest.
  std::_Exit (result);
}

int main (int argc, char** argv) {
  QApplication app (argc, argv);
  QTemporaryDir profile;
  if (!profile.isValid ()) return 1;
  qputenv ("ATHENA_HOME_PATH", profile.path ().toUtf8 ());
  cache_initialize ();
  init_athena ();
  start_scheme (argc, argv, run_tests);
  return 1;
}

#include "preview_math_font_test.moc"
