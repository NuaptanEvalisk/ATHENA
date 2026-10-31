
/******************************************************************************
* MODULE     : env.cpp
* DESCRIPTION: the environment of the math_editor
* COPYRIGHT  : (C) 1999  Joris van der Hoeven
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "env.hpp"
#include "local_environment.hpp"
#include "Data/NodeContracts/macro_analysis.hpp"
#include "iterator.hpp"
extern thread_local hashmap<string,int> default_var_type;
void initialize_default_var_type ();
extern thread_local hashmap<string,tree> default_env;
extern thread_local athena::contracts::definition_entries default_definitions;
extern thread_local athena::contracts::evaluation_input_snapshot default_inputs;
void initialize_default_env ();
#include "page_type.hpp"

/******************************************************************************
* Initialization
******************************************************************************/

edit_env_rep::edit_env_rep (athena::contracts::evaluation_state& contract_state2,
			    url base_file_name2,
			    hashmap<string,tree>& local_ref2,
			    hashmap<string,tree>& global_ref2,
			    hashmap<string,tree>& local_aux2,
			    hashmap<string,tree>& global_aux2,
			    hashmap<string,tree>& local_att2,
			    hashmap<string,tree>& global_att2):
  contract_state (contract_state2),
  env (contract_state2.values), back (UNINIT), src (path (DECORATION)),
  var_type (default_var_type),
  base_file_name (base_file_name2),
  cur_file_name (base_file_name2),
  secure (is_secure (base_file_name2)),
  scheme_modules_enabled (true),
  local_ref (local_ref2), global_ref (global_ref2),
  local_aux (local_aux2), global_aux (global_aux2),
  local_att (local_att2), global_att (global_att2),
  missing (UNINIT), redefined (), touched (false)
{
  initialize_default_env ();
  initialize_default_var_type ();
  env= copy (default_env);
  contract_state.contracts.reset_definitions (default_definitions);
  inputs.reset (default_inputs);
  style_init_env ();
  update ();
  complete= false;
  page_refs_changed= false;
  recover_env= tuple ();
}

edit_env_rep::~edit_env_rep ()= default;

athena::contracts::evaluation_input_snapshot
edit_env_rep::variable_input_snapshot () const {
  return inputs.snapshot ();
}

bool
edit_env_rep::variable_values_match (
    const athena::contracts::evaluation_input_snapshot& previous,
    hashmap<string,tree> values) {
  for (const auto& changed: inputs.snapshot ().changed_since (previous)) {
    const string key (changed.data (), changed.size ());
    const bool present= env->contains (key);
    if (present != values->contains (key)) return false;
    if (present && env[key] != values[key]) return false;
  }
  return true;
}

edit_env::edit_env (athena::contracts::evaluation_state& contract_state,
		    url base_file_name,
		    hashmap<string,tree>& local_ref,
		    hashmap<string,tree>& global_ref,
		    hashmap<string,tree>& local_aux,
		    hashmap<string,tree>& global_aux,
		    hashmap<string,tree>& local_att,
		    hashmap<string,tree>& global_att):
  rep (tm_new<edit_env_rep> (contract_state, base_file_name,
                             local_ref, global_ref,
                             local_aux, global_aux,
                             local_att, global_att)) {}

void
edit_env_rep::style_init_env () {
  dpi = get_int (DPI);
  inch= ((double) dpi*PIXEL);
  flexibility= get_double (PAGE_FLEXIBILITY);
  first_page= get_double (PAGE_FIRST);
  back= hashmap<string,tree> (UNINIT);
  update_page_pars ();
}

/******************************************************************************
* Modification of environment variables
******************************************************************************/

void
edit_env_rep::record_variable_write (const string& name, const tree* value) {
  if (contract_events) {
    const auto write= contract_events->write_variable (name, value);
    if (recorded_contexts) recorded_contexts->write (write);
  }
  else if (recorded_contexts) recorded_contexts->write (name, value);
}

