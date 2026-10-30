
/******************************************************************************
* MODULE     : typeset.cpp
* DESCRIPTION: typeset the tree being edited
* COPYRIGHT  : (C) 1999  Joris van der Hoeven
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include <climits>
#include <stdexcept>
#include "edit_typeset.hpp"
#include "prooftrack_presentation.hpp"
#include "heading_word_count.hpp"
#include "tm_buffer.hpp"
#include "convert.hpp"
#include "Data/Convert/AI/compress.hpp"
#include "file.hpp"
#include "analyze.hpp"
#include "tm_timer.hpp"
#include "Bridge/impl_typesetter.hpp"
#include "new_style.hpp"
#include "iterator.hpp"
#include "merge_sort.hpp"
#include "Typeset/Env/compound_counters.hpp"
#include "Typeset/Env/local_environment.hpp"
#include "scheme.hpp"
#include "node_metadata_transform.hpp"
#include "Data/NodeContracts/editing_query.hpp"
#include "cursor_navigation.hpp"
#include "tree_modify.hpp"
#include "tree_math_stats.hpp"
#include "tree_correct.hpp"
#include "tree_analyze.hpp"
#include "Data/NodeContracts/detached_environment.hpp"
#include "buffer_actor.hpp"
#ifdef EXPERIMENTAL
#include "../../Style/Environment/std_environment.hpp"
#endif // EXPERIMENTAL

//box empty_box (path ip, int x1=0, int y1=0, int x2=0, int y2=0);
static bool
toc_fold_node (tree t) {
  return is_compound (t, "table-of-contents") ||
         is_compound (t, "table-of-contents*");
}

static bool
toc_fold_contains (tree t) {
  if (toc_fold_node (t)) return true;
  if (is_atomic (t)) return false;
  for (int i=0; i<N(t); i++)
    if (toc_fold_contains (t[i])) return true;
  return false;
}

/******************************************************************************
* Contructors, destructors and notification of modifications
******************************************************************************/

struct cursor_environment_checkpoint: evaluation_environment_checkpoint {
  const athena::contracts::contract_event_log trace;
  const athena::contracts::evaluation_boundary_handle boundary;
  const std::shared_ptr<const evaluation_environment_checkpoint> execution_base;
  cursor_environment_checkpoint (const edit_env_rep& env,
      athena::contracts::contract_event_log trace= {},
      athena::contracts::evaluation_boundary_handle boundary= {},
      std::shared_ptr<const evaluation_environment_checkpoint> base= {}):
    evaluation_environment_checkpoint (env),
    trace (std::move (trace)), boundary (std::move (boundary)), execution_base (std::move (base)) {}
  cursor_environment_checkpoint (const evaluation_environment_checkpoint& context,
      const source_occurrence& occurrence, athena::contracts::contract_event_log trace,
      athena::contracts::evaluation_boundary_handle boundary,
      std::shared_ptr<const evaluation_environment_checkpoint> base):
    evaluation_environment_checkpoint (context, occurrence),
    trace (std::move (trace)), boundary (std::move (boundary)), execution_base (std::move (base)) {}
};

edit_typeset_rep::edit_typeset_rep ():
  editor_rep (), // NOTE: ignored by the compiler, but suppresses warning
  the_style (TUPLE),
  init (UNINIT), fin (UNINIT), grefs (UNINIT),
  folded_headings (), folded_tocs (), unfolded_tocs (),
  fold_view_active (false), fold_view_rebuild (false),
  fold_view_has_toc (false),
  heading_word_count_cache_hash (INT_MIN), heading_word_count_cache (),
  heading_word_count_cache_map (0),
  env (contract_state, buf->master,
       buf->data->ref, grefs,
       buf->data->aux, buf->data->aux,
       buf->data->att, buf->data->att),
  ttt (new_typesetter (env, subtree (et, rp), reverse (rp))) {
    pre= std::make_unique<evaluation_environment_checkpoint> (*env.operator-> ());
    initial_background= pre->value (BG_COLOR);
    prepare_page_geometry ();
}

edit_typeset_rep::~edit_typeset_rep () { delete_typesetter (ttt); }

void
edit_typeset_rep::set_data (new_data data) {
  the_style= copy (data->style);
  init= copy (data->init);
  set_fin   (data->fin);
  set_ref   (data->ref);
  set_aux   (data->aux);
  set_att   (data->att);
  notify_page_change ();
  ttt->br->notify_assign (path (), subtree (et, rp));
  notify_change (THE_ENVIRONMENT);
  notify_change (THE_DECORATIONS);
  typeset_invalidate_env ();
  init_update ();
  iterator<string> it = iterate (data->att);
  while (it->busy()) {
    string key= it->next ();
    (void) call (string ("notify-set-attachment"),
                 buf->name, key, data->att [key]);
  }
  fold_view_has_toc= toc_fold_contains (subtree (et, rp));
  if (fold_view_has_toc) fold_view_rebuild= true;
}

void
edit_typeset_rep::get_data (new_data& data) {
  data->style= get_style ();
  data->init = get_init ();
  data->fin  = get_fin ();
  data->ref  = get_ref ();
  data->aux  = get_aux ();
  data->att  = get_att ();
}

typesetter edit_typeset_rep::get_typesetter () { return ttt; }
tree edit_typeset_rep::get_style () { return the_style; }
void edit_typeset_rep::set_style (tree t) {
  the_style= copy (t);
  init_update ();
}
hashmap<string,tree> edit_typeset_rep::get_init () { return init; }
hashmap<string,tree> edit_typeset_rep::get_fin () { return fin; }
hashmap<string,tree> edit_typeset_rep::get_ref () { return buf->data->ref; }
hashmap<string,tree> edit_typeset_rep::get_aux () { return buf->data->aux; }
hashmap<string,tree> edit_typeset_rep::get_att () { return buf->data->att; }
void edit_typeset_rep::set_fin (hashmap<string,tree> H) { fin= H; }
void edit_typeset_rep::set_ref (hashmap<string,tree> H) { buf->data->ref= H; }
void edit_typeset_rep::set_aux (hashmap<string,tree> H) { buf->data->aux= H; }
void edit_typeset_rep::set_att (hashmap<string,tree> H) { buf->data->att= H; }

tree
edit_typeset_rep::get_ref (string key) {
  return buf->data->ref[key];
}

void
edit_typeset_rep::set_ref (string key, tree im) {
  buf->data->ref (key)= im;
}

void
edit_typeset_rep::reset_ref (string key) {
  buf->data->ref->reset (key);
}

static string
concat_as_string (tree t) {
  if (is_atomic (t)) return t->label;
  else if (is_func (t, CONCAT)) {
    string r;
    for (int i=0; i<N(t); i++)
      r << concat_as_string (t[i]);
    return r;
  }
  else return "?";
}

array<string>
edit_typeset_rep::find_refs (string val, bool global) {
  tree a= (tree) buf->data->ref;
  array<string> v;
  int i, n= N(a);
  for (i=0; i<n; i++)
    if (N(a[i]) >= 2 && N(a[i][1]) >= 1 &&
        concat_as_string (a[i][1][0]) == val)
      v << a[i][0]->label;
  return v;
}

array<string>
edit_typeset_rep::list_refs (bool global) {
  tree a= (tree) buf->data->ref;
  array<string> v;
  int i, n= N(a);
  for (i=0; i<n; i++)
    v << a[i][0]->label;
  merge_sort (v);
  return v;
}

tree
edit_typeset_rep::get_aux (string key) {
  return buf->data->aux[key];
}

void
edit_typeset_rep::set_aux (string key, tree im) {
  buf->data->aux (key)= im;
}

void
edit_typeset_rep::reset_aux (string key) {
  buf->data->aux->reset (key);
}

array<string>
edit_typeset_rep::list_auxs (bool global) {
  tree a= (tree) buf->data->aux;
  array<string> v;
  int i, n= N(a);
  for (i=0; i<n; i++)
    v << a[i][0]->label;
  merge_sort (v);
  return v;
}

tree
edit_typeset_rep::get_att (string key) {
  return buf->data->att[key];
}

void
edit_typeset_rep::set_att (string key, tree im) {
  buf->data->att (key)= im;
}

void
edit_typeset_rep::reset_att (string key) {
  buf->data->att->reset (key);
}

array<string>
edit_typeset_rep::list_atts (bool global) {
  tree a= (tree) buf->data->att;
  array<string> v;
  int i, n= N(a);
  for (i=0; i<n; i++)
    v << a[i][0]->label;
  merge_sort (v);
  return v;
}

void
edit_typeset_rep::set_init (hashmap<string,tree> H) {
  init= hashmap<string,tree> (UNINIT);
  add_init (H);
}

void
edit_typeset_rep::add_init (hashmap<string,tree> H) {
  init->join (H);
  ttt->br->notify_assign (path (), subtree (et, rp));
  notify_change (THE_ENVIRONMENT);
  init_update ();
}

