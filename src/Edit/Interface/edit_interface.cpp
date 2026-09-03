
/******************************************************************************
* MODULE     : edit_interface.cpp
* DESCRIPTION: interface between the editor and the window manager
* COPYRIGHT  : (C) 1999  Joris van der Hoeven
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "Interface/edit_interface.hpp"
#include "Interface/selection_autoscroll.hpp"
#include "file.hpp"
#include "convert.hpp"
#include "server.hpp"
#include "tm_window.hpp"
#include "Metafont/tex_files.hpp"
#include "data_cache.hpp"
#include "drd_std.hpp"
#include "drd_mode.hpp"
#include "message.hpp"
#include "tree_traverse.hpp"
#include "boot.hpp"
#include "buffer_actor.hpp"
#include "actor_ui_bridge.hpp"
#ifdef EXPERIMENTAL
#include "../../Style/Evaluate/evaluate_main.hpp"
#endif
#include "gui.hpp" // for gui_interrupted

#include <cmath>

extern void (*env_next_prog)(void);
extern void set_snap_mode (tree t);
extern void set_snap_distance (SI d);

static bool defer_editor_chrome_build= false;

void
defer_next_editor_chrome_build () {
  defer_editor_chrome_build= true;
}

/*static*/ string
MODE_LANGUAGE (string mode) {
  if (mode == "text") return LANGUAGE;
  else if (mode == "math") return MATH_LANGUAGE;
  else if (mode == "prog") return PROG_LANGUAGE;
  else if (mode == "src") return LANGUAGE;
  std_error << "Invalid mode " << mode << ", assuming text mode instead\n";
  return LANGUAGE;
}

/******************************************************************************
* Main edit_interface routines
******************************************************************************/

static double
valid_zoom_or (double zoom, double fallback) {
  const double largest= static_cast<double> (std_shrinkf) * PIXEL;
  return std::isfinite (zoom) && zoom >= 0.001 && zoom <= largest
    ? zoom : fallback;
}

static double
get_zoom (editor_rep* ed, buffer_document_state* buf) {
  double fallback= valid_zoom_or (
    retina_zoom * ed->sv->get_default_zoom_factor (), 1.0);
  if (buf != nullptr && buf->data->init->contains ("no-zoom") &&
      buf->data->init->contains (ZOOM_FACTOR))
    return valid_zoom_or (as_double (buf->data->init [ZOOM_FACTOR]), fallback);
  return fallback;
}

edit_interface_rep::edit_interface_rep ():
  editor_rep (), // NOTE: ignored by the compiler, but suppresses warning
  env_change (0),
  last_change (texmacs_time()), last_update (last_change-1),
  last_event (texmacs_time()),
  anim_next (1.0e12),
  full_screen (false), got_focus (false), cursor_blink_visible (true),
  sh_s (""), sh_mark (0),
  pre_edit_skip (false), pre_edit_s (""), pre_edit_mark (0),
  popup_open (false),
  message_l (""), message_r (""), last_l (""), last_r (""),
  zoomf (get_zoom (this, buf)),
  magf (zoomf / std_shrinkf),
  pixel ((SI) tm_round ((std_shrinkf * PIXEL) / zoomf)),
  zpixel (max ((SI) tm_round (std_shrinkf * PIXEL), pixel)),
  copy_always (),
  last_x (0), last_y (0), last_t (0),
  tremble_count (0), shake_detector (),
  table_selection (false), mouse_adjusting (false),
  image_resize_active (false), image_resize_handle (0),
  image_resize_path (),
  image_resize_start_x (0), image_resize_start_y (0),
  image_resize_x1 (0), image_resize_y1 (0),
  image_resize_x2 (0), image_resize_y2 (0),
  image_resize_rects (),
  table_resize_active (false), table_resize_handle (0),
  table_resize_format_path (), table_resize_row (0), table_resize_column (0),
  table_resize_start_x (0), table_resize_start_y (0),
  table_resize_initial_size (0),
  oc (0, 0), temp_invalid_cursor (false),
  shadow (NULL), stored (NULL),
  cur_sb (2), cur_wb (2),
  resize_wx (0), resize_wy (0),
  pending_idle_menu_update (true),
  external_center_message_active (false),
  typewriter_manual_scroll_time (0),
  typewriter_manual_scroll_path (),
  live_statistics_cache_hash (-1),
  live_statistics_cache (),
  heading_cell_cache_valid (false), heading_cell_cache (),
  heading_cell_pressed (), heading_cell_hovered ()
{
  input_mode= INPUT_NORMAL;
  gui_root_extents (cur_wx, cur_wy);
  resize_wx= cur_wx;
  resize_wy= cur_wy;
}

edit_interface_rep::~edit_interface_rep () {
  if (shadow != NULL) tm_delete (shadow);
  if (stored != NULL) tm_delete (stored);
  shadow = NULL;
  stored = NULL;
}

edit_interface_rep::operator tree () {
  return tuple ("editor", as_string (get_name ()));
}

void
edit_interface_rep::suspend () {
  //cout << "Suspend " << buf->name << LF;
  if (got_focus) {
    interrupt_shortcut ();
    set_message ("", "", false);
  }
  got_focus= false;
  env_change= env_change & (~THE_FREEZE);
  notify_change (THE_FOCUS);
  if (shadow != NULL) tm_delete (shadow);
  if (stored != NULL) tm_delete (stored);
  shadow = NULL;
  stored = NULL;
}

void
edit_interface_rep::resume () {
  //cout << "Resume " << buf->name << LF;
  got_focus= true;
  bool defer_chrome= defer_editor_chrome_build;
  defer_editor_chrome_build= false;
  if (!defer_chrome) {
    bench_start ("build main menu");
    rebuild_ui_chrome ();
    bench_cumul ("build main menu");
  }
  cur_sb= 2;
  bench_start ("initialize editor focus state");
  env_change= env_change & (~THE_FREEZE);
  notify_change (THE_FOCUS + THE_EXTENTS);
  bench_cumul ("initialize editor focus state");
  {
    with_borrowed_drd drd_scope (&drd);
    if (!defer_chrome) {
      bench_start ("make initial cursor accessible");
      path new_tp= make_cursor_accessible (tp, true);
      bench_cumul ("make initial cursor accessible");
      if (new_tp != tp) {
        notify_change (THE_CURSOR);
        tp= new_tp;
      }
    }
  }
  bench_start ("reset initial editor");
  if (!headless_mode && !defer_chrome)
    (void) publish_ui (actor_command_kind::ui_invalidate_all);
  bench_cumul ("reset initial editor");
}

