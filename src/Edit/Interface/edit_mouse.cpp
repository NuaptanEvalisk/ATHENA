
/******************************************************************************
* MODULE     : edit_mouse.cpp
* DESCRIPTION: Mouse handling
* COPYRIGHT  : (C) 1999  Joris van der Hoeven
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "edit_interface.hpp"
#include "Interface/selection_autoscroll.hpp"
#include "tm_buffer.hpp"
#include "tm_timer.hpp"
#include "link.hpp"
#include "analyze.hpp"
#include "drd_mode.hpp"
#include "message.hpp"
#include "scheme.hpp"
#include "tree_select.hpp"
#include "window.hpp"

  // These are tm-defined in graphics-utils.scm (looks like they shouldn't)
#define ShiftMask     256
#define LockMask      512
#define ControlMask  1024
#define Mod1Mask     2048
#define Mod2Mask     4096
#define Mod3Mask     8192
#define Mod4Mask    16384
#define Mod5Mask    32768

void disable_double_clicks ();

static bool
unix_primary_selection_disabled () {
  return get_preference ("disable unix primary selection", "off") == "on";
}

static SI
selection_hit_x (box eb, SI x) {
  return selection_hit_test_x (x, eb->x1, eb->x2);
}

static const int IMAGE_RESIZE_NONE   = 0;
static const int IMAGE_RESIZE_RIGHT  = 1;
static const int IMAGE_RESIZE_BOTTOM = 2;
static const int IMAGE_RESIZE_CORNER = 3;
static const int TABLE_RESIZE_NONE   = 0;
static const int TABLE_RESIZE_COLUMN = 1;
static const int TABLE_RESIZE_ROW    = 2;

/******************************************************************************
* Routines for the mouse
******************************************************************************/

static bool
is_image_tree (tree t) {
  return is_func (t, IMAGE, 5);
}

static bool
inside_rect (SI x, SI y, rectangle r) {
  return x >= r->x1 && x <= r->x2 && y >= r->y1 && y <= r->y2;
}

static SI
abs_si (SI x) {
  return x >= 0? x: -x;
}

static void
extend_with_candidate (array<path>& ps, path p) {
  while (!is_nil (p)) {
    ps << p;
    p= path_up (p);
  }
}

bool
edit_interface_rep::selected_image_path (path& p) {
  array<path> ps;
  path p1, p2;
  get_selection (p1, p2);
  extend_with_candidate (ps, p1);
  extend_with_candidate (ps, p2);
  extend_with_candidate (ps, tp);

  for (int i=0; i<N(ps); i++) {
    path q= ps[i];
    if (rp <= q && has_subtree (et, q) && is_image_tree (subtree (et, q))) {
      p= q;
      return true;
    }
  }
  return false;
}

bool
edit_interface_rep::image_bounds (path p, rectangle& r) {
  if (!(rp <= p) || !has_subtree (et, p) || !is_image_tree (subtree (et, p)))
    return false;
  selection sel= eb->find_check_selection (start (et, p), end (et, p));
  if (!sel->valid || is_nil (sel->rs)) return false;
  r= least_upper_bound (sel->rs);
  return r != rectangle (0, 0, 0, 0);
}

rectangles
edit_interface_rep::image_resize_handles (rectangle r) {
  SI s= 5 * pixel;
  SI mx= (r->x1 + r->x2) / 2;
  SI my= (r->y1 + r->y2) / 2;
  rectangle right  (r->x2 - s, my - s, r->x2 + s, my + s);
  rectangle bottom (mx - s, r->y1 - s, mx + s, r->y1 + s);
  rectangle corner (r->x2 - s, r->y1 - s, r->x2 + s, r->y1 + s);
  return rectangles (right, rectangles (bottom, rectangles (corner)));
}

int
edit_interface_rep::image_resize_handle_at (SI x, SI y,
                                            rectangle& r, path& p) {
  if (!selected_image_path (p) || !image_bounds (p, r)) return IMAGE_RESIZE_NONE;

  SI s= 7 * pixel;
  SI mx= (r->x1 + r->x2) / 2;
  SI my= (r->y1 + r->y2) / 2;
  rectangle corner (r->x2 - s, r->y1 - s, r->x2 + s, r->y1 + s);
  rectangle right  (r->x2 - s, my - s, r->x2 + s, my + s);
  rectangle bottom (mx - s, r->y1 - s, mx + s, r->y1 + s);

  if (inside_rect (x, y, corner)) return IMAGE_RESIZE_CORNER;
  if (inside_rect (x, y, right)) return IMAGE_RESIZE_RIGHT;
  if (inside_rect (x, y, bottom)) return IMAGE_RESIZE_BOTTOM;
  return IMAGE_RESIZE_NONE;
}

bool
edit_interface_rep::image_resize_start (SI x, SI y) {
  if (inside_graphics ()) return false;
  rectangle r;
  path p;
  int handle= image_resize_handle_at (x, y, r, p);
  if (handle == IMAGE_RESIZE_NONE) return false;

  image_resize_active= true;
  image_resize_handle= handle;
  image_resize_path  = p;
  image_resize_start_x= x;
  image_resize_start_y= y;
  image_resize_x1= r->x1; image_resize_y1= r->y1;
  image_resize_x2= r->x2; image_resize_y2= r->y2;
  image_resize_rects= image_resize_handles (r);
  send_mouse_grab (this, true);
  return true;
}