void
edit_typeset_rep::clear_local_info () {
  buf->data->ref= hashmap<string,tree> ();
  buf->data->aux= hashmap<string,tree> ();
}

/******************************************************************************
* Miscellaneous routines for lengths arithmetic (should be elsewhere)
******************************************************************************/

SI
edit_typeset_rep::as_length (string l) {
  return env->as_length (l); }

string
edit_typeset_rep::add_lengths (string l1, string l2) {
  return env->add_lengths (l1, l2); }

string
edit_typeset_rep::sub_lengths (string l1, string l2) {
  return env->sub_lengths (l1, l2); }

string
edit_typeset_rep::max_lengths (string l1, string l2) {
  return env->max_lengths (l1, l2); }

string
edit_typeset_rep::min_lengths (string l1, string l2) {
  return env->min_lengths (l1, l2); }

string
edit_typeset_rep::multiply_length (double x, string l) {
  return env->multiply_length (x, l); }

bool
edit_typeset_rep::is_length (string s) {
  return env->is_length (s); }

double
edit_typeset_rep::divide_lengths (string l1, string l2) {
  return env->divide_lengths (l1, l2); }

/******************************************************************************
* Processing preamble
******************************************************************************/

void
use_modules (tree t) {
  if (is_tuple (t))
    for (int i=0; i<N(t); i++) {
      string s= as_string (t[i]);
      if (starts (s, "(")) eval ("(use-modules " * s * ")");
    }
}

void
edit_typeset_rep::typeset_style_use_cache (tree style) {
  style= preprocess_style (style, buf->master);
  contract_state= athena::contracts::evaluation_state (athena::contracts::builtin_contracts ().snapshot ());
  get_style_environment (style, buf->master, true)->restore (*env.operator-> ());
  use_modules (env->read (THE_MODULES));
}

void
edit_typeset_rep::typeset_preamble () {
  typeset_invalidate_env ();
  env->write_default_env ();
  if (buf->field_environment) {
    buf->field_environment->restore (*env.operator-> ());
    use_modules (env->read (THE_MODULES));
  }
  else typeset_style_use_cache (the_style);
  env->update ();
  env->patch_env (init);
  env->update ();
  if (!buf->field_environment) (void) env->prepare_contracts ({});
  pre= std::make_unique<evaluation_environment_checkpoint> (*env.operator-> ());
  prepare_initial_background ();
  if (!page_geometry_from_layout) prepare_page_geometry ();
  if (compound_counter_view)
    compound_counter_variables= athena::avd::counter_variables (env);
}

void
edit_typeset_rep::typeset_prepare () {
  env->base_file_name= buf->master;
  env->read_only= buf->read_only;
  pre->restore (*env.operator-> ());
  env->style_init_env ();
  if (compound_counter_view)
    athena::avd::apply_counter_state (
      env, compound_counter_variables, compound_incoming_counters);
  env->update ();
}

void
edit_typeset_rep::set_compound_counters (const tree& state) {
  athena::avd::validate_counter_state (state);
  if (compound_counter_view && compound_incoming_counters == state) return;
  compound_incoming_counters= copy (state);
  compound_counter_view= true;
  typeset_invalidate_env ();
  typeset_invalidate_all ();
}

void
edit_typeset_rep::clear_compound_counters () {
  if (!compound_counter_view) return;
  compound_counter_view= false;
  compound_incoming_counters= tree (COLLECTION);
  compound_counter_variables= hashset<string> ();
  typeset_invalidate_env ();
  typeset_invalidate_all ();
}

tree
edit_typeset_rep::compound_counter_environment () {
  typeset_preamble ();
  return (tree) pre->variables ();
}

tree
edit_typeset_rep::evaluate_compound_counters (const tree& incoming) {
  athena::avd::validate_counter_state (incoming);
  // Semantic execution has its own environment and reference/auxiliary maps.
  // It neither builds boxes nor changes this view's live typesetter state.
  athena::contracts::evaluation_state local_state= athena::contracts::evaluation_state (contract_state.contracts.snapshot ());
  hashmap<string,tree> refs= copy (buf->data->ref);
  hashmap<string,tree> global_refs= copy (grefs);
  hashmap<string,tree> aux= copy (buf->data->aux);
  hashmap<string,tree> attachments= copy (buf->data->att);
  edit_env evaluation (local_state, buf->master, refs, global_refs,
                       aux, aux, attachments, attachments);
  pre->restore (*evaluation.operator-> ());
  evaluation->style_init_env ();
  evaluation->update ();
  return athena::avd::evaluate_counter_state (
    evaluation, subtree (et, rp), incoming, buf->storage_version);
}

void
edit_typeset_rep::init_update () {
  typeset_preamble ();
}

void
edit_typeset_rep::prepare_initial_background () {
  const tree value= init->contains (BG_COLOR) ? init[BG_COLOR] : pre->value (BG_COLOR);
  if (!is_func (value, _PATTERN)) {
    initial_background= value;
    return;
  }
  temporary_environment_scope preparation (*env.operator-> ());
  pre->restore (*env.operator-> ());
  env->patch_env (init);
  env->style_init_env ();
  env->update ();
  initial_background= env->exec (value);
}

std::shared_ptr<const evaluation_environment_checkpoint>
edit_typeset_rep::prepare_macro_argument (
    tree invocation, int argument, const evaluation_environment_checkpoint& context) {
  return prepare_tree_position (invocation, path (argument, 0), context);
}

std::shared_ptr<const evaluation_environment_checkpoint>
edit_typeset_rep::prepare_tree_position (
    tree source, path position, const evaluation_environment_checkpoint& context) {
  if (!is_inside (source, position))
    throw std::invalid_argument ("Source preparation has an invalid destination");
  // Command preparation evaluates the selected source/prospective subtree. Its outputs must
  // not publish counters, references or auxiliary data into the live document.
  athena::contracts::evaluation_state state (context.contract_context ().snapshot ());
  athena::contracts::evaluation_state_scope owner (&state);
  hashmap<string,tree> refs= copy (buf->data->ref);
  hashmap<string,tree> global_refs= copy (grefs);
  hashmap<string,tree> aux= copy (buf->data->aux);
  hashmap<string,tree> attachments= copy (buf->data->att);
  edit_env preparation (state, buf->master, refs, global_refs,
                        aux, aux, attachments, attachments);
  context.restore (*preparation.operator-> ());
  preparation->style_init_env ();
  preparation->update ();
  preparation->compound_counter_execution= true;
  if (!preparation->exec_until (source, position)) return {};
  return std::make_shared<const evaluation_environment_checkpoint> (*preparation.operator-> ());
}

athena::document_node::source_contract_preparation
edit_typeset_rep::prepare_source_identity_contracts () {
  // Keep the baseline and advancing paragraph entry private to one identity
  // transaction. The editor never borrows its caret/display occurrence here.
  auto source_maps= std::make_shared<source_contract_maps> ();
  return prepare_source_contract_positions (
    subtree (et, rp), buf->master,
    [this, source_maps] () -> std::shared_ptr<const evaluation_environment_checkpoint> {
      const auto base= prepare_document_context ();
      source_maps->refs= copy (buf->data->ref);
      source_maps->global_refs= copy (grefs);
      source_maps->aux= copy (buf->data->aux);
      source_maps->attachments= copy (buf->data->att);
      // The live command evaluator uses the same local/global aux and
      // attachment stores. Keep those aliases in its isolated preparation.
      source_maps->shared_aux= source_maps->shared_attachments= true;
      // Source identity activation includes the author's document preamble.
      // Unlike display cursor preparation, role planning needs its definitions
      // even when they are not part of the visible BODY traversal.
      const tree preamble= get_document_preamble (subtree (et, rp));
      if (preamble == "") return base;
      athena::contracts::evaluation_state state (base->contract_context ().snapshot ());
      athena::contracts::evaluation_state_scope owner (&state);
      edit_env preparation (state, buf->master,
                            source_maps->refs, source_maps->global_refs,
                            source_maps->aux, source_maps->aux,
                            source_maps->attachments, source_maps->attachments);
      base->restore (*preparation.operator-> ());
      preparation->style_init_env ();
      preparation->update ();
      preparation->exec (preamble);
      preparation->analyze_contracts ();
      return std::make_shared<const evaluation_environment_checkpoint> (
        *preparation.operator-> ());
    }, [source_maps] { return *source_maps; });
}

std::shared_ptr<const evaluation_environment_checkpoint>
editor_rep::prepare_source_position (path position) {
  std::shared_ptr<const evaluation_environment_checkpoint> document_context;
  return prepare_source_position (position, document_context);
}

