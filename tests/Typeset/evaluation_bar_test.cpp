/******************************************************************************
* MODULE     : evaluation_bar_test.cpp
* DESCRIPTION: Stretchable evaluation delimiters in real typesetting and pixels
* COPYRIGHT  : (C) 2026 ATHENA contributors
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* See the file LICENSE in the root directory.
******************************************************************************/

#include <QApplication>
#include <QDir>
#include <QTemporaryDir>
#include <QtTest/QtTest>
#include <vector>
#include "analyze.hpp"
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

static box
evaluation_bar (box b) {
  if (b->get_type () == TEXT_BOX) {
    string s= b->get_leaf_string ();
    if (s == "|" || starts (s, "<large-|") || starts (s, "<right-|"))
      return b;
  }
  for (int i=0; i<N(b); ++i) {
    box bar= evaluation_bar (b[i]);
    if (!is_nil (bar)) return bar;
  }
  return box ();
}

static QImage
render_box (box b, SI x0= 0, SI y0= 0) {
  int width= (b->x4-b->x3+PIXEL-1)/PIXEL + 32;
  int height= (b->y4-b->y3+PIXEL-1)/PIXEL + 32;
  QImage image (width, height, QImage::Format_ARGB32_Premultiplied);
  image.fill (Qt::white);
  QPainter painter (&image);
  qt_renderer_rep renderer (&painter, 1.0, width, height, true);
  renderer.set_clipping (0, -height*PIXEL, width*PIXEL, 0);
  rectangles invalid;
  b->redraw (&renderer, path (), invalid,
             16*PIXEL-b->x3-x0, -16*PIXEL-b->y4-y0);
  painter.end ();
  return image;
}

static int
bar_ink_height (box b) {
  QImage image= render_box (b);
  int width= image.width (), height= image.height ();

  // The rightmost ink belongs to the bar; measuring its longest vertical
  // stroke rejects a blank or fixed-height glyph independently of box metrics.
  int right= width-1;
  for (; right>=0; --right) {
    bool ink= false;
    for (int y=0; y<height; ++y)
      ink= ink || qGray (image.pixel (right, y)) < 180;
    if (ink) break;
  }
  int longest= 0;
  for (int x=std::max (0, right-3); x<=right; ++x) {
    int run= 0;
    for (int y=0; y<height; ++y) {
      run= qGray (image.pixel (x, y)) < 180? run+1: 0;
      longest= std::max (longest, run);
    }
  }
  return longest;
}

struct delimiter_ink {
  SI bottom, top;
};

static void
collect_delimiter_ink (box b, SI y, std::vector<delimiter_ink>& ink,
                       SI x0= 0, SI y0= 0) {
  if (b->get_type () == TEXT_BOX) {
    string s= b->get_leaf_string ();
    if (starts (s, "<left-") || starts (s, "<right-") ||
        s == "<langle>" || s == "<rangle>") {
      QImage image= render_box (b, x0, y0);
      int first= image.height (), last= -1;
      for (int row=0; row<image.height (); ++row)
        for (int col=0; col<image.width (); ++col)
          if (qGray (image.pixel (col, row)) < 180) {
            first= std::min (first, row);
            last= std::max (last, row);
          }
      if (last >= first)
        ink.push_back ({y+b->y4+(15-last)*PIXEL,
                        y+b->y4+(16-first)*PIXEL});
    }
  }
  for (int i=0; i<N(b); ++i)
    collect_delimiter_ink (b[i], y+b->sy (i), ink, b->sx (i), b->sy (i));
}

