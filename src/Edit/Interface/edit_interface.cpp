
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
#include "data_cache.hpp"
#include "drd_std.hpp"
#include "drd_mode.hpp"
#include "message.hpp"
#include "tree_traverse.hpp"
#include "boot.hpp"
#include "buffer_actor.hpp"
#include "actor_ui_bridge.hpp"
#include "document_commands.hpp"
#include "document_style_commands.hpp"
#include "format_commands.hpp"
#include "generic_editor_commands.hpp"
#include "new_style.hpp"
#include "structured_commands.hpp"
#include "language.hpp"
#ifdef EXPERIMENTAL
#include "../../Style/Evaluate/evaluate_main.hpp"
#endif
#include "gui.hpp" // for gui_interrupted

#include <cmath>

extern void (*env_next_prog)(void);

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
  pending_idle_footer_update (true),
  external_center_message_active (false),
  typewriter_manual_scroll_time (0),
  typewriter_manual_scroll_path (),
  resize_viewport_restore (),
  programmatic_scroll_generation (0),
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
  clear_link_peek ();
  resize_viewport_restore.cancel ();
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
  cur_sb= 2;
  bench_start ("initialize editor focus state");
  env_change= env_change & (~THE_FREEZE);
  notify_change (THE_FOCUS + THE_EXTENTS);
  bench_cumul ("initialize editor focus state");
  {
    with_borrowed_drd drd_scope (&drd);
    bench_start ("make initial cursor accessible");
    path new_tp= make_cursor_accessible (tp, true);
    bench_cumul ("make initial cursor accessible");
    if (new_tp != tp) {
      notify_change (THE_CURSOR);
      tp= new_tp;
    }
  }
  bench_start ("reset initial editor");
  if (!headless_mode)
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
  resize_viewport_restore.cancel ();
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
  std::uint64_t generation= programmatic_scroll_generation + 1;
  if (publish_ui (
    actor_command_kind::ui_scroll_to,
    static_cast<std::uint64_t> ((SI) (x * magf)),
    static_cast<std::uint64_t> ((SI) (y * magf)), generation, 0))
    programmatic_scroll_generation= generation;
}

void
edit_interface_rep::scroll_to_if_user_unchanged (
  SI x, SI y, std::uint64_t user_generation) {
  stored_rects= rectangles ();
  copy_always = rectangles ();
  notify_change (THE_FREEZE);
  std::uint64_t generation= programmatic_scroll_generation + 1;
  std::uint64_t user_guard= user_generation + 1;
  if (publish_ui (
    actor_command_kind::ui_scroll_to,
    static_cast<std::uint64_t> ((SI) (x * magf)),
    static_cast<std::uint64_t> ((SI) (y * magf)), generation, user_guard))
    programmatic_scroll_generation= generation;
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
    is_func (t, PENSCRIPT);
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
  if (env_set & (THE_TREE | THE_ENVIRONMENT | THE_CURSOR | THE_SELECTION |
                 THE_FOCUS))
    pending_idle_footer_update= true;
  if (env_set & (THE_TREE | THE_ENVIRONMENT)) {
    live_spelling.reset ();
    live_spelling_dirty= true;
    live_spelling_next= texmacs_time () + 450;
    if (ui_endpoint != nullptr)
      ui_endpoint->invalidate_personal_macro_items ();
  }
  if (env_set & THE_ENVIRONMENT)
    editor_style_command_flags_valid= false;
  if (env_set & THE_TREE) live_spelling_edit_cursor= copy (tp);
  if (env_set & (THE_TREE | THE_ENVIRONMENT | THE_EXTENTS | THE_CURSOR | THE_SELECTION))
    clear_link_peek ();
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
  refresh_editor_style_command_flags ();
  publish_editor_command_state ();
  publish_focus_toolbar_state ();
  set_footer ();
  pending_idle_footer_update= false;
  (void) publish_ui (
    actor_command_kind::ui_set_modified, need_save () ? 1 : 0);
  if (!gui_interrupted ()) drd_update ();
  cache_memorize ();
  last_update= last_change;
  pending_idle_menu_update= false;
  save_user_preferences ();
}

actor_editor_command_snapshot
editor_rep::editor_command_state_snapshot () {
  actor_editor_command_snapshot snapshot;
  if (buf == nullptr) return snapshot;

  snapshot.flags= ACTOR_EDITOR_COMMAND_STATE_VALID;
  if (buf->read_only)
    snapshot.flags |= ACTOR_EDITOR_COMMAND_STATE_READ_ONLY;
  if (selection_active_any ())
    snapshot.flags |= ACTOR_EDITOR_COMMAND_STATE_SELECTION;
  if (selection_active_any () && !selection_active_small ())
    snapshot.flags |= ACTOR_EDITOR_COMMAND_STATE_NON_SMALL_SELECTION;
  if (inside_active_graphics () && native_graphics_selection_active ())
    snapshot.flags |= ACTOR_EDITOR_COMMAND_STATE_GRAPHICS_SELECTION;
  path focus= focus_get ();
  if (test_subtree (focus))
    snapshot.flags |= ACTOR_EDITOR_COMMAND_STATE_FOCUS_NODE;
  const bool graphics_mode= inside_graphics (false);
  const string mode= get_env_string (MODE);
  if (mode == "math" && !graphics_mode)
    snapshot.flags |= ACTOR_EDITOR_COMMAND_STATE_MATH_MODE;
  if (mode == "text" && !graphics_mode)
    snapshot.flags |= ACTOR_EDITOR_COMMAND_STATE_TEXT_MODE;
  if (mode == "prog" && !graphics_mode)
    snapshot.flags |= ACTOR_EDITOR_COMMAND_STATE_PROG_MODE;
  if (mode == "src")
    snapshot.flags |= ACTOR_EDITOR_COMMAND_STATE_SOURCE_MODE;
  if (graphics_mode)
    snapshot.flags |= ACTOR_EDITOR_COMMAND_STATE_GRAPHICS_MODE;
  if (inside ("letter-header"))
    snapshot.flags |= ACTOR_EDITOR_COMMAND_STATE_INSIDE_LETTER_HEADER;
  if (inside ("float") || inside ("footnote"))
    snapshot.flags |= ACTOR_EDITOR_COMMAND_STATE_INSIDE_FLOAT_OR_FOOTNOTE;
  if (generic_in_main_flow ())
    snapshot.flags |= ACTOR_EDITOR_COMMAND_STATE_MAIN_FLOW;
  if (inside ("traverse"))
    snapshot.flags |= ACTOR_EDITOR_COMMAND_STATE_TMDOC_TRAVERSE;
  if (inside ("explain"))
    snapshot.flags |= ACTOR_EDITOR_COMMAND_STATE_TMDOC_EXPLAIN;
  if (inside ("overlays") || inside ("overlays-compressed") ||
      inside ("overlays-phantoms") || inside ("overlays-greyed") ||
      inside ("gr-overlays"))
    snapshot.flags |= ACTOR_EDITOR_COMMAND_STATE_OVERLAYS_CONTEXT;
  tree buffer_root= the_buffer ();
  if (is_document (buffer_root) && N(buffer_root) > 0 &&
      is_compound (buffer_root[N(buffer_root)-1], "screens"))
    snapshot.flags |= ACTOR_EDITOR_COMMAND_STATE_SCREENS_BUFFER;

  snapshot.flags |= editor_style_command_flags;
  const bool screens_mode= inside ("screens");
  if (screens_mode)
    snapshot.flags |= ACTOR_EDITOR_COMMAND_STATE_SCREENS_MODE;
  bool beamer_style= false;
  tree style= get_style ();
  for (int i= 0; i < arity (style); ++i)
    if (is_atomic (style[i]) && as_string (style[i]) == "beamer") {
      beamer_style= true;
      break;
    }
  if (screens_mode || beamer_style ||
      get_user_preference ("presentation tool", "off") == "on")
    snapshot.flags |= ACTOR_EDITOR_COMMAND_STATE_PRESENTATION_MODE;

  int undo= undo_possibilities ();
  int redo= redo_possibilities ();
  if (undo < 0) undo= 0;
  if (redo < 0) redo= 0;
  if (undo > 65535) undo= 65535;
  if (redo > 65535) redo= 65535;
  snapshot.undo_count= static_cast<std::uint16_t> (undo);
  snapshot.redo_count= static_cast<std::uint16_t> (redo);
  return snapshot;
}