bool
edit_interface_rep::image_resize_update (SI x, SI y) {
  if (!image_resize_active) return false;
  path p= image_resize_path;
  if (!(rp <= p) || !has_subtree (et, p) || !is_image_tree (subtree (et, p))) {
    image_resize_finish ();
    return true;
  }

  if (!is_nil (image_resize_rects)) invalidate (image_resize_rects);

  SI min_w= max (as_length ("8px"), (SI) 1);
  SI max_w= max (as_length ("4096px"), min_w);
  SI old_w= max (abs_si (image_resize_x2 - image_resize_x1), (SI) 1);
  SI old_h= max (abs_si (image_resize_y2 - image_resize_y1), (SI) 1);
  SI left = min (image_resize_x1, image_resize_x2);
  SI top  = max (image_resize_y1, image_resize_y2);
  SI new_w= old_w;
  SI new_h= old_h;
  if (image_resize_handle == IMAGE_RESIZE_RIGHT ||
      image_resize_handle == IMAGE_RESIZE_CORNER)
    new_w= max (abs_si (x - left), min_w);
  if (image_resize_handle == IMAGE_RESIZE_BOTTOM ||
      image_resize_handle == IMAGE_RESIZE_CORNER)
    new_h= max (abs_si (top - y), min_w);

  double sx= ((double) new_w) / ((double) old_w);
  double sy= ((double) new_h) / ((double) old_h);
  double scale= sx;
  if (image_resize_handle == IMAGE_RESIZE_BOTTOM) scale= sy;
  else if (image_resize_handle == IMAGE_RESIZE_CORNER)
    scale= (sx >= 1.0 && sy >= 1.0)? min (sx, sy): max (sx, sy);

  SI final_w= (SI) tm_round (((double) old_w) * scale);
  final_w= min (max (final_w, min_w), max_w);
  SI px_len= (SI) tm_round (((double) max (as_length ("1px"), (SI) 1)) *
                            zoomf);
  px_len= max (px_len, (SI) 1);
  int width_px= (int) max ((SI) 1, (SI) tm_round (((double) final_w) /
                                                   ((double) px_len)));
  string width= as_string (width_px) * "px";

  tree t= subtree (et, p);
  tree nt= tree (IMAGE, copy (t[0]), tree (width), tree (""),
                 copy (t[3]), copy (t[4]));
  assign (p, nt);
  set_selection (start (et, p), end (et, p));

  rectangle r;
  if (image_bounds (p, r)) image_resize_rects= image_resize_handles (r);
  else image_resize_rects= rectangles ();
  if (!is_nil (image_resize_rects)) invalidate (image_resize_rects);
  notify_change (THE_SELECTION + THE_DECORATIONS);
  return true;
}

void
edit_interface_rep::image_resize_finish () {
  if (!image_resize_active) return;
  image_resize_active= false;
  image_resize_handle= IMAGE_RESIZE_NONE;
  if (!is_nil (image_resize_rects)) invalidate (image_resize_rects);
  image_resize_rects= rectangles ();
  send_mouse_grab (this, false);
  notify_change (THE_DECORATIONS);
}

bool
edit_interface_rep::table_cell_bounds (path fp, int row, int column,
                                       rectangle& r) {
  if (!(rp <= fp) || !has_subtree (et, fp)) return false;
  tree format= subtree (et, fp);
  if (!is_func (format, TFORMAT) && !is_func (format, TABLE)) return false;
  int rows, columns;
  table_get_extents (fp, rows, columns);
  if (row < 0 || row >= rows || column < 0 || column >= columns) return false;
  path cp= fp * ::table_search_cell (format, row, column);
  selection sel= eb->find_check_selection (cp * 0, cp * 1);
  if (is_nil (sel->rs)) return false;
  r= least_upper_bound (sel->rs);
  return r != rectangle (0, 0, 0, 0);
}

int
edit_interface_rep::table_resize_handle_at (SI x, SI y, path& best_fp,
                                            int& best_row, int& best_column,
                                            rectangle& best_rect) {
  if (inside_graphics ()) return TABLE_RESIZE_NONE;
  SI threshold= 6 * pixel;
  SI probe_x[5]= {x, x - threshold, x + threshold, x, x};
  SI probe_y[5]= {y, y, y, y - threshold, y + threshold};
  SI best_distance= threshold + 1;
  int best_handle= TABLE_RESIZE_NONE;

  for (int probe=0; probe<5; probe++) {
    path hit= tree_path (path (), probe_x[probe], probe_y[probe], 0);
    if (!(rp <= hit)) continue;
    path structural= path_up (hit);
    if (!(rp <= structural) || !has_subtree (et, structural)) continue;
    path fp= ::table_search_format (et, structural);
    if (is_nil (fp) || !(rp <= fp) || !has_subtree (et, fp)) continue;
    tree format= subtree (et, fp);
    if (!is_func (format, TFORMAT) && !is_func (format, TABLE)) continue;

    int row, column, rows, columns;
    ::table_search_coordinates (format, tail (structural, N(fp)), row, column);
    table_get_extents (fp, rows, columns);
    if (row < 0 || row >= rows || column < 0 || column >= columns) continue;

    rectangle cell;
    if (!table_cell_bounds (fp, row, column, cell)) continue;
    auto consider= [&] (int handle, int target_row, int target_column,
                         rectangle target, SI distance) {
      if (distance > threshold || distance >= best_distance) return;
      best_distance= distance;
      best_handle= handle;
      best_fp= fp;
      best_row= target_row;
      best_column= target_column;
      best_rect= target;
    };

    if (y >= cell->y1 - threshold && y <= cell->y2 + threshold) {
      consider (TABLE_RESIZE_COLUMN, row, column, cell,
                abs_si (x - cell->x2));
      if (column > 0) {
        rectangle previous;
        if (table_cell_bounds (fp, row, column - 1, previous))
          consider (TABLE_RESIZE_COLUMN, row, column - 1, previous,
                    abs_si (x - cell->x1));
      }
    }
    if (x >= cell->x1 - threshold && x <= cell->x2 + threshold) {
      consider (TABLE_RESIZE_ROW, row, column, cell,
                abs_si (y - cell->y1));
      if (row > 0) {
        rectangle previous;
        if (table_cell_bounds (fp, row - 1, column, previous))
          consider (TABLE_RESIZE_ROW, row - 1, column, previous,
                    abs_si (y - cell->y2));
      }
    }
  }
  return best_handle;
}