void
edit_interface_rep::keyboard_focus_on (string field) {
  (void) publish_ui_text (
    actor_command_kind::ui_keyboard_focus_field, std::move (field));
}

void box_broadcast (string msg);

void
edit_interface_rep::broadcast_message (string message) {
  rectangles rs;
  eb->broadcast (message, rs);
  if (N(rs) != 0) invalidate (rs);
  box_broadcast (message);
}

/******************************************************************************
* Routines for dealing with shrinked coordinates
******************************************************************************/

int
edit_interface_rep::get_pixel_size () {
  return pixel;
}

void
edit_interface_rep::set_zoom_factor (double zoom) {
  zoom= valid_zoom_or (zoom, valid_zoom_or (zoomf, 1.0));
  zoomf = zoom;
  magf  = zoomf / std_shrinkf;
  pixel = (SI) tm_round ((std_shrinkf * PIXEL) / zoomf);
  zpixel= max ((SI) tm_round (std_shrinkf * PIXEL), pixel);
}

void
edit_interface_rep::invalidate (SI x1, SI y1, SI x2, SI y2) {
  (void) publish_ui (
    actor_command_kind::ui_invalidate,
    static_cast<std::uint64_t> ((SI) floor (x1*magf)),
    static_cast<std::uint64_t> ((SI) floor (y1*magf)),
    static_cast<std::uint64_t> ((SI) ceil (x2*magf)),
    static_cast<std::uint64_t> ((SI) ceil (y2*magf)));
}

void
edit_interface_rep::invalidate (rectangles rs) {
  while (!is_nil (rs)) {
    invalidate (rs->item->x1-pixel, rs->item->y1-pixel,
                rs->item->x2+pixel, rs->item->y2+pixel);
    rs= rs->next;
  }
}

void
edit_interface_rep::invalidate_all () {
  (void) publish_ui (actor_command_kind::ui_invalidate_all);
}

void
edit_interface_rep::update_visible () {
  actor_viewport_snapshot viewport= ui_viewport ();
  vx1= viewport.visible_x1;
  vy1= viewport.visible_y1;
  vx2= viewport.visible_x2;
  vy2= viewport.visible_y2;
  vx1= (SI) (vx1 / magf); vy1= (SI) (vy1 / magf);
  vx2= (SI) (vx2 / magf); vy2= (SI) (vy2 / magf);
}

SI
edit_interface_rep::get_visible_width () {
  update_visible ();
  return vx2 - vx1;
}

SI
edit_interface_rep::get_visible_height () {
  update_visible ();
  return vy2 - vy1;
}

SI
edit_interface_rep::interface_scrollbar_width () const {
  if (ui_endpoint == nullptr) return 20 * PIXEL;
  SI width= ui_endpoint->viewport ().scrollbar_width;
  return width > 0 ? width : 20 * PIXEL;
}

SI
edit_interface_rep::get_window_width () {
  actor_viewport_snapshot viewport= ui_viewport ();
  SI w= viewport.window_width;
  bool sb= (get_init_string (SCROLL_BARS) != "false");
  if (full_screen) {
    string medium= get_init_string (PAGE_MEDIUM);
    if (medium == "automatic" || medium == "beamer") sb= false;
  }
  if (sb) w -= interface_scrollbar_width ();
  return w;
}

SI
edit_interface_rep::get_window_height () {
  return ui_viewport ().window_height;
}

SI
edit_interface_rep::get_window_x () {
  return ui_viewport ().window_x;
}

SI
edit_interface_rep::get_window_y () {
  return ui_viewport ().window_y;
}

SI
edit_interface_rep::get_canvas_x () {
  return ui_viewport ().canvas_x;
}

SI
edit_interface_rep::get_canvas_y () {
  return ui_viewport ().canvas_y;
}

SI
edit_interface_rep::get_scroll_x () {
  SI scx= ui_viewport ().scroll_x;
  scx= (SI) (scx / magf);
  return scx;
}

SI
edit_interface_rep::get_scroll_y () {
  SI scy= ui_viewport ().scroll_y;
  scy= (SI) (scy / magf);
  return scy;
}

void
edit_interface_rep::scroll_to (SI x, SI y) {
  stored_rects= rectangles ();
  copy_always = rectangles ();
  notify_change (THE_FREEZE);
  (void) publish_ui (
    actor_command_kind::ui_scroll_to,
    static_cast<std::uint64_t> ((SI) (x * magf)),
    static_cast<std::uint64_t> ((SI) (y * magf)));
}

SI
edit_interface_rep::get_cursor_x () {
  cursor cu= get_cursor ();
  return cu->ox;
}

SI
edit_interface_rep::get_cursor_y () {
  cursor cu= get_cursor ();
  return cu->oy;
}

void
edit_interface_rep::set_extents (SI x1, SI y1, SI x2, SI y2) {
  stored_rects= rectangles ();
  copy_always = rectangles ();
  (void) publish_ui (
    actor_command_kind::ui_set_extents,
    static_cast<std::uint64_t> ((SI) floor (x1*magf)),
    static_cast<std::uint64_t> ((SI) floor (y1*magf)),
    static_cast<std::uint64_t> ((SI) ceil (x2*magf)),
    static_cast<std::uint64_t> ((SI) ceil (y2*magf)));
}

/******************************************************************************
* Scroll so as to make the cursor and the selection visible
******************************************************************************/

static SI absval (SI x) { return max (x, -x); }

