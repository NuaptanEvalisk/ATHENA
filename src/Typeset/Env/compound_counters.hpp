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
#include "Data/Convert/Xml/athena_document_xml.hpp"

namespace athena::avd {
// std-counter's counter-<name> macros are the existing counter registry. Resolve
// their indirection (including counter groups); do not guess from a -nr suffix.
hashset<string> counter_variables (edit_env);
void validate_counter_state (const tree&);
void apply_counter_state (edit_env, hashset<string>, const tree&);
tree capture_counter_state (edit_env, const tree& inherited);
// Called only with an isolated evaluation environment, never the live layout.
tree evaluate_counter_state (edit_env, const tree& source, const tree& incoming,
                             document::xml_format_version);
// Native semantic dispatch shared by counter projection and source preparation.
tree exec_native_counter_node (edit_env, const tree&);
bool exec_native_counter_until (edit_env, const tree&, path);
bool exec_native_counter_until (edit_env, const tree&, path, string argument, int level);
} // namespace athena::avd
