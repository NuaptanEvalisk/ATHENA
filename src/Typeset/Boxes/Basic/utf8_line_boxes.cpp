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
#include "Boxes/modifier.hpp"
#include "Format/line_item.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace {
using namespace athena::text;

struct paragraph_flow;
struct flow_position {
  std::shared_ptr<const paragraph_flow> flow;
  int source= 0;
};

SI offset (SI x, SI dx) {
  if ((dx > 0 && x > std::numeric_limits<SI>::max () - dx) ||
      (dx < 0 && x < std::numeric_limits<SI>::min () - dx))
    throw std::overflow_error ("Unicode line box exceeds coordinates");
  return x + dx;
}

bool same_options (const shaping_options& a, const shaping_options& b) {
  return a.direction == b.direction && a.script == b.script && a.language == b.language &&
    a.ligatures == b.ligatures && a.max_glyphs == b.max_glyphs &&
    a.editing_carets == b.editing_carets && a.grapheme_fragments == b.grapheme_fragments &&
    a.max_carets == b.max_carets && a.context_begin == b.context_begin && a.context_end == b.context_end;
}

struct utf8_line_box_rep: box_rep {
  std::shared_ptr<font_paragraph> paragraph;
  int begin, end;
  font nominal;
  pencil pen;
  brush background;
  shaping_options options;
  double horizontal_scale;
  std::vector<line_space_width> space_widths;
  flow_position flow;
  shaped_line line;

