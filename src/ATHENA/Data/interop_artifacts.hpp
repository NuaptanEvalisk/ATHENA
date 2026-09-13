/******************************************************************************
* MODULE     : interop_artifacts.hpp
* DESCRIPTION: Vault artifact resolution and read-only AUDM accessors
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#pragma once
#include "../Interop/resolution.hpp"

namespace athena::interop {
std::shared_ptr<const resolver> artifacts_resolver ();
}