void
edit_interface_rep::cursor_visible () {
  path sp= find_innermost_scroll (eb, tp);
  cursor cu= get_cursor ();
  if (selection_active_any ()) {
    path p1, p2;
    selection_get (p1, p2);
    if (selection_covers_range (p1, p2, start (et, rp), end (et, rp)))
      return;
  }
  if (is_nil (sp)) {
    update_visible ();
    cu->y1 -= 2*pixel; cu->y2 += 2*pixel;
    bool must_update=
      (cu->ox+ ((SI) (cu->y1 * cu->slope)) <  vx1) ||
      (cu->ox+ ((SI) (cu->y2 * cu->slope)) >= vx2) ||
      (cu->oy+ cu->y1 <  vy1) ||
      (cu->oy+ cu->y2 >= vy2);

    string medium= as_string (get_init_value (PAGE_MEDIUM));
    bool selection_scrolling=
      selection_active_any () || selection_active_enlarging ();
    bool typewriter=
      get_user_preference ("typewriter mode", "off") == "on" &&
      (medium == "papyrus" || medium == "automatic") &&
      !selection_scrolling;
    if (typewriter && typewriter_manual_scroll_time != 0) {
      if (tp == typewriter_manual_scroll_path)
        return;
      typewriter_manual_scroll_time= 0;
    }
    if (typewriter && (vx2 - vx1 > 80*pixel) && (vy2 - vy1 > 80*pixel)) {
      SI cy= cu->oy + ((cu->y1 + cu->y2) >> 1);
      SI vc= (vy1 + vy2) >> 1;
      SI slack= max (40 * pixel, (vy2 - vy1) / 20);
      SI cx1= cu->ox + ((SI) (cu->y1 * cu->slope));
      SI cx2= cu->ox + ((SI) (cu->y2 * cu->slope));
      bool vertical  = absval (cy - vc) > slack;
      bool horizontal= cx1 < vx1 || cx2 >= vx2;
      if (vertical || horizontal) {
        scroll_to (horizontal ? cu->ox : ((vx1 + vx2) >> 1), cy);
        invalidate_all ();
        return;
      }
    }

    if (get_user_preference ("snap to pages", "off") == "on") {
      box pages= eb[0];
      if (N(pages) > 1) {
        SI vw= vx2 - vx1, vh= vy2 - vy1;
        for (int i=0; i<N(pages); i++) {
          actor_viewport_snapshot viewport= ui_viewport ();
          SI scx= viewport.scroll_x, scy= viewport.scroll_y;
          scx= (SI) (scx / magf);
          scy= (SI) (scy / magf);
          SI x1= eb->sy(0)+ pages->sx1 (i);
          SI x2= eb->sy(0)+ pages->sx2 (i);
          SI y1= eb->sy(0)+ pages->sy1 (i);
          SI y2= eb->sy(0)+ pages->sy2 (i);
          SI pw= x2 - x1, ph= y2 - y1;
          if (cu->ox >= x1 && x2 > cu->ox &&
              cu->oy >= y1 && y2 > cu->oy &&
              5*vw > 3*pw && 5*vh > 3*ph) {
            if (!must_update) {
              SI d= 5*pixel;
              if (pw >= vw) {
                if (vx1 > x1 + d && absval (x2 - vx2) > d) must_update= true;
                if (x2 > vx2 + d && absval (x1 - vx1) > d) must_update= true;
              }
              else if (vx1 > x1 + d || x2 > vx2 + d) must_update= true;
              if (ph >= vh) {
                if (vy1 > y1 + d && absval (y2 - vy2) > d) must_update= true;
                if (y2 > vy2 + d && absval (y1 - vy1) > d) must_update= true;
              }
              else if (vy1 > y1 + d || y2 > vy2 + d) must_update= true;
            }
            if (must_update) {
              //cout << "Cursor on page " << i << LF;
              //cout << "Visual " << vx1/PIXEL << ", " << vy1/PIXEL
              //     << "; " << vx2/PIXEL << ", " << vy2/PIXEL << LF;
              //cout << "Page " << x1/PIXEL << ", " << y1/PIXEL
              //     << "; " << x2/PIXEL << ", " << y2/PIXEL << LF;
              SI mx= (x1 + x2) >> 1, my= (y1 + y2) >> 1;
              if (pw >= vw) {
                if (cu->ox > mx) mx= x2 - ((vx2 - vx1) >> 1);
                else             mx= x1 + ((vx2 - vx1) >> 1);
              }
              if (ph >= vh) {
                if (cu->oy > my) my= y2 - ((vy2 - vy1) >> 1);
                else             my= y1 + ((vy2 - vy1) >> 1);
              }
              scroll_to (mx, my);
              invalidate_all ();
              return;
            }
          }
        }
      }
    }

    if (must_update) {
      scroll_to (cu->ox, cu->oy);
      invalidate_all ();
    }
  }
  else {
    SI x, y, sx, sy;
    rectangle outer, inner;
    find_canvas_info (eb, sp, x, y, sx, sy, outer, inner);
    if ((cu->ox+ ((SI) (cu->y1 * cu->slope)) < x + outer->x1) ||
        (cu->ox+ ((SI) (cu->y2 * cu->slope)) > x + outer->x2))
      {
        SI tx= inner->x2 - inner->x1;
        SI cx= outer->x2 - outer->x1;
        if (tx > cx) {
          SI outer_cx= cu->ox - x;
          SI inner_cx= outer_cx - sx;
          SI dx= inner_cx - inner->x1;
          double p= 100.0 * ((double) (dx - (cx>>1))) / ((double) (tx-cx));
          p= max (min (p, 100.0), 0.0);
          tree old_xt= eb[path_up (sp)]->get_info ("scroll-x");
          tree new_xt= as_string (p) * "%";
          if (new_xt != old_xt && is_accessible (obtain_ip (old_xt))) {
            object fun= symbol_object ("tree-set");
            object cmd= list_object (fun, old_xt, new_xt);
            exec_delayed (scheme_cmd (cmd));
            temp_invalid_cursor= true;
          }
        }
      }
    if ((cu->oy+ cu->y1 < y + outer->y1) ||
        (cu->oy+ cu->y2 > y + outer->y2))
      {
        SI ty= inner->y2 - inner->y1;
        SI cy= outer->y2 - outer->y1;
        if (ty > cy) {
          SI outer_cy= cu->oy + ((cu->y1 + cu->y2) >> 1) - y;
          SI inner_cy= outer_cy - sy;
          SI dy= inner_cy - inner->y1;
          double p= 100.0 * ((double) (dy - (cy>>1))) / ((double) (ty-cy));
          p= max (min (p, 100.0), 0.0);
          tree old_yt= eb[path_up (sp)]->get_info ("scroll-y");
          tree new_yt= as_string (p) * "%";
          if (new_yt != old_yt && is_accessible (obtain_ip (old_yt))) {
            object fun= symbol_object ("tree-set");
            object cmd= list_object (fun, old_yt, new_yt);
            exec_delayed (scheme_cmd (cmd));
            temp_invalid_cursor= true;
          }
        }
      }
  }
}