std::shared_ptr<const evaluation_environment_checkpoint>
editor_rep::prepare_tree_entry (tree source) {
  const path ip= obtain_ip (source);
  if (!ip_attached (ip))
    return prepare_source_position (selection_active_any () ? selection_get_cursor_path () : tp);
  const path base= reverse (ip);
  if (!(rp <= base) || !test_subtree (base) || !strong_equal (subtree (et, base), source))
    throw std::runtime_error ("Command source is stale or outside this buffer");
  return prepare_source_position (base * 0);
}

void
editor_rep::correct_source_tree (tree source, int direction) {
  const path ip= obtain_ip (source);
  const path base= reverse (ip);
  if (!ip_attached (ip) || !(rp <= base) || !test_subtree (base) ||
      !strong_equal (subtree (et, base), source))
    throw std::runtime_error ("Tree correction requires source attached to this buffer");
  const auto correct= [&] (path p) {
    tree value= subtree (et, p);
    if (is_atomic (value)) return;
    const auto context= athena::contracts::builtin_label (as_string (L(value))) ? nullptr :
      prepare_source_position (p * 0);
    ::correct_node (value, {context ? context->contract_context () :
      athena::contracts::builtin_contracts ()});
  };
  if (direction > 0) {
    const std::function<void (path)> descend= [&] (path p) {
      const tree value= subtree (et, p);
      if (is_compound (value))
        for (int i=0; i<N(value); ++i) descend (p * i);
      correct (p);
    };
    descend (base);
  }
  else {
    path p= base;
    while (true) {
      correct (p);
      if (direction == 0 || p == rp) break;
      p= path_up (p);
    }
  }
}

void editor_rep::correct_tree_node (tree source) { correct_source_tree (source, 0); }
void editor_rep::correct_tree_downwards (tree source) { correct_source_tree (source, 1); }
void editor_rep::correct_tree_upwards (tree source) { correct_source_tree (source, -1); }

bool
editor_rep::command_tree_with_like (tree source) {
  if (is_atomic (source) || N(source) == 0) return false;
  const auto context= athena::contracts::builtin_label (as_string (L(source))) ? nullptr :
    prepare_tree_entry (source);
  return is_with_like (source, {context ? context->contract_context () :
    athena::contracts::builtin_contracts ()});
}

bool
editor_rep::command_tree_with_same_type (tree first, tree second) {
  if (is_atomic (first) || is_atomic (second) || N(first) == 0 || N(second) == 0) return false;
  const auto left= athena::contracts::builtin_label (as_string (L(first))) ? nullptr :
    prepare_tree_entry (first);
  const auto right= athena::contracts::builtin_label (as_string (L(second))) ? nullptr :
    prepare_tree_entry (second);
  return with_same_type (first, second,
    {left ? left->contract_context () : athena::contracts::builtin_contracts ()},
    {right ? right->contract_context () : athena::contracts::builtin_contracts ()});
}

bool
editor_rep::with_admitted_navigation (
    const evaluation_environment_checkpoint& policy,
    const std::function<void (const cursor_navigation&)>& action,
    const std::optional<path>& display, const std::optional<path>& root) {
  using athena::contracts::access_policy;
  if (policy.effective_value (MODE) == "src" || policy.effective_value (PREAMBLE) == "true") {
    action (cursor_navigation ({athena::contracts::builtin_contracts (), access_policy::source}));
    return true;
  }
  struct pending_context {};
  const cursor_context_preparation prepare= [&] (path node) -> navigation_context {
    const auto entry= display ? display_context_at (node * 0, *display) :
      editor_rep::cursor_context_at (node * 0);
    if (!entry) throw pending_context {};
    const bool source= entry->effective_value (MODE) == "src" ||
                       entry->effective_value (PREAMBLE) == "true";
    return {{entry, &entry->contract_context ()},
      source ? access_policy::source : access_policy::normal};
  };
  try {
    action (cursor_navigation (prepare, root ? *root : rp));
    return true;
  }
  catch (const pending_context&) { return false; }
}

std::optional<path>
editor_rep::try_admitted_boundary (path p, bool at_start) {
  const auto context= editor_rep::cursor_context_at (p * 0);
  path result;
  if (!context || !with_admitted_navigation (*context, [&] (const cursor_navigation& navigation) {
    const tree source= subtree (et, p);
    result= p * (at_start ? navigation.start (source) : navigation.end (source));
  }, {}, p)) return {};
  return result;
}

path
editor_rep::admitted_boundary (path p, bool at_start) {
  const auto result= try_admitted_boundary (p, at_start);
  if (!result) throw std::runtime_error ("Cursor boundary is awaiting layout");
  return *result;
}

path
editor_rep::prepare_command_boundary (path p, bool at_start) {
  const tree t= subtree (et, p);
  if (is_atomic (t)) return p * (at_start ? 0 : N(t->label));
  const auto context= prepare_source_position (p * 0);
  return prepare_command_boundary (p, p, at_start, *context);
}

path
editor_rep::prepare_command_boundary (path root, path target, bool at_start,
    const evaluation_environment_checkpoint& context) {
  return root * prepare_tree_boundary (subtree (et, root), target / root, at_start, context);
}

path
editor_rep::prepare_tree_boundary (tree source, path target, bool at_start,
    const evaluation_environment_checkpoint& context) {
  using athena::contracts::access_policy;
  struct path_order {
    bool operator () (path a, path b) const {
      // Subtree addresses include the empty root and proper prefixes, unlike
      // the cursor positions ordered by path_less.
      while (!is_nil (a) && !is_nil (b)) {
        if (a->item != b->item) return a->item < b->item;
        a= a->next;
        b= b->next;
      }
      return is_nil (a) && !is_nil (b);
    }
  };
  std::map<path, std::shared_ptr<const evaluation_environment_checkpoint>, path_order> entries;
  entries.emplace (path (), std::make_shared<const evaluation_environment_checkpoint> (context));
  const cursor_context_preparation prepare= [&] (path relative) -> navigation_context {
    auto found= entries.find (relative);
    if (found == entries.end ()) {
      const auto entry= prepare_tree_position (source, relative * 0, context);
      if (!entry) throw std::runtime_error ("Command boundary has no evaluable field context");
      found= entries.emplace (relative, entry).first;
    }
    const auto& entry= found->second;
    const bool source_mode= entry->effective_value (MODE) == "src" ||
                           entry->effective_value (PREAMBLE) == "true";
    return {{entry, &entry->contract_context ()},
      source_mode ? access_policy::source : access_policy::normal};
  };
  const cursor_navigation navigation (prepare, path ());
  return at_start ? navigation.start (source, target) : navigation.end (source, target);
}

std::shared_ptr<const evaluation_environment_checkpoint>
editor_rep::prepare_source_position (
    path position, std::shared_ptr<const evaluation_environment_checkpoint>& document_context) {
  auto context= editor_rep::cursor_context_at (position);
  if (!context) {
    path source= path_up (position);
    while (rp < source) {
      context= editor_rep::cursor_context_at (source * 0);
      if (context) break;
      source= path_up (source);
    }
    if (!context) {
      if (!document_context) document_context= prepare_document_context ();
      context= document_context;
    }
    context= prepare_tree_position (subtree (et, source), position / source, *context);
  }
  if (!context) {
    const string where= as_string (position / rp);
    throw std::runtime_error ("Command source has no evaluable context at " +
      std::string (where.data (), N(where)));
  }
  return context;
}

void edit_typeset_rep::set_field_environment (
    std::shared_ptr<const athena::contracts::detached_environment> context) {
  buf->field_environment= std::move (context);
  notify_change (THE_ENVIRONMENT);
  init_update ();
}

std::shared_ptr<const source_occurrence>
edit_typeset_rep::source_occurrence_at (
    path source, const tree& value,
    const athena::contracts::evaluation_boundary_handle& selected) {
  if (fold_view_active && rp <= source) {
    const path relative= source / rp;
    if (folded_source_pairs->contains (relative)) {
      const tree pair= folded_source_pairs[relative];
      if (pair[0] != value) return {};
      return find_source_occurrence (
        ttt->br->source_occurrences, source, pair[1], selected);
    }
  }
  return find_source_occurrence (
    ttt->br->source_occurrences, source, value, selected);
}

std::shared_ptr<const cursor_environment_checkpoint>
edit_typeset_rep::prepared_context_at (
    path position, athena::contracts::evaluation_boundary_handle selected_boundary) {
  if (!ttt->execution_base || !(rp < position)) return {};
  const path source_path= path_up (position);
  if (!has_subtree (et, source_path)) return {};
  const tree source= subtree (et, source_path);
  const auto trace= ttt->br->contract_changes;
  const auto base= ttt->execution_base;
  const auto occurrence= source_occurrence_at (source_path, source, selected_boundary);
  if (!occurrence) return {};
  const auto boundary= !is_atomic (source) && last_item (position) == right_index (source) ?
    occurrence->end_boundary : occurrence->boundary;
  if (!boundary) return {};
  for (const auto& cached: cursor_contexts)
    if (cached->trace == trace && cached->boundary == boundary &&
        cached->execution_base == base) return cached;
  const auto* recorded= ttt->execution_contexts.find (boundary);
  const auto context= recorded ? *recorded : nullptr;
  if (!context) return {};
  const auto prepared= std::make_shared<const cursor_environment_checkpoint> (
    *context, *occurrence, trace, boundary, base);
  if (cursor_contexts.size () >= 25) cursor_contexts.clear ();
  cursor_contexts.push_back (prepared);
  return prepared;
}