bool
edit_interface_rep::table_resize_start (SI x, SI y) {
  path fp;
  int row= 0, column= 0;
  rectangle r;
  int handle= table_resize_handle_at (x, y, fp, row, column, r);
  if (handle == TABLE_RESIZE_NONE) return false;

  if (is_func (subtree (et, fp), TABLE)) insert_node (fp * 0, TFORMAT);
  table_resize_active= true;
  table_resize_handle= handle;
  table_resize_format_path= fp;
  table_resize_row= row;
  table_resize_column= column;
  table_resize_start_x= x;
  table_resize_start_y= y;
  table_resize_initial_size= handle == TABLE_RESIZE_COLUMN ?
    abs_si (r->x2 - r->x1) : abs_si (r->y2 - r->y1);
  send_mouse_grab (this, true);
  return true;
}

bool
edit_interface_rep::table_resize_update (SI x, SI y) {
  if (!table_resize_active) return false;
  path fp= table_resize_format_path;
  if (!(rp <= fp) || !has_subtree (et, fp) ||
      (!is_func (subtree (et, fp), TFORMAT) &&
       !is_func (subtree (et, fp), TABLE))) {
    table_resize_finish ();
    return true;
  }

  SI delta= table_resize_handle == TABLE_RESIZE_COLUMN ?
    x - table_resize_start_x : table_resize_start_y - y;
  SI min_size= max (as_length ("16px"), (SI) 1);
  SI final_size= max (table_resize_initial_size + delta, min_size);
  SI px_len= (SI) tm_round (((double) max (as_length ("1px"), (SI) 1)) *
                            zoomf);
  px_len= max (px_len, (SI) 1);
  int size_px= (int) max ((SI) 1,
    (SI) tm_round (((double) final_size) / ((double) px_len)));
  tree value (as_string (size_px) * "px");

  if (table_resize_handle == TABLE_RESIZE_COLUMN) {
    table_set_format (fp, 1, table_resize_column + 1, -1,
                      table_resize_column + 1, CELL_WIDTH, value);
    table_set_format (fp, 1, table_resize_column + 1, -1,
                      table_resize_column + 1, CELL_HMODE, tree ("exact"));
  }
  else {
    table_set_format (fp, table_resize_row + 1, 1,
                      table_resize_row + 1, -1, CELL_HEIGHT, value);
    table_set_format (fp, table_resize_row + 1, 1,
                      table_resize_row + 1, -1, CELL_VMODE, tree ("exact"));
  }

  path table= search_table (fp);
  if (!is_nil (table))
    call ("table-resize-notify", object (subtree (et, table)));
  notify_change (THE_TREE + THE_ENVIRONMENT);
  return true;
}

void
edit_interface_rep::table_resize_finish () {
  if (!table_resize_active) return;
  table_resize_active= false;
  table_resize_handle= TABLE_RESIZE_NONE;
  table_resize_format_path= path ();
  send_mouse_grab (this, false);
}

bool
edit_interface_rep::mouse_message (string message, SI x, SI y) {
  rectangles rs;
  tree r= eb->message (message, x, y, rs);
  if (N(rs) != 0) invalidate (rs);
  if (is_tuple (r, "direct-link", 1) && is_atomic (r[1])) {
    call ("go-to-url", object (r[1]->label));
    return true;
  }
  return r != "";
}

color
edit_interface_rep::mouse_clickable_color () {
  path sp= find_innermost_scroll (eb, tp);
  path p= tree_path (sp, last_x, last_y, 0);
  tree t= "#20A060";
  if (rp <= p) t= get_env_value (CLICKABLE_COLOR, p);
  if (!is_atomic (t)) t= "#20A060";
  return named_color (t->label);
}

void
edit_interface_rep::mouse_click (SI x, SI y) {
  if (mouse_message ("click", x, y)) return;
  start_x= x;
  start_y= y;
  send_mouse_grab (this, true);
}

bool
edit_interface_rep::mouse_extra_click (SI x, SI y) {
  go_to (x, y);
  if (mouse_message ("double-click", x, y)) return true;
  if (!is_nil (mouse_ids)) {
    call ("link-follow-ids", object (mouse_ids), object ("double-click"));
    return true;
  }
  go_to (x, y);
  path p1, p2;
  get_selection (p1, p2);
  if ((p1==p2) || path_less (tp, p1) || path_less (p2, tp)) select (tp, tp);
  select_enlarge ();
  if (selection_active_any ())
    if (!unix_primary_selection_disabled ())
      selection_set ("mouse", selection_get (), true);
  return false;
}