actor_document_menu_snapshot
editor_rep::document_menu_state_snapshot () {
  actor_document_menu_snapshot snapshot;
  if (buf == nullptr) return snapshot;
  snapshot.ready= true;

  auto assign= [] (std::string& target, string value) {
    target.assign (value.data (), static_cast<std::size_t> (N(value)));
  };
  auto display_name= [] (string name) {
    return upcase_first (replace (name, "-", " "));
  };
  auto append_choice=
    [&] (std::vector<actor_focus_choice_snapshot>& target,
         string value, bool checked) {
      actor_focus_choice_snapshot item;
      assign (item.value, value);
      assign (item.label, display_name (value));
      item.checked= checked;
      target.push_back (std::move (item));
    };

  try {
    list<string> styles= as_list_string (call ("get-style-list"));
    if (!is_nil (styles)) assign (snapshot.document_style, styles->item);
  }
  catch (...) {}
  assign (snapshot.page_type, get_init_string ("page-type"));
  assign (
    snapshot.document_font,
    document_font_display_name (get_init_string ("font")));
  assign (snapshot.font_base_size, get_init_string ("font-base-size"));
  assign (snapshot.document_language, document_get_language ());
  assign (snapshot.magnification, get_init_string ("magnification"));
  assign (snapshot.foreground_color, get_init_string ("color"));
  assign (snapshot.info_flag, get_init_string ("info-flag"));
  assign (snapshot.page_rendering, document_get_init_page_rendering ());
  assign (snapshot.background_color, get_init_string ("bg-color"));

  array<string> styleNames= get_style_names ();
  for (int i=0; i<N(styleNames); ++i)
    append_choice (
      snapshot.document_styles, styleNames[i],
      document_has_main_style (styleNames[i]));
  array<string> packageNames= get_package_names ();
  for (int i=0; i<N(packageNames); ++i)
    append_choice (
      snapshot.document_packages, packageNames[i],
      document_has_style_package (packageNames[i]));
  try {
    list<string> styles= as_list_string (document_get_style_list ());
    if (!is_nil (styles)) {
      styles= styles->next;
      for (; !is_nil (styles); styles= styles->next)
        if (!hidden_package (styles->item))
          append_choice (snapshot.current_packages, styles->item, true);
    }
  }
  catch (...) {}

  try {
    snapshot.beamer_style=
      as_bool (call ("style-has?", object (string ("beamer-style"))));
  }
  catch (...) {}
  try {
    snapshot.automate_style=
      as_bool (call ("style-has?", object (string ("automate-dtd"))));
  }
  catch (...) {}
  try {
    snapshot.commutative_diagram=
      as_bool (call ("in-commutative-diagram?"));
  }
  catch (...) {}
  const bool poster=
    (editor_style_command_flags &
     ACTOR_EDITOR_COMMAND_STATE_POSTER_STYLE) != 0;
  string themeKind= poster ? string ("poster"):
                     snapshot.beamer_style ? string ("beamer"):
                                             string ("basic");
  assign (snapshot.document_theme_kind, themeKind);
  auto append_theme_list=
    [&] (const char* procedure,
         std::vector<actor_focus_choice_snapshot>& target) {
      try {
        list<string> themes= as_list_string (call (procedure));
        for (; !is_nil (themes); themes= themes->next)
          append_choice (
            target, themes->item,
            document_has_style_package (themes->item));
      }
      catch (...) {}
    };
  if (poster) {
    append_theme_list ("poster-themes", snapshot.document_themes);
    append_theme_list (
      "poster-title-styles", snapshot.document_title_themes);
    snapshot.background_available= true;
  }
  else if (snapshot.beamer_style) {
    append_theme_list ("beamer-themes", snapshot.document_themes);
    snapshot.background_available= true;
  }
  else {
    append_theme_list ("basic-themes", snapshot.document_themes);
    for (const char* extra: {"alt-colors", "framed-theorems"}) {
      const string name (extra);
      actor_focus_choice_snapshot item;
      assign (item.value, name);
      assign (
        item.label,
        name == "alt-colors" ? string ("Alternative colors"):
                               string ("Framed theorems"));
      item.checked= document_has_style_package (name);
      snapshot.document_themes.push_back (std::move (item));
    }
    try {
      snapshot.background_available=
        as_string (call ("current-basic-theme")) != "plain";
    }
    catch (...) {}
  }

  try { snapshot.has_preamble= as_bool (call ("buffer-has-preamble?")); }
  catch (...) {}
  try { snapshot.preamble_mode= as_bool (call ("in-preamble-mode?")); }
  catch (...) {}
  snapshot.save_aux= get_init_string (SAVE_AUX) == "true";
  string citationStyle=
    get_user_preference ("materials csl style", "springer-mathphys");
  tree document= the_buffer ();
  for (int i=0; i<N(document); ++i) {
    tree initial= document[i];
    if (!is_compound (initial, "initial", 1)) continue;
    tree attributes= initial[0];
    if (!is_func (attributes, COLLECTION)) continue;
    for (int j=0; j<N(attributes); ++j) {
      tree entry= attributes[j];
      if (!is_compound (entry, "associate", 2) ||
          !is_atomic (entry[0]) || !is_atomic (entry[1]) ||
          as_string (entry[0]) != "materials-csl-style")
        continue;
      string explicitStyle= as_string (entry[1]);
      if (explicitStyle == "") continue;
      citationStyle= explicitStyle;
      snapshot.materials_citation_default= false;
      break;
    }
    if (!snapshot.materials_citation_default) break;
  }
  assign (snapshot.materials_citation_style, citationStyle);
  return snapshot;
}