std::shared_ptr<const evaluation_environment_checkpoint>
edit_typeset_rep::cursor_context_at (
    path position, athena::contracts::evaluation_boundary_handle selected) {
  return occurrence_context_at (position, tp, std::move (selected));
}

std::shared_ptr<const evaluation_environment_checkpoint>
edit_typeset_rep::occurrence_context_at (
    path position, path anchor, athena::contracts::evaluation_boundary_handle selected) {
  if (!ttt->execution_base || !(rp < anchor) ||
      !has_subtree (et, path_up (anchor))) return {};
  if (selected && path_up (position) < path_up (anchor)) {
    const auto leaf= source_occurrence_at (
      path_up (anchor), subtree (et, path_up (anchor)), selected);
    if (leaf && (leaf->boundary == selected || leaf->end_boundary == selected))
      for (auto parent= leaf->parent; parent; parent= parent->parent)
        if (cursor_occurrence_admitted (position, parent->boundary))
          return prepared_context_at (position, parent->boundary);
  }
  return prepared_context_at (position, std::move (selected));
}

bool
edit_typeset_rep::cursor_occurrence_admitted (
    path position, const athena::contracts::evaluation_boundary_handle& selected) {
  if (!ttt->execution_base || !(rp < position)) return false;
  const path source_path= path_up (position);
  if (!has_subtree (et, source_path)) return false;
  const auto occurrence= source_occurrence_at (
    source_path, subtree (et, source_path), selected);
  return occurrence && (occurrence->boundary == selected || occurrence->end_boundary == selected);
}

void
edit_typeset_rep::prepare_cursor_environment () {
  auto selected= cursor_source_boundary ();
  if (!selected && prepared_cursor && prepared_cursor->trace == ttt->br->contract_changes &&
      prepared_cursor->execution_base == ttt->execution_base) selected= prepared_cursor->boundary;
  const auto prepared= prepared_context_at (tp, selected);
  if (!prepared) return;
  path input= search_upwards (HYBRID);
  if (is_nil (input)) input= search_upwards (LATEX);
  if (is_nil (input)) prepare_source_signatures (*prepared);
  else {
    if (is_func (subtree (et, path_up (input)), INACTIVE)) input= path_up (input);
    // The name field has display-only text mode, even in source authoring.
    if (const auto invocation= editor_rep::cursor_context_at (input * 0))
      prepare_source_signatures (*invocation);
  }
  if (prepared->effective_value (PREAMBLE) == "true") return;
  prepared_cursor= prepared;
  prepared->restore (*env.operator-> ());
  env->style_init_env ();
  env->update ();
  std::function<void ()> wake;
  if (const auto* context= current_scheme_execution_context (); context && context->actor) {
    const auto actor= context->actor_id;
    const auto view= context->view_id;
    wake= [actor, view] {
      buffer_actor::notify_contracts_ready (actor, view);
    };
  }
  // A retained context can already be ready; publication still validates it
  // against this owner rather than trusting the worker or the cursor path.
  const bool changed= env->prepare_contracts (std::move (wake), prepared.get ());
  if (changed) {
    math_statistics_cache.reset ();
    auto resolved= std::make_shared<const cursor_environment_checkpoint> (
      *env.operator-> (), prepared->trace, prepared->boundary, prepared->execution_base);
    prepared_cursor= resolved;
    for (auto& cached: cursor_contexts)
      if (cached == prepared) cached= resolved;
  }
}

#ifdef EXPERIMENTAL
void
edit_typeset_rep::environment_update () {
  hashmap<string,tree> h;
  typeset_prepare ();
  env->assign ("base-file-name", as_string (env->base_file_name));
  env->assign ("cur-file-name", as_string (env->cur_file_name));
  env->assign ("secure", bool_as_tree (env->secure));
  env->read_env (h);
  ::primitive (ste, h);
}
#endif

/******************************************************************************
* Routines for getting information
******************************************************************************/

void
edit_typeset_rep::typeset_invalidate_env () {
  math_statistics_cache.reset ();
  ttt->execution_base.reset ();
  prepared_cursor.reset ();
  cursor_contexts.clear ();
}

tree
edit_typeset_rep::get_full_env () {
  if (const auto prepared= editor_rep::cursor_context_at (tp))
    return (tree) prepared->variables ();
  throw std::runtime_error ("Cursor environment is awaiting preparation");
}

bool
edit_typeset_rep::defined_at_cursor (string var) {
  if (const auto prepared= editor_rep::cursor_context_at (tp))
    return prepared->contains (var);
  throw std::runtime_error ("Cursor environment is awaiting preparation for " +
    std::string (var.data (), N(var)));
}

tree
edit_typeset_rep::get_env_value (string var, path p) {
  if (const auto prepared= editor_rep::cursor_context_at (p)) {
    return prepared->effective_value (var);
  }
  throw std::runtime_error ("Cursor environment is awaiting preparation for " +
    std::string (var.data (), N(var)));
}

tree
edit_typeset_rep::get_env_value (string var) {
  return get_env_value (var, tp);
}

bool
edit_typeset_rep::defined_at_init (string var) {
  if (init->contains (var)) return true;
  return pre->contains (var);
}

bool
edit_typeset_rep::defined_in_init (string var) {
  return init->contains (var);
}

tree
edit_typeset_rep::get_init_value (string var) {
  tree t= var == BG_COLOR ? initial_background :
    init->contains (var) ? init[var] : pre->value (var);
  return is_func (t, BACKUP, 2)? t[0]: t;
}

string
edit_typeset_rep::get_env_string (string var) {
  return as_string (get_env_value (var));
}

string
edit_typeset_rep::get_init_string (string var) {
  return as_string (get_init_value (var));
}

int
edit_typeset_rep::get_env_int (string var) {
  return as_int (get_env_value (var));
}

int
edit_typeset_rep::get_init_int (string var) {
  return as_int (get_init_value (var));
}

double
edit_typeset_rep::get_env_double (string var) {
  return as_double (get_env_value (var));
}

double
edit_typeset_rep::get_init_double (string var) {
  return as_double (get_init_value (var));
}

color
edit_typeset_rep::get_env_color (string var) {
  return named_color (as_string (get_env_value (var)));
}

color
edit_typeset_rep::get_init_color (string var) {
  return named_color (as_string (get_init_value (var)));
}

language
edit_typeset_rep::get_env_language () {
  string mode= get_env_string (MODE);
  if (mode == "text")
    return text_language (get_env_string (LANGUAGE));
  else if (mode == "math")
    return math_language (get_env_string (MATH_LANGUAGE));
  else return prog_language (get_env_string (PROG_LANGUAGE));
}

int
edit_typeset_rep::get_page_count () {
  return N (eb[0]);
}

SI
edit_typeset_rep::get_page_width (bool deco) {
  return (prepared_page_geometry.page_width[deco] + std_shrinkf - 1) / std_shrinkf;
}

SI
edit_typeset_rep::get_pages_width (bool deco) {
  return (prepared_page_geometry.pages_width[deco] + std_shrinkf - 1) / std_shrinkf;
}

SI
edit_typeset_rep::get_page_height (bool deco) {
  return (prepared_page_geometry.page_height[deco] + std_shrinkf - 1) / std_shrinkf;
}

void
edit_typeset_rep::prepare_page_geometry () {
  for (int deco=0; deco<2; ++deco) {
    prepared_page_geometry.page_width[deco]= env->get_page_width (deco != 0);
    prepared_page_geometry.pages_width[deco]= env->get_pages_width (deco != 0);
    prepared_page_geometry.page_height[deco]= env->get_page_height (deco != 0);
  }
  prepared_page_geometry.packet= env->page_packet;
  prepared_page_geometry.offset= env->page_offset;
}

SI
edit_typeset_rep::get_total_width (bool deco) {
  SI w= eb->w();
  if (!deco) {
    SI w1= prepared_page_geometry.pages_width[false];
    SI w2= prepared_page_geometry.pages_width[true];
    w -= (w2 - w1);
  }
  return (w + std_shrinkf - 1) / std_shrinkf;
}

SI
edit_typeset_rep::get_total_height (bool deco) {
  SI h= eb->h();
  if (!deco) {
    SI h1= prepared_page_geometry.page_height[false];
    SI h2= prepared_page_geometry.page_height[true];
    int nr= get_page_count ();
    int nx= prepared_page_geometry.packet;
    int ny= ((nr + prepared_page_geometry.offset) + nx - 1) / nx;
    h -= ny * (h2 - h1);
  }
  return (h + std_shrinkf - 1) / std_shrinkf;
}