void
edit_interface_rep::mouse_adjust_selection (SI x, SI y, int mods) {
  if (inside_graphics () || mods <=1) return;
  if (mouse_message ("drag", x, y)) return;
  SI hit_x= selection_hit_x (eb, x);
  go_to (hit_x, y);
  end_x= x;
  end_y= y;
  path sp= find_innermost_scroll (eb, tp);
  path p1= tree_path (sp, selection_hit_x (eb, start_x), start_y, 0);
  path p2= tree_path (sp, selection_hit_x (eb, end_x), end_y, 0);
  path p3= tree_path (sp, hit_x, y, 0);
  
  bool p1_p2= path_inf (p1, p2);
  bool p1_p3= path_inf (p1, p3);
  bool p2_p3= path_inf (p2, p3);
  
  if (mods & ShiftMask) { // Holding shift: enlarge in direction start_ -> end_
    if (!p1_p2 && p1_p3) { // p2<p1<p3
      start_x= end_x;
      start_y= end_y;
      end_x  = x;
      end_y  = y;
      p1     = p2;
      p2     = p3;
    } else if (!p1_p3 && p1_p2) {  // p3<p1<p2
      start_x= end_x;
      start_y= end_y;
      end_x  = x;
      end_y  = y;
      p1     = p3;
    } else if ((p2_p3 && !p1_p3) || (!p1_p2 && !p2_p3)) {  // p2<p3<p1, p3<p2<p1
      end_x= x;
      end_y= y;
      p2   = p1;
      p1   = p3;
    } else if ((p1_p2 && p2_p3) || (p1_p3 && !p2_p3)) {  // p1<p2<p3, p1<p3<p2
      end_x= x;
      end_y= y;
      p2   = p3;
    }
    selection_visible ();
    set_selection (p1, p2);
    notify_change (THE_SELECTION);
    if (!unix_primary_selection_disabled ())
      selection_set ("mouse", selection_get (), true);
  }
}

void
edit_interface_rep::mouse_drag (SI x, SI y) {
  if (inside_graphics ()) return;
  if (mouse_message ("drag", x, y)) return;
  go_to (selection_hit_x (eb, x), y);
  end_x  = x;
  end_y  = y;
  selection_visible ();
  path sp= find_innermost_scroll (eb, tp);
  path p1= tree_path (sp, selection_hit_x (eb, start_x), start_y, 0);
  path p2= tree_path (sp, selection_hit_x (eb, end_x), end_y, 0);
  if (path_inf (p2, p1)) {
    path temp= p1;
    p1= p2;
    p2= temp;
  }
  set_selection (p1, p2);
  notify_change (THE_SELECTION);
}

void
edit_interface_rep::mouse_select (SI x, SI y, int mods, bool drag) {
  if (mouse_message ("select" , x, y)) return;
  if (!is_nil (mouse_ids) && (mods & (ShiftMask+Mod2Mask)) == 0 && !drag) {
    if (!as_bool (call ("link-has-cardlink?", object (mouse_ids)))) {
      call ("link-follow-ids", object (mouse_ids), object ("click"));
      disable_double_clicks ();
      return;
    }
  }
  tree g;
  bool b0= inside_graphics (false);
  bool b= inside_graphics ();
  if (b) g= get_graphics ();
  SI hit_x= selection_hit_x (eb, x);
  go_to (hit_x, y);
  if ((!b0 && inside_graphics (false)) || (b0 && !inside_graphics (false)))
    drag= false;
  if (!b && inside_graphics ())
    eval ("(graphics-reset-context 'begin)");
  tree g2= get_graphics ();
  if (b && (!inside_graphics () || obtain_ip (g) != obtain_ip (g2))) {
    invalidate_graphical_object ();
    eval ("(graphics-reset-context 'exit)");
  }
  if (!drag) {
    path sp= find_innermost_scroll (eb, tp);
    path p0= tree_path (sp, hit_x, y, 0);
    set_selection (p0, p0);
    notify_change (THE_SELECTION);
  }
  if (selection_active_any ())
    if (!unix_primary_selection_disabled ())
      selection_set ("mouse", selection_get (), true);
}

void
edit_interface_rep::mouse_paste (SI x, SI y) { (void) x; (void) y;
  if (unix_primary_selection_disabled ()) return;
  if (mouse_message ("paste", x, y)) return;
  go_to (x, y);
  selection_paste ("mouse");
}

void
edit_interface_rep::mouse_adjust (SI x, SI y, int mods) {
  if (mouse_message ("adjust", x, y)) return;
  x= (SI) (x * magf);
  y= (SI) (y * magf);
  abs_round (x, y);
  if (is_nil (popup_win)) {
    widget wid;
    string menu= "texmacs-popup-menu";
    if ((mods & (ShiftMask + ControlMask)) != 0)
      menu= "texmacs-alternative-popup-menu";
    SERVER (menu_widget ("(vertical (link " * menu * "))", wid));
    widget popup_wid= ::popup_widget (wid);
    popup_win= ::popup_window_widget (popup_wid, "Popup menu");
#if defined (QTTEXMACS)
    SI px, py;
    if (qt_widget_global_position (this, x, y, px, py)) {
      set_position (popup_win, px, py);
    }
    else {
      SI wx, wy, ox, oy, sx, sy;
      ::get_position (get_window (this), wx, wy);
      get_position (this, ox, oy);
      get_scroll_position (this, sx, sy);
      ox -= sx; oy -= sy;
      set_position (popup_win, wx+ ox+ x, wy+ oy+ y);
    }
#endif
    set_visibility (popup_win, true);
    send_keyboard_focus (this);
    send_mouse_grab (popup_wid, true);
  }
}

