
/******************************************************************************
* MODULE     : qt_simple_widget.hpp
* DESCRIPTION: A widget containing a TeXmacs canvas.
* COPYRIGHT  : (C) 2008  Massimiliano Gubinelli
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "iterator.hpp"

#include "qt_simple_widget.hpp"
#include "qt_window_widget.hpp"
#include "qt_utilities.hpp"
#include "qt_renderer.hpp"

#include "QTMWidget.hpp"
#include "QTMMenuHelper.hpp"
#include <QPixmap>
#include <QCursor>
#include <QLayout>
#include <QRegion>
#include <cmath>

#include "QTMImpressIconEngine.hpp"

qt_simple_widget_rep::qt_simple_widget_rep ()
  : qt_widget_rep (simple_widget),  sequencer (0) {
  backingPixmap= headless_mode ? NULL : new QPixmap ();
}

qt_simple_widget_rep::~qt_simple_widget_rep () {
  all_widgets->remove ((pointer) this);
  if (backingPixmap != NULL) delete backingPixmap;
}

QWidget*
qt_simple_widget_rep::as_qwidget (QWidget* parent_widget) {
  (void) parent_widget;
  // todo : fixme, when passing parent_widget,
  // qt_simple_widget_rep::send: canvas size is wrong
  // causing the scrollbar to be at the wrong position
  qwid = new QTMWidget (nullptr, this);
  reapply_sent_slots();
  SI width, height;
  handle_get_size_hint (width, height);
  QSize sz = to_qsize (width, height);
  scrollarea()->editor_flag= is_editor_widget ();
  if (!is_editor_widget ()) {
    canvas()->resize (sz);
    scrollarea()->setExtents (QRect (QPoint(0,0), sz));
  }
  all_widgets->insert((pointer) this);
  backing_pos = canvas()->origin ();
  backing_physical_pos= QPoint (
    qRound (device_pixel_ratio () * backing_pos.x ()),
    qRound (device_pixel_ratio () * backing_pos.y ()));
  backing_valid = false;
  return qwid;
}

/******************************************************************************
* Empty handlers for redefinition by our subclasses editor_rep, 
* box_widget_rep...
******************************************************************************/

bool
qt_simple_widget_rep::is_editor_widget () {
  return false;
}

bool
qt_simple_widget_rep::is_embedded_widget () {
  return false;
}

void
qt_simple_widget_rep::handle_get_size_hint (SI& w, SI& h) {
  gui_root_extents (w, h);
}

void
qt_simple_widget_rep::handle_notify_resize (SI w, SI h) {
  (void) w; (void) h;
}

void
qt_simple_widget_rep::handle_keypress (string key, time_t t) {
  (void) key; (void) t;
}

void
qt_simple_widget_rep::handle_text_input (string text, time_t t) {
  (void) text; (void) t;
}

void
qt_simple_widget_rep::handle_keyboard_focus (bool has_focus, time_t t) {
  (void) has_focus; (void) t;
}

void
qt_simple_widget_rep::handle_cursor_blink (bool visible) {
  (void) visible;
}

void
qt_simple_widget_rep::handle_user_scroll (time_t t) {
  (void) t;
}

void
qt_simple_widget_rep::handle_mouse (string kind, SI x, SI y, int mods, time_t t,
                                    array<double> data) {
  (void) kind; (void) x; (void) y; (void) mods; (void) t; (void) data;
}

void
qt_simple_widget_rep::handle_set_zoom_factor (double zoom) {
  (void) zoom;
}

void
qt_simple_widget_rep::handle_clear (renderer win, SI x1, SI y1, SI x2, SI y2) {
  (void) win; (void) x1; (void) y1; (void) x2; (void) y2;
}

void
qt_simple_widget_rep::handle_repaint (renderer win, SI x1, SI y1, SI x2, SI y2) {
  (void) win; (void) x1; (void) y1; (void) x2; (void) y2;
}

void
qt_simple_widget_rep::handle_post_repaint (bool painted) {
  (void) painted;
}


