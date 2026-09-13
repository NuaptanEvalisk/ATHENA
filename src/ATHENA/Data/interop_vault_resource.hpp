/******************************************************************************
* MODULE     : interop_vault_resource.hpp
* DESCRIPTION: Captured vault context boundary between native resource resolvers
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#pragma once
#include "../Interop/resolution.hpp"
#include "vault.hpp"

namespace athena::interop {
class vault_resource: public resource {
public:
  virtual vault_context_handle captured_vault () const = 0;
};
} // namespace athena::interop