void
edit_interface_rep::mouse_scroll (SI x, SI y, bool up) {
  string message= up? string ("scroll up"): string ("scroll down");
  if (mouse_message (message, x, y)) return;
  SI dy= 100*PIXEL;
  if (!up) dy= -dy;
  path sp= find_innermost_scroll (eb, tp);
  if (is_nil (sp)) {
    SERVER (scroll_where (x, y));
    y += dy;
    SERVER (scroll_to (x, y));
  }
  else {
    SI x, y, sx, sy;
    rectangle outer, inner;
    find_canvas_info (eb, sp, x, y, sx, sy, outer, inner);
    SI ty= inner->y2 - inner->y1;
    SI cy= outer->y2 - outer->y1;
    if (ty > cy) {
      tree   old_yt= eb[path_up (sp)]->get_info ("scroll-y");
      string old_ys= as_string (old_yt);
      double old_p = 0.0;
      if (ends (old_ys, "%")) old_p= as_double (old_ys (0, N(old_ys)-1));
      double new_p= old_p + 100.0 * ((double) dy) / ((double) (ty - cy));
      new_p= max (min (new_p, 100.0), 0.0);
      tree new_yt= as_string (new_p) * "%";
      if (new_yt != old_yt && is_accessible (obtain_ip (old_yt))) {
        object fun= symbol_object ("tree-set");
        object cmd= list_object (fun, old_yt, new_yt);
        exec_delayed (scheme_cmd (cmd));
        temp_invalid_cursor= true;
      }
    }
  }
}

/******************************************************************************
* getting the cursor (both for text and graphics)
******************************************************************************/

cursor
edit_interface_rep::get_cursor () {
  if (inside_graphics ()) {
    frame f= find_frame ();
    if (!is_nil (f)) {
      point p= f [point (last_x, last_y)];
      p= f (adjust (p));
      SI x= (SI) p[0];
      SI y= (SI) p[1];
      return cursor (x, y, 0, -5*pixel, 5*pixel, 1.0);
    }
  }
  return copy (the_cursor ());
}

array<SI>
edit_interface_rep::get_mouse_position () {
  rectangle wr= get_window_extents ();
  SI sz= get_pixel_size ();
  double sf= ((double) sz) / 256.0;
  SI mx= ((SI) (last_x / sf)) + wr->x1;
  SI my= ((SI) (last_y / sf)) + wr->y2;
  return array<SI> (mx, my);
}

void
edit_interface_rep::set_pointer (string name) {
  send_mouse_pointer (this, name);
}

void
edit_interface_rep::set_pointer (
  string curs_name, string mask_name)
{
  send_mouse_pointer (this, curs_name, mask_name);
}

/******************************************************************************
* Active loci
******************************************************************************/

void
edit_interface_rep::update_mouse_loci () {
  if (is_nil (eb)) {
    locus_new_rects= rectangles ();
    mouse_ids= list<string> ();
    return;
  }

#ifdef USE_EXCEPTIONS
  try {
#endif
  int old_mode= set_access_mode (DRD_ACCESS_SOURCE);
  path cp= path_up (tree_path (path (), last_x, last_y, 0));
  set_access_mode (old_mode);
  tree mt= subtree (et, cp);
  path p = cp;
  list<string> ids1, ids2;
  rectangles rs1, rs2;
  eb->loci (last_x, last_y, 0, ids1, rs1);
  while (rp <= p) {
    ids2 << get_ids (subtree (et, p));
    p= path_up (p);
  }

  locus_new_rects= rectangles ();
  mouse_ids= list<string> ();
  if (!is_nil (ids1 * ids2) && !has_changed (THE_FOCUS)) {
    ids1= as_list_string (call ("link-mouse-ids", object (ids1)));
    ids2= as_list_string (call ("link-mouse-ids", object (ids2)));
    list<tree> l= as_list_tree (call ("link-active-upwards", object (mt)));
    while (!is_nil (l)) {
      tree lt= l->item;
      path lp= reverse (obtain_ip (lt));
      selection sel= eb->find_check_selection (lp * start(lt), lp * end(lt));
      rs2 << outlines (sel->rs, pixel);
      l= l->next;
    }
    ids1= as_list_string (call ("link-active-ids", object (ids1)));
    ids2= as_list_string (call ("link-active-ids", object (ids2)));
    if (is_nil (ids1)) rs1= rectangles ();
    if (is_nil (ids2)) rs2= rectangles ();
    // FIXME: we should keep track which id corresponds to which rectangle
    if (!is_nil (ids1 * ids2)) {
      locus_new_rects= rs1 * rs2;
      mouse_ids= ids1 * ids2;
    }
  }
  if (locus_new_rects != locus_rects) notify_change (THE_LOCUS);
#ifdef USE_EXCEPTIONS
  }
  catch (string msg) {}
  handle_exceptions ();
#endif
}

void
edit_interface_rep::update_focus_loci () {
  path p= path_up (tp);
  list<string> ids;
  while (rp <= p) {
    ids << get_ids (subtree (et, p));
    p= path_up (p);
  }
  focus_ids= list<string> ();
  if (!is_nil (ids) && !has_changed (THE_FOCUS)) {
    ids= as_list_string (call ("link-active-ids", object (ids)));
    focus_ids= ids;
  }
}