/******************************************************************************
* Execution without typesetting
******************************************************************************/

static tree
simplify_execed (tree t) {
  if (is_atomic (t)) {
    tree r= copy (t);
    athena::node::transform_rich_metadata (t, r, simplify_execed);
    return r;
  }
  int i, n= N(t);
  tree r (t, n);
  for (i=0; i<n; i++)
    r[i]= simplify_execed (t[i]);
  athena::node::transform_rich_metadata (t, r, simplify_execed);
  if (is_func (r, QUOTE, 1) && is_atomic (r[0]) && !athena::node::get (r))
    return r[0];
  else return r;
}

static tree
expand_references (tree t, hashmap<string,tree> h) {
  auto finish= [&] (tree r) {
    athena::node::transform_rich_metadata (
      t, r, [&] (tree rich) { return expand_references (rich, h); });
    return r;
  };
  if (is_atomic (t)) return finish (copy (t));
  if (is_func (t, REFERENCE, 1) || is_func (t, PAGEREF)) {
    string ref= as_string (simplify_execed (t[0]));
    tree destination ("#" * ref);
    athena::node::copy_metadata (t[0], destination);
    if (h->contains (ref)) {
      int which= is_func (t, REFERENCE, 1)? 0: 1;
      return finish (tree (HLINK, copy (h[ref][which]), destination));
    }
    return finish (tree (HLINK, "?", destination));
  }
  int i, n= N(t);
  tree r (t, n);
  for (i=0; i<n; i++)
    r[i]= expand_references (t[i], h);
  return finish (r);
}

static void
prefix_specific (hashmap<string,tree>& H, string prefix) {
  hashmap<string,tree> R;
  iterator<string> it = iterate (H);
  while (it->busy()) {
    string key= it->next ();
    string var= prefix * key;
    if (H->contains (var)) R(key)= H[var];
    else R(key)= H[key];
  }
  H= R;
}

std::shared_ptr<const evaluation_environment_checkpoint>
edit_typeset_rep::prepare_document_context () {
  if (has_changed (THE_ENVIRONMENT)) typeset_preamble ();
  return std::make_shared<const evaluation_environment_checkpoint> (*pre);
}

std::shared_ptr<const evaluation_environment_checkpoint>
edit_typeset_rep::conversion_context_at (path p) {
  if (p == rp * 0) {
    // Whole-document expansion starts before its body effects. This is an
    // explicit conversion operation, not a getter preparing a missing cursor.
    return prepare_document_context ();
  }
  const auto context= editor_rep::cursor_context_at (p);
  if (!context)
    throw std::runtime_error ("Conversion source context is not prepared");
  return context;
}

tree
edit_typeset_rep::exec (tree t, const evaluation_environment_checkpoint& context,
                       hashmap<string,tree> overlay, bool expand_refs) {
  temporary_environment_scope execution (*env.operator-> ());
  context.restore (*env.operator-> ());
  env->patch_env (overlay);
  env->update ();
  t= env->exec (t);
  if (expand_refs)
    t= expand_references (t, buf->data->ref);
  t= simplify_execed (t);
  t= simplify_correct (t);
  return t;
}

tree
edit_typeset_rep::exec_texmacs (tree t, path p) {
  const auto context= conversion_context_at (p);
  return exec (t, *context, hashmap<string,tree> (UNINIT));
}

tree
edit_typeset_rep::exec_texmacs (tree t) {
  return exec_texmacs (t, rp * 0);
}

tree
edit_typeset_rep::exec_verbatim (tree t, path p) {
  t= convert_OTS1_symbols_to_universal_encoding (t);
  const auto context= conversion_context_at (p);
  hashmap<string,tree> H (UNINIT);
  H ("TeXmacs")= tree (MACRO, "TeXmacs");
  H ("LaTeX")= tree (MACRO, "LaTeX");
  H ("TeX")= tree (MACRO, "TeX");
  return exec (t, *context, H);
}

tree
edit_typeset_rep::exec_verbatim (tree t) {
  return exec_verbatim (t, rp * 0);
}

struct editor_math_statistics {
  std::shared_ptr<const evaluation_environment_checkpoint> base;
  athena::contracts::contract_event_log trace;
  math_statistics buffer, cursor;
  path cursor_source;
};

athena::contracts::source_environment_preparation
editor_rep::correction_environments (
    tree source, std::shared_ptr<const evaluation_environment_checkpoint> entry) {
  return [this, source, entry= std::move (entry)] (path relative)
      -> std::shared_ptr<const evaluation_environment_checkpoint> {
    return prepare_tree_position (source, relative * 0, *entry);
  };
}

tree
editor_rep::correct_with (tree source) {
  const auto entry= prepare_tree_entry (source);
  return with_correct (source, athena::contracts::source_contracts (
    correction_environments (source, entry)));
}

tree
editor_rep::correct_superfluous_with (tree source) {
  const auto entry= prepare_tree_entry (source);
  return superfluous_with_correct (source, correction_environments (source, entry));
}

tree
editor_rep::correct_math (tree source) {
  const auto entry= prepare_tree_entry (source);
  return manual_correct (source, [this, entry] (tree stage) {
    return correction_environments (stage, entry);
  }, as_string (entry->effective_value (MODE)));
}

tree
editor_rep::correct_superfluous_invisible (tree source) {
  const auto entry= prepare_tree_entry (source);
  return superfluous_invisible_correct (source, [this, entry] (tree stage) {
    return correction_environments (stage, entry);
  });
}

tree
editor_rep::correct_missing_invisible (tree source, int force) {
  const auto entry= prepare_tree_entry (source);
  return missing_invisible_correct (source, force, [this, entry] (tree stage) {
    return correction_environments (stage, entry);
  });
}

void
edit_typeset_rep::update_math_statistics () {
  if (!ttt->execution_base) {
    math_statistics_cache.reset ();
    return;
  }
  const auto contexts= [this] (path relative)
      -> std::shared_ptr<const athena::contracts::contract_scope> {
    const auto entry= editor_rep::cursor_context_at (rp * relative * 0);
    return entry ? std::shared_ptr<const athena::contracts::contract_scope> (
      entry, &entry->contract_context ()) : nullptr;
  };
  const tree root_mode= ttt->execution_base->effective_value (MODE);
  const string mode= is_atomic (root_mode) ? root_mode->label : string ("text");
  if (!math_statistics_cache || math_statistics_cache->base != ttt->execution_base ||
      math_statistics_cache->trace != ttt->br->contract_changes) {
    auto state= std::make_shared<editor_math_statistics> ();
    state->base= ttt->execution_base;
    state->trace= ttt->br->contract_changes;
    state->buffer.compile (subtree (et, rp), mode, contexts);
    math_statistics_cache= std::move (state);
  }
  auto& state= *math_statistics_cache;
  path cursor_source;
  if (rp < tp && is_compound (subtree (et, rp))) {
    const int child= (tp / rp)->item;
    if (child >= 0 && child < N(subtree (et, rp))) cursor_source= rp * child;
  }
  if (state.cursor_source == cursor_source) return;
  state.cursor_source= cursor_source;
  state.cursor= math_statistics ();
  if (!is_nil (cursor_source))
    state.cursor.compile (subtree (et, cursor_source), mode, contexts, cursor_source / rp);
}

int
edit_typeset_rep::math_statistics_occurrences (string id, tree source) {
  update_math_statistics ();
  if (!math_statistics_cache) return 0;
  if (id == "buffer") return math_statistics_cache->buffer.number_occurrences (source);
  if (id == "cursor") return math_statistics_cache->cursor.number_occurrences (source);
  return 0;
}

int
edit_typeset_rep::math_statistics_role (string id, tree source) {
  update_math_statistics ();
  if (!math_statistics_cache) return 0;
  if (id == "buffer") return math_statistics_cache->buffer.number_in_role (source);
  if (id == "cursor") return math_statistics_cache->cursor.number_in_role (source);
  return 0;
}

tree
editor_rep::compress_tree (tree source) {
  if (!compression) compression= std::make_shared<tree_compression> ();
  const path ip= obtain_ip (source);
  const bool attached= ip_attached (ip);
  const path base= attached ? reverse (ip) : path ();
  const auto context= prepare_tree_entry (source);
  std::shared_ptr<const evaluation_environment_checkpoint> document_context;
  return compression->compress (source, [&] (path relative)
      -> std::shared_ptr<const athena::contracts::contract_scope> {
    const auto entry= attached ?
      prepare_source_position (base * relative * 0, document_context) :
      prepare_tree_position (source, relative * 0, *context);
    return entry ? std::shared_ptr<const athena::contracts::contract_scope> (
      entry, &entry->contract_context ()) : nullptr;
  });
}

