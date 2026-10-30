// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#include "local_environment.hpp"
#include "Data/NodeContracts/detached_environment.hpp"
#include "iterator.hpp"
#include <immer/algorithm.hpp>
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

static std::shared_ptr<const source_occurrence> select_source_occurrence (
    const source_occurrence_log& log, path address, const tree* source,
    const athena::contracts::evaluation_boundary_handle& preferred) {
  if (!log) return {};
  auto key= source_address_key (address);
  if (!make_relative (key, source_address_key (reverse (log->anchor)))) return {};
  struct lookup { const source_occurrence_block* block; std::vector<int> key; };
  std::vector<lookup> pending {{log.get (), std::move (key)}};
  std::shared_ptr<const source_occurrence> found;
  bool ambiguous= false;
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
        if (source && item.occurrence->value != *source) continue;
        if (preferred && (item.occurrence->boundary == preferred ||
                          item.occurrence->end_boundary == preferred))
          return item.occurrence;
        if (found) ambiguous= true;
        found= item.occurrence;
      }
    }
  }
  return ambiguous ? nullptr : found;
}

std::shared_ptr<const source_occurrence> find_source_occurrence (
    const source_occurrence_log& log, path address) {
  return select_source_occurrence (log, address, nullptr, {});
}

std::shared_ptr<const source_occurrence> find_source_occurrence (
    const source_occurrence_log& log, path address, const tree& source,
    const athena::contracts::evaluation_boundary_handle& preferred) {
  return select_source_occurrence (log, address, &source, preferred);
}

source_occurrence_capture::source_occurrence_capture (
    edit_env_rep& owner, source_occurrence_log& output, path ip):
  env (owner), parent (owner.source_occurrences), destination (output), anchor (source_anchor (ip)) {
  env.source_occurrences= this;
}

source_occurrence_capture::~source_occurrence_capture () {
  if (!committed) env.source_occurrences= parent;
}

std::shared_ptr<source_occurrence>
source_occurrence_capture::record (tree source, path ip) {
  auto key= source_address_key (reverse (ip));
  if (!make_relative (key, source_address_key (reverse (anchor)))) return {};
  auto occurrence= std::make_shared<source_occurrence> (source_occurrence {
    key, source, env.src, env.macro_arg, env.macro_src, env.cur_file_name,
    env.package_origin, env.secure,
    env.mark_execution_boundary (), {},
    env.active_source_occurrence ? env.active_source_occurrence->enclosing () : nullptr});
  entries.push_back ({key, key, {}, {}, occurrence, {}});
  return occurrence;
}

source_occurrence_scope::source_occurrence_scope (
    edit_env_rep& owner, tree value, path ip):
  env (owner), previous (owner.active_source_occurrence), source (value), address (ip) {
  if (!is_accessible (ip) || !env.source_occurrences || !env.contract_events) return;
  for (auto* active= previous; active; active= active->previous)
    if (active->address == ip && strong_equal (active->source, value)) return;
  occurrence= env.source_occurrences->record (value, ip);
  env.active_source_occurrence= this;
  previous_box_boundary= box_source_boundary;
  if (occurrence) box_source_boundary= occurrence->boundary;
}

source_occurrence_scope::~source_occurrence_scope () {
  if (env.active_source_occurrence == this) {
    env.active_source_occurrence= previous;
    box_source_boundary= std::move (previous_box_boundary);
  }
}

void source_occurrence_scope::finish (bool reached_end) {
  if (occurrence) {
    if (reached_end) {
      occurrence->end_boundary= is_atomic (source) ? occurrence->boundary :
        env.mark_execution_boundary ();
    }
    occurrence.reset ();
  }
}