actor_popup_menu_snapshot
editor_rep::popup_menu_state_snapshot () {
  actor_popup_menu_snapshot snapshot;
  snapshot.ready= true;

  range_set ranges= get_alt_selection ("spell-live");
  path cursor= the_path ();
  path first;
  path last;
  bool found= false;
  for (int i= 0; i + 1 < N(ranges); i += 2)
    if (path_less_eq (ranges[i], cursor) &&
        path_less (cursor, ranges[i + 1])) {
      first= ranges[i];
      last= ranges[i + 1];
      found= true;
      break;
    }
  if (!found) return snapshot;

  tree selected= selection_compute (the_root (), first, last);
  if (!is_atomic (selected) || N(selected->label) == 0) return snapshot;
  string word= selected->label;
  snapshot.spell_word.assign (
    word.data (), static_cast<std::size_t> (N(word)));

  tree language= get_env_value ("language", first);
  string lan= is_atomic (language) ? as_string (language):
                                    get_init_string ("language");
  tree checked= spell_check (lan, word);
  if (is_tuple (checked))
    for (int i= 1; i < N(checked) && i <= 9; ++i)
      if (is_atomic (checked[i])) {
        string value= as_string (checked[i]);
        snapshot.spell_suggestions.emplace_back (
          value.data (), static_cast<std::size_t> (N(value)));
      }
  return snapshot;
}

