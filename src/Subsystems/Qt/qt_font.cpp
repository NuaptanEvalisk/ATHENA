
/******************************************************************************
* MODULE     : qt_font.cpp
* DESCRIPTION: Qt fonts
* COPYRIGHT  : (C) 2012  Joris van der Hoeven
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "Qt/qt_font.hpp"
#include "Qt/qt_picture.hpp"
#include "Qt/qt_utilities.hpp"
#include "Qt/qt_renderer.hpp"

#include <QFontDatabase>
#include <QPainter>
#include <QStringList>
#include <QVector>

#include "analyze.hpp"
#include "dictionary.hpp"

#define MAGN (dpi * PIXEL / 72.0)
#define ROUND(x) ((SI) round (x * MAGN))
#define FLOOR(x) ((SI) floor (x * MAGN))
#define CEIL(x)  ((SI) ceil  (x * MAGN))

/******************************************************************************
* Helpers
******************************************************************************/

static QFont
make_qt_font (string family, int size) {
  QFont qfn;
  if (family == "emoji") {
    QString noto= "Noto Color Emoji";
#if QT_VERSION >= 0x060000
    QStringList families= QFontDatabase::families ();
#else
    QFontDatabase db;
    QStringList families= db.families ();
#endif
    if (families.contains (noto, Qt::CaseInsensitive))
      qfn.setFamily (noto);
  }
  else if (family != "") qfn.setFamily (to_qstring (family));
  qfn.setPixelSize (size);
  return qfn;
}

/******************************************************************************
* The implementation
******************************************************************************/

qt_font_rep::qt_font_rep (string name, string family2, int size2, int dpi2):
  font_rep (name), family (family2), size (size2), dpi (dpi2),
  qfn (make_qt_font (family, size)),
  qfm (qfn)
{
  type= FONT_TYPE_QT;

  // get main font parameters
  y1= FLOOR (-qfm.descent ());
  y2= CEIL  (qfm.ascent () + 1);
  design_size  = size << 8;

  if (family == "emoji") {
    y1 = max(y1, - (int)(0.3 * design_size));
    y2 = min(y2, (int)(1.1 * design_size));
  }

  display_size = y2-y1;

  // get character dimensions
  metric ex;
  yx           = ROUND (qfm.xHeight ());
  get_extents ("M", ex);
  wquad        = ex->x2;

  // compute other heights
  yfrac        = yx >> 1;
  ysub_lo_base = -yx/3;
  ysub_hi_lim  = (5*yx)/6;
  ysup_lo_lim  = yx/2;
  ysup_lo_base = (5*yx)/6;
  ysup_hi_lim  = yx;
  yshift       = yx/6;

  // compute other widths
  wpt          = (dpi*PIXEL)/72;
  hpt          = (dpi*PIXEL)/72;
  wfn          = (wpt*design_size) >> 8;
  wline        = wfn/20;

  // get fraction bar parameters
  get_extents ("-", ex);
  yfrac= (ex->y3 + ex->y4) >> 1;

  // get space length
  get_extents (" ", ex);
  spc  = space ((3*(ex->x2-ex->x1))>>2, ex->x2-ex->x1, (ex->x2-ex->x1)<<1);
  extra= spc;
  mspc = spc;
  sep  = wfn/10;

  // get_italic space
  get_extents ("f", ex);
  SI italic_spc= (ex->x4-ex->x3)-(ex->x2-ex->x1);
  slope= ((double) italic_spc) / ((double) display_size);
  if (slope<0.15) slope= 0.0;
}

bool
qt_font_rep::supports (string c) {
  QString qs= utf8_to_qstring (cork_to_utf8 (c));
  QVector<uint> ucs4= qs.toUcs4 ();
  if (ucs4.size () == 0) return false;
  if (family == "" || family == "emoji") return true;
  if (ucs4.size () != 1) return false;
  return qfm.inFontUcs4 (ucs4[0]);
}

void
qt_font_rep::get_extents (string s, metric& ex) {
  QString qs  = utf8_to_qstring (cork_to_utf8 (s));
  QRectF  rect= qfm.tightBoundingRect (qs);
#if QT_VERSION >= 0x060000
  qreal   w   = qfm.horizontalAdvance (qs);
#else
  qreal   w   = qfm.width (qs);
#endif
  ex->x1= 0;
  ex->x2= ROUND (w);
  ex->y1= FLOOR (-rect.bottom ());
  ex->y2= CEIL  (-rect.top ());
  ex->x3= FLOOR (rect.left ());
  ex->x4= CEIL  (rect.right ());
  
  if (family == "emoji") {
    ex->y1 = max(ex->y1, y1);
    ex->y2 = min(ex->y2, y2);
  }

  ex->y3= ex->y1;
  ex->y4= ex->y2;
}

void
qt_font_rep::draw_fixed (renderer ren, string s, SI x, SI y) {
  if (N(s)!=0) {
    QString qs= utf8_to_qstring (cork_to_utf8 (s));
    if (ren->is_screen) {
      double zoom= dpi / (std_shrinkf * 72.0);
      qt_renderer_rep* qren= (qt_renderer_rep*) ren->get_handle ();
      qren -> draw (qfn, qs, x, y, zoom);
    }
    else if (ren->is_printer ()) {
      double scale= ((double) dpi) / 72.0;
      QFont pqfn= qfn;
      pqfn.setPixelSize (max (1, (int) ceil (((double) size) * scale)));
      QFontMetricsF pqfm (pqfn);
      QRectF rect= pqfm.tightBoundingRect (qs);
#if QT_VERSION >= 0x060000
      qreal advance= pqfm.horizontalAdvance (qs);
#else
      qreal advance= pqfm.width (qs);
#endif
      qreal left  = min ((qreal) 0.0, rect.left ());
      qreal right = max (advance, rect.right ());
      qreal top   = rect.top ();
      qreal bottom= rect.bottom ();
      int pad= 2;
      int w= max (1, (int) ceil (right - left) + 2 * pad);
      int h= max (1, (int) ceil (bottom - top) + 2 * pad);
      int ox= (int) floor (-left) + pad;
      int baseline= (int) floor (-top) + pad;
      int oy= h - 1 - baseline;

      QImage im (w, h, QImage::Format_ARGB32);
      im.fill (QColor (0, 0, 0, 0));
      QPainter painter (&im);
      painter.setFont (pqfn);
      painter.setPen (to_qcolor (ren->get_pencil ()->get_color ()));
      painter.drawText (QPointF (ox, baseline), qs);
      painter.end ();

      ren->draw_picture (qt_picture (im, ox, oy), x, y);
    }
  }
}

font
qt_font_rep::magnify (double zoomx, double zoomy) {
  if (zoomx != zoomy) return poor_magnify (zoomx, zoomy);
  return qt_font (family, size, (int) round (dpi * zoomx));
}

/******************************************************************************
* Interface
******************************************************************************/

font
qt_font (string family, int size, int dpi) {
  string name= "qt:" * (family == "" ? string ("default") : family) *
               as_string (size) * "@" * as_string (dpi);
  if (font::instances -> contains (name)) return font (name);
  else return tm_new<qt_font_rep> (name, family, size, dpi);
}