void
edit_interface_rep::selection_visible () {
  update_visible ();
  if ((vx2 - vx1 <= 80*pixel) || (vy2 - vy1 <= 80*pixel)) return;

  SI edge= (cur_sb == 1? 20 * pixel: 0);
  SI maximum_step= 20 * pixel;
  SI dx= selection_autoscroll_delta (end_x, vx1, vx2, edge, maximum_step);
  SI dy= selection_autoscroll_delta (end_y, vy1, vy2, edge, maximum_step);

  if (dx != 0 || dy != 0) {
    scroll_to (((vx1 + vx2) >> 1) + dx, ((vy1 + vy2) >> 1) + dy);
    invalidate_all ();
    SI old_vx1= vx1, old_vy1= vy1;
    update_visible ();
    end_x += vx1- old_vx1;
    end_y += vy1- old_vy1;
  }
}

/******************************************************************************
* Computation of environment rectangles
******************************************************************************/

static bool
is_graphical (tree t) {
  return
    is_func (t, _POINT) ||
    is_func (t, LINE) || is_func (t, CLINE) ||
    is_func (t, ARC) || is_func (t, CARC) ||
    is_func (t, SPLINE) || is_func (t, CSPLINE) ||
    is_func (t, BEZIER) || is_func (t, CBEZIER) ||
    is_func (t, SMOOTH) || is_func (t, CSMOOTH) ||
    is_func (t, PENSCRIPT) || is_func (t, CALLIGRAPHY);
}

static void
correct_adjacent (rectangles& rs1, rectangles& rs2) {
  if (N(rs1) != 1 || N(rs2) != 1) return;
  SI bot1= rs1->item->y1;
  SI top2= rs2->item->y2;
  if (rs1->item->y1 <= rs2->item->y1) {
    //cout << "Discard " << rs1->item->y1 << ", " << rs2->item->y1 << "\n";
    return;
  }
  if (rs1->item->y2 <= rs2->item->y2) {
    //cout << "Discard " << rs1->item->y2 << ", " << rs2->item->y2 << "\n";
    return;
  }
  SI mid= (bot1 + top2) >> 1;
  rs1->item->y1= mid;
  rs2->item->y2= mid;
}

static SI
focus_outline_width (SI pixel) {
  return max ((SI) gui_focus_border_width, (SI) 1) * pixel;
}

void
edit_interface_rep::compute_env_rects (path p, rectangles& rs, bool recurse,
                                       SI outline_width) {
  if (p == rp) return;
  tree pt= subtree (et, path_up (p));
  tree st= subtree (et, p);
  if ((is_func (st, TABLE) || is_func (st, SUBTABLE)) &&
      recurse && get_preference ("show table cells") == "on") {
    rectangles rl;
    for (int i=0; i<N(st); i++) {
      if (is_func (st[i], ROW))
        for (int j=0; j<N(st[i]); j++) {
          selection sel= eb->find_check_selection (p*i*j*0, p*i*j*1);
          rectangles rsel= copy (thicken (sel->rs, 0, 2 * pixel));
          if (i > 0 && is_func (st[i-1], ROW) && j < N(st[i-1])) {
            selection bis= eb->find_check_selection (p*(i-1)*j*0, p*(i-1)*j*1);
            rectangles rbis= copy (thicken (bis->rs, 0, 2 * pixel));
            correct_adjacent (rbis, rsel);
          }
          if (i+1 < N(st) && is_func (st[i+1], ROW) && j < N(st[i+1])) {
            selection bis= eb->find_check_selection (p*(i+1)*j*0, p*(i+1)*j*1);
            rectangles rbis= copy (thicken (bis->rs, 0, 2 * pixel));
            correct_adjacent (rsel, rbis);
          }
          rectangles selp= thicken (rsel,  pixel/2,  pixel/2);
          rectangles selm= thicken (rsel, -pixel/2, -pixel/2);
          rl << simplify (::correct (selp - selm));
        }
    }
    rs << simplify (rl);
    if (recurse) compute_env_rects (path_up (p), rs, recurse, outline_width);
  }
  else if (is_atomic (st) ||
           drd->is_child_enforcing (st) ||
           //is_document (st) || is_concat (st) ||
           is_func (st, TABLE) || is_func (st, SUBTABLE) ||
           is_func (st, ROW) || is_func (st, TFORMAT) ||
           is_graphical (st) ||
           (is_func (st, WITH) && is_graphical (st[N(st)-1])) ||
           (is_func (st, WITH) && is_graphical_text (st[N(st)-1])) ||
           (is_func (pt, GRAPHICS) &&
            (is_compound (st, "anim-edit") ||
             is_compound (st, "anim-static") ||
             is_compound (st, "anim-dynamic"))) ||
           (is_compound (st, "math", 1) &&
            is_compound (subtree (et, path_up (p)), "input")))
    compute_env_rects (path_up (p), rs, recurse, outline_width);
  else {
    int new_mode= DRD_ACCESS_NORMAL;
    if (get_init_string (MODE) == "src") new_mode= DRD_ACCESS_SOURCE;
    int old_mode= set_access_mode (new_mode);
    tree st= subtree (et, p);
    if (is_accessible_cursor (et, p * right_index (st)) || in_source ()) {
      bool right;
      path p1= p * 0, p2= p * 1, q1, q2;
      if (is_script (subtree (et, p), right) ||
          is_func (st, TEXT_AT) ||
          is_func (st, MATH_AT))
        {
          p1= start (et, p * 0);
          p2= end   (et, p * 0);
        }
      if (is_func (st, CELL)) { q1= p1; q2= p2; }
      else selection_correct (p1, p2, q1, q2);
      selection sel= eb->find_check_selection (q1, q2);
      if (N(focus_get ()) >= N(p))
        if (!recurse || get_preference ("show full context") == "on")
          rs << outlines (sel->rs, outline_width);
    }
    set_access_mode (old_mode);
    if (recurse || N(rs) == 0)
      compute_env_rects (path_up (p), rs, recurse, outline_width);
  }
}