actor_focus_toolbar_snapshot
editor_rep::focus_toolbar_state_snapshot () {
  actor_focus_toolbar_snapshot snapshot;
  if (buf == nullptr || inside_graphics (false)) return snapshot;

  path focus= focus_get ();
  if (!test_subtree (focus)) return snapshot;
  tree t= the_subtree (focus);
  snapshot.flags= ACTOR_FOCUS_TOOLBAR_VALID;

  for (path p= focus; !is_nil (p); p= path_up (p)) {
    if (!test_subtree (p)) continue;
    tree node= the_subtree (p);
    if (!snapshot.transclusion_context && is_compound (node, "transclude"))
      snapshot.transclusion_context= true;
    if (!snapshot.referenced_materials_context &&
        is_compound (node, "referenced-materials", 3)) {
      snapshot.referenced_materials_context= true;
      if (is_atomic (node[0])) {
        string style= as_string (node[0]);
        snapshot.materials_reference_style.assign (
          style.data (), static_cast<std::size_t> (N(style)));
      }
    }
  }

  auto query= [&] (const char* procedure, bool fallback= false) {
    try {
      object value= call (procedure, object (t));
      return is_bool (value) ? as_bool (value) : fallback;
    }
    catch (...) {
      return fallback;
    }
  };
  auto set= [&] (actor_focus_toolbar_flag flag, bool enabled) {
    if (enabled) snapshot.flags |= static_cast<std::uint32_t> (flag);
  };

  set (ACTOR_FOCUS_TOOLBAR_BUFFER, query ("tree-is-buffer?"));
  try {
    set (ACTOR_FOCUS_TOOLBAR_CODE_CONTEXT, as_bool (call ("in-code?")));
  }
  catch (...) {}
  set (ACTOR_FOCUS_TOOLBAR_SCREENS_CONTEXT, query ("screens-context?"));
  set (ACTOR_FOCUS_TOOLBAR_TABLE_CONTEXT, query ("table-markup-context?"));
  set (ACTOR_FOCUS_TOOLBAR_DOC_TITLE_CONTEXT, query ("doc-title-context?"));
  set (ACTOR_FOCUS_TOOLBAR_DOC_AUTHOR_CONTEXT, query ("doc-author-context?"));
  set (ACTOR_FOCUS_TOOLBAR_ABSTRACT_CONTEXT, query ("abstract-data-context?"));
  set (ACTOR_FOCUS_TOOLBAR_ALGORITHM_CONTEXT, query ("algorithm-context?"));
  set (ACTOR_FOCUS_TOOLBAR_MARGINAL_NOTE_CONTEXT,
       query ("marginal-note-context?"));
  set (ACTOR_FOCUS_TOOLBAR_RICH_FLOAT_CONTEXT, query ("rich-float-context?"));
  set (ACTOR_FOCUS_TOOLBAR_PHANTOM_FLOAT_CONTEXT,
       query ("phantom-float-context?"));
  set (ACTOR_FOCUS_TOOLBAR_FLOATABLE_CONTEXT, query ("floatable-context?"));
  set (ACTOR_FOCUS_TOOLBAR_FOOTNOTE_CONTEXT, query ("footnote-context?"));
  set (ACTOR_FOCUS_TOOLBAR_BALLOON_CONTEXT, query ("balloon-context?"));
  set (ACTOR_FOCUS_TOOLBAR_DETACHED_NOTE_CONTEXT,
       query ("detached-note-context?"));
  set (ACTOR_FOCUS_TOOLBAR_TITLED_CONTEXT, query ("titled-context?"));
  set (ACTOR_FOCUS_TOOLBAR_FRAME_CONTEXT,
       query ("frame-context?") || query ("frame-titled-context?"));
  set (ACTOR_FOCUS_TOOLBAR_CAN_MOVE, query ("focus-can-move?", true));
  set (ACTOR_FOCUS_TOOLBAR_CAN_INSERT_REMOVE,
       query ("focus-can-insert-remove?"));
  set (ACTOR_FOCUS_TOOLBAR_HORIZONTAL, query ("structured-horizontal?"));
  set (ACTOR_FOCUS_TOOLBAR_VERTICAL, query ("structured-vertical?"));
  set (ACTOR_FOCUS_TOOLBAR_CAN_INSERT, query ("focus-can-insert?"));
  set (ACTOR_FOCUS_TOOLBAR_CAN_REMOVE, query ("focus-can-remove?"));
  set (ACTOR_FOCUS_TOOLBAR_CURSOR_INSIDE, query ("cursor-inside?"));
  set (ACTOR_FOCUS_TOOLBAR_HAS_PREFERENCES,
       query ("focus-has-preferences?"));
  set (ACTOR_FOCUS_TOOLBAR_HAS_PARAMETERS, query ("focus-has-parameters?"));
  set (ACTOR_FOCUS_TOOLBAR_CAN_SEARCH, query ("focus-can-search?"));
  set (ACTOR_FOCUS_TOOLBAR_HAS_SEARCH_MENU,
       query ("focus-has-search-menu?"));
  try {
    object label= call ("focus-label", object (t));
    set (ACTOR_FOCUS_TOOLBAR_HAS_LABEL, is_tree (label));
    object value= generic_focus_get_label (t);
    if (is_string (value)) {
      string labelValue= as_string (value);
      snapshot.focus_label_value.assign (
        labelValue.data (), static_cast<std::size_t> (N(labelValue)));
    }
  }
  catch (...) {}

  snapshot.numbered_available= query ("numbered-context?");
  if (snapshot.numbered_available)
    snapshot.numbered_checked= query ("numbered-numbered?");
  const bool alternateFirst= query ("alternate-first?");
  const bool alternateSecond= query ("alternate-second?");
  snapshot.alternate_available= alternateFirst || alternateSecond;
  snapshot.alternate_checked= alternateSecond;
  snapshot.pure_alternate_context= query ("pure-alternate-context?");
  if (snapshot.alternate_available) {
    try {
      object label= call ("alternate-second-name", object (t));
      if (is_string (label)) {
        string value= as_string (label);
        snapshot.alternate_label.assign (
          value.data (), static_cast<std::size_t> (N(value)));
      }
      object icon= call (
        alternateSecond ? "alternate-second-icon" : "alternate-first-icon",
        object (t));
      if (is_string (icon)) {
        string value= as_string (icon);
        snapshot.alternate_icon.assign (
          value.data (), static_cast<std::size_t> (N(value)));
      }
    }
    catch (...) {}
    const string focusTag= as_string (L(t));
    if (focusTag == "wide" || focusTag == "wide*") {
      snapshot.alternate_label= "Accent below";
      snapshot.alternate_icon= "tm_wide_under";
    }
    else if (focusTag == "around" || focusTag == "around*") {
      snapshot.alternate_label= "Large brackets";
      snapshot.alternate_icon= "tm_large_around";
    }
  }
  for (int i=0; i<N(t); ++i)
    if (!drd->is_accessible_child (t, i)) {
      snapshot.hidden_toggle_available= true;
      break;
    }
  if (snapshot.hidden_toggle_available && N(focus) > 0) {
    path parentPath= path_up (focus);
    if (test_subtree (parentPath))
      snapshot.hidden_checked= is_compound (the_subtree (parentPath), "inactive");
  }

  for (int i=0; i<N(t); ++i) {
    if (!generic_hidden_child (t, i)) continue;
    const string type= get_child_type (t, i);
    if (!generic_inputter_active (t[i], type)) continue;
    actor_focus_hidden_field_snapshot field;
    field.index= i;
    string name= generic_tree_child_name_star (t, i);
    string value= generic_inputter_decode (t[i], type);
    field.name.assign (name.data (), static_cast<std::size_t> (N(name)));
    field.type.assign (type.data (), static_cast<std::size_t> (N(type)));
    field.value.assign (value.data (), static_cast<std::size_t> (N(value)));
    snapshot.hidden_fields.push_back (std::move (field));
  }

  snapshot.sqrt_context= is_compound (t, "sqrt");
  snapshot.sqrt_multiple= snapshot.sqrt_context && N(t) == 2;
  try {
    snapshot.poster_block_context=
      as_bool (call ("poster-block-context?", object (t)));
    if (snapshot.poster_block_context) {
      snapshot.poster_block_titled=
        as_bool (call ("titled-block-context?", object (t)));
      snapshot.poster_block_wide=
        as_bool (call ("block-wide?", object (t)));
    }
  }
  catch (...) {}
  snapshot.script_context= query ("script-context?");
  if (snapshot.script_context) {
    const bool only= query ("script-only-script?");
    if (only) {
      string tag= as_string (L(t));
      snapshot.script_insert_up= (tag == "lsub" || tag == "rsub");
      snapshot.script_insert_down= (tag == "lsup" || tag == "rsup");
    }
  }
  snapshot.automatic_section_context= query ("automatic-section-context?");
  if (query ("dueto-supporting-context?"))
    snapshot.dueto_available= !query ("dueto-added?");

  if (snapshot.has (ACTOR_FOCUS_TOOLBAR_TABLE_CONTEXT)) {
    const string cellMode= get_cell_mode ();
    snapshot.table_cell_mode.assign (
      cellMode.data (), static_cast<std::size_t> (N(cellMode)));
    try { snapshot.table_parwidth= as_bool (call ("table-test-parwidth?")); }
    catch (...) {}
    bool spansMore= false;
    try { spansMore= as_bool (call ("cell-spans-more?")); }
    catch (...) {}
    const bool tableSelection= selection_active_table ();
    snapshot.table_subtable_available=
      cellMode == "cell" &&
      !tableSelection &&
      (spansMore || table_nr_rows () * table_nr_columns () > 1);
    snapshot.table_subtable_spanned= spansMore;
    snapshot.table_join_cells_available= tableSelection;
    snapshot.table_reset_span_available= cellMode == "cell" && spansMore;
  }

  snapshot.pen_effect_context= format_pen_effect_context (t);
  if (snapshot.pen_effect_context) {
    static const char* pens[]= {"gaussian", "oval", "rectangular", "motion"};
    for (const char* pen: pens)
      if (format_test_effect_pen (object (t), pen)) {
        snapshot.pen_effect= pen;
        break;
      }
  }

  try {
    snapshot.overlays_context= as_bool (call ("overlays-context?", object (t)));
    snapshot.overlay_context= as_bool (call ("overlay-context?", object (t)));
    if (snapshot.overlays_context || snapshot.overlay_context) {
      object current= call (
        snapshot.overlays_context ? "overlays-current" : "overlay-current",
        object (t));
      object count= call (
        snapshot.overlays_context ? "overlays-arity" : "overlay-arity",
        object (t));
      if (is_int (current)) snapshot.overlay_current= as_int (current);
      if (is_int (count)) snapshot.overlay_count= as_int (count);
      if (snapshot.overlay_context && N(t) > 0 && is_atomic (t[0])) {
        const string ref= as_string (t[0]);
        snapshot.overlay_reference= as_int (ref);
      }
      if (snapshot.overlay_context && snapshot.overlay_count > 0)
        for (int i=1; i<=snapshot.overlay_count; ++i) {
          bool visible= false;
          try {
            visible= as_bool (
              call ("overlay-visible?", object (t), object (i)));
          }
          catch (...) {}
          snapshot.overlay_visible.push_back (visible);
        }
    }
  }
  catch (...) {}

  if (snapshot.has (ACTOR_FOCUS_TOOLBAR_BUFFER)) {
    try {
      snapshot.document_insert_title_available=
        as_bool (call ("document-propose-title?"));
    }
    catch (...) {}
    try {
      snapshot.document_insert_abstract_available=
        as_bool (call ("document-propose-abstract?"));
    }
    catch (...) {}
    try {
      snapshot.document_insert_screens_available=
        as_bool (call ("document-propose-screens?"));
    }
    catch (...) {}
    try {
      bool poster= as_bool (call ("style-has?", object (string ("poster-style"))));
      snapshot.poster_insert_title_available=
        poster && snapshot.document_insert_title_available;
    }
    catch (...) {}
    try {
      snapshot.tmdoc_insert_title_available=
        as_bool (call ("tmdoc-propose-title?"));
    }
    catch (...) {}
    try {
      snapshot.tmdoc_insert_copyright_available=
        as_bool (call ("tmdoc-propose-copyright-and-license?"));
    }
    catch (...) {}
  }

  if (snapshot.has (ACTOR_FOCUS_TOOLBAR_ALGORITHM_CONTEXT)) {
    snapshot.algorithm_numbered= query ("algorithm-numbered?");
    snapshot.algorithm_named= query ("algorithm-named?");
    snapshot.algorithm_specified= query ("algorithm-specified?");
  }
  if (snapshot.has (ACTOR_FOCUS_TOOLBAR_DETACHED_NOTE_CONTEXT))
    snapshot.detached_note_custom= query ("custom-note-context?");
  if (snapshot.has (ACTOR_FOCUS_TOOLBAR_TITLED_CONTEXT)) {
    snapshot.figure_context= query ("figure-context?");
    snapshot.titled_named= query ("titled-named?");
  }
  if (snapshot.has (ACTOR_FOCUS_TOOLBAR_FRAME_CONTEXT))
    snapshot.frame_titled= query ("frame-titled?");

  if (snapshot.has (ACTOR_FOCUS_TOOLBAR_DOC_TITLE_CONTEXT) ||
      snapshot.has (ACTOR_FOCUS_TOOLBAR_DOC_AUTHOR_CONTEXT)) {
    try {
      snapshot.title_hidden_available=
        as_bool (call ("doc-data-has-hidden?"));
      snapshot.title_hidden_checked=
        snapshot.title_hidden_available &&
        as_bool (call ("doc-data-deactivated?"));
    }
    catch (...) {}
    try {
      if (as_bool (
            call ("test-doc-title-clustering?", object (string ("cluster-all")))))
        snapshot.title_clustering= "all";
      else if (as_bool (
                 call ("test-doc-title-clustering?",
                       object (string ("cluster-by-affiliation")))))
        snapshot.title_clustering= "affiliation";
      else
        snapshot.title_clustering= "none";
    }
    catch (...) {}
  }

  bool sectionContext= query ("section-context?");
  bool bufferHasPreviousSection= false;
  if (snapshot.has (ACTOR_FOCUS_TOOLBAR_BUFFER)) {
    try {
      object previous= call ("previous-section");
      bufferHasPreviousSection= is_tree (previous);
    }
    catch (...) {}
  }
  if (sectionContext || bufferHasPreviousSection) {
    array<tree> sections= search_sections (the_buffer ());
    for (int i=0; i<N(sections); ++i) {
      string label= "Section " * as_string (i + 1);
      try {
        object name= call (
          "tm/section-get-title-string",
          object (sections[i]), object (true));
        if (is_string (name)) label= as_string (name);
      }
      catch (...) {}
      snapshot.section_names.emplace_back (
        label.data (), static_cast<std::size_t> (N(label)));
    }
    snapshot.section_navigation_available= !snapshot.section_names.empty ();
  }

  snapshot.embedded_image_context= generic_embedded_image_context (t);
  snapshot.linked_image_context= generic_linked_image_context (t);
  if (snapshot.embedded_image_context) {
    object proposal= generic_embedded_propose (t, 1);
    if (is_string (proposal)) {
      string value= as_string (proposal);
      snapshot.embedded_image_proposal.assign (
        value.data (), static_cast<std::size_t> (N(value)));
    }
  }

  if (snapshot.has (ACTOR_FOCUS_TOOLBAR_RICH_FLOAT_CONTEXT) ||
      snapshot.has (ACTOR_FOCUS_TOOLBAR_FLOATABLE_CONTEXT) ||
      snapshot.has (ACTOR_FOCUS_TOOLBAR_FOOTNOTE_CONTEXT)) {
    try {
      snapshot.multicol_style= as_bool (call ("in-multicol-style?"));
    }
    catch (...) {}
  }
  if (snapshot.has (ACTOR_FOCUS_TOOLBAR_RICH_FLOAT_CONTEXT) ||
      snapshot.has (ACTOR_FOCUS_TOOLBAR_FOOTNOTE_CONTEXT)) {
    snapshot.float_context_available=
      inside ("float") || inside ("wide-float");
    try { snapshot.float_wide= as_bool (call ("float-wide?", object (t))); }
    catch (...) {}
    try { snapshot.cursor_at_anchor= as_bool (call ("cursor-at-anchor?")); }
    catch (...) {}
  }
  if (snapshot.has (ACTOR_FOCUS_TOOLBAR_FLOATABLE_CONTEXT)) {
    try {
      snapshot.floatable_wide= as_bool (call ("floatable-wide?", object (t)));
    }
    catch (...) {}
  }
  if (snapshot.has (ACTOR_FOCUS_TOOLBAR_MARGINAL_NOTE_CONTEXT)) {
    static const char* hpos[]= {
      "normal", "left", "right", "even-left", "even-right"
    };
    for (const char* value: hpos)
      if (generic_test_marginal_note_hpos (value)) {
        snapshot.marginal_hpos= value;
        break;
      }
    static const char* valign[]= {"t", "c", "b"};
    for (const char* value: valign)
      if (generic_test_marginal_note_valign (value)) {
        snapshot.marginal_valign= value;
        break;
      }
  }
  if (snapshot.has (ACTOR_FOCUS_TOOLBAR_BALLOON_CONTEXT)) {
    static const char* halign[]= {
      "Left", "left", "center", "right", "Right"
    };
    for (const char* value: halign)
      if (generic_test_balloon_halign (value)) {
        snapshot.balloon_halign= value;
        break;
      }
    static const char* valign[]= {
      "Bottom", "bottom", "center", "top", "Top"
    };
    for (const char* value: valign)
      if (generic_test_balloon_valign (value)) {
        snapshot.balloon_valign= value;
        break;
      }
  }

  string tag= as_string (L (t));
  snapshot.tag_label.assign (tag.data (), static_cast<std::size_t> (N(tag)));
  try {
    snapshot.tag_extension=
      as_bool (call ("tree-label-extension?", symbol_object (tag)));
  }
  catch (...) {}
  if (snapshot.tag_extension) {
    try {
      snapshot.tag_macro_source_available=
        as_bool (call ("has-macro-source?", symbol_object (tag)));
    }
    catch (...) {}
    const string command= "(make '" * tag * ")";
    snapshot.tag_shortcut_command.assign (
      command.data (), static_cast<std::size_t> (N(command)));
    try {
      snapshot.tag_shortcut_exists=
        as_bool (call ("has-user-shortcut?", object (command)));
    }
    catch (...) {}
  }
  try {
    object name= call ("focus-tag-name", symbol_object (tag));
    if (is_string (name)) {
      string value= as_string (name);
      snapshot.tag_name.assign (
        value.data (), static_cast<std::size_t> (N(value)));
    }
  }
  catch (...) {}
  if (snapshot.tag_name.empty ()) snapshot.tag_name= snapshot.tag_label;

  try {
    object raw= call ("focus-variants-of", object (t));
    if (is_list (raw)) {
      array<object> values= as_array_object (raw);
      for (int i=0; i<N(values); ++i) {
        string value;
        if (is_symbol (values[i])) value= as_symbol (values[i]);
        else if (is_string (values[i])) value= as_string (values[i]);
        else continue;
        snapshot.variants.emplace_back (
          value.data (), static_cast<std::size_t> (N(value)));
        string display= value;
        try {
          object name= call ("focus-tag-name", symbol_object (value));
          if (is_string (name)) display= as_string (name);
        }
        catch (...) {}
        snapshot.variant_names.emplace_back (
          display.data (), static_cast<std::size_t> (N(display)));
      }
    }
  }
  catch (...) {}
  set (ACTOR_FOCUS_TOOLBAR_HAS_VARIANTS, snapshot.variants.size () > 1);

  auto append_parameter=
    [&] (std::vector<actor_focus_parameter_snapshot>& target,
         const string& parameter, const string& explicitLabel,
         object mode) {
      const std::string key (
        parameter.data (), static_cast<std::size_t> (N(parameter)));
      for (const auto& existing: target)
        if (existing.name == key) return;

      actor_focus_parameter_snapshot item;
      item.name= key;
      string label= explicitLabel;
      if (N(label) == 0) {
        try { label= generic_parameter_name (parameter); }
        catch (...) { label= parameter; }
      }
      item.label.assign (
        label.data (), static_cast<std::size_t> (N(label)));
      try {
        object type= call (
          "tree-label-type", symbol_object (parameter));
        if (is_string (type)) {
          string value= as_string (type);
          item.type.assign (
            value.data (), static_cast<std::size_t> (N(value)));
        }
      }
      catch (...) {}
      try {
        string current= generic_parameter_get_string (parameter, mode);
        item.current.assign (
          current.data (), static_cast<std::size_t> (N(current)));
      }
      catch (...) {}
      try { item.is_default= generic_parameter_default (parameter, mode); }
      catch (...) {}

      try {
        object choices= format_parameter_choices (parameter);
        if (is_list (choices)) {
          array<object> values= as_array_object (choices);
          for (int i=0; i<N(values); ++i) {
            actor_focus_parameter_choice_snapshot choice;
            if (is_string (values[i])) {
              string value= as_string (values[i]);
              choice.value.assign (
                value.data (), static_cast<std::size_t> (N(value)));
              choice.label= choice.value;
            }
            else if (is_list (values[i])) {
              array<object> pair= as_array_object (values[i]);
              if (N(pair) != 2 ||
                  !is_string (pair[0]) || !is_string (pair[1]))
                continue;
              string labelValue= as_string (pair[0]);
              string value= as_string (pair[1]);
              choice.label.assign (
                labelValue.data (),
                static_cast<std::size_t> (N(labelValue)));
              choice.value.assign (
                value.data (), static_cast<std::size_t> (N(value)));
            }
            else continue;
            item.choices.push_back (std::move (choice));
          }
        }
      }
      catch (...) {}
      target.push_back (std::move (item));
    };

  object globalMode= keyword_object ("global");
  array<object> localModeItems;
  localModeItems << keyword_object ("local")
                 << symbol_object (as_string (L (t)));
  object localMode= as_list_object (localModeItems);

  auto append_parameter_list=
    [&] (std::vector<actor_focus_parameter_snapshot>& target,
         object mode) {
      try {
        object raw= generic_focus_parameters_list_memo (t, mode);
        if (!is_list (raw)) return;
        array<object> values= as_array_object (raw);
        for (int i=0; i<N(values); ++i)
          if (is_string (values[i]))
            append_parameter (
              target, as_string (values[i]), string (""), mode);
      }
      catch (...) {}
    };
  append_parameter_list (snapshot.global_parameters, globalMode);
  append_parameter_list (snapshot.local_parameters, localMode);

  try {
    object raw= format_customizable_parameters_memo (t);
    if (is_list (raw)) {
      array<object> values= as_array_object (raw);
      for (int i=0; i<N(values); ++i) {
        if (!is_list (values[i])) continue;
        array<object> pair= as_array_object (values[i]);
        if (N(pair) < 1 || !is_string (pair[0])) continue;
        string label= "";
        if (N(pair) > 1 && is_string (pair[1])) label= as_string (pair[1]);
        append_parameter (
          snapshot.local_parameters, as_string (pair[0]), label, localMode);
      }
    }
  }
  catch (...) {}

  try {
    object themes= call ("search-tag-themes", object (t));
    if (is_list (themes)) {
      array<object> themeValues= as_array_object (themes);
      for (int i=0; i<N(themeValues); ++i) {
        string theme;
        if (is_string (themeValues[i])) theme= as_string (themeValues[i]);
        else if (is_symbol (themeValues[i])) theme= as_symbol (themeValues[i]);
        else continue;
        object members= call ("theme->members", object (theme));
        if (!is_list (members)) continue;
        array<object> memberValues= as_array_object (members);
        for (int j=0; j<N(memberValues); ++j) {
          string member;
          if (is_string (memberValues[j]))
            member= as_string (memberValues[j]);
          else if (is_symbol (memberValues[j]))
            member= as_symbol (memberValues[j]);
          else continue;
          string variable= theme * "-" * member;
          append_parameter (
            snapshot.global_parameters, variable, string (""), globalMode);
          append_parameter (
            snapshot.local_parameters, variable, string (""), localMode);
        }
      }
    }
  }
  catch (...) {}

  try {
    object options= call ("search-tag-options", object (t));
    if (is_list (options)) {
      array<object> values= as_array_object (options);
      for (int i=0; i<N(values); ++i) {
        string option;
        if (is_string (values[i])) option= as_string (values[i]);
        else if (is_symbol (values[i])) option= as_symbol (values[i]);
        else continue;
        actor_focus_style_option_snapshot item;
        item.name.assign (
          option.data (), static_cast<std::size_t> (N(option)));
        string label= document_style_get_menu_name (option);
        item.label.assign (
          label.data (), static_cast<std::size_t> (N(label)));
        object help= document_style_get_documentation (object (option));
        if (is_string (help)) {
          string text= as_string (help);
          item.help.assign (
            text.data (), static_cast<std::size_t> (N(text)));
        }
        item.checked= document_has_style_package (option);
        snapshot.style_options.push_back (std::move (item));
      }
    }
  }
  catch (...) {}

  if (snapshot.has (ACTOR_FOCUS_TOOLBAR_CODE_CONTEXT)) {
    string language= get_env_string ("prog-language");
    string display= language;
    try {
      object name= call ("format-get-name", object (language));
      if (is_string (name)) display= as_string (name);
    }
    catch (...) {}
    snapshot.code_language.assign (
      display.data (), static_cast<std::size_t> (N(display)));
  }
  if (snapshot.has (ACTOR_FOCUS_TOOLBAR_BUFFER) ||
      snapshot.has (ACTOR_FOCUS_TOOLBAR_SCREENS_CONTEXT)) {
    try {
      list<string> styles= as_list_string (call ("get-style-list"));
      if (!is_nil (styles)) {
        const string style= styles->item;
        snapshot.document_style.assign (
          style.data (), static_cast<std::size_t> (N(style)));
      }
    }
    catch (...) {}
    const string pageType= get_init_string ("page-type");
    const string font= document_font_display_name (get_init_string ("font"));
    const string fontSize= get_init_string ("font-base-size");
    const string language= document_get_language ();
    snapshot.page_type.assign (
      pageType.data (), static_cast<std::size_t> (N(pageType)));
    snapshot.document_font.assign (
      font.data (), static_cast<std::size_t> (N(font)));
    snapshot.font_base_size.assign (
      fontSize.data (), static_cast<std::size_t> (N(fontSize)));
    snapshot.document_language.assign (
      language.data (), static_cast<std::size_t> (N(language)));

    auto display_name= [] (string name) {
      return upcase_first (replace (name, "-", " "));
    };
    auto append_choice=
      [&] (std::vector<actor_focus_choice_snapshot>& target,
           string value, bool checked) {
        actor_focus_choice_snapshot item;
        item.value.assign (
          value.data (), static_cast<std::size_t> (N(value)));
        string label= display_name (value);
        item.label.assign (
          label.data (), static_cast<std::size_t> (N(label)));
        item.checked= checked;
        target.push_back (std::move (item));
      };

    array<string> styleNames= get_style_names ();
    for (int i=0; i<N(styleNames); ++i)
      append_choice (
        snapshot.document_styles, styleNames[i],
        document_has_main_style (styleNames[i]));
    array<string> packageNames= get_package_names ();
    for (int i=0; i<N(packageNames); ++i)
      append_choice (
        snapshot.document_packages, packageNames[i],
        document_has_style_package (packageNames[i]));
    try {
      list<string> styles= as_list_string (document_get_style_list ());
      if (!is_nil (styles)) {
        styles= styles->next;
        for (; !is_nil (styles); styles= styles->next)
          if (!hidden_package (styles->item))
            append_choice (
              snapshot.current_packages, styles->item, true);
      }
    }
    catch (...) {}

    const string background= get_init_string ("bg-color");
    snapshot.background_color.assign (
      background.data (), static_cast<std::size_t> (N(background)));

    try {
      snapshot.beamer_style=
        as_bool (call ("style-has?", object (string ("beamer-style"))));
    }
    catch (...) {}
    const bool poster=
      (editor_style_command_flags &
       ACTOR_EDITOR_COMMAND_STATE_POSTER_STYLE) != 0;
    string themeKind= poster ? string ("poster"):
                       snapshot.beamer_style ? string ("beamer"):
                       string ("basic");
    snapshot.document_theme_kind.assign (
      themeKind.data (), static_cast<std::size_t> (N(themeKind)));

    auto append_theme_list=
      [&] (const char* procedure,
           std::vector<actor_focus_choice_snapshot>& target) {
        try {
          list<string> themes= as_list_string (call (procedure));
          for (; !is_nil (themes); themes= themes->next)
            append_choice (
              target, themes->item,
              document_has_style_package (themes->item));
        }
        catch (...) {}
      };
    if (poster) {
      append_theme_list ("poster-themes", snapshot.document_themes);
      append_theme_list (
        "poster-title-styles", snapshot.document_title_themes);
      snapshot.background_available= true;
    }
    else if (snapshot.beamer_style) {
      append_theme_list ("beamer-themes", snapshot.document_themes);
      snapshot.background_available= true;
    }
    else {
      append_theme_list ("basic-themes", snapshot.document_themes);
      for (const char* extra: {"alt-colors", "framed-theorems"}) {
        const string name (extra);
        actor_focus_choice_snapshot item;
        item.value.assign (
          name.data (), static_cast<std::size_t> (N(name)));
        const string label=
          name == "alt-colors" ? string ("Alternative colors"):
                                 string ("Framed theorems");
        item.label.assign (
          label.data (), static_cast<std::size_t> (N(label)));
        item.checked= document_has_style_package (name);
        snapshot.document_themes.push_back (std::move (item));
      }
      try {
        snapshot.background_available=
          as_string (call ("current-basic-theme")) != "plain";
      }
      catch (...) {}
    }

    if (snapshot.has (ACTOR_FOCUS_TOOLBAR_SCREENS_CONTEXT)) {
      try {
        object rawSwitch= call ("slide-get-switch", object (t));
        if (is_tree (rawSwitch)) {
          tree sw= as_tree (rawSwitch);
          for (int i=0; i<N(sw); ++i) {
            string label= "Slide " * as_string (i + 1);
            try {
              object name= call (
                "get-slide-name", object (sw[i]), object (i));
              if (is_string (name)) label= as_string (name);
            }
            catch (...) {}
            snapshot.slide_names.emplace_back (
              label.data (), static_cast<std::size_t> (N(label)));
          }
        }
      }
      catch (...) {}
      try {
        object rawDocument= call ("slide-get-document", object (t));
        if (is_tree (rawDocument)) {
          tree slide= as_tree (rawDocument);
          const bool hasTitle=
            N(slide) > 0 && is_compound (slide[0], "tit");
          snapshot.slide_propose_title= !hasTitle;
          const bool emptyDocument=
            is_document (slide) && N(slide) == 1 && slide[0] == tree ("");
          const bool titleOnly=
            is_document (slide) && N(slide) == 1 && hasTitle;
          const bool titleAndEmpty=
            is_document (slide) && N(slide) == 2 && hasTitle &&
            slide[1] == tree ("");
          snapshot.slide_propose_graphics=
            emptyDocument || titleOnly || titleAndEmpty;
        }
      }
      catch (...) {}
    }
  }
  return snapshot;
}

