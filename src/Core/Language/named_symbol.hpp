/******************************************************************************
* MODULE     : named_symbol.hpp
* DESCRIPTION: Semantic symbol identities and declarative native glyph recipes
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#pragma once

#include <string>
#include <string_view>
#include <map>
#include <memory>

namespace athena::text {

enum class named_symbol_recipe_kind {
  glyph,
  rotate,
  stack,
  glue_above,
  glue_below,
  scale_x
};

struct named_symbol_recipe {
  named_symbol_recipe_kind kind= named_symbol_recipe_kind::glyph;
  std::string glyph_utf8;
  double parameter= 0.0;
  std::shared_ptr<const named_symbol_recipe> first;
  std::shared_ptr<const named_symbol_recipe> second;
};

struct named_symbol_definition {
  std::string identity;
  std::string glyph_utf8; // Rendering only, never a replacement source identity.
  std::string virtual_font;
  std::string virtual_symbol;
  std::shared_ptr<const named_symbol_recipe> recipe;
  int op_type;
  bool italic;
};

// Only immutable standard values are shared; font/ICU state stays owner-local.
class named_symbol_registry {
  std::map<std::string, named_symbol_definition, std::less<>> definitions_;
public:
  explicit named_symbol_registry (std::string_view json);
  const named_symbol_definition* lookup (std::string_view identity) const;
  std::size_t size () const { return definitions_.size (); }
};

const named_symbol_registry& standard_named_symbols ();

} // namespace athena::text