/******************************************************************************
* handling changes
******************************************************************************/

void
edit_interface_rep::notify_change (int env_set, int env_unset) {
  env_change= (env_change | env_set) & (~env_unset);
  needs_update ();
  if ((env_set & (THE_TREE | THE_SELECTION | THE_CURSOR)) != 0)
    manual_focus_set (path (), (env_set & THE_TREE) != 0);
}

bool
edit_interface_rep::has_changed (int question) {
  return (env_change & question) != 0;
}

int
edit_interface_rep::idle_time (int event_type) {
  if (env_change == 0 &&
      got_focus &&
      (!ui_viewport ().invalid) &&
      (!check_event (event_type)))
    return texmacs_time () - last_change;
  else return 0;
}

int
edit_interface_rep::change_time () {
  return last_change;
}

void
edit_interface_rep::update_menus () {
  rebuild_ui_chrome ();
  set_footer ();
  (void) publish_ui (
    actor_command_kind::ui_set_modified, need_save () ? 1 : 0);
  if (!gui_interrupted ()) drd_update ();
  cache_memorize ();
  last_update= last_change;
  pending_idle_menu_update= false;
  save_user_preferences ();
}

int
edit_interface_rep::find_alt_selection_index
  (range_set alt_sel, SI y, int b, int e) {
  if (e - b <= 2) return b;
  int half= ((b + e) >> 2) << 1;
  int h= half;
  SI sy= 0;
  while (h < e) {
    range_set sub_sel= simple_range (alt_sel[h], alt_sel[h+1]);
    selection sel= compute_selection (sub_sel);
    if (is_nil (sel->rs)) { h += 2; continue; }
    sy= (sel->rs->item->y1 + sel->rs->item->y2) >> 1;
    break;
  }
  if (h >= e || y > sy)
    return find_alt_selection_index (alt_sel, y, b, half);
  else
    return find_alt_selection_index (alt_sel, y, h, e);
}