tree
editor_rep::decompress_tree (tree source) {
  if (!compression) compression= std::make_shared<tree_compression> ();
  return compression->decompress (source);
}

string
editor_rep::compress_html (tree source, int mode) {
  const tree value= compress_tree (source);
  return compression->html (value, mode);
}

tree
editor_rep::decompress_html (string source, int mode) {
  if (!compression) compression= std::make_shared<tree_compression> ();
  return compression->from_html (source, mode);
}

static tree
search_doc_title (tree t) {
  if (is_atomic (t)) return "";
  else if (is_compound (t, "doc-title", 1)) return t[0];
  else {
    for (int i=0; i<N(t); i++) {
      tree r= search_doc_title (t[i]);
      if (r != "") return r;
    }
    return "";
  }
}

tree
edit_typeset_rep::exec_html (tree t, path p) {
  if (buf->storage_version != athena::document::xml_storage_version::v3)
    t= convert_OTS1_symbols_to_universal_encoding (t);
  else t= copy (t);
  const auto context= conversion_context_at (p);
  hashmap<string,tree> H= context->variables ();
  tree patch= html_export_environment_patch ();
  hashmap<string,tree> P (UNINIT, patch);
  H->join (P);
  prefix_specific (H, "tmhtml-");
  // Native enunciation counters must advance during HTML expansion, but their
  // executed ASSIGN trees are effects, not visible HTML source nodes.
  H ("html-export")= tree ("true");
  tree w (WITH);
  tree doc_title= search_doc_title (t);
  if (doc_title != "")
    w << string ("html-doc-title") << doc_title;
  if (H->contains ("html-title"))
    w << string ("html-title") << H["html-title"];
  if (H->contains ("html-css"))
    w << string ("html-css") << H["html-css"];
  if (H->contains ("html-head-javascript"))
    w << string ("html-head-javascript") << H["html-head-javascript"];
  if (H->contains ("html-head-javascript-src"))
    w << string ("html-head-javascript-src") << H["html-head-javascript-src"];
  if (H->contains ("html-head-favicon"))
    w << string ("html-head-favicon") << H["html-head-favicon"];
  if (H->contains ("html-extra-css"))
    w << string ("html-extra-css") << H["html-extra-css"];
  if (H->contains ("html-extra-javascript-src"))
    w << string ("html-extra-javascript-src")
      << H["html-extra-javascript-src"];
  if (H->contains ("html-extra-javascript"))
    w << string ("html-extra-javascript") << H["html-extra-javascript"];
  if (H->contains ("html-site-version"))
    w << string ("html-site-version") << H["html-site-version"];
  if (N(w) == 0) return exec (t, *context, H);
  else {
    w << t;
    return exec (w, *context, H);
  }
}

tree
edit_typeset_rep::exec_html (tree t) {
  return exec_html (t, rp * 0);
}

static tree
value_to_compound (tree t, hashmap<string,tree> h) {
  if (is_atomic (t)) return t;
  else if (is_func (t, VALUE, 1) &&
	   is_atomic (t[0]) &&
	   h->contains (t[0]->label))
    return compound (t[0]->label);
  else {
    int i, n= N(t);
    tree r (t, n);
    for (i=0; i<n; i++)
      r[i]= value_to_compound (t[i], h);
    return r;
  }
}

tree
edit_typeset_rep::exec_latex (tree t, path p) {
  t= convert_OTS1_symbols_to_universal_encoding (t);
  bool expand_unknown_macros= "on" == as_string (
      call ("get-preference", "texmacs->latex:expand-macros"));
  bool expand_user_macro= "on" == as_string (
      call ("get-preference", "texmacs->latex:expand-user-macros"));
  if (!expand_unknown_macros && !expand_user_macro)
    return t;
  const auto context= conversion_context_at (p);
  hashmap<string,tree> H= context->variables ();
  object l= null_object ();
  iterator<string> it= iterate (H);
  while (it->busy ()) l= cons (object (it->next ()), l);
  tree patch= scheme_tree_to_tree (latex_compat_tmtex_env_patch (t, l));
  hashmap<string,tree> P (UNINIT, patch);
  H->join (P);
  prefix_specific (H, "tmlatex-");

  if (!expand_user_macro &&
      is_document (t) && is_compound (t[0], "hide-preamble")) {
    tree r= copy (t);
    r[0]= "";
    r= exec (value_to_compound (r, P), *context, H, false);
    r[0]= exec (t[0], *context, H, false);
    return r;
  }
  else {
    tree r= exec (value_to_compound (t, P), *context, H, false);
    return r;
  }
}

tree
edit_typeset_rep::exec_latex (tree t) {
  return exec_latex (t, rp * 0);
}

tree
edit_typeset_rep::texmacs_exec (tree t) {
  return ::texmacs_exec (env, t);
}

tree
edit_typeset_rep::var_texmacs_exec (tree t) {
  const auto context= editor_rep::cursor_context_at (tp);
  if (!context) throw std::runtime_error ("Evaluation context is awaiting preparation");
  temporary_environment_scope execution (*env.operator-> ());
  context->restore (*env.operator-> ());
  env->style_init_env ();
  env->update ();
  return texmacs_exec (t);
}

/******************************************************************************
* Initialization
******************************************************************************/

void
edit_typeset_rep::change_style (tree t) {
  bool changed= (the_style != t);
  the_style= copy (t);
  if (changed) {
    require_save ();
    notify_change (THE_ENVIRONMENT);
    init_update ();
  }
}

void
edit_typeset_rep::init_style () {
  notify_change (THE_ENVIRONMENT);
  init_update ();
}

void
edit_typeset_rep::init_style (string name) {
  if ((name == "none") || (name == "") || (name == "style")) the_style= TUPLE;
  else if (arity (the_style) == 0) the_style= tree (TUPLE, name);
  else the_style= tree (TUPLE, name) * the_style (1, N(the_style));
  require_save ();
  notify_change (THE_ENVIRONMENT);
  init_update ();
}

tree
edit_typeset_rep::get_init_all () {
  return (tree) init;
}

void
edit_typeset_rep::init_env (string var, tree by) {
  if (init (var) == by) return;
  init (var)= by;
  prepare_initial_background ();
  if (var == "full-screen-mode") return;
  if (var != PAGE_SCREEN_WIDTH &&
      var != PAGE_SCREEN_HEIGHT &&
      var != ZOOM_FACTOR)
    require_save ();
  notify_change (THE_ENVIRONMENT);
}

void
edit_typeset_rep::init_default (string var) {
  if (!init->contains (var)) return;
  init->reset (var);
  notify_change (THE_ENVIRONMENT);
  init_update ();
}

/******************************************************************************
* Actual typesetting
******************************************************************************/

static int
heading_level (tree t) {
  return athena_heading_level (t);
}

static bool
heading_fold_container (tree t) {
  return is_func (t, DOCUMENT) ||
         is_func (t, CONCAT) ||
         is_compound (t, "ignore") ||
         is_compound (t, "show-part") ||
         is_compound (t, "hide-part");
}

static void
heading_collect_paths (tree t, path p, array<path>& paths,
                       bool include_toc= false) {
  if (is_atomic (t)) return;
  if (toc_fold_node (t)) {
    if (include_toc) paths << p;
    return;
  }
  if (heading_level (t) != 0) {
    paths << p;
    return;
  }
  for (int i=0; i<N(t); i++)
    heading_collect_paths (t[i], p * i, paths, include_toc);
}

static bool
heading_path_starts_with (path p, path prefix) {
  if (is_nil (prefix)) return true;
  if (is_nil (p)) return false;
  return p->item == prefix->item &&
         heading_path_starts_with (p->next, prefix->next);
}

static bool
heading_path_before_or_contains (path heading, path cursor) {
  if (is_nil (heading)) return true;
  if (is_nil (cursor)) return false;
  if (heading->item < cursor->item) return true;
  if (heading->item > cursor->item) return false;
  return heading_path_before_or_contains (heading->next, cursor->next);
}

static bool
heading_find_for_cursor (tree doc, path cursor, path& heading) {
  for (path p= cursor; !is_nil (p); p= path_up (p))
    if (has_subtree (doc, p) && heading_level (subtree (doc, p)) != 0) {
      heading= p;
      return true;
    }

  array<path> headings;
  heading_collect_paths (doc, path (), headings);

  bool found= false;
  for (int i=0; i<N(headings); i++)
    if (heading_path_before_or_contains (headings[i], cursor)) {
      heading= headings[i];
      found= true;
    }
    else break;

  return found && has_subtree (doc, heading) &&
         heading_level (subtree (doc, heading)) != 0;
}

static tree
heading_fold_hidden (tree source) {
  return compound ("folded-hidden", source);
}