/******************************************************************************
* Handling of TeXmacs messages
******************************************************************************/

/*! Stores messages (SLOTS) sent to this widget for later replay.
 
 This is useful for recompilation of the QWidget inside as_qwidget() in some
 cases, where state information of the parsed widget (i.e. the qt_widget) is
 stored by us directly in the QWidget, and thus is lost if we delete it.

 Each SLOT is stored only once, repeated occurrences of the same one overwriting
 previous ones. Sequence information is also stored, allowing for correct replay.
 */
void
qt_simple_widget_rep::save_send_slot (slot s, blackbox val) {
  sent_slots[s].seq = sequencer;
  sent_slots[s].val = val;
  sent_slots[s].id  = s.sid;
  sequencer = (sequencer + 1) % slot_id__LAST;
}

void
qt_simple_widget_rep::reapply_sent_slots () {
  if (DEBUG_QT_WIDGETS)
    debug_widgets << ">>>>>>>> reapply_sent_slots() for widget: " << type_as_string() << LF;
  
  t_slot_entry sorted_slots[slot_id__LAST];
  for (int i = 0; i < slot_id__LAST; ++i)
    sorted_slots[i] = sent_slots[i];
  std::sort (&sorted_slots[0], &sorted_slots[slot_id__LAST]);
  
  for (int i = 0; i < slot_id__LAST; ++i)
    if (sorted_slots[i].seq >= 0)
      this->send(sorted_slots[i].id, sorted_slots[i].val);
  
  if (DEBUG_QT_WIDGETS)
    debug_widgets << "<<<<<<<< reapply_sent_slots() for widget: " << type_as_string() << LF;
}

void
qt_simple_widget_rep::send (slot s, blackbox val) {
  save_send_slot (s, val);

  switch (s) {
    case SLOT_INVALIDATE:
    {
      check_type<coord4>(val, s);
      coord4 p= open_box<coord4> (val);      
      qt_renderer_rep* ren = the_qt_renderer (device_pixel_ratio ());
      coord2 pt_or = from_qpoint(backing_pos);
      SI ox = -pt_or.x1;
      SI oy = -pt_or.x2;
      ren->set_origin(ox,oy);
      SI x1 = p.x1, y1 = p.x2, x2 = p.x3, y2 = p.x4;
      ren->outer_round (x1, y1, x2, y2);
      ren->decode (x1, y1);
      ren->decode (x2, y2);
      invalidate_rect (x1, y2, x2, y1);
    }
      break;
      
    case SLOT_INVALIDATE_ALL:
    {
      check_type_void (val, s);
      invalidate_all ();
    }
      break;
      
    case SLOT_EXTENTS:
    {
      check_type<coord4>(val, s);
      coord4 p = open_box<coord4> (val);
      scrollarea()->setExtents (to_qrect (p));
    }
      break;
      
    case SLOT_SIZE:
    {
      check_type<coord2>(val, s);
      coord2 p = open_box<coord2> (val);
      canvas()->resize (to_qsize(p));
    }
      break;
      
    case SLOT_SCROLL_POSITION:
    {
      check_type<coord2>(val, s);
      coord2  p = open_box<coord2> (val);
      QPoint qp = to_qpoint (p);
      QSize  sz = canvas()->surface()->size();
      qp -= QPoint (sz.width() / 2, sz.height() / 2);
        // NOTE: adjust because child is centered
      scrollarea()->setOrigin (qp);
    }
      break;
      
    case SLOT_ZOOM_FACTOR:
    {
      check_type<double> (val, s);
      double new_zoom = open_box<double> (val);
      canvas()->tm_widget()->handle_set_zoom_factor (new_zoom);
      reset_all ();
    }
      break;
      
    case SLOT_MOUSE_GRAB:
    {
      check_type<bool> (val, s);
      bool grab = open_box<bool>(val);
      if (grab && canvas() && !canvas()->hasFocus())
        canvas()->setFocus (Qt::MouseFocusReason);
    }
      break;

    case SLOT_MOUSE_POINTER:
    {
      typedef pair<string, string> T;
      check_type<T> (val, s);
      T contents = open_box<T> (val); // x1 = name, x2 = mask.
      Qt::CursorShape shape= Qt::ArrowCursor;
      if (contents.x1 == "XC_hand2") shape= Qt::PointingHandCursor;
      else if (contents.x1 == "XC_right_side" ||
               contents.x1 == "XC_sb_h_double_arrow")
        shape= Qt::SizeHorCursor;
      else if (contents.x1 == "XC_bottom_side" ||
               contents.x1 == "XC_sb_v_double_arrow")
        shape= Qt::SizeVerCursor;
      else if (contents.x1 == "XC_bottom_right_corner")
        shape= Qt::SizeFDiagCursor;
      if (canvas () != nullptr) canvas ()->setCursor (QCursor (shape));
    }
      break;

    case SLOT_CURSOR:
    {
      check_type<coord2>(val, s);
      coord2 p = open_box<coord2> (val);
      canvas()->setCursorPos(to_qpoint(p));
    }
      break;
      
    default:
      qt_widget_rep::send(s, val);
      return;
  }
  
  if (DEBUG_QT_WIDGETS && s != SLOT_INVALIDATE)
    debug_widgets << "qt_simple_widget_rep: sent " << slot_name (s)
    << "\t\tto widget\t" << type_as_string() << LF;
}