class EvaluationBarTest: public QObject {
  Q_OBJECT
private slots:
  void nestedAngles_data () { growsWithBody_data (); }
  void nestedAngles () {
    QFETCH (QString, family);
    QFETCH (QString, series);
    drd_info drd ("angle-test", std_drd);
    hashmap<string,tree> h1 (UNINIT), h2 (UNINIT), h3 (UNINIT);
    hashmap<string,tree> h4 (UNINIT), h5 (UNINIT), h6 (UNINIT);
    edit_env env (drd, url_none (), h1, h2, h3, h4, h5, h6);
    env->write_default_env ();
    env->write (FONT, string (family.toUtf8 ().constData ()));
    env->write (FONT_SERIES, string (series.toUtf8 ().constData ()));
    env->write (FONT_BASE_SIZE, "24");
    env->write (MODE, "math");
    env->write (MATH_DISPLAY, "true");
    env->update ();
    tree bodies[]= {tree ("x"), tree (FRAC, "x", "y"),
                    tree (FRAC, tree (FRAC, "x", "y"), "z")};
    QString directory= qEnvironmentVariable ("ATHENA_ANGLE_ARTIFACTS");
    if (!directory.isEmpty ()) QVERIFY (QDir ().mkpath (directory));
    for (int size=0; size<3; size++)
      for (int kind=0; kind<2; kind++) {
        tree t= tree (kind == 0 ? AROUND : VAR_AROUND,
                      "<langle>", bodies[size], "<rangle>");
        t= tree (VAR_AROUND, "|", t, "|");
        box b= typeset_as_concat (env, t, path ());
        if (!directory.isEmpty ())
          QVERIFY (render_box (b).save (QDir (directory).filePath (
            QString ("%1-%2-%3-%4.png").arg (family, series).arg (size).arg (kind))));
        std::vector<delimiter_ink> ink;
        collect_delimiter_ink (b, 0, ink);
        QCOMPARE (ink.size (), size_t (4));
        // Compare actual glyph pixels in their composed positions, allowing
        // two raster pixels for rounding and antialiased font overshoot.
        for (int outer: {0, 3})
          for (int inner: {1, 2}) {
            QVERIFY2 (ink[outer].bottom <= ink[inner].bottom+2*PIXEL,
                      "Outer bar stops above the inner angle's bottom");
            QVERIFY2 (ink[outer].top >= ink[inner].top-2*PIXEL,
                      "Outer bar stops below the inner angle's top");
          }
      }

    for (tree body: bodies)
      for (string delim: {string ("("), string ("|"), string ("<langle>")}) {
        string close= delim == "(" ? ")" : delim == "|" ? "|" : "<rangle>";
        tree nested= body;
        SI height= 0;
        for (int n=0; n<12; ++n) {
          nested= tree (VAR_AROUND, delim, nested, close);
          box b= typeset_as_concat (env, nested, path ());
          if (n == 0) height= b->h ();
          else QCOMPARE (b->h (), height);
        }
      }
  }
  void growsWithBody_data () {
    QTest::addColumn<QString> ("family");
    QTest::addColumn<QString> ("series");
    QTest::newRow ("pagella") << "TeX Gyre Pagella" << "medium";
    QTest::newRow ("pagella-bold") << "TeX Gyre Pagella" << "bold";
    QTest::newRow ("termes") << "TeX Gyre Termes" << "medium";
  }

  void growsWithBody () {
    QFETCH (QString, family);
    QFETCH (QString, series);
    drd_info drd ("evaluation-test", std_drd);
    hashmap<string,tree> h1 (UNINIT), h2 (UNINIT), h3 (UNINIT);
    hashmap<string,tree> h4 (UNINIT), h5 (UNINIT), h6 (UNINIT);
    edit_env env (drd, url_none (), h1, h2, h3, h4, h5, h6);
    env->write_default_env ();
    env->write (FONT, string (family.toUtf8 ().constData ()));
    env->write (FONT_SERIES, string (series.toUtf8 ().constData ()));
    env->write (MODE, "math");
    env->write (MATH_DISPLAY, "true");
    env->update ();

    tree bodies[]= {tree ("x"), tree (FRAC, "d", "d x"),
                    tree (FRAC, tree (FRAC, "x", "y"), "z")};
    SI previous_height= 0;
    int previous_ink= 0;
    for (tree body: bodies) {
      box b= typeset_as_concat (
        env, tree (VAR_AROUND, "<nobracket>", body, "|"), path ());
      box bar= evaluation_bar (b);
      QVERIFY (!is_nil (bar));
      QVERIFY (bar->h () > previous_height);
      int ink= bar_ink_height (b);
      QVERIFY2 (ink > previous_ink, "Evaluation bar pixels did not grow");
      previous_height= bar->h ();
      previous_ink= ink;
    }
  }
};

static void
run_tests (int argc, char** argv) {
  init_plugins ();
  gui_open (argc, argv);
  int result;
  {
    server sv;
    EvaluationBarTest test;
    result= QTest::qExec (&test, argc, argv);
  }
  gui_close ();
  release_boot_lock ();
  std::exit (result);
}

int main (int argc, char** argv) {
  QTemporaryDir profile;
  if (!profile.isValid ()) return 1;
  qputenv ("HOME", profile.path ().toUtf8 ());
  qputenv ("ATHENA_HOME_PATH", profile.path ().toUtf8 ());
  qputenv ("XDG_CONFIG_HOME", profile.filePath ("config").toUtf8 ());
  qputenv ("XDG_CACHE_HOME", profile.filePath ("cache").toUtf8 ());
  qputenv ("XDG_DATA_HOME", profile.filePath ("data").toUtf8 ());
  QApplication app (argc, argv);
  cache_initialize ();
  init_athena ();
  start_scheme (argc, argv, run_tests);
  return 1;
}

#include "evaluation_bar_test.moc"