void
editor_rep::refresh_editor_style_command_flags () {
  if (editor_style_command_flags_valid) return;
  editor_style_command_flags= 0;
  if (buf == nullptr) return;
  auto style_has= [] (const char* capability) {
    try {
      return as_bool (call ("style-has?", object (string (capability))));
    }
    catch (...) {
      return false;
    }
  };
  if (style_has ("poster-style"))
    editor_style_command_flags |= ACTOR_EDITOR_COMMAND_STATE_POSTER_STYLE;
  if (style_has ("tmdoc-style") && !is_rooted_tmfs (buf->name))
    editor_style_command_flags |= ACTOR_EDITOR_COMMAND_STATE_MANUAL_STYLE;
  if (style_has ("header-letter-dtd"))
    editor_style_command_flags |= ACTOR_EDITOR_COMMAND_STATE_HEADER_LETTER;
  if (style_has ("book-style"))
    editor_style_command_flags |= ACTOR_EDITOR_COMMAND_STATE_BOOK_STYLE;
  if (style_has ("section-base-dtd"))
    editor_style_command_flags |= ACTOR_EDITOR_COMMAND_STATE_SECTION_BASE;
  if (style_has ("env-theorem-dtd"))
    editor_style_command_flags |= ACTOR_EDITOR_COMMAND_STATE_ENV_THEOREM;
  if (style_has ("std-markup-dtd"))
    editor_style_command_flags |= ACTOR_EDITOR_COMMAND_STATE_STD_MARKUP;
  if (style_has ("std-list-dtd"))
    editor_style_command_flags |= ACTOR_EDITOR_COMMAND_STATE_STD_LIST;
  if (style_has ("env-float-dtd"))
    editor_style_command_flags |= ACTOR_EDITOR_COMMAND_STATE_ENV_FLOAT;
  if (style_has ("std-fold-dtd"))
    editor_style_command_flags |= ACTOR_EDITOR_COMMAND_STATE_STD_FOLD;
  if (style_has ("std-dtd"))
    editor_style_command_flags |= ACTOR_EDITOR_COMMAND_STATE_STD_DTD;
  if (style_has ("env-math-dtd"))
    editor_style_command_flags |= ACTOR_EDITOR_COMMAND_STATE_ENV_MATH;
  editor_style_command_flags_valid= true;
}