blackbox
qt_simple_widget_rep::query (slot s, int type_id) {
    // Some slots are too noisy
  if (DEBUG_QT_WIDGETS && (s != SLOT_IDENTIFIER))
    debug_widgets << "qt_simple_widget_rep: queried " << slot_name(s)
                  << "\t\tto widget\t" << type_as_string() << LF;
  
  switch (s) {
    case SLOT_IDENTIFIER:
    {
      if (qwid) {
        widget_rep* wid = qt_window_widget_rep::widget_from_qwidget(qwid);
        if (wid)
          return wid->query(s, type_id);
      }
      return close_box<int>(0);
    }
    case SLOT_INVALID:
    {
      return close_box<bool> (is_invalid());
    }
      
    case SLOT_POSITION:
    {
      check_type_id<coord2> (type_id, s);
        // HACK: mapTo() does not work as we expect on the Mac, so we manually
        // calculate the global screen cordinates and substract
      QPoint sg = scrollarea()->surface()->mapToGlobal (QPoint (0,0));
      QRect  wg = scrollarea()->window()->frameGeometry();
      sg.ry() -= wg.y();
      sg.rx() -= wg.x();
      return close_box<coord2> (from_qpoint (sg));
    }
      
    case SLOT_SIZE:
    {
      check_type_id<coord2> (type_id, s);
      return close_box<coord2> (from_qsize (canvas()->size()));
    }
      
    case SLOT_SCROLL_POSITION:
    {
      check_type_id<coord2> (type_id, s);
      return close_box<coord2> (from_qpoint (canvas()->origin()));
    }
      
    case SLOT_EXTENTS:
    {
      check_type_id<coord4> (type_id, s);
      return close_box<coord4> (from_qrect (canvas()->extents()));
    }
      
    case SLOT_VISIBLE_PART:
    {
      check_type_id<coord4> (type_id, s);
       if (scrollarea()) {
 	      QSize sz = scrollarea()->QAbstractScrollArea::viewport()->size();
 	      QPoint pos= scrollarea()->origin();
 	      return close_box<coord4> (from_qrect(QRect(pos, sz)));
      } else {
        return close_box<coord4>(coord4(0,0,0,0));
      }
    }
      
    default:
      return qt_widget_rep::query(s, type_id);
  }
}

