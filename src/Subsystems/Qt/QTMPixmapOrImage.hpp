
/******************************************************************************
* MODULE     : QTMPixmapOrImage.hpp
* DESCRIPTION: Union of QPixmap and QImage for headless mode
* COPYRIGHT  : (C) 2022 Gregoire Lecerf
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef QTMPIXMAPORIMAGE_HPP
#define QTMPIXMAPORIMAGE_HPP

#include "boot.hpp"
#include <QPixmap>
#include <QImage>

// if QTMPIXMAPS is defined we use QPixmap for characters
// otherwise we use QImage (which supports alpha also under X11)

#undef QTMPIXMAPS

struct QTMPixmapOrImage {
  void* rep;
  bool image_backed;

  QTMPixmapOrImage (): image_backed (headless_mode) {
    if (image_backed) rep= (void*) new QImage ();
    else rep= (void*) new QPixmap ();
  }
  ~QTMPixmapOrImage () {
    if (image_backed) delete (QImage*) rep;
    else delete (QPixmap*) rep;
  }
  QTMPixmapOrImage (int w, int h, bool force_image= false):
    image_backed (force_image || headless_mode) {
    if (image_backed)
      rep= (void*) new QImage (w, h, QImage::Format_ARGB32);
    else
      rep= (void*) new QPixmap (w, h);
  }
  QTMPixmapOrImage (QSize s, bool force_image= false):
    image_backed (force_image || headless_mode) {
    if (image_backed)
      rep= (void*) new QImage (s, QImage::Format_ARGB32);
    else
      rep= (void*) new QPixmap (s);
  }
  QTMPixmapOrImage (const QPixmap& px):
    rep ((void*) new QPixmap (px)), image_backed (false) {}
  QTMPixmapOrImage (const QImage& im):
    rep ((void*) new QImage (im)), image_backed (true) {}
  QTMPixmapOrImage (const QTMPixmapOrImage& pxim):
    image_backed (pxim.image_backed) {
    if (image_backed)
      rep= (void*) new QImage (*((QImage*) pxim.rep));
    else
      rep= (void*) new QPixmap (*((QPixmap*) pxim.rep));
  }
  QTMPixmapOrImage& operator=(const QTMPixmapOrImage& pxim) {
    if (this == &pxim) return *this;
    if (image_backed != pxim.image_backed) {
      if (image_backed) delete (QImage*) rep;
      else delete (QPixmap*) rep;
      image_backed= pxim.image_backed;
      if (image_backed) rep= (void*) new QImage ();
      else rep= (void*) new QPixmap ();
    }
    if (image_backed)
      *((QImage*) rep)= *((QImage*) pxim.rep);
    else
      *((QPixmap*) rep)= *((QPixmap*) pxim.rep);
    return *this;
  }
  void fill (const QColor& c) {
    if (image_backed)
      ((QImage*) rep)->fill (c);
    else
      ((QPixmap*) rep)->fill (c);
  }
  bool isNull () {
    return image_backed ?
      ((QImage*) rep)->isNull () : ((QPixmap*) rep)->isNull ();
  }
  bool is_image () const { return image_backed; }
  QImage* QImage_ptr () {
    ASSERT (image_backed, "internal bug in QTMPixmapOrImage::QImage_ptr");
    return (QImage*) rep;
  }
  QPixmap* QPixmap_ptr () {
    ASSERT (!image_backed, "internal bug in QTMPixmapOrImage::QPixmap_ptr");
    return (QPixmap*) rep;
  }
  void* void_ptr () {
    return rep;
  } 
};

#ifdef QTMPIXMAPS
#define QTMImage QTMPixmapOrImage
#else
#define QTMImage QImage
#endif

#endif