/******************************************************************************
* drag and double click detection for left button
******************************************************************************/

static void*  left_handle  = NULL;
static bool   left_started = false;
static bool   left_dragging= false;
static SI     left_x= 0;
static SI     left_y= 0;
static time_t left_last= 0;
static int    double_click_delay= 500;

void
drag_left_reset () {
  left_started = false;
  left_dragging= false;
  left_x       = 0;
  left_y       = 0;
}

void
disable_double_clicks () {
  left_last -= (double_click_delay + 1);
}

static string
detect_left_drag (void* handle, string type, SI x, SI y, time_t t,
                  int m, SI d) {
  if (left_handle != handle) drag_left_reset ();
  left_handle= handle;
  if (left_dragging && type == "move" && (m&1) == 0)
    type= "release-left";
  if (type == "press-left") {
    left_dragging= true;
    left_started = true;
    left_x       = x;
    left_y       = y;
  }
  else if (type == "move") {
    if (left_started) {
      if (norm (point (x - left_x, y - left_y)) < d) return "wait-left";
      left_started= false;
      return "start-drag-left";
    }
    if (left_dragging) return "dragging-left";
  }
  else if (type == "release-left") {
    if (left_started) drag_left_reset ();
    if (left_dragging) {
      drag_left_reset ();
      return "end-drag-left";
    }
    if ((t >= left_last) && ((t - left_last) <= double_click_delay)) {
      left_last= t;
      return "double-left";
    }
    left_last= t;
  }
  return type;
}

/******************************************************************************
* drag and double click detection for right button
******************************************************************************/

static void*  right_handle  = NULL;
static bool   right_started = false;
static bool   right_dragging= false;
static SI     right_x= 0;
static SI     right_y= 0;
static time_t right_last= 0;

void
drag_right_reset () {
  right_started = false;
  right_dragging= false;
  right_x       = 0;
  right_y       = 0;
  right_last    = 0;
}

static string
detect_right_drag (void* handle, string type, SI x, SI y, time_t t,
                   int m, SI d) {
  if (right_handle != handle) drag_right_reset ();
  right_handle= handle;
  if (right_dragging && type == "move" && (m&4) == 0)
    type= "release-right";
  if (type == "press-right") {
    right_dragging= true;
    right_started = true;
    right_x       = x;
    right_y       = y;
  }
  else if (type == "move") {
    if (right_started) {
      if (norm (point (x - right_x, y - right_y)) < d) return "wait-right";
      right_started= false;
      return "start-drag-right";
    }
    if (right_dragging) return "dragging-right";
  }
  else if (type == "release-right") {
    if (right_started) drag_right_reset ();
    if (right_dragging) {
      drag_right_reset ();
      return "end-drag-right";
    }
    if ((t >= right_last) && ((t - right_last) <= 500)) {
      right_last= t;
      return "double-right";
    }
    right_last= t;
  }
  return type;
}

/******************************************************************************
* dispatching
******************************************************************************/

