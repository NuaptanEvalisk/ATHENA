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
#include <QtTest/QtTest>
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