  utf8_line_box_rep (path ip, std::shared_ptr<font_paragraph> source,
                     int first, int last, font fn, pencil p,
                     const shaping_options& opts, brush bg, double scale,
                     std::vector<line_space_width> spaces= {}):
    box_rep (ip), paragraph (std::move (source)), begin (first), end (last),
    nominal (fn), pen (p), background (bg), options (opts), horizontal_scale (scale),
    space_widths (std::move (spaces)) {
    if (!paragraph || begin < 0 || end < begin ||
        static_cast<std::size_t> (end) > paragraph->analysis ().source ().size ())
      throw std::invalid_argument ("Invalid Unicode line box source range");
    line= paragraph->line (begin, end, options, scale);
    line.set_space_widths (paragraph->analysis ().source (), space_widths);
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
    auto spaces= space_widths;
    for (auto& space: spaces) {
      const double width= std::round (space.width * (1.0 + factor));
      if (width > std::numeric_limits<SI>::max ())
        throw std::overflow_error ("Expanded Unicode glue exceeds coordinates");
      space.width= static_cast<SI> (width);
    }
    auto* result= tm_new<utf8_line_box_rep> (ip, paragraph, begin, end, nominal, pen,
      options, background, horizontal_scale * (1.0 + factor), std::move (spaces));
    result->flow= flow;
    return result;
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
  std::optional<caret_affinity> cursor_affinities (path bp) override {
    if (begin == end) return caret_affinity::both;
    const int at= begin + relative (bp);
    return at == begin ? caret_affinity::downstream :
      at == end ? caret_affinity::upstream : caret_affinity::both;
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

struct source_span {
  int begin, end, source_begin;
  path ip;
  font nominal;
};

struct paragraph_flow {
  path ip;
  std::shared_ptr<font_paragraph> paragraph;
  std::shared_ptr<const std::vector<source_span>> sources;
  std::vector<int> piece_sources;
  font nominal;
  pencil pen;
  brush background;
};

struct flow_marker_box_rep final: modifier_box_rep {
  flow_position flow;
  flow_marker_box_rep (box marker, flow_position position):
    modifier_box_rep (marker->ip, marker), flow (std::move (position)) {}
};

// Source order is independent of ICU's visual order. A shared byte endpoint
// can name either adjacent source node, so box paths retain a source-span id.
struct mapped_utf8_line_box_rep final: utf8_line_box_rep {
  std::shared_ptr<const std::vector<source_span>> sources;
  int first_source, last_source;

  mapped_utf8_line_box_rep (path ip, std::shared_ptr<font_paragraph> paragraph,
    std::shared_ptr<const std::vector<source_span>> spans, font nominal,
    pencil pen, brush background, double scale= 1.0,
    int first= 0, int last= -1, int source_first= 0, int source_last= -1,
    std::vector<line_space_width> spaces= {}):
    utf8_line_box_rep (ip, paragraph, first,
                       last < 0 ? paragraph->analysis ().source ().size () : last,
                       nominal, pen, {}, background, scale, std::move (spaces)),
    sources (std::move (spans)), first_source (source_first),
    last_source (source_last < 0 ? sources->size ()-1 : source_last) {
    for (int i=first_source; i<=last_source; ++i) {
      const auto& s= (*sources)[i];
      y1= min (y1, s.nominal->y1);
      y2= max (y2, s.nominal->y2);
    }
  }

  int source_index (path bp) const {
    const int at= begin + relative (bp);
    if (N(bp) >= 3) {
      const int i= bp->next->next->item;
      if (i >= first_source && i <= last_source &&
          (*sources)[i].begin <= at && at <= (*sources)[i].end) return i;
    }
    if (affinity (bp) == caret_affinity::upstream) {
      for (int i=first_source; i<=last_source; ++i)
        if ((*sources)[i].end >= at) return i;
    }
    else {
      for (int i=last_source; i>=first_source; --i)
        if ((*sources)[i].begin <= at) return i;
    }
    throw std::logic_error ("Unicode source map does not cover cursor");
  }
  path source_path (int index, int at) const {
    const auto& s= (*sources)[index];
    return is_accessible (s.ip) ? descend (s.ip, s.source_begin + at - s.begin) :
      descend_decode (s.ip, at == s.end ? 1 : 0);
  }
  path find_lip () override {
    const auto& s= (*sources)[first_source];
    return is_accessible (s.ip) ? source_path (first_source, begin) : s.ip;
  }
  path find_rip () override {
    const auto& s= (*sources)[last_source];
    return is_accessible (s.ip) ? source_path (last_source, end) : s.ip;
  }
  path find_left_box_path () override {
    return path (0, path (static_cast<int> (caret_affinity::downstream), first_source));
  }
  path find_right_box_path () override {
    return path (end-begin, path (static_cast<int> (caret_affinity::upstream), last_source));
  }
  path find_box_path (path p, bool& found) override {
    found= false;
    if (!is_nil (p)) for (int i=first_source; i<=last_source; ++i) {
      const auto& s= (*sources)[i];
      if (!is_accessible (s.ip) || path_up (p) != reverse (s.ip)) continue;
      const int at= last_item (p) - s.source_begin;
      const int absolute= s.begin + at;
      if (at < 0 || at > s.end - s.begin || absolute < begin || absolute > end ||
          snap (absolute-begin) != absolute-begin) continue;
      found= true;
      const auto side= at == s.end - s.begin ? caret_affinity::upstream : caret_affinity::downstream;
      return path (absolute-begin, path (static_cast<int> (side), i));
    }
    return path (0, static_cast<int> (caret_affinity::downstream));
  }
  path find_tree_path (path bp) override {
    return reverse (source_path (source_index (bp), begin + relative (bp)));
  }
  path with_cursor_affinity (path bp, caret_affinity side) override {
    return path (relative (bp), path (static_cast<int> (side), source_index (bp)));
  }
  box expand_glyphs (int, double factor) override {
    if (!std::isfinite (factor) || factor <= -1.0)
      throw std::invalid_argument ("Invalid Unicode line expansion");
    auto spaces= space_widths;
    for (auto& space: spaces) {
      const double width= std::round (space.width * (1.0 + factor));
      if (width > std::numeric_limits<SI>::max ())
        throw std::overflow_error ("Expanded Unicode glue exceeds coordinates");
      space.width= static_cast<SI> (width);
    }
    return tm_new<mapped_utf8_line_box_rep> (ip, paragraph, sources, nominal, pen,
      background, horizontal_scale * (1.0 + factor), begin, end,
      first_source, last_source, std::move (spaces));
  }
  int get_type () override { return box_rep::get_type (); }
};

bool same_paint (pencil a, pencil b) {
  return a == b || (a->get_brush () == b->get_brush () &&
    a->get_width () == b->get_width () && a->get_cap () == b->get_cap () &&
    a->get_join () == b->get_join () && a->get_miter_lim () == b->get_miter_lim ());
}

flow_position flow_at (box b) {
  if (auto* text= dynamic_cast<utf8_line_box_rep*> (b.operator-> ())) return text->flow;
  if (auto* marker= dynamic_cast<flow_marker_box_rep*> (b.operator-> ())) return marker->flow;
  return {};
}
}

box utf8_line_box (path ip, std::shared_ptr<athena::text::font_paragraph> paragraph,
                   int begin, int end, font nominal, pencil pen,
                   const athena::text::shaping_options& options,
                   brush background, double horizontal_scale) {
  return tm_new<utf8_line_box_rep> (ip, std::move (paragraph), begin, end,
                                   nominal, pen, options, background, horizontal_scale);
}

bool is_utf8_line_box (box b) {
  return dynamic_cast<utf8_line_box_rep*> (b.operator-> ()) != nullptr;
}

static std::shared_ptr<paragraph_flow>
build_utf8_flow (path ip, array<box> pieces, array<bool> markers, bool restore_spaces) {
  if (N(pieces) != N(markers))
    throw std::invalid_argument ("Unicode source marker count mismatch");
  utf8_line_box_rep* base= nullptr;
  int text_count= 0;
  for (int i=0; i<N(pieces); ++i) {
    if (markers[i]) {
      if (pieces[i]->w () != 0 || pieces[i]->get_leaf_string () != "") return {};
      continue;
    }
    auto* b= dynamic_cast<utf8_line_box_rep*> (pieces[i].operator-> ());
    const shaping_options defaults;
    if (!b || dynamic_cast<mapped_utf8_line_box_rep*> (b) || b->horizontal_scale != 1.0 ||
        !b->options.ligatures || !b->options.script.empty () ||
        b->options.language != "und" || b->options.direction != defaults.direction ||
        b->options.context_begin != defaults.context_begin || b->options.context_end != defaults.context_end ||
        b->options.max_glyphs != defaults.max_glyphs || b->options.max_carets != defaults.max_carets ||
        b->options.grapheme_fragments || b->options.editing_carets) return {};
    if (!base) base= b;
    const auto& first= base->paragraph->request ();
    const auto& next= b->paragraph->request ();
    if (!same_paint (base->pen, b->pen) || base->background != b->background ||
        first.horizontal_dpi != next.horizontal_dpi || first.vertical_dpi != next.vertical_dpi ||
        first.direction != next.direction) return {};
    ++text_count;
  }
  if (text_count < 2) return {};
  std::string text;
  std::vector<font_style_span> styles;
  auto sources= std::make_shared<std::vector<source_span>> ();
  auto result= std::make_shared<paragraph_flow> ();
  const auto append= [&] (const utf8_line_box_rep* b, int first, int last) {
    const int begin= text.size ();
    if (last-first > std::numeric_limits<int>::max () - text.size ())
      throw std::length_error ("Unicode inline source exceeds path range");
    text.append (b->bytes (), first, last-first);
    std::size_t at= first;
    auto add_style= [&] (std::size_t end, const font_request& request) {
      if (end > at) styles.push_back ({begin + at - first, begin + end - first, request});
      at= end;
    };
    for (const auto& style: b->paragraph->styles ()) {
      if (style.end <= at || style.begin >= static_cast<std::size_t> (last)) continue;
      if (style.begin > at) add_style (style.begin, b->paragraph->request ());
      add_style (std::min (style.end, static_cast<std::size_t> (last)), style.request);
    }
    add_style (last, b->paragraph->request ());
    sources->push_back ({begin, static_cast<int> (text.size ()), first, b->ip, b->nominal});
  };
  const utf8_line_box_rep* previous= nullptr;
  for (int i=0; i<N(pieces); ++i) {
    if (!markers[i]) {
      const auto* b= static_cast<utf8_line_box_rep*> (pieces[i].operator-> ());
      if (restore_spaces) {
        if (previous && previous->paragraph == b->paragraph && previous->ip == b->ip) {
          if (b->begin < previous->end) return {};
          if (b->begin > previous->end) {
            if (i == 0 || markers[i-1] ||
                b->bytes ().substr (previous->end, b->begin - previous->end)
                  .find_first_not_of (' ') != std::string_view::npos) return {};
            append (previous, previous->end, b->begin);
          }
        }
        else if (b->begin != 0 ||
                 (previous && previous->end != static_cast<int> (previous->bytes ().size ()))) return {};
      }
      result->piece_sources.push_back (sources->size ());
      append (b, b->begin, b->end);
      previous= b;
    }
    else {
      const int at= text.size ();
      result->piece_sources.push_back (sources->size ());
      sources->push_back ({at, at, pieces[i]->get_leaf_left_pos (),
                          pieces[i]->ip, pieces[i]->get_leaf_font ()});
    }
  }
  if (restore_spaces && previous->end != static_cast<int> (previous->bytes ().size ())) return {};
  result->ip= ip;
  result->paragraph= std::make_shared<font_paragraph> (std::move (text), base->paragraph->request (), styles);
  result->sources= sources;
  result->nominal= base->nominal;
  result->pen= base->pen;
  result->background= base->background;
  return result;
}

box join_utf8_line_boxes (path ip, array<box> pieces, array<bool> markers) {
  const auto flow= build_utf8_flow (ip, pieces, markers, false);
  if (!flow) return {};
  return tm_new<mapped_utf8_line_box_rep> (flow->ip, flow->paragraph, flow->sources,
                                         flow->nominal, flow->pen, flow->background);
}

void prepare_utf8_paragraph (path ip, array<line_item>& items) {
  for (int first=0; first<N(items);) {
    int last= first;
    array<box> pieces;
    array<bool> markers;
    std::shared_ptr<font_paragraph> previous;
    bool multiple= false;
    while (last < N(items)) {
      auto item= items[last];
      auto* text= dynamic_cast<utf8_line_box_rep*> (item->b.operator-> ());
      if (item->type != MARKER_ITEM && (item->type != STD_ITEM || !text)) break;
      if (text) {
        if (text->flow.flow) break;
        multiple= multiple || (previous && previous != text->paragraph);
        previous= text->paragraph;
      }
      pieces << item->b;
      markers << (item->type == MARKER_ITEM);
      ++last;
    }
    const auto flow= multiple ? build_utf8_flow (ip, pieces, markers, true) : nullptr;
    if (flow) {
      const auto& analysis= flow->paragraph->analysis ();
      const auto& breaks= analysis.breaks ();
      std::size_t previous_boundary= 0;
      auto boundary= breaks.begin ();
      for (int i=first; i<last; ++i) {
        const int index= flow->piece_sources[i-first];
        flow_position position {flow, index};
        if (items[i]->type == MARKER_ITEM)
          items[i]->b= tm_new<flow_marker_box_rep> (items[i]->b, position);
        else static_cast<utf8_line_box_rep*> (items[i]->b.operator-> ())->flow= position;
        if (i+1 < last) {
          const auto byte= static_cast<std::size_t> (
            (*flow->sources)[flow->piece_sources[i+1-first]].begin);
          while (boundary != breaks.end () && boundary->byte < byte) ++boundary;
          // Atom-local end penalties are not paragraph boundaries. Retain one
          // break per logical byte even when empty atoms or markers intervene.
          const bool allowed= byte > previous_boundary && byte < analysis.source ().size () &&
            boundary != breaks.end () && boundary->byte == byte;
          if (items[i]->penalty >= 0) items[i]->penalty= allowed ? 0 : HYPH_INVALID;
          previous_boundary= byte;
        }
      }
    }
    first= max (first+1, last);
  }
}

void reassemble_utf8_line (array<box>& pieces, array<SI>& spaces) {
  if (N(pieces) != N(spaces))
    throw std::invalid_argument ("Unicode line spacing count mismatch");
  array<box> output;
  array<SI> output_spaces;
  for (int first=0; first<N(pieces);) {
    const auto position= flow_at (pieces[first]);
    if (position.flow) {
      const auto& flow= *position.flow;
      const auto& sources= *flow.sources;
      const auto bytes= flow.paragraph->analysis ().source ();
      const int begin= sources[position.source].begin;
      int end= sources[position.source].end, source_last= position.source;
      int last= first;
      double scale= 1.0;
      bool have_text= false;
      std::vector<line_space_width> widths;
      for (; last<N(pieces); ++last) {
        const auto next= flow_at (pieces[last]);
        if (next.flow != position.flow || next.source < source_last) break;
        const auto& span= sources[next.source];
        if (span.begin < end && last != first) break;
        auto* text= dynamic_cast<utf8_line_box_rep*> (pieces[last].operator-> ());
        if (text && (have_text && text->horizontal_scale != scale)) break;
        if (last != first) {
          const auto gap= bytes.substr (end, span.begin-end);
          if (spaces[last] < 0 || (gap.empty () ? spaces[last] != 0 :
              gap.find_first_not_of (' ') != std::string_view::npos)) break;
          if (!gap.empty ()) widths.push_back ({static_cast<std::size_t> (end),
            static_cast<std::size_t> (span.begin), spaces[last]});
        }
        if (text) { scale= text->horizontal_scale; have_text= true; }
        source_last= next.source;
        end= span.end;
      }
      if (have_text) {
        output << box (tm_new<mapped_utf8_line_box_rep> (flow.ip, flow.paragraph,
          flow.sources, flow.nominal, flow.pen, flow.background, scale,
          begin, end, position.source, source_last, std::move (widths)));
        output_spaces << spaces[first];
        first= last;
        continue;
      }
    }
    auto* base= dynamic_cast<utf8_line_box_rep*> (pieces[first].operator-> ());
    int last= first+1;
    std::vector<line_space_width> widths;
    if (base && !dynamic_cast<mapped_utf8_line_box_rep*> (base) && base->space_widths.empty ()) {
      int end= base->end;
      for (; last<N(pieces); ++last) {
        auto* next= dynamic_cast<utf8_line_box_rep*> (pieces[last].operator-> ());
        if (!next || next->paragraph != base->paragraph || next->ip != base->ip ||
            !next->space_widths.empty () || next->begin < end || spaces[last] < 0 ||
            !same_paint (base->pen, next->pen) || base->background != next->background ||
            !same_options (base->options, next->options) ||
            base->horizontal_scale != next->horizontal_scale) break;
        const auto gap= base->bytes ().substr (end, next->begin - end);
        if (gap.empty () ? spaces[last] != 0 : gap.find_first_not_of (' ') != std::string_view::npos) break;
        if (!gap.empty ()) widths.push_back ({static_cast<std::size_t> (end),
          static_cast<std::size_t> (next->begin), spaces[last]});
        end= next->end;
      }
      if (last > first+1) {
        output << box (tm_new<utf8_line_box_rep> (base->ip, base->paragraph,
          base->begin, end, base->nominal, base->pen, base->options,
          base->background, base->horizontal_scale, std::move (widths)));
      }
      else output << pieces[first];
    }
    else output << pieces[first];
    output_spaces << spaces[first];
    first= last;
  }
  pieces= output;
  spaces= output_spaces;
}