void
edit_interface_rep::apply_changes () {
  //cout << "Apply changes\n";
  //cout << "et= " << et << "\n";
  //cout << "tp= " << tp << "\n";
  //cout << HRULE << "\n";

  update_visible ();
  rectangle new_visible= rectangle (vx1, vy1, vx2, vy2);  

  if (kbd_show_keys && N(kbd_last_times) > 0) {
    if (got_focus) {
      time_t last= kbd_last_times[N(kbd_last_times)-1];
      if (last + kbd_hide_delay < texmacs_time ()) {
        kbd_last_keys = array<string> ();
        kbd_last_times= array<time_t> ();
      }
      bool change= (env_change != 0);
      if (kbd_shown_keys != kbd_last_keys) {
        kbd_shown_keys= copy (kbd_last_keys);
        change= true;
      }
      rectangles rs (rectangle (vx1, vy1, vx2, vy1 + 100 * pixel));
      if (rs != keys_rects) { invalidate (keys_rects); change= true; }
      keys_rects= rs;
      if (change) invalidate (keys_rects);
    }
    else {
      if (!is_nil (keys_rects)) invalidate (keys_rects);
      keys_rects= rectangles ();
    }
  }

  if (tremble_count > 0 &&
      last_change-last_update > 0 &&
      (idle_time (INTERRUPTED_EVENT) >= 80 ||
       texmacs_time() - last_event >= 3000)) {
    tremble_count--;
    if (tremble_count > 2) {
      env_change = env_change | (THE_CURSOR + THE_FREEZE);
      last_change= texmacs_time ();
    }
    //cout << "Tremble- " << tremble_count << LF;
  }
  
  if (env_change == 0) {
    if (pending_idle_menu_update &&
        idle_time (INTERRUPTED_EVENT) >= 1000/6)
      update_menus ();
    if (new_visible == last_visible) return;
  }

  // cout << "Applying changes " << env_change << " to " << get_name() << "\n";
  // time_t t1= texmacs_time ();
  
  // cout << "Handling automatic resizing\n";
  int sb= 1;
  actor_viewport_snapshot viewport= ui_viewport ();
  if (viewport.attached) {
    tree new_zoom= as_string (zoomf);
    tree old_zoom= get_init_value (ZOOM_FACTOR);
    if (new_zoom != old_zoom) {
      init_env (ZOOM_FACTOR, new_zoom);
      notify_change (THE_ENVIRONMENT);
    }
  
    if (get_init_string (PAGE_MEDIUM) == "automatic")
    {
      SI wx= viewport.window_width, wy= viewport.window_height;
      bool visible_size= false;
      SI ax1= viewport.visible_x1, ay1= viewport.visible_y1;
      SI ax2= viewport.visible_x2, ay2= viewport.visible_y2;
      if (ax2 > ax1 && ay2 > ay1) {
        wx= ax2 - ax1;
        wy= ay2 - ay1;
        visible_size= true;
      }
      if (get_init_string (SCROLL_BARS) == "false") sb= 0;
      if (viewport.full_screen) sb= 0;
      if (sb && !visible_size) wx -= interface_scrollbar_width ();
      bool layout_changed= wx != cur_wx || new_zoom != old_zoom;
      if (layout_changed) {
        cur_wx= wx; cur_wy= wy;
        init_env (PAGE_SCREEN_WIDTH, as_string ((SI) (wx/magf)) * "tmpt");
        init_env (PAGE_SCREEN_HEIGHT, as_string ((SI) (wy/magf)) * "tmpt");
      }
      else cur_wy= wy;
    }
  }  
  if (get_init_string (PAGE_MEDIUM) == "beamer" && full_screen) sb= 0;
  if (sb != cur_sb) {
    cur_sb= sb;
    (void) publish_ui (
      actor_command_kind::ui_set_scrollbars,
      static_cast<std::uint64_t> (sb));
  }
  init_env ("full-screen-mode", string (full_screen? "true": "false"));

  // window decorations (menu bar, icon bars, footer)
  int wb= 2;
  if (viewport.attached) {
    string val= get_init_string (WINDOW_BARS);
    if (val == "auto") wb= 2;
    else if (val == "false") wb= 0;
    else if (val == "true") wb= 1;
    if (wb != cur_wb) {
      cur_wb= wb;
      if (wb != 2) {
        (void) publish_ui (
          actor_command_kind::ui_show_header, wb != 0 ? 1 : 0);
        (void) publish_ui (
          actor_command_kind::ui_show_footer, wb != 0 ? 1 : 0);
      }
    }
  }
  
  // cout << "Handling selection\n";
  if (env_change & (THE_TREE+THE_ENVIRONMENT+THE_SELECTION)) {
    if (!is_nil (selection_rects)) {
      invalidate (selection_rects);
      if (!selection_active_any ()) {
        set_selection (tp, tp);
        selection_rects= rectangles ();
      }
    }
    if (N (alt_selection_rects) != 0) {
      rectangles visible (rectangle (vx1, vy1, vx2, vy2));
      for (int i=0; i<N(alt_selection_rects); i++)
        invalidate (alt_selection_rects[i] & visible);
      range_set alt_sel= append (get_alt_selection ("alternate"),
                                 get_alt_selection ("brackets"));
      alt_sel << get_alt_selection ("athena-diff-left");
      alt_sel << get_alt_selection ("athena-diff-right");
      if (is_empty (alt_sel))
        alt_selection_rects= array<rectangles> ();
    }
    if (N (spell_selection_rects) != 0) {
      rectangles visible (rectangle (vx1, vy1, vx2, vy2));
      for (int i=0; i<N(spell_selection_rects); i++)
        invalidate (thicken (spell_selection_rects[i], pixel, pixel) & visible);
      if (is_empty (get_alt_selection ("spell-live")))
        spell_selection_rects= array<rectangles> ();
    }
  }
  
  // cout << "Handling environment\n";
  if (env_change & THE_ENVIRONMENT)
    typeset_invalidate_all ();

  // cout << "Handling tree\n";
  if (env_change & (THE_TREE+THE_ENVIRONMENT)) {
    typeset_invalidate_env ();
    SI old_heading_right= is_nil (eb) ? vx2 : eb->x2;
    SI x1, y1, x2, y2;
    typeset (x1, y1, x2, y2);
    heading_cell_cache_valid= false;
    the_ghost_cursor ()= eb->find_check_cursor (tp);
    SI heading_strip_width= 80 * pixel;
    invalidate (old_heading_right - heading_strip_width, vy1,
                old_heading_right + 2 * pixel, vy2);
    if (!is_nil (eb) && eb->x2 != old_heading_right)
      invalidate (eb->x2 - heading_strip_width, vy1,
                  eb->x2 + 2 * pixel, vy2);
    invalidate (x1- 2*pixel, y1- 2*pixel, x2+ 2*pixel, y2+ 2*pixel);
    // check_data_integrety ();
  }
  
#ifdef EXPERIMENTAL
  if (env_change & THE_ENVIRONMENT)
    environment_update ();
  if (env_change & THE_TREE) {
    cout << HRULE;
    mem= evaluate (ste, cct);
    tree rew= mem->get_tree ();
    cout << HRULE;
    cout << tree_to_texmacs (rew) << LF;
    //print_tree (rew);
  }
#endif
  
  // cout << "Handling extents\n";
  if (env_change & (THE_TREE+THE_ENVIRONMENT+THE_EXTENTS)) {
    string medium= get_init_string (PAGE_MEDIUM);
    SI ex1= (SI) (((double) eb->x1) * magf);
    SI ey1= (SI) (((double) eb->y1) * magf);
    SI ex2= (SI) (((double) eb->x2) * magf);
    SI ey2= (SI) (((double) eb->y2) * magf);
    abs_round (ex1, ey1);
    abs_round (ex2, ey2);
    actor_viewport_snapshot extents_viewport= ui_viewport ();
    SI w= extents_viewport.window_width;
    SI h= extents_viewport.window_height;
    bool visible_size= false;
    if (medium == "automatic") {
      SI ax1= extents_viewport.visible_x1;
      SI ay1= extents_viewport.visible_y1;
      SI ax2= extents_viewport.visible_x2;
      SI ay2= extents_viewport.visible_y2;
      if (ax2 > ax1 && ay2 > ay1) {
        w= ax2 - ax1;
        h= ay2 - ay1;
        visible_size= true;
      }
    }
    if (!visible_size && cur_sb && ey2 - ey1 > h)
      w -= interface_scrollbar_width ();
    if (!visible_size && cur_sb && ex2 - ex1 > w)
      h -= interface_scrollbar_width ();
    if (ex2 - ex1 <= w + 2*PIXEL) {
      if (medium == "automatic")
        ex2= ex1 + w;
    }
    if (ey2 - ey1 <= h + 2*PIXEL) {
      if (medium == "papyrus" || medium == "automatic")
        ey1= ey2 - h;
    }
    if (get_user_preference ("typewriter mode", "off") == "on" &&
        (medium == "papyrus" || medium == "automatic") && h > 0)
      ey1 -= h >> 1;
    (void) publish_ui (
      actor_command_kind::ui_set_extents,
      static_cast<std::uint64_t> (ex1),
      static_cast<std::uint64_t> (ey1),
      static_cast<std::uint64_t> (ex2),
      static_cast<std::uint64_t> (ey2));
    //set_extents (eb->x1, eb->y1, eb->x2, eb->y2);
  }
  
  // cout << "Cursor\n";
  temp_invalid_cursor= false;
  if (env_change & (THE_TREE+THE_ENVIRONMENT+THE_EXTENTS+
                    THE_CURSOR+THE_SELECTION+THE_FOCUS)) {
    int THE_CURSOR_BAK= env_change & THE_CURSOR;
    go_to_here ();
    env_change= (env_change & (~THE_CURSOR)) | THE_CURSOR_BAK;
    if ((env_change & (THE_TREE+THE_ENVIRONMENT+
                       THE_CURSOR+THE_SELECTION+THE_FOCUS)) != 0)
      if (!inside_active_graphics ())
        if ((env_change & THE_FREEZE) == 0)
          cursor_visible ();

    SI dw= 0;
    if (tremble_count > 3) dw= (1 + min (tremble_count - 3, 25)) * 2 * pixel;
    SI /*P1= zpixel,*/ P2= 2*zpixel, P3= 3*zpixel;
    cursor cu= get_cursor();
    rectangle ocr (oc->ox+ ((SI) ((oc->y1-dw)*oc->slope))- P3 - dw,
                   oc->oy+ (oc->y1-dw)- P3,
                   oc->ox+ ((SI) ((oc->y2+dw)*oc->slope))+ P2 + dw,
                   oc->oy+ (oc->y2+dw)+ P3);
    copy_always= rectangles (ocr, copy_always);
    invalidate (ocr->x1, ocr->y1, ocr->x2, ocr->y2);
    rectangle ncr (cu->ox+ ((SI) ((cu->y1-dw)*cu->slope))- P3 - dw,
                   cu->oy+ (cu->y1-dw)- P3,
                   cu->ox+ ((SI) ((cu->y2+dw)*cu->slope))+ P2 + dw,
                   cu->oy+ (cu->y2+dw)+ P3);
    invalidate (ncr->x1, ncr->y1, ncr->x2, ncr->y2);
    copy_always= rectangles (ncr, copy_always);
    oc= copy (cu);
   
    // Set the input-method hot spot on the Qt owner thread.
    (void) publish_ui (
      actor_command_kind::ui_set_cursor,
      static_cast<std::uint64_t> ((SI) floor (cu->ox * magf)),
      static_cast<std::uint64_t> ((SI) floor (cu->oy * magf)));

    path sp= selection_get_cursor_path ();
    bool semantic_flag= semantic_active (path_up (sp));
    bool full_context= (get_preference ("show full context") == "on");
    bool table_cells= (get_preference ("show table cells") == "on");
    bool show_focus= (get_preference ("show focus") == "on");
    bool semantic_only= (get_preference ("show only semantic focus") == "on");
    rectangles old_env_rects= env_rects;
    rectangles old_foc_rects= foc_rects;
    env_rects= rectangles ();
    foc_rects= rectangles ();
    path pp= path_up (tp);
    tree pt= subtree (et, pp);
    if (none_accessible (pt));
    else pp= path_up (pp);
    if (full_context || table_cells)
      compute_env_rects (pp, env_rects, true, pixel);
    if (show_focus && (!semantic_flag || !semantic_only))
      compute_env_rects (pp, foc_rects, false, focus_outline_width (pixel));
    if (env_rects != old_env_rects) {
      invalidate (old_env_rects);
      invalidate (env_rects);
    }
    else if (env_change & THE_FOCUS) invalidate (env_rects);
    if (foc_rects != old_foc_rects) {
      invalidate (old_foc_rects);
      invalidate (foc_rects);
    }
    else if (env_change & THE_FOCUS) invalidate (foc_rects);
    
    rectangles old_sem_rects= sem_rects;
    bool old_sem_correct= sem_correct;
    sem_rects= rectangles ();
    sem_correct= true;
    if (semantic_flag && show_focus) {
      path sp= selection_get_cursor_path ();
      path p1= tp, p2= tp;
      if (selection_active_any ()) selection_get (p1, p2);
      sem_correct= semantic_select (path_up (sp), p1, p2, 2);
      if (!sem_correct) {
        path sr= semantic_root (path_up (sp));
        p1= start (et, sr);
        p2= end (et, sr);
      }
      path q1, q2;
      selection_correct (p1, p2, q1, q2);
      selection sel= eb->find_check_selection (q1, q2);
      sem_rects << outlines (sel->rs, pixel);
    }
    if (sem_rects != old_sem_rects || sem_correct != old_sem_correct) {
      invalidate (old_sem_rects);
      invalidate (sem_rects);
    }
    else if (env_change & THE_FOCUS) invalidate (sem_rects);
    
    invalidate_graphical_object ();
  }
  
  // cout << "Handling selection\n";
  if (env_change & THE_SELECTION) {
    if (selection_active_any ()) {
      table_selection= selection_active_table ();
      selection sel; selection_get (sel);
      rectangles rs= thicken (sel->rs, pixel, 3*pixel);
      selection_rects= rs;
      invalidate (selection_rects);
    }
  }

  // cout << "Handling alternative selection\n";
  if ((env_change & THE_SELECTION) || new_visible != last_visible) {
    range_set alt_sel= append (get_alt_selection ("alternate"),
                               get_alt_selection ("brackets"));
    alt_sel << get_alt_selection ("athena-diff-left");
    alt_sel << get_alt_selection ("athena-diff-right");
    if (!is_empty (alt_sel)) {
      alt_selection_rects= array<rectangles> (); int b= 0, e= N(alt_sel);
      if (e - b >= 200) {
        b= max (find_alt_selection_index (alt_sel, vy2, b, e) - 100, b);
        e= min (find_alt_selection_index (alt_sel, vy1, b, e) + 100, e);
      }
      for (int i=b; i+1<e; i+=2) {
        range_set sub_sel= simple_range (alt_sel[i], alt_sel[i+1]);
        selection sel= compute_selection (sub_sel);
        rectangles rs= thicken (sel->rs, pixel, 3*pixel);
        if (N(rs) != 0) alt_selection_rects << rs;
      }
      rectangles visible (new_visible);
      for (int i=0; i<N(alt_selection_rects); i++)
        invalidate (alt_selection_rects[i] & visible);
    }

    range_set spell_sel= get_alt_selection ("spell-live");
    if (!is_empty (spell_sel)) {
      spell_selection_rects= array<rectangles> (); int b= 0, e= N(spell_sel);
      if (e - b >= 200) {
        b= max (find_alt_selection_index (spell_sel, vy2, b, e) - 100, b);
        e= min (find_alt_selection_index (spell_sel, vy1, b, e) + 100, e);
      }
      for (int i=b; i+1<e; i+=2) {
        range_set sub_sel= simple_range (spell_sel[i], spell_sel[i+1]);
        selection sel= compute_selection (sub_sel);
        rectangles rs= thicken (sel->rs, pixel, 3*pixel);
        if (N(rs) != 0) spell_selection_rects << rs;
      }
      rectangles visible (new_visible);
      for (int i=0; i<N(spell_selection_rects); i++)
        invalidate (thicken (spell_selection_rects[i], pixel, pixel) & visible);
    }
  }
  
  // cout << "Handling locus highlighting\n";
  if (env_change & (THE_TREE+THE_ENVIRONMENT+THE_EXTENTS)) {
    update_mouse_loci ();
    update_focus_loci ();
    if (!is_nil (focus_ids) && got_focus)
      call ("link-follow-ids", object (focus_ids), object ("focus"));
  }
  else if (env_change & THE_SELECTION) {
    update_focus_loci ();
    call ("close-tooltip");
    if (!is_nil (focus_ids) && got_focus)
      call ("link-follow-ids", object (focus_ids), object ("focus"));
  }
  if (env_change & THE_LOCUS) {
    if (locus_new_rects != locus_rects) {
      invalidate (locus_rects);
      invalidate (locus_new_rects);
      locus_rects= locus_new_rects;
    }
  }
  
  // cout << "Handling backing store\n";
  if (!is_nil (stored_rects)) {
    if (env_change & (THE_TREE+THE_ENVIRONMENT+THE_SELECTION+THE_EXTENTS))
      stored_rects= rectangles ();
  }
  if (inside_active_graphics ()) {
    SI gx1, gy1, gx2, gy2;
    if (find_graphical_region (gx1, gy1, gx2, gy2)) {
      rectangle gr= rectangle (gx1, gy1, gx2, gy2);
      if (!is_nil (gr - stored_rects))
        invalidate (gx1, gy1, gx2, gy2);
    }
  }

  // cout << "Graphics snapping\n";
  if (inside_active_graphics () && is_current_editor ()) {
    tree t= as_tree (call ("graphics-get-snap-mode"));
    set_snap_mode (t);
    string val= as_string (call ("graphics-get-snap-distance"));
    set_snap_distance (as_length (val));
  }
  
  // cout << "Handling environment changes\n";
  if (env_change & THE_ENVIRONMENT)
    invalidate_all ();

  // cout << "Handling menus\n";
  if (env_change & THE_MENUS)
    update_menus ();

  // cout << "Applied changes\n";
  // time_t t2= texmacs_time ();
  // if (t2 - t1 >= 10) cout << "apply_changes took " << t2-t1 << "ms\n";
  bool schedule_idle_menu_update=
    (env_change & (THE_ENVIRONMENT | THE_SELECTION | THE_FOCUS | THE_MENUS)) !=
    0 ||
    ((env_change & THE_CURSOR) != 0 && (env_change & THE_TREE) == 0);
  env_change  = 0;
  last_change = texmacs_time ();
  pending_idle_menu_update= schedule_idle_menu_update;
  last_update = schedule_idle_menu_update? last_change-1: last_change;
  last_visible= new_visible;
  manual_focus_release ();
}

