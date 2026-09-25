/******************************************************************************
* MODULE     : concat_symbol.cpp
* DESCRIPTION: Atomic semantic symbols rendered through native Unicode shaping
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#include "concater.hpp"
#include "Boxes/utf8_line.hpp"
#include "named_symbol.hpp"
#include "math_font.hpp"
#include "Boxes/construct.hpp"
#include "frame.hpp"
#include <cmath>
#include <stdexcept>

namespace {

box
native_symbol_glyph_box (path ip, std::string_view utf8, font fn, pencil pen,
                         bool italic, bool math_mode, brush background) {
  athena::text::native_text_source native;
  if (!fn->native_text_source (native))
    throw std::runtime_error ("Symbol font has no native Unicode source");
  auto request= athena::text::font_request_with_italic (
    athena::text::font_request_from_source (native.physical), italic);
  request.fallback= std::move (native.fallback);
  request.features= std::move (native.features);
  if (math_mode)
    request= athena::text::math_font_request (fn, italic ?
      athena::text::math_alphabet::italic : athena::text::math_alphabet::normal);
  auto paragraph= std::make_shared<athena::text::font_paragraph> (
    std::string (utf8), std::move (request));
  const int length= paragraph->analysis ().source ().size ();
  return utf8_line_box (ip, std::move (paragraph), 0, length, fn, pen, {}, background);
}

box
combine_native_symbol_boxes (path ip, box first, box second, double overlap_em,
                             font fn, athena::text::named_symbol_recipe_kind kind) {
  const SI overlap= (SI) tm_round (overlap_em * fn->wfn);
  const SI first_center= (first->x1 + first->x2) >> 1;
  const SI second_center= (second->x1 + second->x2) >> 1;
  const SI dx= first_center - second_center;
  SI dy= 0;
  switch (kind) {
    case athena::text::named_symbol_recipe_kind::glue_above:
      dy= first->y2 - second->y1 - overlap;
      break;
    case athena::text::named_symbol_recipe_kind::glue_below:
    case athena::text::named_symbol_recipe_kind::stack:
      dy= first->y1 - second->y2 + overlap;
      break;
    default:
      throw std::runtime_error ("Invalid binary native symbol recipe");
  }
  array<box> boxes; boxes << first << second;
  array<SI> xs; xs << 0 << dx;
  array<SI> ys; ys << 0 << dy;
  return composite_box (ip, boxes, xs, ys, false);
}

box
render_native_symbol_recipe (const athena::text::named_symbol_recipe& recipe,
                             path ip, font fn, pencil pen, bool italic,
                             bool math_mode, brush background) {
  using athena::text::named_symbol_recipe_kind;
  if (recipe.kind == named_symbol_recipe_kind::glyph)
    return native_symbol_glyph_box (ip, recipe.glyph_utf8, fn, pen, italic,
                                    math_mode, background);
  if (!recipe.first) throw std::runtime_error ("Incomplete native symbol recipe");
  box first= render_native_symbol_recipe (
    *recipe.first, ip, fn, pen, italic, math_mode, background);
  if (recipe.kind == named_symbol_recipe_kind::rotate) {
    const double cx= 0.5 * (first->x1 + first->x2);
    const double cy= 0.5 * (first->y1 + first->y2);
    return transformed_box (ip, first,
      rotation_2D (point (cx, cy), recipe.parameter / 57.2957795131));
  }
  if (recipe.kind == named_symbol_recipe_kind::scale_x) {
    const double cx= 0.5 * (first->x1 + first->x2);
    const double shift= (1.0 - recipe.parameter) * cx;
    return transformed_box (ip, first,
      scaling (point (recipe.parameter, 1.0), point (shift, 0.0)));
  }
  if (!recipe.second) throw std::runtime_error ("Incomplete native symbol recipe");
  box second= render_native_symbol_recipe (
    *recipe.second, ip, fn, pen, italic, math_mode, background);
  return combine_native_symbol_boxes (
    ip, first, second, recipe.parameter, fn, recipe.kind);
}

box
center_native_symbol_on_axis (path ip, box glyph, font fn) {
  const auto metrics= athena::text::math_layout_metrics (fn);
  const SI axis= metrics ? metrics->axis_height : fn->yfrac;
  const SI dy= axis - ((glyph->y1 + glyph->y2) >> 1);
  return dy == 0 ? glyph : move_box (ip, glyph, 0, dy, false, true);
}

} // namespace

void concater_rep::typeset_named_symbol (tree t, path ip) {
  if (N(t) != 1 || !is_atomic (t[0])) { typeset_error (t, ip); return; }
  const auto& name= t[0]->label;
  const auto* symbol= athena::text::standard_named_symbols ().lookup (
    std::string_view (name.data (), N(name)));
  if (symbol && !symbol->virtual_font.empty ()) {
    const string vname (symbol->virtual_font.data (), symbol->virtual_font.size ());
    const string raw (symbol->virtual_symbol.data (), symbol->virtual_symbol.size ());
    box glyph= macro_box (ip,
      virtual_recipe_box (decorate (ip), vname, raw, env->fn, env->pen), env->fn);
    if (env->read (MODE) == "math") print_semantic (glyph, t);
    else print (STD_ITEM, symbol->op_type, glyph);
    return;
  }
  const bool math_mode= env->read (MODE) == "math";
  // Missing definitions are visible errors, not guessed Unicode substitutes.
  // The complete identity is still in the source node and survives saving.
  const tree background= env->read ("text-background-color");
  const brush bg= background == "" ? brush (false) : brush (background, env->alpha);
  box glyph;
  if (symbol && symbol->recipe) {
    glyph= render_native_symbol_recipe (
      *symbol->recipe, decorate (ip), env->fn, env->pen, symbol->italic, math_mode, bg);
    if (math_mode) glyph= center_native_symbol_on_axis (decorate (ip), glyph, env->fn);
  }
  else
    glyph= native_symbol_glyph_box (
      decorate (ip), symbol ? std::string_view (symbol->glyph_utf8) :
      std::string_view ("[missing symbol]"), env->fn,
      symbol ? env->pen : pencil (red), symbol && symbol->italic, math_mode, bg);
  // Reuse the standard indivisible box mapping, not the unfinished SYMBOL tag.
  box result= macro_box (ip, glyph, env->fn);
  if (math_mode) print_semantic (result, t);
  else print (STD_ITEM, symbol ? symbol->op_type : OP_SYMBOL, result);
}