athena::contracts::evaluation_boundary_handle
edit_env_rep::mark_execution_boundary () {
  const auto boundary= contract_events->mark_boundary ();
  if (recorded_contexts) recorded_contexts->mark (boundary);
  return boundary;
}

void
edit_env_rep::write_value (string name, tree value, bool monitored, bool refresh) {
  record_variable_write (name, &value);
  synchronize_definition_value (name, value);
  const bool changed= !env->contains (name) || env[name] != value;
  if (changed) inputs.record (std::string (name.data (), N(name)), true);
  if (monitored && (changed || !refresh)) back->write_back (name, env);
  if (changed || !refresh) env(name)= value;
  if (changed && refresh) update (name);
}

void
edit_env_rep::assign (string name, tree expression) {
  tree value= exec (expression);
  track_definition (name, value);
  monitored_write_update (name, value);
}

void
edit_env_rep::restore_local_value (string name, tree value, bool previously_present) {
  if (previously_present) {
    record_variable_write (name, &value);
    // The enclosing scope restores the original binding after its values.
    // Restoring a value is not execution of a new definition producer.
    if (existing_tree_label (name)) {
      const auto label= as_tree_label (name);
      const auto binding= contract_state.contracts.definition (label);
      if (binding && !binding->value->expression.matches (value))
        contract_state.contracts.forget_definition (label);
    }
    const bool changed= !env->contains (name) || env[name] != value;
    if (changed) {
      inputs.record (std::string (name.data (), N(name)), true);
      env(name)= value;
      update (name);
    }
  }
  else {
    record_variable_write (name, nullptr);
    if (existing_tree_label (name))
      contract_state.contracts.forget_definition (as_tree_label (name));
    const bool changed= env[name] != value;
    if (env->contains (name)) inputs.record (std::string (name.data (), N(name)), false);
    env->reset (name);
    if (changed) update (name);
  }
}

/*
void
edit_env_rep::set (string s, tree t) {
  // cout << "Set " << s << " := " << t << "\n";
  tree r= exec (t); // Replacing r by exec(t) below yields bug when optimizing
  // cout << "Do " << s << " := " << r << "\n";
  monitored_write (s, tree (BACKUP, r, read(s)));
  update (s);
}

void
edit_env_rep::reset (string s) {
  // cout << "Reset " << s << "\n";
  if (is_func (read(s), BACKUP, 2)) monitored_write (s, read(s)[1]);
  update (s);
}

void
edit_env_rep::assign (string s, tree t) {
  // cout << "Assign " << s << " := " << t << "\n";
  t= exec (t);
  // cout << "Do     " << s << " := " << t << "\n";
  if (is_func (read(s), BACKUP, 2)) {
    if (read(s)[0] != t) {
      monitored_write (s, tree (BACKUP, t, read(s)[1]));
      update (s);
    }
  }
  else {
    if (read(s) != t) {
      monitored_write (s, t);
      update (s);
    }
  }
}
*/

tree
edit_env_rep::local_begin_extents (box b) {
  tree old= tree (TUPLE,
                  read ("w-length"), read ("h-length"),
                  read ("l-length"), read ("b-length"),
                  read ("r-length"), read ("t-length"));
  write ("w-length", as_string (b->w ()) * "tmpt");
  write ("h-length", as_string (b->h ()) * "tmpt");
  write ("l-length", as_string (b->x1) * "tmpt");
  write ("b-length", as_string (b->y1) * "tmpt");
  write ("r-length", as_string (b->x2) * "tmpt");
  write ("t-length", as_string (b->y2) * "tmpt");
  return old;
}

void
edit_env_rep::local_end_extents (tree t) {
  write ("w-length", t[0]);
  write ("h-length", t[1]);
  write ("l-length", t[2]);
  write ("b-length", t[3]);
  write ("r-length", t[4]);
  write ("t-length", t[5]);
}

/******************************************************************************
* Global manipulations of the environment
******************************************************************************/

