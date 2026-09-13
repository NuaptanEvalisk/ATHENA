/******************************************************************************
* MODULE     : resources.hpp
* DESCRIPTION: Immutable registry interface for native ATHENA resource resolvers
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#pragma once
#include "resolution.hpp"

namespace athena::interop {
// Registry is constructed once and thereafter shared as immutable process data.
std::shared_ptr<const resolver_registry> native_resolvers ();
} // namespace athena::interop
