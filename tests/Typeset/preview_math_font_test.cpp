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
#include "data_cache.hpp"
#include "drd_std.hpp"
#include "gui.hpp"
#include "scheme.hpp"
#include "server.hpp"
#include "typesetter.hpp"
#include "qt_renderer.hpp"

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

static SI
delimiter_center (box b, string symbol, SI y=0) {
  if (b->get_type () == TEXT_BOX && b->get_leaf_string () == symbol)
    return y + (b->y1 + b->y2) / 2;
  for (int i=0; i<N(b); ++i) {
    SI center= delimiter_center (b[i], symbol, y+b->sy(i));
    if (center != MAX_SI) return center;
  }
  return MAX_SI;
}

class PreviewMathFontTest: public QObject {
  Q_OBJECT
private slots:
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
      for (string pair: {string ("{}"), string ("()")}) {
        for (string body: {string (""), string ("0")}) {
          box b= typeset_as_concat (env,
            tree (VAR_AROUND, pair(0,1), body, pair(1,2)), path ());
          for (int side=0; side<2; ++side) {
            string symbol= (side == 0 ? string ("<left-") : string ("<right-")) *
                           pair(side,side+1) * "-0>";
            SI center= delimiter_center (b, symbol);
            QVERIFY2 (center != MAX_SI, as_charp (symbol));
            QVERIFY2 (abs (center-env->fn->yfrac) <= PIXEL/2,
                      as_charp (as_string (center-env->fn->yfrac)));
          }
        }
      }
    }
  }

  void configuredTypewriterMetrics () {
    string family= "typewriter=JetBrains Mono,TeX Gyre Pagella";
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
    }
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
  init_tex_resources ();
  gui_open (argc, argv);
  int result;
  {
    server sv;
    PreviewMathFontTest test;
    result= QTest::qExec (&test, argc, argv);
  }
  gui_close ();
  release_boot_lock ();
  std::exit (result);
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