void
edit_interface_rep::mouse_any (string type, SI x, SI y, int mods, time_t t,
                               array<double> data) {
  //cout << "Mouse any " << type << ", " << x << ", " << y << "; " << mods << ", " << t << ", " << data << "\n";
  if (is_nil (eb)) return;
  if (t < last_t && (last_x != 0 || last_y != 0 || last_t != 0)) {
    //cout << "Ignored " << type << ", " << x << ", " << y << "; " << mods << ", " << t << "\n";
    return;
  }
  if (t > last_event) last_event= t;
  if (type == "move" && (mods & 31) != 0)
    shake_detector.reset ();
  else if (type == "move") {
    SI shake_x= x;
    SI shake_y= y;
    SI shake_pixel= pixel;
    if (N(data) >= 2) {
      shake_x= (SI) tm_round (data[0]);
      shake_y= (SI) tm_round (data[1]);
      shake_pixel= 1;
    }
    if (shake_detector.update (shake_x, shake_y, t, shake_pixel)) {
      tremble_count= min (max (tremble_count, 3) + 4, 28);
      env_change = env_change | (THE_CURSOR + THE_FREEZE);
      last_change= texmacs_time ();
    }
  }

  bool found_flag= false;
  path old_p= eb->find_box_path (last_x, last_y, 0, false, found_flag);
  found_flag= false;
  path new_p= eb->find_box_path (x, y, 0, false, found_flag);
  if (path_up (old_p) != path_up (new_p)) {
    mouse_message ("leave", last_x, last_y);
    mouse_message ("enter", x, y);
  }

  if (!starts (type, "swipe-") && !starts (type, "pinch-") &&
      type != "scale" && type != "rotate" && type != "wheel") {
    last_x= x;
    last_y= y;
    last_t= t;
  }

  heading_cell_bracket heading_bracket;
  bool over_heading_bracket=
    type != "leave" && heading_cell_bracket_at (x, y, heading_bracket);
  if (type == "move" || type == "leave") {
    path hovered= over_heading_bracket ? heading_bracket.heading_path : path ();
    if (hovered != heading_cell_hovered) {
      heading_cell_hovered= hovered;
      notify_change (THE_DECORATIONS);
    }
  }

  bool move_like=
    (type == "move" || type == "dragging-left" || type == "dragging-right");
  if ((!move_like) || (is_attached (this) && !check_event (MOTION_EVENT)))
    update_mouse_loci ();
  if (!is_nil (mouse_ids) && type == "move") {
    notify_change (THE_FREEZE);
    // NOTE: this notification is needed to prevent the window to scroll to
    // the current cursor position when hovering over the locus
    // but a cleaner solution would be welcome
    call ("link-follow-ids", object (mouse_ids), object ("mouse-over"));
  }
  if (type == "move") {
    mouse_message ("move", x, y);
    rectangle r;
    path p;
    int handle= image_resize_handle_at (x, y, r, p);
    if (handle == IMAGE_RESIZE_CORNER) set_pointer ("XC_bottom_right_corner");
    else if (handle == IMAGE_RESIZE_RIGHT) set_pointer ("XC_right_side");
    else if (handle == IMAGE_RESIZE_BOTTOM) set_pointer ("XC_bottom_side");
    else {
      int row, column;
      handle= table_resize_handle_at (x, y, p, row, column, r);
      if (handle == TABLE_RESIZE_COLUMN)
        set_pointer ("XC_sb_h_double_arrow");
      else if (handle == TABLE_RESIZE_ROW)
        set_pointer ("XC_sb_v_double_arrow");
    }
    if (over_heading_bracket) set_pointer ("XC_hand2");
  }

  if (type == "leave")
    set_pointer ("XC_top_left_arrow");
  if ((!move_like) && (type != "enter") && (type != "leave"))
    set_input_normal ();
  if (!is_nil (popup_win) && (type != "leave")) {
    set_visibility (popup_win, false);
    destroy_window_widget (popup_win);
    popup_win= widget ();
  }

  if (starts (type, "swipe-")) eval ("(" * type * ")");
  if (type == "pinch-start") eval ("(pinch-start)");
  if (type == "pinch-end") eval ("(pinch-end)");
  if (type == "scale") eval ("(pinch-scale " * as_string (data[0]) * ")");
  if (type == "rotate") eval ("(pinch-rotate " * as_string (-data[0]) * ")");

  if (type == "double-left" && over_heading_bracket) {
    select_heading_cell (heading_bracket);
    heading_cell_pressed= path ();
    send_mouse_grab (this, false);
    drag_left_reset ();
    heading_fold_toggle_at (as_string (heading_bracket.heading_path));
    return;
  }
  if ((type == "press-left" || type == "start-drag-left") &&
      over_heading_bracket) {
    heading_cell_pressed= heading_bracket.heading_path;
    select_heading_cell (heading_bracket);
    send_mouse_grab (this, true);
    return;
  }
  if (!is_nil (heading_cell_pressed) && type == "dragging-left") return;
  if (!is_nil (heading_cell_pressed) &&
      (type == "release-left" || type == "end-drag-left")) {
    heading_cell_pressed= path ();
    send_mouse_grab (this, false);
    return;
  }

  if ((type == "press-left" || type == "start-drag-left") &&
      mouse_message ("click", x, y)) {
    start_x= x;
    start_y= y;
    send_mouse_grab (this, true);
    return;
  }
  if (type == "dragging-left" && mouse_message ("drag", x, y)) return;
  if ((type == "release-left" || type == "end-drag-left") &&
      mouse_message ("select", x, y)) {
    send_mouse_grab (this, false);
    return;
  }

  path mouse_tree_path= tree_path (path (), x, y, 0);
  bool over_commutative_diagram= false;
  tree mouse_tree= et;
  for (path p= mouse_tree_path; !is_nil (p); p= p->next) {
    if (is_compound (mouse_tree, "commutative-diagram")) {
      over_commutative_diagram= true;
      break;
    }
    if (is_atomic (mouse_tree) || p->item < 0 || p->item >= N(mouse_tree))
      break;
    mouse_tree= mouse_tree[p->item];
  }

  if (inside_graphics () && !over_commutative_diagram) {
    path gp= search_upwards (GRAPHICS);
    bool b= inside_graphics (type != "release-left");
    if (!is_nil (gp) && gp != previous_gp) {
      if (!is_nil (previous_gp) && type == "move")
	mouse_click (x, y);
      previous_gp= gp;
    }
    if (b) {
      if (mouse_graphics (type, x, y, mods, t, data)) return;
      if (!over_graphics (x, y))
	eval ("(graphics-reset-context 'text-cursor)");
    }
  }
  
  if (type == "press-left" || type == "start-drag-left") {
    if (mods <= 1 && image_resize_start (x, y)) return;
    if (mods <= 1 && table_resize_start (x, y)) return;
    if (mods > 1) {
      mouse_adjusting = mods;
      mouse_adjust_selection(x, y, mods);
    } else
      mouse_click (x, y);
  }
  if (type == "dragging-left") {
    if (image_resize_active) {
      image_resize_update (x, y);
      return;
    }
    if (table_resize_active) {
      table_resize_update (x, y);
      return;
    }
    if (mouse_adjusting && mods > 1) {
      mouse_adjusting = mods;
      mouse_adjust_selection(x, y, mods);
    } else if (is_attached (this) && check_event (DRAG_EVENT)) return;
    else mouse_drag (x, y);
  }
  if ((type == "release-left" || type == "end-drag-left")) {
    if (image_resize_active) {
      image_resize_update (x, y);
      image_resize_finish ();
      return;
    }
    if (table_resize_active) {
      table_resize_update (x, y);
      table_resize_finish ();
      return;
    }
    // Wayland may coalesce the last motion before release.  Finish the
    // selection at the release coordinates instead of the last move event.
    if (type == "end-drag-left" && mouse_adjusting == 0)
      mouse_drag (x, y);
    if (!(mouse_adjusting & ShiftMask))
      mouse_select (x, y, mods, type == "end-drag-left");
    mouse_adjusting &= ~mouse_adjusting;
    send_mouse_grab (this, false);
  }

  if (type == "double-left") {
    send_mouse_grab (this, false);
    if (mouse_extra_click (x, y))
      drag_left_reset ();
  }
  if (type == "press-middle") mouse_paste (x, y);
  if (type == "press-right") mouse_adjust (x, y, mods);
  if (type == "press-up") mouse_scroll (x, y, true);
  if (type == "press-down") mouse_scroll (x, y, false);

  if ((type == "press-left") ||
      (type == "release-left") ||
      (type == "end-drag-left") ||
      (type == "press-middle") ||
      (type == "press-right"))
    notify_change (THE_DECORATIONS);

  if (type == "wheel" && N(data) == 2)
    eval ("(wheel-event " * as_string (data[0]) *
          " " * as_string (data[1]) * ")");
}