bool
qt_widget_global_position (widget w, SI x, SI y, SI& gx, SI& gy) {
  if (is_nil (w)) return false;

  qt_simple_widget_rep* rep= concrete_simple_widget (w);
  if (rep == NULL || rep->canvas () == NULL ||
      rep->canvas ()->surface () == NULL)
    return false;

  QPoint content= to_qpoint (coord2 (x, y));
  QPoint local= content - rep->canvas ()->origin ();
  coord2 global= from_qpoint (rep->canvas ()->surface ()->mapToGlobal (local));
  gx= global.x1;
  gy= global.x2;
  return true;
}

widget
qt_simple_widget_rep::read (slot s, blackbox index) {
  if (DEBUG_QT_WIDGETS)
    debug_widgets << "qt_simple_widget_rep::read " << slot_name(s)
    << "\tWidget id: " << id << LF;
  
  switch (s) {
    case SLOT_WINDOW:
      check_type_void (index, s);
      return qt_window_widget_rep::widget_from_qwidget(qwid);
    default:
      return qt_widget_rep::read (s, index);
  }
}


/******************************************************************************
* Translation into QAction for insertion in menus (i.e. for buttons)
******************************************************************************/

// Prints the current contents of the canvas onto a QPixmap

QAction*
qt_simple_widget_rep::as_qaction () {
  QAction* a= new QTMAction (NULL);
  QIcon icon (new QTMImpressIconEngine (this));
  a->setIcon (icon);
  return a;
}

/******************************************************************************
 * Backing store management
 ******************************************************************************/

static QRect
physical_rect_to_logical_qrect (rectangle r, double pixel_ratio) {
  int x1= (int) floor (((double) r->x1) / pixel_ratio);
  int y1= (int) floor (((double) r->y1) / pixel_ratio);
  int x2= (int) ceil  (((double) r->x2) / pixel_ratio);
  int y2= (int) ceil  (((double) r->y2) / pixel_ratio);
  return QRect (x1, y1, max (0, x2 - x1), max (0, y2 - y1));
}

static bool
fractional_pixel_ratio (double pixel_ratio) {
  return fabs (pixel_ratio - floor (pixel_ratio + 0.5)) > 0.001;
}

void
qt_simple_widget_rep::invalidate_rect (int x1, int y1, int x2, int y2,
                                       bool widen_fractional_text) {
  // Because of accumulated rounding error on screen with a dpr > 1, 
  // we enlarge the invalid rect by a few pixels.
  // todo : the solution would be to use a float for the coordinates
  // and sizes in the whole code.
  qreal dpr = canvas()->devicePixelRatio();
  int padding = (int) ceil (dpr * 8.0);
  if (widen_fractional_text && fractional_pixel_ratio (dpr) &&
      canvas()->surface()) {
    // Centered and right-aligned paragraphs can shift the old glyph positions
    // horizontally when the text changes.  On fractional Wayland scales, a
    // narrow physical invalidation band may then repaint only the new glyphs,
    // leaving stale pixels from the old line position in the backing pixmap.
    QSize sz= canvas()->surface()->size();
    x1= 0;
    x2= (int) ceil (dpr * sz.width());
  }
  rectangle r = rectangle (x1-padding, y1-padding, x2+padding, y2+padding);
  invalid_regions = invalid_regions | rectangles (r);  
}

void
qt_simple_widget_rep::invalidate_all () {
  QSize sz = canvas()->surface()->size();
  // QPoint pt = QAbstractScrollArea::viewport()->pos();
  //cout << "invalidate all " << LF;
  invalid_regions = rectangles();
  double pixel_ratio = canvas()->surface()->devicePixelRatio();
  invalidate_rect (0, 0, pixel_ratio * sz.width(), pixel_ratio * sz.height());
}

bool
qt_simple_widget_rep::is_invalid () {
  return !is_nil (invalid_regions);
}

basic_renderer
qt_simple_widget_rep::get_renderer() {
  ASSERT (backingPixmap != NULL,
	  "internal error in qt_simple_widget_rep::get_renderer");
  qt_renderer_rep * ren = the_qt_renderer (device_pixel_ratio ());
  ren->begin ((void*) backingPixmap);
  return ren;
}

