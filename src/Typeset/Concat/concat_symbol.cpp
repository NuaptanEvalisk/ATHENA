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
#include <stdexcept>

void concater_rep::typeset_named_symbol (tree t, path ip) {
  if (N(t) != 1 || !is_atomic (t[0])) { typeset_error (t, ip); return; }
  const auto& name= t[0]->label;
  const auto* symbol= athena::text::standard_named_symbols ().lookup (
    std::string_view (name.data (), N(name)));
  athena::text::physical_font_source physical;
  if (!env->fn->physical_source (physical))
    throw std::runtime_error ("Symbol font has no physical Unicode source");
  auto request= athena::text::font_request_with_italic (
    athena::text::font_request_from_source (physical), symbol && symbol->italic);
  // Missing definitions are visible errors, not guessed Unicode substitutes.
  // The complete identity is still in the source node and survives saving.
  auto paragraph= std::make_shared<athena::text::font_paragraph> (
    symbol ? symbol->glyph_utf8 : "[missing symbol]", std::move (request));
  const int length= paragraph->analysis ().source ().size ();
  const tree background= env->read ("text-background-color");
  box glyph= utf8_line_box (decorate (ip), std::move (paragraph), 0, length,
    env->fn, symbol ? env->pen : pencil (red), {},
    background == "" ? brush (false) : brush (background, env->alpha));
  // Reuse the standard indivisible box mapping, not the unfinished SYMBOL tag.
  box result= macro_box (ip, glyph, env->fn);
  if (env->read (MODE) == "math") print_semantic (result, t);
  else print (STD_ITEM, symbol ? symbol->op_type : OP_SYMBOL, result);
}