/******************************************************************************
* Animations
******************************************************************************/

void
edit_interface_rep::animate () {
  if (((double) texmacs_time ()) >= anim_next) {
    rectangles rs= eb->anim_invalid ();
    invalidate (rs);
    stored_rects= rectangles ();
  }
}

/******************************************************************************
* Miscellaneous routines
******************************************************************************/

void
edit_interface_rep::full_screen_mode (bool flag) {
  full_screen= flag;
  invalidate_all ();
}

void
edit_interface_rep::before_menu_action () {
  archive_state ();
  start_editing ();
  set_input_normal ();
}

void
edit_interface_rep::after_menu_action () {
  notify_change (THE_DECORATIONS);
  end_editing ();
  windows_delayed_refresh (1);
}

void
edit_interface_rep::cancel_menu_action () {
  notify_change (THE_DECORATIONS);
  cancel_editing ();
  windows_delayed_refresh (1);
}

rectangle
edit_interface_rep::get_window_extents () {
  actor_viewport_snapshot viewport= ui_viewport ();
  SI ox= viewport.canvas_x, oy= viewport.canvas_y;
  SI w= viewport.window_width, h= viewport.window_height;
  SI vx1= viewport.visible_x1, vy2= viewport.visible_y2;
  ox -= vx1; oy -= vy2;
  return rectangle (ox, oy - h, ox + w, oy);
}

