/******************************************************************************
* MODULE     : compound_document_edit.hpp
* DESCRIPTION: Source-relative compound selections and coordinated edit history
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#pragma once
#include "actor_transport.hpp"
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace athena::avd {
struct source_range {
  athena_actor_id actor= ATHENA_NO_ACTOR;
  athena_view_id view= ATHENA_NO_VIEW;
  std::uint64_t epoch= 0;
  // Relative to the source body. Empty means the corresponding body boundary.
  std::vector<int> first, last;
};

void erase_ranges (std::vector<source_range>, std::function<void(std::string)>);
// Owner-actor only. Returns true when the request belongs to compound history,
// including a refused/conflicting request. Never waits for another actor.
bool coordinate_compound_history (bool redo, int branch= 0);
} // namespace athena::avd