std::shared_ptr<const source_occurrence_ancestor> source_occurrence_scope::enclosing () {
  for (auto* scope= this; scope; scope= scope->previous)
    if (scope->occurrence) {
      if (!scope->ancestry)
        scope->ancestry= std::make_shared<const source_occurrence_ancestor> (
          source_occurrence_ancestor {scope->occurrence->boundary, scope->occurrence->parent});
      return scope->ancestry;
    }
  return {};
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
  values (std::make_shared<const hashmap<string,tree>> (copy (state.env))),
  inputs (state.inputs.snapshot ()),
  sources (copy (state.src)), contracts (state.contract_state.contracts),
  arguments (state.macro_arg), argument_sources (state.macro_src),
  file_name (state.cur_file_name), package_origin (state.package_origin), secure (state.secure) {}

evaluation_environment_checkpoint::evaluation_environment_checkpoint (
    const evaluation_environment_checkpoint& base, const source_occurrence& occurrence):
  evaluation_environment_checkpoint (base) {
  sources= occurrence.sources;
  arguments= occurrence.arguments;
  argument_sources= occurrence.argument_sources;
  file_name= occurrence.file_name;
  package_origin= occurrence.package_origin;
  secure= occurrence.secure;
}

std::shared_ptr<const evaluation_environment_checkpoint>
evaluation_environment_checkpoint::at (const athena::contracts::contract_event_log& log) const {
  auto effects= athena::contracts::resolve_evaluation_effects (log, contracts, {},
    athena::contracts::execution_resolution::recorded_context);
  if (!effects) return {};
  auto result= std::make_shared<evaluation_environment_checkpoint> (*this);
  athena::contracts::evaluation_inputs changed;
  changed.reset (inputs);
  for (const auto& [key, value]: effects->variables) {
    result->overrides= std::move (result->overrides).set (key, value);
    changed.record (key, bool (value));
  }
  result->inputs= changed.snapshot ();
  result->contracts= std::move (effects->contracts);
  return result;
}

hashmap<string,tree> evaluation_environment_checkpoint::variables () const {
  auto result= copy (*values);
  for (const auto& [key, value]: overrides) {
    const string name (key.data (), key.size ());
    if (value) result (name)= tree (*value);
    else result->reset (name);
  }
  return result;
}

bool evaluation_environment_checkpoint::contains (string name) const {
  const auto* changed= overrides.find (std::string (name.data (), N(name)));
  if (changed) return bool (*changed);
  auto baseline= *values;
  return baseline->contains (name);
}

tree evaluation_environment_checkpoint::value (string name) const {
  const auto* changed= overrides.find (std::string (name.data (), N(name)));
  if (changed) return *changed ? tree (**changed) : tree (UNINIT);
  return (*values)[name];
}

std::optional<athena::node::frozen_tree>
evaluation_environment_checkpoint::retained_value (const string& name) const {
  const auto* changed= overrides.find (std::string (name.data (), N(name)));
  return changed ? *changed : std::nullopt;
}

tree evaluation_environment_checkpoint::effective_value (string name) const {
  tree current= value (name);
  return is_func (current, BACKUP, 2) ? current[0] : current;
}

evaluation_context_capture::evaluation_context_capture (
    edit_env_rep& owner, const evaluation_environment_checkpoint& base):
  env (owner), previous (owner.recorded_contexts), recorded {base, {}} {
  env.recorded_contexts= this;
}

evaluation_context_capture::~evaluation_context_capture () {
  env.recorded_contexts= previous;
}

void evaluation_context_capture::write (const athena::contracts::variable_write& value) {
  recorded.current.overrides= std::move (recorded.current.overrides).set (value.name, value.value);
}

void evaluation_context_capture::write (const string& name, const tree* value) {
  const std::string key (name.data (), N(name));
  const auto* previous= recorded.current.overrides.find (key);
  if (previous && (value ? *previous && (*previous)->matches (*value) : !*previous)) return;
  write (athena::contracts::variable_write {
    key, value ? std::optional<athena::node::frozen_tree> (*value) : std::nullopt});
}

void evaluation_context_capture::mark (
    const athena::contracts::evaluation_boundary_handle& boundary) {
  recorded.current.contracts= env.contract_state.contracts;
  recorded.current.inputs= env.inputs.snapshot ();
  recorded.boundaries= std::move (recorded.boundaries).set (
    boundary, std::make_shared<const evaluation_environment_checkpoint> (recorded.current));
}

void evaluation_context_capture::rebase () {
  recorded.current= evaluation_environment_checkpoint (env);
}

void evaluation_context_capture::rebase (const evaluation_environment_checkpoint& restored) {
  recorded.current= restored;
}

std::optional<athena::contracts::evaluation_effects>
evaluation_context_capture::replay (
    const athena::contracts::contract_event_log& events,
    const athena::contracts::variable_lookup& lookup,
    const athena::contracts::external_matcher& external,
    const athena::contracts::package_matcher& packages) {
  using namespace athena::contracts;
  auto draft= recorded;
  const auto incoming= draft.current.overrides;
  evaluation_variable_overlay previous_variables;
  evaluation_inputs inputs;
  inputs.reset (env.inputs.snapshot ());
  const auto advance= [&] (const evaluation_variable_overlay& variables) {
    const auto set= [&] (const auto& item) {
      draft.current.overrides= std::move (draft.current.overrides).set (item.first, item.second);
      inputs.record (item.first, bool (item.second));
    };
    const auto remove= [&] (const auto& item) {
      if (const auto* before= incoming.find (item.first)) {
        draft.current.overrides= std::move (draft.current.overrides).set (item.first, *before);
        inputs.record (item.first, bool (*before));
      }
      else {
        draft.current.overrides= std::move (draft.current.overrides).erase (item.first);
        inputs.record (item.first, recorded.current.contains (
          string (item.first.data (), item.first.size ())));
      }
    };
    immer::diff (previous_variables, variables, immer::make_differ (
      set, remove, [&] (const auto&, const auto& item) { set (item); }));
    previous_variables= variables;
    draft.current.inputs= inputs.snapshot ();
  };
  auto effects= resolve_evaluation_effects (events, env.contract_state.contracts, lookup,
    execution_resolution::cache_admission, external, packages,
    [&] (const auto& boundary, const auto& contracts, const auto& variables) {
      advance (variables);
      draft.current.contracts= contracts;
      draft.boundaries= std::move (draft.boundaries).set (boundary,
        std::make_shared<const evaluation_environment_checkpoint> (draft.current));
    });
  if (!effects) return {};
  advance (effects->variables);
  draft.current.contracts= effects->contracts;
  recorded= std::move (draft);
  return effects;
}

std::shared_ptr<const athena::contracts::detached_environment>
evaluation_environment_checkpoint::detach () const {
  auto result= std::make_shared<athena::contracts::detached_environment> (
    athena::node::frozen_tree (as_tree (file_name)));
  result->contracts= contracts.snapshot ();
  result->inputs= inputs;
  result->package= package_origin;
  result->secure= secure;
  auto keys= iterate (*values);
  while (keys->busy ()) {
    const string key= keys->next ();
    const std::string name (key.data (), N(key));
    if (overrides.find (name)) continue;
    const auto binding= existing_tree_label (key) ? contracts.definition (as_tree_label (key)) : nullptr;
    result->values.emplace (name,
      binding ? binding->value->expression : athena::node::frozen_tree ((*values)[key]));
  }
  for (const auto& [name, value]: overrides)
    if (value) result->values.emplace (name, *value);
  for (auto frame= arguments; !is_nil (frame); frame= frame->next) {
    athena::contracts::detached_environment::variables frozen;
    auto args= frame->item;
    auto names= iterate (args);
    while (names->busy ()) {
      const string name= names->next ();
      frozen.emplace (std::string (name.data (), N(name)), athena::node::frozen_tree (args[name]));
    }
    result->arguments.push_back (std::move (frozen));
  }
  return result;
}

bool evaluation_environment_checkpoint::same_field_context (
    const evaluation_environment_checkpoint& before) const {
  if (as_tree (file_name) != as_tree (before.file_name) || secure != before.secure ||
      package_origin != before.package_origin || arguments != before.arguments) return false;
  if (!contracts.same_state (before.contracts) &&
      (!contracts.changed_definitions (before.contracts).empty () ||
       contracts.contracts_differ (before.contracts) ||
       contracts.groups () != before.contracts.groups ())) return false;
  for (const auto& name: inputs.changed_since (before.inputs)) {
    const string key (name.data (), name.size ());
    if (contains (key) != before.contains (key) ||
        value (key) != before.value (key)) return false;
  }
  return true;
}

void athena::contracts::detached_environment::restore (edit_env_rep& state) const {
  if (state.contract_events) state.contract_events->record (evaluation_barrier {});
  state.env= hashmap<string,tree> (UNINIT);
  for (const auto& [name, value]: values)
    state.env (string (name.data (), name.size ()))= tree (value);
  state.inputs.reset (inputs);
  state.contract_state.contracts= contract_scope (contracts);
  state.src= hashmap<string,path> (path (DECORATION));
  state.macro_arg= {};
  state.macro_src= {};
  for (auto frame= arguments.rbegin (); frame != arguments.rend (); ++frame) {
    hashmap<string,tree> args (UNINIT);
    for (const auto& [name, value]: *frame)
      args (string (name.data (), name.size ()))= tree (value);
    state.macro_arg= list<hashmap<string,tree>> (args, state.macro_arg);
    state.macro_src= list<hashmap<string,path>> (
      hashmap<string,path> (path (DECORATION)), state.macro_src);
  }
  state.cur_file_name= as_url (tree (file_name));
  state.package_origin= package;
  state.secure= secure;
  if (state.recorded_contexts) state.recorded_contexts->rebase ();
}

void evaluation_environment_checkpoint::restore (edit_env_rep& state) const {
  if (state.contract_events) state.contract_events->record (athena::contracts::evaluation_barrier {});
  state.env= variables ();
  state.inputs.reset (inputs);
  state.src= copy (sources);
  state.contract_state.contracts= contracts;
  state.macro_arg= arguments;
  state.macro_src= argument_sources;
  state.cur_file_name= file_name;
  state.package_origin= package_origin;
  state.secure= secure;
  if (state.recorded_contexts) state.recorded_contexts->rebase (*this);
}

evaluation_environment_transaction::evaluation_environment_transaction (edit_env_rep& state):
  env (state), contracts (state.contract_state.contracts),
  values (state.env), back (state.back),
  inputs (state.inputs), sources (state.src),
  arguments (state.macro_arg), argument_sources (state.macro_src), links (state.link_env),
  recovery (state.recover_env), file_name (state.cur_file_name),
  package_origin (state.package_origin), secure (state.secure),
  capture (state.contract_events), event_count (capture ? capture->record_count () : 0),
  occurrences (state.source_occurrences),
  occurrence_count (occurrences ? occurrences->record_count () : 0) {
  if (env.recorded_contexts) recorded= env.recorded_contexts->snapshot ();
  auto variables= copy (values);
  auto source_variables= copy (sources);
  auto monitored= copy (back);
  env.env= variables;
  env.src= source_variables;
  env.back= monitored;
}

void evaluation_environment_transaction::rollback () {
  if (finished) return;
  finished= true;
  env.env= values;
  env.inputs= inputs;
  env.contract_state.contracts= contracts;
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
  if (recorded) env.recorded_contexts->restore (std::move (*recorded));
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
  env (owner), previous_contracts (owner.contract_state.contracts), values (owner.env), back (owner.back),
  inputs (owner.inputs), sources (owner.src), arguments (owner.macro_arg), argument_sources (owner.macro_src),
  file_name (owner.cur_file_name), secure (owner.secure),
  package_origin (owner.package_origin),
  capture (owner.contract_events), occurrences (owner.source_occurrences),
  recorded (owner.recorded_contexts), current (&owner.contract_state) {
  auto variables= copy (input);
  auto source_variables= copy (sources);
  env.env= variables;
  env.src= source_variables;
  env.back= hashmap<string,tree> (UNINIT);
  env.contract_events= nullptr;
  env.source_occurrences= nullptr;
  env.recorded_contexts= nullptr;
  if (replace) env.initialize_value_inputs ();
}

temporary_environment_scope::~temporary_environment_scope () {
  env.env= values;
  env.inputs= inputs;
  env.contract_state.contracts= previous_contracts;
  env.src= sources;
  env.macro_arg= arguments;
  env.macro_src= argument_sources;
  env.cur_file_name= file_name;
  env.secure= secure;
  env.package_origin= package_origin;
  env.style_init_env ();
  env.update ();
  env.back= back;
  env.contract_events= capture;
  env.source_occurrences= occurrences;
  env.recorded_contexts= recorded;
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

native_math_context native_script_position (tree_label kind, int child) {
  if (kind == MATH_SCRIPTS)
    return child == 1 || child == 3 ? native_math_context::lower : native_math_context::upper;
  if (kind == MATH_LONG_ARROW)
    return child == 1 ? native_math_context::upper : native_math_context::lower;
  return native_math_context::inherited;
}

void native_math_context_scope::bind (string name, tree value) {
  const tree old= env.read (name);
  previous.emplace_back (name, old);
  if (old != value) env.write_update (name, value);
}

native_math_context_scope::native_math_context_scope (
    edit_env_rep& owner, native_math_context kind): env (owner) {
  switch (kind) {
  case native_math_context::math: bind (MODE, "math"); break;
  case native_math_context::display:
    bind (MODE, "math");
    bind (MATH_DISPLAY, "true");
    break;
  case native_math_context::script:
    bind (MATH_DISPLAY, "false");
    bind (MATH_CONDENSED, "true");
    bind (MATH_LEVEL, as_string (env.get_int (MATH_LEVEL) + 1));
    break;
  case native_math_context::lower: bind (MATH_VPOS, "-1"); break;
  case native_math_context::upper: bind (MATH_VPOS, "1"); break;
  case native_math_context::inherited: break;
  }
}

native_math_context_scope::native_math_context_scope (
    edit_env_rep& owner, const tree& node):
  native_math_context_scope (owner, native_math_context::math) {
  if (L(node) != MATH_VARIANT) return;
  const string& kind= node[0]->label;
  if (kind == "bb" || kind == "cal" || kind == "frak" ||
      kind == "mbf" || kind == "mbfu") {
    if (kind == "mbf" || kind == "mbfu") bind (MATH_FONT_SERIES, "bold");
    if (kind != "mbf")
      bind (MATH_FONT_SHAPE, kind == "bb" ? "bbb" : kind == "cal" ? "cal" :
                            kind == "frak" ? "frak" : "right");
    return;
  }
  // Roman/sans/monospace variants are textual mathematical operators;
  // mathematical alphabets above use the actual math face instead.
  bind (MODE, "text");
  bind (FONT_FAMILY, kind == "ss" ? "ss" : kind == "tt" ? "tt" : "rm");
  bind (FONT_SHAPE, kind == "it" ? "italic" : kind == "sl" ? "slanted" : "right");
  if (kind == "bf") bind (FONT_SERIES, "bold");
}

void native_math_context_scope::finish (bool remain_inside) {
  if (!remain_inside)
    for (auto it= previous.rbegin (); it != previous.rend (); ++it)
      env.write_update (it->first, it->second);
  finished= true;
}

native_math_context_scope::~native_math_context_scope () {
  if (!finished) finish ();
}

local_environment_bindings table_cell_environment (edit_env_rep& env, tree format) {
  local_environment_bindings result;
  for (int i=0; i<N(format); ++i) {
    const tree binding= format[i];
    if (is_func (binding, CWITH, 2) && is_atomic (binding[0]) &&
        !starts (binding[0]->label, "cell-")) {
      const string name= binding[0]->label;
      result.push_back ({name, binding[1], env.read (name), env.provides (name)});
    }
  }
  return result;
}

local_environment_scope::local_environment_scope (
    edit_env_rep& owner, const local_environment_bindings& values, bool monitored):
  env (owner), bindings (values), before (owner.contract_state.contracts),
  capture (owner.contract_events, events, &owner.contract_state.contracts) {
  try {
    resolve_targets ();
    env.contract_state.contracts.enter_local_targets (targets);
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
  if (!env.contract_state.contracts.same_state (before)) {
    resolve_targets ();
    env.contract_state.contracts.restore_targets (before, targets);
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
  previous_contracts (owner.contract_state.contracts),
  capture (owner.contract_events, events, &owner.contract_state.contracts) {
  env.read_env (previous_values);
  evaluation_environment_transaction package_transaction (env);
  athena::contracts::contract_event_log writes;
  {
    athena::contracts::contract_event_capture package_capture (
      env.contract_events, writes, &env.contract_state.contracts);
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
  env.contract_state.contracts.restore_targets (previous_contracts, targets);
}

void scoped_package_environment::finish (bool remain_inside) {
  if (!remain_inside) restore ();
  capture.commit (std::move (targets), !remain_inside);
  finished= true;
}

scoped_package_environment::~scoped_package_environment () {
  if (!finished) restore ();
}