/******************************************************************************
* Event handlers
******************************************************************************/

static tree
relativize (tree t, url base) {
  if (is_atomic (t)) return t;
  else {
    tree r (t, N(t));
    for (int i=0; i<N(t); i++)
      r[i]= relativize (t[i], base);
    if (is_func (r, IMAGE) && N(r) >= 1 && is_atomic (r[0])) {
      url name= url_system (r[0]->label);
      if (descends (name, head (base)))
        r[0]= as_string (delta (base, name));
    }
    return r;
  }
}

static void
call_drop_event (string kind, SI x, SI y, SI ticket, time_t t, url base) {
#ifdef QTTEXMACS
  (void) kind; (void) x; (void) y; (void) t;
  extern hashmap<int, tree> payloads;
  tree doc = payloads [ticket];
  payloads->reset (ticket);
  array<object> args;
  args << object ((int) x) << object ((int) y)
       << object (relativize (doc, base));
  call ("mouse-drop-event", args);
  //eval (list_object (symbol_object ("insert"), relativize (doc, base)));
  //array<object> args;
  //args << object (kind) << object (x) << object (y)
  //<< object (doc) << object ((double) t);
  //call ("mouse-event", args);
#else
  (void) kind; (void) x; (void) y; (void) ticket; (void) t;
#endif
}

static void
call_mouse_event (string kind, SI x, SI y, SI m, time_t t, array<double> d) {
  array<object> args;
  args << object (kind) << object ((int) x) << object ((int) y)
       << object ((int) m) << object ((double) t) << object (d);
  call ("mouse-event", args);
}

static string
as_scm_string (array<double> a) {
  string s= "(list";
  for (int i=0; i<N(a); i++)
    s << " " << as_string (a[i]);
  s << ")";
  return s;
}

static void
delayed_call_mouse_event (string kind, SI x, SI y, SI m, time_t t,
                          array<double> d) {
  // NOTE: preserve the historical idle delay for appropriate updating.
  string cmd=
    "(delayed (:idle 1) (mouse-event " * scm_quote (kind) * " " *
    as_string (x) * " " * as_string (y) * " " *
    as_string (m) * " " * as_string ((long int) t) * " " *
    as_scm_string (d) * "))";
  eval (cmd);
}

void
edit_interface_rep::handle_mouse (string kind, SI x, SI y, int m, time_t t,
                                  array<double> data) {
  if (is_nil (buf)) return;
  bool started= false;
#ifdef USE_EXCEPTIONS
  try {
#endif
  if (is_nil (eb) || (env_change & (THE_TREE + THE_ENVIRONMENT)) != 0) {
    //cout << "handle_mouse in " << buf->buf->name << ", " << got_focus << LF;
    //cout << kind << " (" << x << ", " << y << "; " << m << ", " << data << ")"
    //     << " at " << t << "\n";
    if (!got_focus) return;
    apply_changes ();
  }
  start_editing ();
  started= true;
  x= ((SI) (x / magf));
  y= ((SI) (y / magf));
  //cout << kind << " (" << x << ", " << y << "; " << m << ", " << data << ")"
  //     << " at " << t << "\n";

  if (kind == "drop") {
    call_drop_event (kind, x, y, m, t, buf->buf->name);
    if (inside_graphics (true))
      mouse_graphics ("drop-object", x, y, m, t, data);
  }
  else {
    string rew= kind;
    SI dist= (SI) (5 * PIXEL / magf);
    rew= detect_left_drag ((void*) this, rew, x, y, t, m, dist);
    if (rew == "start-drag-left") {
      call_mouse_event (rew, left_x, left_y, m, t, data);
      delayed_call_mouse_event ("dragging-left", x, y, m, t, data);
    }
    else {
      rew= detect_right_drag ((void*) this, rew, x, y, t, m, dist);
      if (rew == "start-drag-right") {
        call_mouse_event (rew, right_x, right_y, m, t, data);
        delayed_call_mouse_event ("dragging-right", x, y, m, t, data);
      }
      else call_mouse_event (rew, x, y, m, t, data);
    }
  }
  end_editing ();
#ifdef USE_EXCEPTIONS
  }
  catch (string msg) {
    if (started) cancel_editing ();
  }
  handle_exceptions ();
#endif
}