/*
 This function is called by the qt_gui::update method (via repaint_all) to keep
 the backing store in sync and propagate the changes to the surface on screen.
 First we check that the backing store geometry is right and then we
 request to the texmacs canvas widget to repaint the regions which were
 marked invalid. Subsequently, for each succesfully repainted region, we
 propagate its contents from the backing store to the onscreen surface.
 If repaint has been interrupted we do not propagate the changes and proceed
 to mark the region invalid again.
 */
void
qt_simple_widget_rep::repaint_invalid_regions () {
  double pixel_ratio= canvas()->surface()->devicePixelRatio();
  // complete redrawing whenever the pixel ratio changes
  QSize canvas_physical_size (pixel_ratio * canvas()->surface()->size());
  if (canvas_physical_size != backingPixmap->size() || backing_valid==false) {
    // cout << "repaint_invalid_regions after change of dpr" << LF;
    QPixmap newBackingPixmap (0, 0);
    long h= backingPixmap->height();
    backing_pos = h == 0 ? QPoint (0, 0)
      : (backing_pos * pixel_ratio * canvas_physical_size.height()) / h;
    backing_physical_pos= QPoint (qRound (pixel_ratio * backing_pos.x ()),
                                  qRound (pixel_ratio * backing_pos.y ()));
    *backingPixmap= newBackingPixmap;
  }
  // Look if the scroll position has changed. backing_pos is the old position, 
  // while origin is the new one. Instead of repainting the whole backing store,
  // we move the contents of the backing store, and invalidate the regions that
  // are not covered by the moved contents.
  QRegion qrgn;
  QPoint origin = canvas()->origin();
  if (backing_pos != origin && !(backingPixmap->size().isNull())) {
    QSize sz = backingPixmap->size();
    QSize surface_logical_size = canvas()->surface()->size();
    QRect full_surface_logical_rect (QPoint (0, 0), surface_logical_size);

    QPoint physical_origin (qRound (pixel_ratio * origin.x()),
                            qRound (pixel_ratio * origin.y()));
    int dx= physical_origin.x() - backing_physical_pos.x();
    int dy= physical_origin.y() - backing_physical_pos.y();
    backing_pos= origin;
    backing_physical_pos= physical_origin;

    rectangles invalid;
    while (!is_nil (invalid_regions)) {
      rectangle r= invalid_regions->item;
      invalid= rectangles (
        rectangle (r->x1-dx, r->y1-dy, r->x2-dx, r->y2-dy), invalid);
      invalid_regions= invalid_regions->next;
    }
    invalid_regions= invalid & rectangles (
      rectangle (0, 0, sz.width(), sz.height()));

    if (!backing_valid) invalidate_rect (0, 0, sz.width(), sz.height());
    else if (dx != 0 || dy != 0) {
      // QPixmap::scroll moves the reusable pixels in place and reports the
      // newly exposed physical strips.  Tracking the rounded absolute
      // physical origin, rather than rounding each logical delta, prevents
      // fractional-DPR error from accumulating over a smooth scroll.
      QRegion exposed;
      backingPixmap->scroll (-dx, -dy, backingPixmap->rect(), &exposed);
      for (const QRect& r: exposed)
        invalidate_rect (r.left(), r.top(), r.right() + 1, r.bottom() + 1,
                         false);
    }
    // we call update now to allow repainting of invalid regions
    // this cannot be done directly since interpose_handler needs
    // to be run at least once in some situations
    // (for example when scrolling is initiated by TeXmacs itself)
    //the_gui->update();
    //  QAbstractScrollArea::viewport()->scroll (-dx,-dy);
    // QAbstractScrollArea::viewport()->update();
    // QWidget repaint regions are in logical coordinates.  `sz` is the
    // physical backing-store size; adding it here makes the next paint event
    // interpret physical pixels as logical pixels on high-DPI Wayland.
    qrgn += full_surface_logical_rect;
  }
  
  // Check if the window has been resized. If so, we need to resize the backing
  // store as well. During the resize, the origin remain the same. So we can just
  // crop the backing store if the window is smaller, or fill the new regions with
  // the background color if the window is bigger.
  {
    QSize _oldSize = backingPixmap->size();
    QSize _new_logical_Size = canvas()->surface()->size();
    QSize _newSize = _new_logical_Size;
    _newSize *= pixel_ratio;
    //cout << "      surface size of " << _newSize.width() << " x "
    // << _newSize.height() << LF;
    
    if (_newSize != _oldSize) {
      // qDebug () << "RESIZING BITMAP to " << _newSize << LF;
      QPixmap newBackingPixmap (_newSize);
      QPainter p (&newBackingPixmap);
      p.drawPixmap (0,0,*backingPixmap);
      //p.fillRect (0, 0, _newSize.width(), _newSize.height(), Qt::red);
      if (_newSize.width() >= _oldSize.width()) {
        invalidate_rect (_oldSize.width(), 0, _newSize.width(), _newSize.height());
        p.fillRect (QRect (_oldSize.width(), 0, _newSize.width()-_oldSize.width(), _newSize.height()), Qt::gray);
      }
      if (_newSize.height() >= _oldSize.height()) {
        invalidate_rect (0,_oldSize.height(), _newSize.width(), _newSize.height());
        p.fillRect (QRect (0,_oldSize.height(), _newSize.width(), _newSize.height()-_oldSize.height()), Qt::gray);
      }
      p.end();
      *backingPixmap = newBackingPixmap;
    }
  }
  
  // repaint invalid rectangles
  bool repaint_interrupted= false;
  {
    rectangles new_regions;
    if (!is_nil (invalid_regions)) {
      rectangle lub= least_upper_bound (invalid_regions);
      if (area (lub) < 1.2 * area (invalid_regions))
        invalid_regions= rectangles (lub);
      
      basic_renderer_rep* ren = get_renderer();
      
      coord2 pt_or = from_qpoint(backing_pos);
      SI ox = -pt_or.x1;
      SI oy = -pt_or.x2;
      
      rectangles rects = invalid_regions;
      invalid_regions = rectangles();

      while (!is_nil (rects)) {
        rectangle r = copy (rects->item);
        rectangle r0 = rects->item;
	QRect qr = physical_rect_to_logical_qrect (r, pixel_ratio);
        //cout << "repainting " << r0 << "\n";
        ren->set_origin (ox, oy);
        ren->encode (r->x1, r->y1);
        ren->encode (r->x2, r->y2);
        ren->set_clipping (r->x1, r->y2, r->x2, r->y1);
        handle_repaint (ren, r->x1, r->y2, r->x2, r->y1);
        if (gui_interrupted ()) {
          //cout << "interrupted repainting of  " << r0 << "\n";
          //ren->set_pencil (green);
          //ren->line (r->x1, r->y1, r->x2, r->y2);
          //ren->line (r->x1, r->y2, r->x2, r->y1);
          invalidate_rect (r0->x1, r0->y1, r0->x2, r0->y2);
          repaint_interrupted= true;
        }
        else {
          qrgn += qr;
        }
        rects = rects->next;
      }
      ren->end();
    } // !is_nil (invalid_regions)
  }
  
  // propagate immediately the changes to the screen
  bool painted= !qrgn.isEmpty () && !repaint_interrupted;
  if (painted) {
    canvas()->surface()->repaint (qrgn);
    backing_valid= true;
    canvas()->finishGestureZoomCommitPreview ();
  }
  if (!repaint_interrupted) handle_post_repaint (painted);
}

hashset<pointer> qt_simple_widget_rep::all_widgets;

void
qt_simple_widget_rep::repaint_all () {
  iterator<pointer> i = iterate(qt_simple_widget_rep::all_widgets);
  while (i->busy()) {
    qt_simple_widget_rep *w = static_cast<qt_simple_widget_rep*>(i->next());
    if (w->canvas() && w->canvas()->isVisible()) w->repaint_invalid_regions();
  }
}