static tree
heading_fold_screen_tree (tree t, path p, hashset<string> folded,
                          hashset<string> folded_tocs,
                          hashset<string> unfolded_tocs,
                          bool fold_tocs_by_default,
                          hashmap<path,tree>* source_pairs= nullptr) {
  if (is_atomic (t)) return t;
  if (toc_fold_node (t)) {
    string key= as_string (p);
    tree_label label= L(t);
    if (folded_tocs->contains (key) ||
        (fold_tocs_by_default && !unfolded_tocs->contains (key)))
      label= make_tree_label (is_compound (t, "table-of-contents*")
                               ? "screen-folded-table-of-contents*"
                               : "screen-folded-table-of-contents");
    else
      label= make_tree_label (is_compound (t, "table-of-contents*")
                               ? "screen-unfolded-table-of-contents*"
                               : "screen-unfolded-table-of-contents");
    if (label != L(t)) {
      tree r (label, N(t));
      for (int i=0; i<N(t); i++) r[i]= t[i];
      if (source_pairs) (*source_pairs)(p)= tuple (t, r);
      return r;
    }
    return t;
  }
  if (!heading_fold_container (t)) return t;

  tree r (t, N(t));
  bool changed= false;
  int folded_level= 0;
  for (int i=0; i<N(t); i++) {
    path cp= p * i;
    int level= heading_level (t[i]);
    bool folded_here= level != 0 && folded->contains (as_string (cp));

    if (folded_level != 0) {
      if (level != 0 && level <= folded_level) folded_level= 0;
      else {
        r[i]= heading_fold_hidden (t[i]);
        changed= true;
        continue;
      }
    }

    r[i]= heading_fold_screen_tree (t[i], cp, folded,
                                    folded_tocs, unfolded_tocs,
                                    fold_tocs_by_default, source_pairs);
    changed |= !strong_equal (r[i], t[i]);
    if (folded_here)
      folded_level= level;
  }
  if (!changed) return t;
  if (source_pairs) (*source_pairs)(p)= tuple (t, r);
  return r;
}

tree
edit_typeset_rep::folded_screen_tree () {
  tree doc= subtree (et, rp);
  bool fold_tocs_by_default=
    env->get_string (PAGE_MEDIUM) == "automatic" &&
    get_preference ("fold table of contents in reflow", "on") == "on";
  return heading_fold_screen_tree (doc, path (), folded_headings,
                                   folded_tocs, unfolded_tocs,
                                   fold_tocs_by_default);
}

bool
edit_typeset_rep::heading_fold_toggle () {
  return heading_fold_set_current (false, true);
}

bool
edit_typeset_rep::heading_fold_current () {
  return heading_fold_set_current (true, false);
}

bool
edit_typeset_rep::heading_unfold_current () {
  return heading_fold_set_current (false, false);
}

bool
edit_typeset_rep::heading_fold_toggle_at (string p) {
  path hp= as_path (p);
  tree doc= subtree (et, rp);
  if (has_subtree (doc, hp) && toc_fold_node (subtree (doc, hp))) {
    bool folded= folded_tocs->contains (p) ||
      (!unfolded_tocs->contains (p) &&
       get_init_string (PAGE_MEDIUM) == "automatic" &&
       get_preference ("fold table of contents in reflow", "on") == "on");
    return toc_fold_set_at (rp * hp, !folded);
  }
  if (!has_subtree (doc, hp) || heading_level (subtree (doc, hp)) == 0) {
    set_message ("No heading at path", p);
    return false;
  }

  string key= as_string (hp);
  if (folded_headings->contains (key)) {
    folded_headings->remove (key);
    set_message ("Heading unfolded", "");
  }
  else {
    folded_headings->insert (key);
    set_message ("Heading folded", "");
  }
  fold_view_rebuild= true;
  typeset_invalidate_all ();
  invalidate_all ();
  return true;
}

void
edit_typeset_rep::heading_unfold_all () {
  folded_headings= hashset<string> ();
  fold_view_rebuild= true;
  typeset_invalidate_all ();
  invalidate_all ();
}

array<heading_cell_range>
edit_typeset_rep::heading_cell_ranges () {
  array<heading_cell_range> ranges;
  tree doc= subtree (et, rp);
  array<path> headings;
  heading_collect_paths (doc, path (), headings, true);
  int minimum_level= INT_MAX;
  for (int i=0; i<N(headings); i++) {
    int level= heading_level (subtree (doc, headings[i]));
    if (level > 0) minimum_level= min (minimum_level, level);
  }
  if (minimum_level == INT_MAX) minimum_level= 1;

  int hidden_level= 0;
  for (int i=0; i<N(headings); i++) {
    path hp= headings[i];
    if (toc_fold_node (subtree (doc, hp))) {
      if (hidden_level != 0) continue;
      string key= as_string (hp);
      heading_cell_range range;
      range.level= minimum_level;
      range.heading_path= hp;
      range.selection_start= rp * hp * 0;
      range.selection_end= rp * hp * 1;
      range.heading_end= range.selection_end;
      range.visual_start= range.selection_start;
      range.visual_end= range.selection_end;
      range.folded= folded_tocs->contains (key) ||
        (!unfolded_tocs->contains (key) &&
         get_init_string (PAGE_MEDIUM) == "automatic" &&
         get_preference ("fold table of contents in reflow", "on") == "on");
      range.table_of_contents= true;
      ranges << range;
      continue;
    }
    int level= heading_level (subtree (doc, hp));
    if (hidden_level != 0) {
      if (level <= hidden_level) hidden_level= 0;
      else continue;
    }

    bool folded_here= folded_headings->contains (as_string (hp));
    int next= i + 1;
    while (next < N(headings) &&
           (toc_fold_node (subtree (doc, headings[next])) ||
            heading_level (subtree (doc, headings[next])) > level))
      next++;

    path absolute= rp * hp;
    // The section owns complete source nodes, including its heading. These
    // ranges do not require an editable field in an offscreen next heading.
    path logical_end= next < N(headings)
      ? rp * headings[next] * 0
      : rp * right_index (doc);

    heading_cell_range range;
    range.level          = level;
    range.heading_path   = hp;
    range.selection_start= absolute * 0;
    range.selection_end  = logical_end;
    range.heading_end    = absolute * 1;
    range.visual_start   = range.selection_start;
    range.visual_end     = folded_here ? range.heading_end : logical_end;
    range.folded         = folded_here;
    ranges << range;

    if (folded_here) hidden_level= level;
  }
  return ranges;
}

bool
edit_typeset_rep::heading_fold_set_current (bool folded, bool toggle) {
  tree doc= subtree (et, rp);
  if (!heading_path_starts_with (tp, rp)) {
    set_message ("No heading at cursor", "");
    return false;
  }

  path hp;
  if (!heading_find_for_cursor (doc, tp / rp, hp)) {
    set_message ("No heading at cursor", "");
    return false;
  }

  string key= as_string (hp);
  bool was_folded= folded_headings->contains (key);
  bool now_folded= toggle? !was_folded: folded;
  if (now_folded) folded_headings->insert (key);
  else folded_headings->remove (key);
  fold_view_rebuild= true;
  typeset_invalidate_all ();
  invalidate_all ();
  set_message (now_folded? "Heading folded": "Heading unfolded", "");
  return true;
}

static bool
toc_fold_find_for_path (tree doc, path p, path& toc) {
  for (path q= p; !is_nil (q); q= path_up (q))
    if (has_subtree (doc, q) && toc_fold_node (subtree (doc, q))) {
      toc= q;
      return true;
    }
  if (!toc_fold_node (doc)) return false;
  toc= path ();
  return true;
}

bool
edit_typeset_rep::toc_fold_set_at (path p, bool folded) {
  tree doc= subtree (et, rp);
  if (!heading_path_starts_with (p, rp)) return false;
  path toc;
  if (!toc_fold_find_for_path (doc, p / rp, toc)) return false;
  string key= as_string (toc);
  if (folded) {
    folded_tocs->insert (key);
    unfolded_tocs->remove (key);
  }
  else {
    unfolded_tocs->insert (key);
    folded_tocs->remove (key);
  }
  notify_change (THE_FREEZE);
  fold_view_rebuild= true;
  typeset_invalidate_all ();
  invalidate_all ();
  set_message (folded? "Table of contents folded":
                        "Table of contents unfolded", "");
  return true;
}

int
edit_typeset_rep::heading_word_count_at (path p) {
  if (!has_subtree (et, rp)) return 0;
  tree doc= subtree (et, rp);

  if (!heading_path_starts_with (p, rp)) return 0;
  path hp;
  if (!heading_find_for_cursor (doc, p / rp, hp)) return 0;
  hp= rp * hp;

  int h= hash (doc);
  if (h != heading_word_count_cache_hash) {
    heading_word_count_cache= athena_heading_word_count_entries (doc, rp);
    heading_word_count_cache_map= hashmap<path,int> (0);
    for (int i=0; i<N(heading_word_count_cache); i++)
      heading_word_count_cache_map (
        heading_word_count_cache[i].tree_path)=
          heading_word_count_cache[i].words;
    heading_word_count_cache_hash= h;
  }

  return heading_word_count_cache_map[hp];
}