void
edit_env_rep::write_default_env () {
  if (contract_events) contract_events->record (athena::contracts::evaluation_barrier {});
  contract_state.contracts.reset_definitions (default_definitions);
  env= copy (default_env);
  inputs.reset (default_inputs);
  if (recorded_contexts) recorded_contexts->rebase ();
}

void
edit_env_rep::write_env (hashmap<string,tree> user_env) {
  if (contract_events) contract_events->record (athena::contracts::evaluation_barrier {});
  env= copy (user_env);
  initialize_value_inputs ();
  if (recorded_contexts) recorded_contexts->rebase ();
}

void
edit_env_rep::initialize_value_inputs () {
  athena::contracts::definition_entries baseline;
  std::set<std::string> names;
  iterator<string> keys= iterate (env);
  while (keys->busy ()) {
    const string name= keys->next ();
    names.emplace (name.data (), N(name));
    const tree value= env[name];
    if (!is_func (value, MACRO) && !is_func (value, XMACRO)) continue;
    const auto current= contract_state.contracts.definition (make_tree_label (name));
    auto binding= athena::contracts::capture_definition (
      name, value, current ? current->value : nullptr);
    const auto target= binding->target;
    baseline= std::move (baseline).set (target, std::move (binding));
  }
  contract_state.contracts.reset_definitions (std::move (baseline));
  inputs.reset (athena::contracts::evaluation_input_snapshot::baseline (std::move (names)));
}

void
edit_env_rep::analyze_contracts () {
  if (!contract_analysis) contract_analysis= std::make_unique<athena::contracts::macro_analysis> ();
  analyze_contracts (*contract_analysis);
}

void
edit_env_rep::analyze_contracts (athena::contracts::macro_analysis& analysis) {
  analysis.synchronize (contract_state.contracts, inputs.snapshot (), env);
}

bool
edit_env_rep::prepare_contracts (std::function<void ()> notify,
                               const evaluation_environment_checkpoint* prepared) {
  if (!contract_analysis) contract_analysis= std::make_unique<athena::contracts::macro_analysis> ();
  const auto selected= inputs.snapshot ();
  athena::contracts::retained_analysis_value retained;
  if (prepared) retained= [prepared] (const string& name) {
    return prepared->retained_value (name);
  };
  contract_analysis->request (contract_state.contracts, selected, env, std::move (notify), retained);
  return contract_analysis->publish_ready (contract_state.contracts);
}

void
edit_env_rep::monitored_patch_env (hashmap<string,tree> patch) {
  if (patch->size == 0) return;
  int i=0, n=patch->n;
  for (; i<n; i++) {
    list<hashentry<string,tree> > l=patch->a[i];
    for (; !is_nil(l); l=l->next)
      monitored_write_update (l->item.key, l->item.im);
  }
}

void
edit_env_rep::patch_env (hashmap<string,tree> patch) {
  if (patch->size == 0) return;
  int i=0, n=patch->n;
  for (; i<n; i++) {
    list<hashentry<string,tree> > l=patch->a[i];
    for (; !is_nil(l); l=l->next)
      write_update (l->item.key, l->item.im);
  }
}

void
edit_env_rep::read_env (hashmap<string,tree>& ret) {
  ret= copy (env);
}

void
edit_env_rep::local_start (hashmap<string,tree>& prev_back) {
  prev_back= back;
  back= hashmap<string,tree> (UNINIT);  
}

void
edit_env_rep::local_update (hashmap<string,tree>& old_patch,
			    hashmap<string,tree>& change)
{
  old_patch->pre_patch (back, env);
  old_patch->post_patch (change, env);
  change= invert (back, env);
}

void
edit_env_rep::local_end (hashmap<string,tree>& prev_back) {
  int i=0, n=back->n;
  for (; i<n; i++) {
    list<hashentry<string,tree> > l=back->a[i];
    for (; !is_nil(l); l=l->next)
      prev_back->write_back (l->item.key, back);
  }
  back= prev_back;
}

tm_ostream&
operator << (tm_ostream& out, edit_env env) {
  return out << env->env;
}
