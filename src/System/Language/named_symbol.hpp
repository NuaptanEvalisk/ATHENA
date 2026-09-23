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

namespace athena::text {

struct named_symbol_definition {
  std::string identity;
  std::string glyph_utf8; // Rendering only, never a replacement source identity.
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