void
editor_rep::publish_editor_command_state () {
  if (ui_endpoint == nullptr) return;
  string par_sep= get_env_string ("par-par-sep");
  bool prominent_spacing=
    !ends (par_sep, "fns") && !starts (par_sep, "0fn");
  ui_endpoint->set_prominent_spacing_available (prominent_spacing);
  ui_endpoint->set_inside_table (inside ("table"));
  ui_endpoint->update_editor_command_state (editor_command_state_snapshot ());
  publish_document_menu_state ();
}

void
editor_rep::publish_document_menu_state () {
  if (ui_endpoint == nullptr) return;
  ui_endpoint->update_document_menu_state (document_menu_state_snapshot ());
}

void
editor_rep::publish_focus_toolbar_state () {
  if (ui_endpoint == nullptr) return;
  ui_endpoint->update_focus_toolbar_state (focus_toolbar_state_snapshot ());
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

  update_live_spelling ();

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
    // Typing does not rebuild menus, but must refresh document statistics.
    // Keep this pending across unrelated repaint/extent changes until idle.
    if (pending_idle_footer_update &&
        idle_time (INTERRUPTED_EVENT) >= 1000/6) {
      set_footer ();
      pending_idle_footer_update= false;
    }
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
    the_ghost_cursor ()= eb->find_check_cursor (tp, the_cursor ()->affinity);
    SI heading_strip_width= 80 * pixel;
    invalidate (old_heading_right - heading_strip_width, vy1,
                old_heading_right + 2 * pixel, vy2);
    if (!is_nil (eb) && eb->x2 != old_heading_right)
      invalidate (eb->x2 - heading_strip_width, vy1,
                  eb->x2 + 2 * pixel, vy2);
    invalidate (x1- 2*pixel, y1- 2*pixel, x2+ 2*pixel, y2+ 2*pixel);
    mark_native_ink_interaction_dirty ();
    refresh_native_ink_interaction ();
    (void) publish_ui (actor_command_kind::ui_native_drawing_focus_refresh);
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

  // cout << "Handling environment changes\n";
  if (env_change & THE_ENVIRONMENT)
    invalidate_all ();

  // A resize in reflow mode is the one place where viewport motion is part of
  // the layout transaction itself. Preserve the cursor's vertical distance
  // from the old viewport top once, after the new box tree and cursor geometry
  // are available. Never override a user scroll, link navigation, or another
  // programmatic scroll which superseded the resize snapshot.
  if (resize_viewport_restore.pending) {
    bool programmatic_scroll_pending=
      ui_endpoint != nullptr &&
      ui_endpoint->applied_programmatic_scroll_generation () <
        programmatic_scroll_generation;
    if (get_init_string (PAGE_MEDIUM) != "automatic" ||
        ui_endpoint == nullptr ||
        !resize_viewport_restore.matches (
          tp, programmatic_scroll_generation,
          ui_endpoint->user_scroll_generation ()) ||
        programmatic_scroll_pending || is_nil (eb))
      resize_viewport_restore.cancel ();
    else {
      actor_viewport_snapshot resize_viewport= ui_viewport ();
      SI visible_height=
        resize_viewport.visible_y2 - resize_viewport.visible_y1;
      SI visible_width=
        resize_viewport.visible_x2 - resize_viewport.visible_x1;
      if (visible_height > 0 && visible_width > 0) {
        SI height= (SI) (visible_height / magf);
        SI center_x= (SI) (((resize_viewport.visible_x1 +
                             resize_viewport.visible_x2) / 2) / magf);
        SI center_y= resize_viewport_target_center_y (
          get_cursor_y (), resize_viewport_restore.cursor_distance_from_top,
          height);
        std::uint64_t expected_user_generation=
          resize_viewport_restore.user_scroll_generation;
        resize_viewport_restore.cancel ();
        scroll_to_if_user_unchanged (
          center_x, center_y, expected_user_generation);
        invalidate_all ();
      }
      else resize_viewport_restore.cancel ();
    }
  }

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
  publish_editor_command_state ();
  env_change  = 0;
  last_change = texmacs_time ();
  pending_idle_menu_update= schedule_idle_menu_update;
  last_update = schedule_idle_menu_update? last_change-1: last_change;
  last_visible= new_visible;
  manual_focus_release ();
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
  if (ui_endpoint != nullptr)
    (void) publish_ui (actor_command_kind::ui_refresh_chrome, 1);
  else windows_delayed_refresh (1);
}

