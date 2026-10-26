// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#include "local_environment.hpp"
#include <algorithm>
#include <set>

namespace {
std::vector<int> source_address_key (path p) {
  std::vector<int> result;
  for (; !is_nil (p); p= p->next) result.push_back (p->item);
  return result;
}
path source_anchor (path ip) {
  path anchor= ip;
  for (path p= ip; !is_nil (p); p= p->next)
    if (p->item < 0) anchor= p->next;
  return anchor;
}
bool make_relative (std::vector<int>& key, const std::vector<int>& prefix) {
  if (key.size () < prefix.size () || !std::equal (prefix.begin (), prefix.end (), key.begin ()))
    return false;
  key.erase (key.begin (), key.begin () + prefix.size ());
  return true;
}
std::vector<int> prepend_address (const std::vector<int>& prefix, const std::vector<int>& suffix) {
  auto key= prefix;
  key.insert (key.end (), suffix.begin (), suffix.end ());
  return key;
}
}

void source_occurrence::restore_context (edit_env_rep& env) const {
  env.src= sources;
  env.macro_arg= arguments;
  env.macro_src= argument_sources;
  env.cur_file_name= file_name;
  env.package_origin= package_origin;
  env.secure= secure;
}

std::shared_ptr<const source_occurrence> find_source_occurrence (
    const source_occurrence_log& log, path address) {
  if (!log) return {};
  auto key= source_address_key (address);
  if (!make_relative (key, source_address_key (reverse (log->anchor)))) return {};
  struct lookup { const source_occurrence_block* block; std::vector<int> key; };
  std::vector<lookup> pending {{log.get (), std::move (key)}};
  std::shared_ptr<const source_occurrence> found;
  while (!pending.empty ()) {
    auto current= std::move (pending.back ());
    pending.pop_back ();
    const auto& index= current.block->index;
    const auto& key= current.key;
    auto end= std::upper_bound (index.begin (), index.end (), key,
      [] (const auto& key, const auto& item) { return key < item.first; });
    while (end != index.begin ()) {
      const auto& item= *--end;
      if (item.prefix_last < key) break;
      if (item.last < key) continue;
      if (item.child) {
        auto relative= key;
        if (make_relative (relative, item.child_prefix))
          pending.push_back ({item.child.get (), std::move (relative)});
      }
      else {
        if (found) return {};
        found= item.occurrence;
      }
    }
  }
  return found;
}

source_occurrence_capture::source_occurrence_capture (
    edit_env_rep& owner, source_occurrence_log& output, path ip):
  env (owner), parent (owner.source_occurrences), destination (output), anchor (source_anchor (ip)) {
  env.source_occurrences= this;
}

source_occurrence_capture::~source_occurrence_capture () {
  if (!committed) env.source_occurrences= parent;
}

void source_occurrence_capture::record (tree source, path ip) {
  auto key= source_address_key (reverse (ip));
  if (!make_relative (key, source_address_key (reverse (anchor)))) return;
  auto occurrence= std::make_shared<const source_occurrence> (source_occurrence {
    key, source, env.src, env.macro_arg, env.macro_src, env.cur_file_name,
    env.package_origin, env.secure,
    env.contract_events->mark_boundary ()});
  entries.push_back ({key, key, {}, {}, std::move (occurrence), {}});
}

void source_occurrence_capture::reuse (const source_occurrence_log& child) {
  if (!child) return;
  auto prefix= source_address_key (reverse (child->anchor));
  if (!make_relative (prefix, source_address_key (reverse (anchor)))) return;
  entries.push_back ({prepend_address (prefix, child->index.front ().first),
    prepend_address (prefix, child->index.back ().prefix_last), {}, prefix, {}, child});
}

void source_occurrence_capture::commit () {
  source_occurrence_log completed;
  if (!entries.empty ()) {
    const auto before= [] (const auto& a, const auto& b) { return a.first < b.first; };
    if (!std::is_sorted (entries.begin (), entries.end (), before))
      std::sort (entries.begin (), entries.end (), before);
    std::vector<int> last;
    for (auto& entry: entries) {
      if (last < entry.last) last= entry.last;
      entry.prefix_last= last;
    }
    completed= std::make_shared<const source_occurrence_block> (
      source_occurrence_block {anchor, std::move (entries)});
    if (parent) parent->reuse (completed);
  }
  destination= std::move (completed);
  env.source_occurrences= parent;
  committed= true;
}