void
edit_typeset_rep::typeset_sub (SI& x1, SI& y1, SI& x2, SI& y2) {
  //time_t t1= texmacs_time ();
  typeset_prepare ();
  box previous_eb= eb;
  eb= empty_box (reverse (rp));
  // saves memory, also necessary for change_log update
  bench_start ("typeset");
#ifdef USE_EXCEPTIONS
  try {
#endif
    bool printed= env->get_string (PAGE_PRINTED) == "true";
    ttt->progressive= !printed &&
      env->get_string (PAGE_MEDIUM) != "paper";
    path relative_cursor= rp <= tp? tp / rp: path ();
    ttt->progressive_required=
      is_nil (relative_cursor)? 0: max (0, relative_cursor->item);
    bool progressive_was_pending= progressive_typeset_pending;
    ttt->progressive_advance=
      !progressive_was_pending || progressive_typeset_continue;
    ttt->progressive_budget_ms= progressive_was_pending? 12: 0;
    if (progressive_typeset_continue) {
      ttt->br->status= CORRUPTED;
      progressive_typeset_continue= false;
    }
    bool folded_screen= !printed &&
      (fold_view_has_toc || N(folded_headings) != 0 ||
       N(folded_tocs) != 0 || N(unfolded_tocs) != 0);
    bool full_repaint= fold_view_rebuild ||
                       folded_screen != fold_view_active;
    if (full_repaint) {
      tree doc= subtree (et, rp);
      folded_source_pairs= hashmap<path,tree> ();
      if (folded_screen)
        doc= heading_fold_screen_tree (doc, path (), folded_headings,
                                      folded_tocs, unfolded_tocs,
                                      env->get_string (PAGE_MEDIUM) == "automatic" &&
                                      get_preference (
                                        "fold table of contents in reflow",
                                        "on") == "on", &folded_source_pairs);
      ttt->screen_tree= folded_screen;
      ::notify_assign (ttt, path (), doc);
      fold_view_rebuild= false;
      fold_view_active= folded_screen;
    }
    prooftrack_screen_scope prooftrack_screen (!printed);
    eb= ::typeset (ttt, x1, y1, x2, y2);
    prepare_page_geometry ();
    page_geometry_from_layout= true;
    progressive_typeset_pending=
      ttt->progressive_root_active && ttt->progressive_pending;
    if (full_repaint) {
      SI big= (SI) (1 << 30);
      x1= -big; y1= -big; x2= big; y2= big;
    }
#ifdef USE_EXCEPTIONS
  }
  catch (string msg) {
    the_exception= msg;
    std_error << "Typesetting failure; preserving source document\n";
    ttt->screen_tree= false;
    fold_view_active= false;
    folded_source_pairs= hashmap<path,tree> ();
    progressive_typeset_pending= false;
    progressive_typeset_continue= false;
    // The failed pass may have left bridge-local state half updated.  Rebind it
    // to the real source tree, but never mutate the editor document as an error
    // recovery mechanism.  Keep the last successful layout when available.
    ::notify_assign (ttt, path(), subtree (et, rp));
    if (!is_nil (previous_eb)) eb= previous_eb;
    SI big= (SI) (1 << 30);
    x1= -big; y1= -big; x2= big; y2= big;
  }
  handle_exceptions ();
#endif
  bench_end ("typeset");
  //time_t t2= texmacs_time ();
  //if (t2 - t1 >= 10) cout << "typeset took " << t2-t1 << "ms\n";
  picture_cache_clean ();
}

static void
report_missing (hashmap<string,tree> missing) {
  array<string> a;
  for (iterator<string> it= iterate (missing); it->busy(); a << it->next ()) {}
  merge_sort (a);
  for (int i=0; i<N(a); i++)
    if (!starts (a[i], "bib-"))
      typeset_warning << "Undefined reference " << a[i] << LF;
}

static void
report_redefined (array<tree> redefined) {
  for (int i=0; i<N(redefined); i++) {
    tree t= redefined[i];
    if (t[1]->label == "")
      typeset_warning << "Redefined " << t[0]->label << LF;
    else
      typeset_warning << "Redefined " << t[0]->label
                      << " as " << t[1]->label << LF;
  }
}

static void
clean_unused (hashmap<string,tree>& refs, hashmap<string,bool> used) {
  array<string> a;
  for (iterator<string> it= iterate (refs); it->busy(); ) {
    string key= it->next ();
    if (!used->contains (key)) a << key;
  }
  for (int i=0; i<N(a); i++)
    refs->reset (a[i]);
}

void
edit_typeset_rep::typeset (SI& x1, SI& y1, SI& x2, SI& y2) {
  int missing_nr= INT_MAX;
  int redefined_nr= INT_MAX;
  int page_ref_updates= 0;
  x1= MAX_SI; y1= MAX_SI; x2= MIN_SI; y2= MIN_SI;
  while (true) {
    SI sx1, sy1, sx2, sy2;
    typeset_sub (sx1, sy1, sx2, sy2);
    bool page_refs_changed= env->page_refs_changed;
    x1= min (x1, sx1); y1= min (y1, sy1);
    x2= max (x2, sx2); y2= max (y2, sy2);
    if (!env->complete) break;
    env->complete= false;
    clean_unused (env->local_ref, env->touched);
    if (N(env->missing) == 0 && N(env->redefined) == 0 &&
        !page_refs_changed)
      break;
    if (page_refs_changed) {
      page_ref_updates++;
      if (page_ref_updates > 5) {
        typeset_warning << "Page references did not stabilize" << LF;
        break;
      }
    }
    if (!page_refs_changed &&
        ((N(env->missing) == missing_nr && N(env->redefined) == redefined_nr) ||
         (N(env->missing) > missing_nr || N(env->redefined) > redefined_nr))) {
      report_missing (env->missing);
      report_redefined (env->redefined);
      break;
    }
    missing_nr= N(env->missing);
    redefined_nr= N(env->redefined);
    ttt->execution_base.reset ();
    ttt->br->notify_assign (path (), ttt->br->st);
  }
}

void
edit_typeset_rep::typeset_forced () {
  //cout << "Typeset forced\n";
  SI x1, y1, x2, y2;
  typeset (x1, y1, x2, y2);
  prepare_cursor_environment ();
}

void
edit_typeset_rep::typeset_invalidate (path p) {
  if (rp <= p) {
    //cout << "Invalidate " << p << "\n";
    if (!fold_view_has_toc && toc_fold_contains (subtree (et, p)))
      fold_view_has_toc= true;
    if (fold_view_active || fold_view_has_toc) fold_view_rebuild= true;
    notify_change (THE_TREE);
    ::notify_assign (ttt, p / rp, subtree (et, p));
  }
}

void
edit_typeset_rep::typeset_invalidate_presentation (path p) {
  if (!(rp <= p)) return;
  path q= p / rp;
  // Interaction decorations change layout, not the source or its folded view.
  // Refresh the displayed subtree without discarding unrelated bridge caches.
  if (!has_subtree (ttt->br->st, q)) return;
  ::notify_assign (ttt, q, subtree (ttt->br->st, q));
  notify_change (THE_TREE);
}

bool edit_typeset_rep::set_enunciation_editing_id (const std::string& id) {
  if (env->editing_enunciation_id == id) return false;
  env->editing_enunciation_id= id;
  return true;
}

void
edit_typeset_rep::typeset_invalidate_all () {
  //cout << "Invalidate all\n";
  heading_word_count_cache_hash= INT_MIN;
  heading_word_count_cache_map= hashmap<path,int> (0);
  if (!fold_view_has_toc)
    fold_view_has_toc= toc_fold_contains (subtree (et, rp));
  if (fold_view_active || fold_view_has_toc || N(folded_headings) != 0 ||
      N(folded_tocs) != 0 || N(unfolded_tocs) != 0)
    fold_view_rebuild= true;
  notify_change (THE_ENVIRONMENT);
  typeset_preamble ();
  ttt->br->notify_assign (path (), subtree (et, rp));
}

void
edit_typeset_rep::typeset_refresh_radioactive_links () {
  if (refresh_radioactive_links (ttt)) notify_change (THE_TREE);
}

SI
edit_typeset_rep::typeset_enunciation_slot_width (tree content) {
  // Match unhyphenated table cells: measure the native inline box without
  // constraining it to the previous viewport width.
  temporary_environment_scope measurement (*env.operator-> ());
  typeset_prepare ();
  env->patch_env (init);
  env->update ();
  box natural= typeset_as_concat (env, content, path (DECORATION));
  return max (natural->w (), env->as_length ("1.5fn")) +
    env->as_length ("0.7fn") + env->as_length ("2px");
}