void
edit_interface_rep::cancel_menu_action () {
  notify_change (THE_DECORATIONS);
  cancel_editing ();
  if (ui_endpoint != nullptr)
    (void) publish_ui (actor_command_kind::ui_refresh_chrome, 1);
  else windows_delayed_refresh (1);
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
  return eb->find_check_cursor (p, p == tp ? the_cursor ()->affinity :
                                athena::text::caret_affinity::downstream);
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
  clear_link_peek ();
  if (buf == nullptr || is_nil (eb)) return;
  resize_viewport_restore.cancel ();
  typewriter_manual_scroll_time= t;
  typewriter_manual_scroll_path= copy (tp);
}

void
edit_interface_rep::handle_get_size_hint (SI& w, SI& h) {
  gui_root_extents (w, h);
}

void
edit_interface_rep::handle_notify_resize (
  SI w, SI h, SI old_vy2, bool old_viewport_valid,
  std::uint64_t old_programmatic_scroll_generation,
  std::uint64_t old_user_scroll_generation) {
  if (buf == nullptr) return;
  bool width_changed= w != resize_wx;
  bool height_changed= h != resize_wy;
  resize_wx= w;
  resize_wy= h;
  if (!width_changed && !height_changed) return;

  resize_viewport_restore.cancel ();
  bool cursor_or_layout_pending=
    (env_change & (THE_TREE + THE_ENVIRONMENT + THE_CURSOR)) != 0;
  bool viewport_snapshot_current=
    ui_endpoint != nullptr &&
    resize_viewport_snapshot_current (
      programmatic_scroll_generation, old_programmatic_scroll_generation,
      ui_endpoint->user_scroll_generation (), old_user_scroll_generation);
  if (!is_embedded_widget () &&
      get_init_string (PAGE_MEDIUM) == "automatic" &&
      old_viewport_valid && !is_nil (eb) &&
      viewport_snapshot_current && !cursor_or_layout_pending) {
    SI old_top= (SI) (old_vy2 / magf);
    resize_viewport_restore.arm (
      tp, get_cursor_y (), old_top, programmatic_scroll_generation,
      old_user_scroll_generation);
  }

  notify_change ((width_changed ? THE_TREE : THE_EXTENTS) + THE_FREEZE);
  if (width_changed &&
      as_bool (call ("defined?",
                     symbol_object ("schedule-persistent-fit-width"))))
    call ("schedule-persistent-fit-width");
}

double
edit_interface_rep::handle_get_zoom_factor () {
  return zoomf;
}

void
edit_interface_rep::handle_set_zoom_factor (double zoom) {
  set_zoom_factor (zoom);
  if (ui_endpoint != nullptr) ui_endpoint->set_zoom_factor (zoom);
}