evaluation_environment_checkpoint::evaluation_environment_checkpoint (const edit_env_rep& state):
  values (copy (state.env)), inputs (state.inputs.snapshot ()),
  sources (copy (state.src)), contracts (state.drd->contracts),
  arguments (state.macro_arg), argument_sources (state.macro_src),
  file_name (state.cur_file_name), package_origin (state.package_origin), secure (state.secure) {}

void evaluation_environment_checkpoint::restore (edit_env_rep& state) const {
  if (state.contract_events) state.contract_events->record (athena::contracts::evaluation_barrier {});
  state.env= copy (values);
  state.inputs.reset (inputs);
  state.src= copy (sources);
  state.drd->contracts= contracts;
  state.drd->set_environment (state.env);
  state.macro_arg= arguments;
  state.macro_src= argument_sources;
  state.cur_file_name= file_name;
  state.package_origin= package_origin;
  state.secure= secure;
}

evaluation_environment_transaction::evaluation_environment_transaction (edit_env_rep& state):
  env (state), owner (state.drd), contracts (state.drd->contracts),
  values (state.env), back (state.back), syntax (state.drd->env),
  inputs (state.inputs), sources (state.src),
  arguments (state.macro_arg), argument_sources (state.macro_src), links (state.link_env),
  recovery (state.recover_env), file_name (state.cur_file_name),
  package_origin (state.package_origin), secure (state.secure),
  capture (state.contract_events), event_count (capture ? capture->record_count () : 0),
  occurrences (state.source_occurrences),
  occurrence_count (occurrences ? occurrences->record_count () : 0) {
  auto variables= copy (values);
  auto source_variables= copy (sources);
  auto monitored= copy (back);
  env.env= variables;
  env.src= source_variables;
  env.back= monitored;
  owner->set_environment (variables);
}

void evaluation_environment_transaction::rollback () {
  if (finished) return;
  finished= true;
  env.env= values;
  env.inputs= inputs;
  env.drd= owner;
  owner->contracts= contracts;
  owner->set_environment (syntax);
  env.src= sources;
  env.macro_arg= arguments;
  env.macro_src= argument_sources;
  env.link_env= links;
  env.recover_env= recovery;
  env.cur_file_name= file_name;
  env.package_origin= package_origin;
  env.secure= secure;
  env.contract_events= capture;
  if (capture) capture->discard_since (event_count);
  env.source_occurrences= occurrences;
  if (occurrences) occurrences->discard_since (occurrence_count);
  {
    athena::contracts::contract_event_suspension pause (env.contract_events);
    env.style_init_env ();
    env.update ();
  }
  env.back= back;
}

evaluation_environment_transaction::~evaluation_environment_transaction () {
  if (!finished) rollback ();
}

temporary_environment_scope::temporary_environment_scope (edit_env_rep& owner):
  temporary_environment_scope (owner, owner.env, false) {}

temporary_environment_scope::temporary_environment_scope (
    edit_env_rep& owner, const hashmap<string,tree>& variables):
  temporary_environment_scope (owner, variables, true) {}

temporary_environment_scope::temporary_environment_scope (
    edit_env_rep& owner, const hashmap<string,tree>& input, bool replace):
  env (owner), previous_drd (owner.drd), values (owner.env), back (owner.back),
  inputs (owner.inputs), sources (owner.src), arguments (owner.macro_arg), argument_sources (owner.macro_src),
  package_origin (owner.package_origin),
  capture (owner.contract_events), occurrences (owner.source_occurrences), current (&owner.drd) {
  auto temporary= drd_info ("temporary-evaluation", previous_drd);
  auto variables= copy (input);
  auto source_variables= copy (sources);
  temporary->set_environment (variables);
  env.env= variables;
  env.src= source_variables;
  env.back= hashmap<string,tree> (UNINIT);
  env.drd= temporary;
  env.contract_events= nullptr;
  env.source_occurrences= nullptr;
  if (replace) env.initialize_value_inputs ();
}

temporary_environment_scope::~temporary_environment_scope () {
  env.env= values;
  env.inputs= inputs;
  env.drd= previous_drd;
  env.src= sources;
  env.macro_arg= arguments;
  env.macro_src= argument_sources;
  env.package_origin= package_origin;
  env.style_init_env ();
  env.update ();
  env.back= back;
  env.contract_events= capture;
  env.source_occurrences= occurrences;
}

package_execution_scope::package_execution_scope (edit_env_rep& owner, url source):
  env (owner), previous (owner.package_origin) {
  const string file= as_string (source);
  env.package_origin= std::make_shared<const athena::contracts::package_inclusion> (
    athena::contracts::package_inclusion {
      std::string (file.data (), N(file)), previous});
}

package_execution_scope::~package_execution_scope () {
  env.package_origin= previous;
}