cursor
edit_interface_rep::search_cursor (path p) {
  return eb->find_check_cursor (p);
}

selection
edit_interface_rep::search_selection (path start, path end) {
  return eb->find_check_selection (start, end);
  //rectangle r= least_upper_bound (sel->rs) / std_shrinkf;
}

/******************************************************************************
* event handlers
******************************************************************************/

bool
edit_interface_rep::is_editor_widget () {
  return true;
}

bool
edit_interface_rep::is_embedded_widget () {
  if (buf == nullptr || !has_subtree (et, rp) ||
      subtree (et, rp) == tree (UNINIT))
    return false;
  string name= as_string (buf->name);
  return starts (name, "tmfs://aux/");
  // FIXME: could be made more robust: test should not be based on file name
}

void
edit_interface_rep::handle_user_scroll (time_t t) {
  if (buf == nullptr || is_nil (eb)) return;
  typewriter_manual_scroll_time= t;
  typewriter_manual_scroll_path= copy (tp);
}

void
edit_interface_rep::handle_get_size_hint (SI& w, SI& h) {
  gui_root_extents (w, h);
}

void
edit_interface_rep::handle_notify_resize (SI w, SI h) {
  if (buf == nullptr) return;
  bool width_changed= w != resize_wx;
  bool height_changed= h != resize_wy;
  resize_wx= w;
  resize_wy= h;
  if (!width_changed && !height_changed) return;
  notify_change ((width_changed ? THE_TREE : THE_EXTENTS) + THE_FREEZE);
  if (width_changed &&
      as_bool (call ("defined?",
                     symbol_object ("schedule-persistent-fit-width"))))
    call ("schedule-persistent-fit-width");
  if (!is_embedded_widget () &&
      as_bool (call ("defined?",
                     symbol_object ("schedule-resize-editing-position"))))
    call ("schedule-resize-editing-position");
}

double
edit_interface_rep::handle_get_zoom_factor () const {
  return zoomf;
}

void
edit_interface_rep::handle_set_zoom_factor (double zoom) {
  set_zoom_factor (zoom);
  if (ui_endpoint != nullptr) ui_endpoint->set_zoom_factor (zoom);
}
