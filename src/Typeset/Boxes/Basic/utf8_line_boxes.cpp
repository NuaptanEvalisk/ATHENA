/******************************************************************************
* MODULE     : utf8_line_boxes.cpp
* DESCRIPTION: Multi-font Unicode line display, hit testing and visual selection
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#include "Boxes/utf8_line.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace {
using namespace athena::text;

SI offset (SI x, SI dx) {
  if ((dx > 0 && x > std::numeric_limits<SI>::max () - dx) ||
      (dx < 0 && x < std::numeric_limits<SI>::min () - dx))
    throw std::overflow_error ("Unicode line box exceeds coordinates");
  return x + dx;
}

struct utf8_line_box_rep final: box_rep {
  std::shared_ptr<font_paragraph> paragraph;
  int begin, end;
  font nominal;
  pencil pen;
  brush background;
  shaping_options options;
  double horizontal_scale;
  shaped_line line;

  utf8_line_box_rep (path ip, std::shared_ptr<font_paragraph> source,
                     int first, int last, font fn, pencil p,
                     const shaping_options& opts, brush bg, double scale):
    box_rep (ip), paragraph (std::move (source)), begin (first), end (last),
    nominal (fn), pen (p), background (bg), options (opts), horizontal_scale (scale) {
    if (!paragraph || begin < 0 || end < begin ||
        static_cast<std::size_t> (end) > paragraph->analysis ().source ().size ())
      throw std::invalid_argument ("Invalid Unicode line box source range");
    line= paragraph->line (begin, end, options, scale);
    x1= min (0, line.advance); x2= max (0, line.advance);
    y1= nominal->y1; y2= nominal->y2;
    x3= x4= y3= y4= 0;
    bool ink= false;
    for (const auto& placed: line.runs) {
      const auto& run= placed.text;
      if (!run.has_ink) continue;
      const auto left= offset (placed.x, run.ink_x1), right= offset (placed.x, run.ink_x2);
      x3= ink ? min (x3, left) : left;
      x4= ink ? max (x4, right) : right;
      y3= ink ? min (y3, run.ink_y1) : run.ink_y1;
      y4= ink ? max (y4, run.ink_y2) : run.ink_y2;
      ink= true;
    }
    y1= min (y1, y3); y2= max (y2, y4);
  }

  std::string_view bytes () const { return paragraph->analysis ().source (); }
  int snap (int relative) const {
    const auto byte= static_cast<std::size_t> (begin + std::clamp (relative, 0, end - begin));
    const auto at= std::upper_bound (line.carets.begin (), line.carets.end (), byte,
      [] (std::size_t b, const line_caret& c) { return b < c.byte; });
    return static_cast<int> ((at - 1)->byte) - begin;
  }
  int relative (path bp) const { return snap (is_nil (bp) ? 0 : bp->item); }
  caret_affinity affinity (path bp) const {
    if (is_nil (bp) || is_nil (bp->next)) return caret_affinity::downstream;
    const int value= bp->next->item;
    if (value < static_cast<int> (caret_affinity::upstream) ||
        value > static_cast<int> (caret_affinity::both))
      throw std::invalid_argument ("Invalid line box caret affinity");
    return static_cast<caret_affinity> (value);
  }
  operator tree () override { return get_leaf_string (); }
  void display (renderer ren) override {
    if (begin == end) return;
    if (background->get_type () != brush_none) {
      const auto previous= ren->get_background ();
      ren->set_background (background);
      ren->clear_pattern (x1, y1, x2, y2);
      ren->set_background (previous);
    }
    ren->set_pencil (pen);
    line.draw_fixed (ren, bytes (), 0, 0);
  }
  box expand_glyphs (int, double factor) override {
    if (!std::isfinite (factor) || factor <= -1.0)
      throw std::invalid_argument ("Invalid Unicode line expansion");
    return utf8_line_box (ip, paragraph, begin, end, nominal,
                          pen, options, background, horizontal_scale * (1.0 + factor));
  }
  double left_slope () override { return nominal->slope * horizontal_scale; }
  double right_slope () override { return nominal->slope * horizontal_scale; }
  SI sub_lo_base (int level) override {
    return nominal->ysub_lo_base + (level > 0 ? nominal->yshift : 0);
  }
  SI sub_hi_lim (int) override { return nominal->ysub_hi_lim; }
  SI sup_lo_lim (int) override { return nominal->ysup_lo_lim; }
  SI sup_lo_base (int level) override {
    return nominal->ysup_lo_base - (level < 0 ? nominal->yshift : 0);
  }
  SI sup_hi_lim (int) override { return nominal->ysup_hi_lim; }
  path find_box_path (SI x, SI, SI delta, bool, bool& found) override {
    const auto caret= line.hit_test (x, delta >= 0);
    found= true;
    return path (static_cast<int> (caret.byte) - begin, static_cast<int> (caret.affinity));
  }
  path find_lip () override { return is_accessible (ip) ? descend (ip, begin) : ip; }
  path find_rip () override { return is_accessible (ip) ? descend (ip, end) : ip; }
  path find_right_box_path () override {
    return path (end - begin, static_cast<int> (caret_affinity::upstream));
  }
  path find_box_path (path p, bool& found) override {
    found= !is_nil (p) && is_accessible (ip);
    if (!found) return path (0, static_cast<int> (caret_affinity::downstream));
    const int position= snap (std::clamp (last_item (p), begin, end) - begin);
    found= position + begin == last_item (p);
    return path (position, static_cast<int> (caret_affinity::downstream));
  }
  path find_tree_path (path bp) override {
    const int at= relative (bp);
    return is_accessible (ip) ? reverse (descend (ip, begin + at)) :
      reverse (descend_decode (ip, at == end - begin ? 1 : 0));
  }
  path with_cursor_affinity (path bp, caret_affinity side) override {
    return path (relative (bp), static_cast<int> (side));
  }
  cursor find_cursor (path bp) override {
    cursor result (line.caret_x (begin + relative (bp), affinity (bp)), 0);
    result->affinity= affinity (bp) == caret_affinity::both ?
      caret_affinity::downstream : affinity (bp);
    result->y1= min (y1, 0); result->y2= max (y2, nominal->yx);
    result->slope= nominal->slope * horizontal_scale;
    return result;
  }
  selection find_selection (path first, path last) override {
    rectangles regions;
    for (const auto span: line.selection_spans (begin + relative (first), begin + relative (last)))
      regions << rectangle (span.left, y1, span.right, y2);
    return selection (regions, find_tree_path (first), find_tree_path (last));
  }
  int get_type () override { return TEXT_BOX; }
  int get_leaf_left_pos () override { return begin; }
  int get_leaf_right_pos () override { return end; }
  string get_leaf_string () override { return string (bytes ().data () + begin, end - begin); }
  font get_leaf_font () override { return nominal; }
  pencil get_leaf_pencil () override { return pen; }
  brush get_leaf_background () override { return background; }
  SI get_leaf_offset (string search) override {
    const auto source= bytes ();
    const std::string_view needle (search.data (), N(search));
    for (const auto& caret: line.carets)
      if (needle.size () <= static_cast<std::size_t> (end) - caret.byte &&
          source.substr (caret.byte, needle.size ()) == needle)
        return line.caret_x (caret.byte, caret_affinity::downstream);
    return w ();
  }
};
}

box utf8_line_box (path ip, std::shared_ptr<athena::text::font_paragraph> paragraph,
                   int begin, int end, font nominal, pencil pen,
                   const athena::text::shaping_options& options,
                   brush background, double horizontal_scale) {
  return tm_new<utf8_line_box_rep> (ip, std::move (paragraph), begin, end,
                                   nominal, pen, options, background, horizontal_scale);
}
