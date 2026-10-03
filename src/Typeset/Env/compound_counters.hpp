/******************************************************************************
* MODULE     : compound_counters.hpp
* DESCRIPTION: Counter-only state transfer between compound document members
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#pragma once
#include "env.hpp"
#include "hashset.hpp"

namespace athena::avd {
// std-counter's counter-<name> macros are the existing counter registry. Resolve
// their indirection (including counter groups); do not guess from a -nr suffix.
hashset<string> counter_variables (edit_env);
void validate_counter_state (const tree&);
void apply_counter_state (edit_env, hashset<string>, const tree&);
tree capture_counter_state (edit_env, const tree& inherited);
} // namespace athena::avd