bool evaluate_local_environment (edit_env_rep& env, const tree& source,
                                 local_environment_bindings& bindings) {
  if (N(source) == 0 || N(source)%2 != 1) return false;
  bindings.reserve ((N(source)-1)/2);
  for (int i=0; i+1<N(source); i+=2) {
    tree name= env.exec (source[i]);
    if (!is_atomic (name)) return false;
    tree value= env.exec (source[i+1]);
    string variable= env.normalize_legacy_math_font_variable (name->label, value);
    bindings.push_back ({variable, value, tree (), false});
  }
  // RHS evaluation may itself assign names. Those effects precede the local
  // scope and must survive its exit, including writes made by later RHSs.
  for (auto& binding: bindings) {
    binding.previous= env.read (binding.name);
    binding.previously_present= env.provides (binding.name);
  }
  return true;
}

local_environment_scope::local_environment_scope (
    edit_env_rep& owner, const local_environment_bindings& values, bool monitored):
  env (owner), bindings (values), before (owner.drd->contracts),
  capture (owner.contract_events, events, &owner.drd->contracts) {
  try {
    resolve_targets ();
    env.drd->contracts.enter_local_targets (targets);
    for (const auto& binding: bindings) {
      ++installed;
      env.track_definition (binding.name, binding.value);
      if (monitored) env.monitored_write_update (binding.name, binding.value);
      else env.write_update (binding.name, binding.value);
    }
  } catch (...) {
    restore ();
    throw;
  }
}

void local_environment_scope::resolve_targets () {
  if (!targets.empty ()) return;
  targets.reserve (bindings.size ());
  for (const auto& binding: bindings)
    targets.push_back ({make_tree_label (binding.name),
      std::string (binding.name.data (), N(binding.name))});
}

void local_environment_scope::restore () {
  for (std::size_t i=installed; i>0; --i) {
    const auto& binding= bindings[i-1];
    env.restore_local_value (binding.name, binding.previous, binding.previously_present);
  }
  if (!env.drd->contracts.same_state (before)) {
    resolve_targets ();
    for (auto label: env.drd->contracts.restore_targets (before, targets))
      env.drd->declaration_changed (label);
  }
}

void local_environment_scope::finish (bool remain_inside) {
  if (!remain_inside) {
    restore ();
    if (capture.has_effects ()) resolve_targets ();
  }
  capture.commit (std::move (targets), !remain_inside);
  finished= true;
}

local_environment_scope::~local_environment_scope () {
  if (!finished) restore ();
}

scoped_package_environment::scoped_package_environment (edit_env_rep& owner, tree package):
  env (owner), previous_values (UNINIT),
  previous_contracts (owner.drd->contracts),
  capture (owner.contract_events, events, &owner.drd->contracts) {
  env.read_env (previous_values);
  evaluation_environment_transaction package_transaction (env);
  athena::contracts::contract_event_log writes;
  {
    athena::contracts::contract_event_capture package_capture (
      env.contract_events, writes, &env.drd->contracts);
    const string name= env.exec_string (package);
    env.exec (tree (USE_PACKAGE, name));
    package_capture.commit ();
  }
  std::set<std::string> names;
  std::vector<std::set<std::string>> locals;
  const auto add= [&] (const std::string& name) {
    for (const auto& frame: locals)
      if (frame.count (name)) return;
    names.insert (name);
  };
  athena::contracts::visit_contract_events (writes,
    [&] (const auto& declaration) { add (declaration->name); },
    [&] (const auto& targets) {
      std::set<std::string> frame;
      for (const auto& target: targets) frame.insert (target.name);
      locals.push_back (std::move (frame));
    },
    [&] (const auto&) { locals.pop_back (); },
    [&] (const auto& binding) { add (binding->name); },
    [&] (const auto& variable) { add (variable.name); });
  for (const auto& name: names) {
    const string key (name.data (), name.size ());
    targets.push_back ({make_tree_label (key), name});
  }
  package_transaction.commit ();
}

void scoped_package_environment::restore () {
  for (const auto& target: targets) {
    const string name (target.name.data (), target.name.size ());
    const bool present= previous_values->contains (name);
    env.restore_local_value (name, present ? previous_values[name] : tree (UNINIT), present);
  }
  for (auto label: env.drd->contracts.restore_targets (previous_contracts, targets))
    env.drd->declaration_changed (label);
}

void scoped_package_environment::finish (bool remain_inside) {
  if (!remain_inside) restore ();
  capture.commit (std::move (targets), !remain_inside);
  finished= true;
}

scoped_package_environment::~scoped_package_environment () {
  if (!finished) restore ();
}
