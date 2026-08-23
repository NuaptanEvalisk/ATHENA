/******************************************************************************
* MODULE     : edit_heading_cells.cpp
* DESCRIPTION: screen-only heading cell brackets
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
*******************************************************************************/

#include "edit_interface.hpp"

static int
heading_bracket_minimum_level (const array<heading_cell_bracket>& brackets) {
  int minimum_level= N(brackets) == 0 ? 0 : brackets[0].level;
  for (int i=0; i<N(brackets); i++)
    minimum_level= min (minimum_level, brackets[i].level);
  return minimum_level;
}

static SI
heading_bracket_x (heading_cell_bracket bracket, int minimum_level,
                   SI rendering_right, SI pixel) {
  int depth= max (0, min (8, bracket.level - minimum_level));
  return rendering_right - (12 + 7 * depth) * pixel;
}

array<heading_cell_bracket>
edit_interface_rep::heading_cell_brackets () {
  if (is_embedded_widget () || get_init_string (MODE) == "src")
    return array<heading_cell_bracket> ();
  if (heading_cell_cache_valid) return heading_cell_cache;

  heading_cell_cache_valid= true;
  heading_cell_cache= array<heading_cell_bracket> ();
  if (is_nil (eb)) return heading_cell_cache;

  array<heading_cell_range> ranges= heading_cell_ranges ();
  for (int i=0; i<N(ranges); i++) {
    selection sel= search_selection (ranges[i].selection_start,
                                     ranges[i].heading_end);
    if (!sel->valid || is_nil (sel->rs)) continue;
    rectangle bounds= least_upper_bound (sel->rs);
    if (bounds->y2 <= bounds->y1) continue;

    heading_cell_bracket bracket;
    bracket.level          = ranges[i].level;
    bracket.heading_path   = ranges[i].heading_path;
    bracket.selection_start= ranges[i].selection_start;
    bracket.selection_end  = ranges[i].selection_end;
    bracket.y1             = bounds->y1;
    bracket.y2             = bounds->y2;
    bracket.folded         = ranges[i].folded;
    heading_cell_cache << bracket;
  }

  for (int i=0; i<N(heading_cell_cache); i++) {
    if (heading_cell_cache[i].folded) continue;
    SI bottom= eb->y1;
    for (int j=i+1; j<N(heading_cell_cache); j++)
      if (heading_cell_cache[j].level <= heading_cell_cache[i].level) {
        bottom= heading_cell_cache[j].y2;
        break;
      }
    heading_cell_cache[i].y1= min (heading_cell_cache[i].y1, bottom);
  }
  return heading_cell_cache;
}

bool
edit_interface_rep::heading_cell_bracket_at (
  SI x, SI y, heading_cell_bracket& bracket) {
  array<heading_cell_bracket> brackets= heading_cell_brackets ();
  update_visible ();
  rectangle visible (vx1, vy1, vx2, vy2);
  int minimum_level= heading_bracket_minimum_level (brackets);
  SI hit_width= 7 * pixel;
  for (int i=N(brackets)-1; i>=0; i--) {
    SI bx= heading_bracket_x (brackets[i], minimum_level, eb->x2, pixel);
    SI low = max (brackets[i].y1, visible->y1);
    SI high= min (brackets[i].y2, visible->y2);
    if (high < low || x < bx-hit_width || x > bx+hit_width ||
        y < low || y > high)
      continue;
    bracket= brackets[i];
    return true;
  }
  return false;
}

void
edit_interface_rep::select_heading_cell (heading_cell_bracket bracket) {
  set_selection (bracket.selection_start, bracket.selection_end);
  notify_change (THE_SELECTION + THE_DECORATIONS);
}

void
edit_interface_rep::draw_heading_cell_brackets (renderer ren,
                                                rectangle repaint) {
  array<heading_cell_bracket> brackets= heading_cell_brackets ();
  if (N(brackets) == 0) return;

  update_visible ();
  rectangle visible (vx1, vy1, vx2, vy2);
  int minimum_level= heading_bracket_minimum_level (brackets);
  path selected_start, selected_end;
  get_selection (selected_start, selected_end);
  pencil old= ren->get_pencil ();

  for (int i=0; i<N(brackets); i++) {
    heading_cell_bracket bracket= brackets[i];
    SI low = max (bracket.y1, visible->y1 + 2 * pixel);
    SI high= min (bracket.y2, visible->y2 - 2 * pixel);
    if (high <= low || high < repaint->y1 || low > repaint->y2) continue;

    SI x= heading_bracket_x (bracket, minimum_level, eb->x2, pixel);
    SI cap= 6 * pixel;
    bool selected=
      (selected_start == bracket.selection_start &&
       selected_end == bracket.selection_end) ||
      (selected_start == bracket.selection_end &&
       selected_end == bracket.selection_start);
    bool hovered= heading_cell_hovered == bracket.heading_path;
    color col= selected ? rgb_color (54, 108, 166)
                        : hovered ? rgb_color (92, 92, 92)
                                  : rgb_color (145, 145, 145);
    SI width= (selected || hovered) ? 2 * ren->pixel : ren->pixel;
    ren->set_pencil (pencil (col, width));
    ren->line (x-cap, high, x, high);
    ren->line (x-cap, low, x, low);

    if (!bracket.folded) ren->line (x, low, x, high);
    else {
      SI middle= (low + high) / 2;
      SI notch= min (4 * pixel, (high - low) / 4);
      ren->line (x, high, x, middle + notch);
      ren->line (x, middle + notch, x-cap, middle);
      ren->line (x-cap, middle, x, middle - notch);
      ren->line (x, middle - notch, x, low);
    }
  }
  ren->set_pencil (old);
}
