
/******************************************************************************
* MODULE     : edit_dynamic.cpp
* DESCRIPTION: editing dynamic content
* COPYRIGHT  : (C) 1999  Joris van der Hoeven, 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "edit_dynamic.hpp"
#include "Typeset/Env/local_environment.hpp"
#include "tree_analyze.hpp"
#include "cursor_navigation.hpp"
#include "ATHENA/Data/enunciation_model.hpp"
#include "format_commands.hpp"
#include "native_dynamic_commands.hpp"
#include "native_latex_commands.hpp"
#include "native_interfaces.hpp"
#include "buffer_state.hpp"
#include "buffer_actor.hpp"
#include "node_metadata.hpp"
#include "Data/NodeContracts/node_groups.hpp"
#include "Data/NodeContracts/node_group_scope.hpp"
#include "Data/NodeContracts/editing_query.hpp"
#include "Data/NodeContracts/builtin_contracts.hpp"

#include <algorithm>

/******************************************************************************
* Constructors and destructors
******************************************************************************/

edit_dynamic_rep::edit_dynamic_rep () {}
edit_dynamic_rep::~edit_dynamic_rep () {}

/******************************************************************************
* Subroutines for inactive content
******************************************************************************/

namespace {
bool source_editing (const evaluation_environment_checkpoint& context) {
  return context.effective_value (MODE) == "src" ||
         context.effective_value (PREAMBLE) == "true";
}

struct source_edit_scope {
  std::optional<bool>& selected;
  std::optional<bool> previous;
  source_edit_scope (std::optional<bool>& selected, bool source):
    selected (selected), previous (selected) { selected= source; }
  ~source_edit_scope () { selected= previous; }
};

struct macro_edit_scope {
  std::shared_ptr<const evaluation_environment_checkpoint>& selected;
  std::shared_ptr<const evaluation_environment_checkpoint> previous;
  macro_edit_scope (std::shared_ptr<const evaluation_environment_checkpoint>& selected,
                    std::shared_ptr<const evaluation_environment_checkpoint> context):
    selected (selected), previous (selected) { selected= std::move (context); }
  ~macro_edit_scope () { selected= std::move (previous); }
};

}

std::shared_ptr<const evaluation_environment_checkpoint>
edit_dynamic_rep::macro_invocation_context () {
  if (macro_environment) return macro_environment;
  path input= search_upwards (HYBRID);
  if (is_nil (input)) input= search_upwards (LATEX);
  if (is_nil (input)) return editor_rep::cursor_context_at (tp);
  if (is_func (subtree (et, path_up (input)), INACTIVE)) input= path_up (input);
  // Read the invocation site, outside the name field's display-only text style.
  return editor_rep::cursor_context_at (input * 0);
}

bool
edit_dynamic_rep::in_source () {
  if (source_mode_during_edit) return *source_mode_during_edit;
  const auto context= macro_environment ? macro_environment :
    editor_rep::cursor_context_at (tp);
  if (context) return source_editing (*context);
  return get_env_string (MODE) == "src";
}

path
edit_dynamic_rep::find_dynamic (path p) {
  path parent= path_up (p);
  if (!(rp < parent)) return path ();
  tree source= subtree (et, parent);
  const auto prepared= prepare_source_position (parent * 0);
  const athena::contracts::editing_query query {prepared->contract_context ()};
  if (L(source) >= START_EXTENSIONS ||
      query.variable_arity (source))
    return p;
  return find_dynamic (parent);
}

/******************************************************************************
* Making general compound objects
******************************************************************************/

static bool
is_multi_paragraph_macro (tree t, bool pure,
                          const evaluation_environment_checkpoint& context) {
  int n= arity (t);
  if (is_document (t) || is_func (t, PARA) || is_func (t, SURROUND))
    return true;
  if (is_func (t, MACRO) || is_func (t, WITH) ||
      is_func (t, LOCUS) ||
      is_func (t, CANVAS) || is_func (t, ORNAMENT) || is_func (t, ART_BOX))
    return is_multi_paragraph_macro (t [n-1], pure, context);
  if (is_extension (t) &&
      !is_compound (t, "footnote") &&
      !is_compound (t, "footnote-anchor") &&
      !is_compound (t, "note-footnote") &&
      !is_compound (t, "note-footnote*") &&
      !is_compound (t, "folded-comment")) {
    int i;
    for (i=1; i<n; i++)
      if (is_multi_paragraph_macro (t[i], pure, context))
        return true;
    tree f= context.effective_value (as_string (L(t)));
    return is_multi_paragraph_macro (f, pure, context);
  }
  if (!pure)
    if (is_func (t, ARG))
      return true;
  return false;
}

static bool
contains_table_format (tree t, tree var) {
  if (is_atomic (t)) return false;
  else {
    int i, n= N(t);
    for (i=0; i<n; i++)
      if (contains_table_format (t[i], var))
        return true;
    return is_func (t, TFORMAT) && (t[N(t)-1] == tree (ARG, var));
  }
}

void
edit_dynamic_rep::make_compound (tree_label l, int n= -1) {
  (void) make_compound (l, n, path ());
}

bool
edit_dynamic_rep::make_compound (tree_label l, int n,
    const std::shared_ptr<const evaluation_environment_checkpoint>& context) {
  macro_edit_scope invocation (macro_environment, context);
  return make_compound (l, n, path ());
}

bool
edit_dynamic_rep::make_compound (tree_label l, int n, path replace_input) {
  const auto context= macro_invocation_context ();
  if (!context) {
    set_message ("Macro insertion is awaiting layout", "Macro insertion");
    return false;
  }
  macro_edit_scope invocation (macro_environment, context);
  const athena::contracts::editing_query query {context->contract_context ()};
  const auto consume_input= [&] {
    if (!is_nil (replace_input)) {
      assign (replace_input, "");
      correct (path_up (replace_input));
    }
  };
  const string tag= as_string (l);
  const auto signature= source_signature (tag, *context);
  if (signature && signature->kind != athena::contracts::definition_kind::value) {
    // Source authoring constructs syntax, not an evaluated macro/table field.
    if (n < 0) n= signature->kind == athena::contracts::definition_kind::macro ?
      int (signature->parameters.size ()) : 1;
    tree result (l, n);
    consume_input ();
    tree selected= "";
    if (n > 0 && selection_active_small ()) selected= selection_get_cut ();
    insert_tree (result, n == 0 ? path (1) : path (0, 0));
    if (selected != "") {
      const cursor_navigation source ({athena::contracts::builtin_contracts (),
        athena::contracts::access_policy::source});
      insert_tree (selected, source.end (selected));
    }
    return true;
  }
  const auto* declaration= athena::enunciation::standard_registry ().legacy (
    std::string (tag.data (), N(tag)));
  if (declaration && !in_source () && !inside ("show-preamble")) {
    const int arity= declaration->layout == athena::enunciation::legacy_layout::body ? 1 : 2;
    if (n == -1 || n == arity) {
      tree prototype (l, arity);
      prototype[arity-1]= tree (DOCUMENT, "");
      athena::enunciation::conversion_options options;
      if (!declaration->numbering_preference.empty ())
        options.numbering_preferences[declaration->numbering_preference]=
          get_preference (string (declaration->numbering_preference.c_str ())) == "on";
      auto converted= athena::enunciation::convert_detached_source (prototype, options);
      ASSERT (converted.diagnostics.empty () && converted.converted == 1,
              "Could not construct canonical enunciation");
      consume_input ();
      tree selected= "";
      if (selection_active_normal ()) selected= selection_get_cut ();
      insert_tree (converted.source, path (0, 0, 0));
      if (selected != "") {
        const auto field= prepare_source_position (tp);
        insert_tree (selected, prepare_tree_boundary (selected, path (), false, *field));
      }
      return true;
    }
  }
  eval ("(use-modules (generic generic-edit))");
  const tree definition= context->effective_value (tag);
  if (n == -1 && is_func (definition, MACRO) &&
      query.describe (l).layout_origin == athena::contracts::property_authority::inferred)
    n= N(definition)-1;
  if (n == -1) {
    for (n=0; true; n++) {
      if (query.describe (l).layout.accepts (n) &&
          ((n>0) || (query.describe (l).layout.arity == athena::contracts::arity_kind::fixed))) break;
      if (n == 100) return false;
    }
  }

  tree t (l, n);
  path p (0, 0);
  int  acc=0;
  for (; acc<n; acc++)
    if (query.accessible (t, acc))
      break;
  if (acc<n) p->item= acc;
  tree f= definition;
  bool block_macro= (N(f) == 2) && is_multi_paragraph_macro (f, true, *context);
  bool large_macro= (N(f) == 2) && is_multi_paragraph_macro (f, false, *context);
  bool table_macro= (N(f) == 2) && contains_table_format (f[1], f[0]);
  if (tag == "explain") block_macro= true;

  std::shared_ptr<const evaluation_environment_checkpoint> table_entry;
  if (n > 0 && table_macro) {
    table_entry= prepare_macro_argument (t, 0, *context);
    if (!table_entry) {
      set_message ("Table macro argument is not ready", "Table insertion");
      return false;
    }
  }

  consume_input ();
  if (n == 0) insert_tree (t, 1);
  else if (query.describe (l).with_like && format_with_like_check_insert (t));
  else {
    tree sel= "";
    if (selection_active_small () ||
        (large_macro && selection_active_normal ()))
      sel= selection_get_cut ();
    else if (query.describe (l).with_like && selection_active_normal ()) {
      sel= selection_get_cut ();
      t[n-1]= sel;
      insert_tree (t, p);
      return true;
    }
    if ((block_macro && (!table_macro) &&
         l != INCLUDE && l != VAR_INCLUDE) ||
        (l == make_tree_label ("footnote")) ||
        (l == make_tree_label ("footnote-anchor")) ||
        (l == make_tree_label ("note-footnote")) ||
        (l == make_tree_label ("note-footnote*")) ||
        (l == make_tree_label ("folded-comment")))
      {
        t[0]= tree (DOCUMENT, "");
        p   = path (0, 0, 0);
      }
    if (!query.all_fields_editable (l))
      if (get_init_string (MODE) != "src" && !inside ("show-preamble")) {
        t= tree (INACTIVE, t);
        p= path (0, p);
      }
    insert_tree (t, p);
    if (table_macro) make_table (1, 1, table_entry);
    if (sel != "") {
      // Table construction may move the field and introduce cell-local values.
      // The invocation checkpoint is not the selected content's destination.
      const auto field= prepare_source_position (tp);
      insert_tree (sel, prepare_tree_boundary (sel, path (), false, *field));
    }
    
    tree mess= concat ();
    if (query.describe (l).layout.arity != athena::contracts::arity_kind::fixed)
      mess= concat (kbd ("A-right"), ": insert argument");
    if (!query.all_fields_editable (l)) {
      if (mess != "") mess << ", ";
      mess << kbd ("return") << ": activate";
    }
    if (mess == concat ()) mess= "Move to the right when finished";
    set_status_hint (mess, query.name (l));
  }
  return true;
}

void
edit_dynamic_rep::activate () {
  path p= search_upwards (INACTIVE);
  if (is_nil (p)) return;
  const auto prepared= prepare_source_position (p * 0);
  const athena::contracts::editing_query query {prepared->contract_context ()};
  tree st= subtree (et, p * 0);

  if (is_func (st, COMPOUND) && is_atomic (st[0])) {
    tree u (make_tree_label (st[0]->label));
    u << A (st (1, N(st)));
    assign (p, u);
    call ("notify-activated", object (subtree (et, p)));
    go_to (prepare_command_boundary (p, p, false, *prepared));
    correct (path_up (p));
  }
  else {
    bool acc= (p * 0 < path_up (tp) && query.accessible (st, tp[N(p)+1]));
    remove_node (p * 0);
    call ("notify-activated", object (subtree (et, p)));
    if (!acc) go_to (prepare_command_boundary (p, p, false, *prepared));
    correct (path_up (p));
  }
}

/******************************************************************************
* Inserting and removing arguments
******************************************************************************/

void
edit_dynamic_rep::go_to_argument (path p, bool start_flag) {
  const auto prepared= prepare_source_position (path_up (p) * 0);
  source_edit_scope source_scope (source_mode_during_edit, source_editing (*prepared));
  go_to_argument (p, start_flag, *prepared);
}

void
edit_dynamic_rep::go_to_argument (path p, bool start_flag,
    const evaluation_environment_checkpoint& prepared) {
  const athena::contracts::editing_query query {prepared.contract_context ()};
  tree t= subtree (et, path_up (p));
  bool inactive= is_func (subtree (et, path_up (p, 2)), INACTIVE);
  int i= last_item (p), n= N(t);
  path root= path_up (p, inactive? 2: 1);
  if (i < 0) go_to (prepare_command_boundary (root, root, true, prepared));
  else if (i >= n) go_to (prepare_command_boundary (root, root, false, prepared));
  else {
    if ((!query.accessible (t, i)) &&
        (!inactive) && (!in_source ()))
      {
        insert_node (path_up (p) * 0, INACTIVE);
        call ("notify-disactivated", object (subtree (et, path_up (p) * 0)));
        p= path_up (p) * path (0, i);
      }
    go_to (prepare_command_boundary (root, p, start_flag, prepared));
  }
}

void
edit_dynamic_rep::insert_argument (path p, bool forward) {
  const auto prepared= prepare_source_position (path_up (p) * 0);
  const athena::contracts::editing_query query {prepared->contract_context ()};
  source_edit_scope source_scope (source_mode_during_edit, source_editing (*prepared));
  tree t= subtree (et, path_up (p));
  int i= last_item (p), n= N(t), d= 1;
  if (is_func (t, WITH) ||
      is_func (t, STYLE_WITH) ||
      is_func (t, VAR_STYLE_WITH))
    if (i == n-1) i--;
  if ((!in_source ()) || query.contains (as_string (L(t)))) {
    const auto& layout= query.describe (L(t)).layout;
    if (forward) do i++; while ((i<=n) && (!layout.insertion (i, n)));
    else while ((i>=0) && (!layout.insertion (i, n))) i--;
    if ((i<0) || (i>n)) return;
    while (!layout.accepts (n+d)) d++;
  }
  else if (forward) i++;
  path q= path_up (p) * i;
  tree ins (L(t), d);
  insert (q, ins);
  go_to_argument (q, forward, *prepared);
}

void
edit_dynamic_rep::insert_argument (bool forward) {
  path p= find_dynamic (tp);
  if (is_nil (p)) return;
  if (p == tp) p= find_dynamic (path_up (tp));
  if (is_nil (p)) return;
  insert_argument (p, forward);
}

void
edit_dynamic_rep::remove_empty_argument (path p, bool forward) {
  const auto prepared= prepare_source_position (path_up (p) * 0);
  const athena::contracts::editing_query query {prepared->contract_context ()};
  tree t= subtree (et, path_up (p));
  int i= last_item (p), j, d, n= N(t);
  const bool source= source_editing (*prepared);
  source_edit_scope source_scope (source_mode_during_edit, source);
  bool src_flag= source && (!query.contains (as_string (L(t))));

  for (d=1; d<=n-i; d++)
    if ((src_flag && (d==1)) ||
        ((!src_flag) &&
         query.describe (L(t)).layout.accepts (n-d) &&
         query.describe (L(t)).layout.insertion (i, n-d)))
      {
        bool flag= true;
        for (j=0; j<d; j++)
          flag= flag && is_empty (t[i+j]);
        if (flag) {
          const path parent= path_up (p, 2);
          const bool unwrap= d == n && is_mod_active_once (subtree (et, parent));
          const auto outer= unwrap ? prepare_source_position (parent * 0) : nullptr;
          remove (p, d);
          if (unwrap) {
            remove_node (parent * 0);
            go_to (prepare_command_boundary (parent, parent, forward, *outer));
          }
          else if (forward) go_to_argument (path_up (p) * i, true, *prepared);
          else go_to_argument (path_up (p) * (i-1), false, *prepared);
          return;
        }
        else break;
      }

  bool flag= true;
  for (j=0; j<n; j++)
    flag= flag && is_empty (t[j]);
  if (flag) {
    const path parent= path_up (p, 2);
    tree st= subtree (et, parent);
    const bool heading= is_compound (st, "doc-data") ||
      is_compound (st, "abstract-data") || is_compound (st, "doc-author") ||
      is_compound (st, "author-data");
    const auto outer= heading && N(st) > 1 ? prepare_source_position (parent * 0) : nullptr;
    assign (path_up (p), "");
    st= subtree (et, parent);
    if ((is_mod_active_once (st) || is_compound (st, "doc-inactive")) &&
        (st[0] == "")) {
      assign (path_up (p, 2), "");
      correct (path_up (p, 3));
    }
    // FIXME: temporary hack for doc-data and doc-author-data
    else if (heading) {
      if (N(st)==1) {
        assign (path_up (p, 2), "");
        correct (path_up (p, 3));
      }
      else {
        int i= last_item (path_up (p)) + (forward? 0: -1);
        remove (path_up (p), 1);
        go_to (prepare_command_boundary (parent, i < 0 ? parent : parent * i,
                                         i < 0 || forward, *outer));
      }
    }
    else correct (path_up (p, 2));
    return;
  }

  if (forward) go_to_argument (path_up (p) * (i+1), true, *prepared);
  else go_to_argument (path_up (p) * (i-1), false, *prepared);
}

void
edit_dynamic_rep::remove_argument (path p, bool forward) {
  const auto prepared= prepare_source_position (path_up (p) * 0);
  const athena::contracts::editing_query query {prepared->contract_context ()};
  source_edit_scope source_scope (source_mode_during_edit, source_editing (*prepared));
  tree t= subtree (et, path_up (p));
  int i= last_item (p), n= N(t), d= 1;
  if ((!in_source ()) || query.contains (as_string (L(t)))) {
    const auto& layout= query.describe (L(t)).layout;
    if (forward) do i++; while (i<=n && !layout.insertion (i, n));
    else while (i>=0 && !layout.insertion (i, n)) i--;
    if ((i<0) || (i>n)) return;
    while (i>=d && !layout.accepts (n-d)) d++;
    if (i<d || n<=d || !layout.insertion (i-d, n-d)) return;
  }
  else {
    if (forward) i++;
    if (i<d || n<=d) return;
  }
  path q= path_up (p) * (i-d);
  remove (q, d);
  go_to_argument (q, forward, *prepared);
}

void
edit_dynamic_rep::remove_argument (bool forward) {
  path p= find_dynamic (tp);
  if (is_nil (p)) return;
  if (p == tp) p= find_dynamic (path_up (tp));
  if (is_nil (p)) return;
  remove_argument (p, forward);
}

/******************************************************************************
* Backspace and delete
******************************************************************************/

void
edit_dynamic_rep::back_monolithic (path p) {
  if (!is_concat (subtree (et, path_up (p)))) assign (p, "");
  else remove (p, 1);
  correct (path_up (p));
}

void
edit_dynamic_rep::back_general (path p, bool forward) {
  const auto prepared= prepare_source_position (p * 0);
  source_edit_scope source_scope (source_mode_during_edit, source_editing (*prepared));
  tree st= subtree (et, p);
  int n= N(st);
  if ((L(st) >= START_EXTENSIONS) && in_source () && (forward || (n == 0))) {
    tree u (COMPOUND, copy (as_string (L(st))));
    u << copy (A (st));
    assign (p, u);
    go_to (prepare_command_boundary (p, p * 0, forward, *prepared));
  }
  else if (n==0) back_monolithic (p);
  else if ((n==1) && is_func (st[0], DOCUMENT, 1) &&
           (is_func (st[0][0], TFORMAT) || is_func (st[0][0], TABLE)))
    back_table (p * path (0, 0), forward);
  else if ((n==1) && (is_func (st[0], TFORMAT) || is_func (st[0], TABLE)))
    back_table (p * 0, forward);
  else go_to_argument (p * (forward? 0: n-1), forward, *prepared);
}

void
edit_dynamic_rep::back_in_general (tree t, path p, bool forward) {
  const auto prepared= prepare_source_position (path_up (p) * 0);
  const athena::contracts::editing_query query {prepared->contract_context ()};
  source_edit_scope source_scope (source_mode_during_edit, source_editing (*prepared));
  if (is_func (subtree (et, path_up (p, 2)), INACTIVE) || in_source ())
    if ((L(t) >= START_EXTENSIONS) && (last_item (p) == 0) && (!forward)) {
      bool src_flag= in_source () && !query.contains (as_string (L(t)));
      tree u (COMPOUND, copy (as_string (L(t))));
      if (is_empty (t[0]) && src_flag) u << A (copy (t (1, N(t))));
      else u << A (copy (t));
      assign (path_up (p), u);
      go_to (prepare_command_boundary (path_up (p), p, false, *prepared));
      return;
    }
  remove_empty_argument (p, forward);
}

/******************************************************************************
* The WITH tag
******************************************************************************/

static tree
remove_changes_in (tree t, string var) {
  if (is_atomic (t)) return t;
  else if (is_func (t, WITH)) {
    int i, n=N(t), k=(n-1)>>1;
    if (k==1) {
      tree r= remove_changes_in (t[2], var);
      if (t[0] != var) r= tree (WITH, t[0], t[1], r);
      return simplify_correct (r);
    }
    tree r (WITH);
    for (i=0; i<k; i++)
      if (t[i<<1] != var) r << t[i<<1] << t[(i<<1)+1];
    r << remove_changes_in (t[i<<1], var);
    return simplify_correct (r);
  }
  else if (is_format (t) || is_func (t, SURROUND)) {
    int i, n= N(t);
    tree r (t, n);
    for (i=0; i<n; i++)
      r[i]= remove_changes_in (t[i], var);
    return simplify_correct (r);
  }
  else return t;
}

void
edit_dynamic_rep::make_with (string var, string val) {
  if (selection_active_normal ()) {
    tree t= remove_changes_in (selection_get (), var);
    const auto context= prepare_source_position (selection_get_cursor_path ());
    tree wrapper (WITH, var, val, t);
    const path destination= prepare_tree_boundary (wrapper, path (2), false, *context);
    selection_cut ();
    insert_tree (wrapper, destination);
  }
  else insert_tree (tree (WITH, var, val, ""), path (2, 0));
}

void
edit_dynamic_rep::insert_with (path p, string var, tree val) {
  tree st= subtree (et, p);
  if (is_func (st, WITH)) {
    int i, n= N(st)-1;
    for (i=0; i<n; i+=2)
      if (st[i] == var) {
        assign (p * (i+1), copy (val));
        return;
      }
    insert (p * n, copy (tree (WITH, var, val)));    
  }
  else if ((rp < p) && is_func (subtree (et, path_up (p)), WITH))
    insert_with (path_up (p), var, val);
  else insert_node (p * 2, copy (tree (WITH, var, val)));
}

void
edit_dynamic_rep::remove_with (path p, string var) {
  tree st= subtree (et, p);
  if (is_func (st, WITH)) {
    int i, n= N(st)-1;
    for (i=0; i<n; i+=2)
      if (st[i] == var) {
        remove (p * i, 2);
        if (n == 2) remove_node (p * 0);
        return;
      }
  }
  else if ((rp < p) && is_func (subtree (et, path_up (p)), WITH))
    remove_with (path_up (p), var);
}

void
edit_dynamic_rep::back_in_with (tree t, path p, bool forward) {
  if (is_func (subtree (et, path_up (p, 2)), INACTIVE) ||
      ((is_func (t, WITH) || is_func (t, LOCUS)) && in_source ()))
    back_in_general (t, p, forward);
  else if (t[N(t)-1] == "") {
    assign (path_up (p), "");
    correct (path_up (p, 2));
  }
  else {
    const path root= path_up (p);
    const auto prepared= prepare_source_position (root * 0);
    go_to (prepare_command_boundary (root, root, !forward, *prepared));
  }
}

/******************************************************************************
* Style file editing
******************************************************************************/

void
edit_dynamic_rep::make_mod_active (tree_label l) {
  if (selection_active_normal ()) {
    tree t= selection_get ();
    const auto context= prepare_source_position (selection_get_cursor_path ());
    tree wrapper (l, t);
    const path destination= prepare_tree_boundary (wrapper, path (0), false, *context);
    selection_cut ();
    insert_tree (wrapper, destination);
  }
  else if ((l == VAR_STYLE_ONLY) || (l == VAR_ACTIVE) || (l == VAR_INACTIVE))
    insert_tree (tree (l, ""), path (0, 0));
  else {
    path p= path_up (tp);
    if (is_atomic (subtree (et, p))) p= path_up (p);
    if (rp < p) insert_node (p * 0, l);
  }
}

void
edit_dynamic_rep::insert_style_with (path p, string var, string val) {
  if (!(rp < p)) return;
  tree st= subtree (et, path_up (p));
  if (is_func (st, STYLE_WITH)) {
    int i, n= N(st);
    for (i=n-1; i>=0; i-=2)
      if (st[i] == var) {
        assign (path_up (p) * (i+1), copy (val));
        return;
      }
    insert (path_up (p) * (n-1), tree (STYLE_WITH, copy (var), copy (val)));
  }
  else insert_node (p * 2, copy (tree (STYLE_WITH, var, val)));
}

void
edit_dynamic_rep::make_style_with (string var, string val) {
  if (selection_active_normal ()) {
    tree t= selection_get ();
    const auto context= prepare_source_position (selection_get_cursor_path ());
    tree wrapper (STYLE_WITH, var, val, t);
    const path destination= prepare_tree_boundary (wrapper, path (2), false, *context);
    selection_cut ();
    if (subtree (et, path_up (tp)) == "") {
      insert_style_with (path_up (tp), var, val);
      insert_tree (t, destination / path (2));
    }
    else insert_tree (wrapper, destination);
  }
  else insert_style_with (path_up (tp), var, val);
}

/******************************************************************************
* The HYBRID and LATEX tags
******************************************************************************/

void
edit_dynamic_rep::make_hybrid () {
  interrupt_shortcut ();
  finish_hybrid ();
  const bool source_mode= in_source ();
  hybrid_selection_start= selection_active_normal ()? selection_get_start (): tp;
  hybrid_selection_end= selection_active_normal ()? selection_get_end (): tp;
  hybrid_mark= new_marker ();
  mark_start (hybrid_mark);
  tree t (HYBRID, "");
  if (selection_active_normal ()) t << selection_get_cut ();
  path p (0, 0);
  if (source_mode) insert_tree (t, p);
  else insert_tree (tree (INACTIVE, t), path (0, p));
  hybrid_owner= subtree (et, search_upwards (HYBRID));
  hybrid_drafts.push_back ({hybrid_owner, N(t) == 2? copy (t[1]): tree ("")});
  set_status_hint (concat (kbd ("return"), ": activate symbol or macro"),
                   "hybrid");
}

void
edit_dynamic_rep::make_native_math_block (bool grid) {
  path hp= search_upwards (HYBRID);
  if (buf->storage_version != athena::document::xml_storage_version::v3 ||
      (is_nil (hp) && in_source ()) || inside ("show-preamble")) {
    set_message ("Math insertion requires XML v3 content mode", "mathematics");
    return;
  }
  if (is_nil (hp) && selection_active_any () && !selection_active_small ()) return;
  const auto context= is_nil (hp) ?
    prepare_source_position (selection_get_cursor_path ()) : macro_invocation_context ();
  if (!context) {
    set_message ("Math insertion is awaiting layout", "mathematics");
    return;
  }
  tree body ("");
  if (!is_nil (hp)) {
    tree input= subtree (et, hp);
    if (N(input) == 2) body= copy (input[1]);
    if (is_func (subtree (et, path_up (hp)), INACTIVE)) hp= path_up (hp);
  }
  else if (selection_active_small ()) body= selection_get ();
  if (is_func (body, DOCUMENT)) {
    if (N(body) != 1) {
      set_message ("Math insertion requires a single selected expression", "mathematics");
      return;
    }
    if (athena::node::get (body)) {
      tree envelope (CONCAT, body[0]);
      athena::node::copy_metadata (body, envelope);
      body= envelope;
    }
    else body= body[0];
  }
  tree content= body;
  if (grid) {
    content= tree (MATH_GRID,
      tree (ROW, tree (CELL, body), tree (CELL, ""), tree (CELL, "")));
    athena::node::metadata properties;
    properties.properties["math:grid-kind"]= athena::node::property (
      std::string ("eqnarray"));
    athena::node::set (content, properties);
  }
  tree block (MATH_BLOCK, content);
  athena::node::metadata properties;
  properties.properties["math:block-numbered"]= athena::node::property (false);
  athena::node::set (block, properties);
  const path destination= prepare_tree_boundary (
    block, grid ? path (0, 0, 0, 0) : path (0), false, *context);
  if (!is_nil (hp)) {
    assign (hp, "");
    go_to (hp * 0);
    correct (path_up (hp));
  }
  else if (selection_active_small ()) selection_cut ("none");
  insert_tree (block, destination);
  ensure_trailing_proof_paragraph ();
  if (!is_nil (hp)) finish_hybrid (true);
}

void
edit_dynamic_rep::finish_hybrid (bool accepted) {
  pending_activation.reset ();
  if (hybrid_mark != 0) {
    interrupt_shortcut ();
    // Include identity/artifact metadata in the same completed input gesture.
    if (accepted) add_undo_mark ();
    mark_end (hybrid_mark);
    hybrid_mark= 0;
  }
  if (accepted)
    hybrid_drafts.erase (std::remove_if (hybrid_drafts.begin (), hybrid_drafts.end (),
      [&] (const hybrid_draft& draft) { return draft.input.operator-> () == hybrid_owner.operator-> (); }),
      hybrid_drafts.end ());
  hybrid_drafts.erase (std::remove_if (hybrid_drafts.begin (), hybrid_drafts.end (),
    [&] (const hybrid_draft& draft) {
      tree input= draft.input;
      path ip= obtain_ip (input);
      if (!is_accessible (ip)) return true;
      path p= reverse (ip);
      return !has_subtree (et, p) ||
             subtree (et, p).operator-> () != draft.input.operator-> ();
    }), hybrid_drafts.end ());
  hybrid_owner= tree ("");
}

bool
edit_dynamic_rep::hybrid_name_context () {
  path p= search_upwards (HYBRID);
  return !is_nil (p) && p * 0 <= tp;
}

bool
edit_dynamic_rep::prepare_hybrid () {
  path p= search_upwards (HYBRID);
  if (is_nil (p)) return false;
  tree st= subtree (et, p);
  if (N(st) == 3 && is_tuple (st[1])) return true;
  if (!is_atomic (st[0]) || st[0] == "") return false;
  const auto context= macro_invocation_context ();
  if (!context) {
    set_message ("Macro input is awaiting layout", "Macro input");
    return false;
  }
  macro_edit_scope invocation (macro_environment, context);
  const athena::contracts::editing_query query {context->contract_context ()};
  string name= st[0]->label;
  tree f= context->effective_value (name);
  const auto signature= source_signature (name, *context);
  tree_label tag= make_tree_label (name);
  int arity= 0;
  if (signature) {
    if (signature->kind == athena::contracts::definition_kind::value) return false;
    arity= signature->kind == athena::contracts::definition_kind::macro ?
      int (signature->parameters.size ()) : 1;
  }
  else if (is_func (f, MACRO)) arity= N(f)-1;
  else if (query.contains (name)) {
    for (; arity < 100 && !query.describe (tag).layout.accepts (arity); ++arity) {}
    if (arity == 100) return false;
  }
  else return false;
  if (!signature && arity == 0 && is_func (f, XMACRO)) arity= 1;
  if (arity == 0) return false;
  tree args (TUPLE, arity);
  if (!signature && arity == 1 && is_func (f, MACRO) && is_multi_paragraph_macro (f, true, *context))
    args[0]= tree (DOCUMENT, "");
  tree selected= N(st) == 2? st[1]: tree ("");
  int body= -1;
  tree prototype (tag, arity);
  for (int i=0; i<arity; ++i) {
    string label= signature ? string (signature->parameters[i].c_str ()) :
      is_func (f, MACRO) && is_atomic (f[i]) ?
        f[i]->label : query.child_name (prototype, i);
    if (label == "body") {
      if (body >= 0) { body= -1; break; }
      body= i;
    }
  }
  if (body < 0 && arity == 1 && (signature || query.accessible (prototype, 0))) body= 0;
  if (selected != "" && body >= 0) {
    args[body]= selected;
    selected= "";
  }
  assign (p, tree (HYBRID, st[0], args, selected));
  tree prepared= subtree (et, p);
  for (auto& draft: hybrid_drafts)
    if (draft.input.operator-> () == st.operator-> ()) draft.input= prepared;
  if (hybrid_owner.operator-> () == st.operator-> ()) hybrid_owner= prepared;
  go_to (prepare_command_boundary (p, p * path (1, 0), true, *context));
  if (selected != "")
    set_status_hint ("Select an argument and press Enter to place the selection", name);
  return true;
}

void
edit_dynamic_rep::choose_hybrid (string name, bool activate) {
  if (!hybrid_name_context ()) return;
  const auto context= macro_invocation_context ();
  if (!context) {
    set_message ("Macro input is awaiting layout", "Macro input");
    return;
  }
  macro_edit_scope invocation (macro_environment, context);
  std::optional<command> resolved;
  if (activate) {
    string help;
    command action;
    (void) kbd_get_command (name, help, action);
    resolved= action;
  }
  path field= path_up (tp);
  assign (field, name);
  go_to (field * N(name));
  if (activate) activate_hybrid (false, resolved);
  else (void) prepare_hybrid ();
}

bool
edit_dynamic_rep::hybrid_keypress (string key) {
  pending_activation.reset ();
  path p= search_upwards (HYBRID);
  if (is_nil (p)) { finish_hybrid (); return false; }
  tree st= subtree (et, p);
  if (hybrid_owner.operator-> () != st.operator-> ()) {
    finish_hybrid ();
    hybrid_owner= st;
  }
  bool name= hybrid_name_context ();
  bool prepared= N(st) == 3 && is_tuple (st[1]);
  if (key == "escape" || (key == "backspace" && name && st[0] == "")) {
    interrupt_shortcut ();
    if (hybrid_mark != 0) {
      double mark= hybrid_mark;
      hybrid_mark= 0;
      if (mark_cancel (mark)) {
        go_to (hybrid_selection_end);
        if (hybrid_selection_start != hybrid_selection_end)
          select (hybrid_selection_start, hybrid_selection_end);
        finish_hybrid (true);
      }
      else set_message ("Could not roll back macro input; its draft is retained", "Macro input");
    }
    else {
      tree selected= N(st) == 2? st[1]: prepared? st[2]: tree ("");
      auto draft= std::find_if (hybrid_drafts.begin (), hybrid_drafts.end (),
        [&] (const hybrid_draft& item) { return item.input.operator-> () == st.operator-> (); });
      if (draft != hybrid_drafts.end ()) selected= copy (draft->selection);
      else if (prepared) {
        set_message ("This saved macro draft has no cancellation snapshot", "Macro input");
        return true;
      }
      if (is_func (subtree (et, path_up (p)), INACTIVE)) p= path_up (p);
      const auto context= macro_invocation_context ();
      if (!context) {
        set_message ("Macro input is awaiting layout", "Macro input");
        return true;
      }
      macro_edit_scope invocation (macro_environment, context);
      assign (p, selected);
      const path last= prepare_command_boundary (p, p, false, *context);
      go_to (last);
      if (selected != "") select (prepare_command_boundary (p, p, true, *context), last);
      correct (path_up (p));
      finish_hybrid (true);
    }
    return true;
  }
  if (key == "tab" || key == "S-tab") {
    if (name && !prepared) { (void) prepare_hybrid (); return true; }
    if (!prepared) return false;
    int index= name || !(p * 1 <= tp)? -1: (tp / (p * 1))->item;
    index+= key == "S-tab"? -1: 1;
    if (index < 0 || index >= N(st[1])) go_to (p * path (0, N(st[0]->label)));
    else {
      const auto context= macro_invocation_context ();
      if (!context) {
        set_message ("Macro input is awaiting layout", "Macro input");
        return true;
      }
      go_to (prepare_command_boundary (p, p * path (1, index), true, *context));
    }
    return true;
  }
  if (prepared && (key == "A-right" || key == "A-left")) {
    const auto invocation= macro_invocation_context ();
    if (!invocation) {
      set_message ("Macro input is awaiting layout", "Macro input");
      return true;
    }
    const athena::contracts::editing_query query {invocation->contract_context ()};
    tree_label tag= make_tree_label (st[0]->label);
    auto layout= query.describe (tag).layout;
    const auto signature= source_signature (st[0]->label, *invocation);
    if (signature) {
      if (signature->kind != athena::contracts::definition_kind::variadic_macro) return true;
      layout= {0, 1, athena::contracts::arity_kind::repeated_suffix,
               athena::contracts::field_mapping::uniform};
    }
    int n= N(st[1]);
    if (key == "A-right") {
      int next= n+1;
      while (next < 100 && !layout.accepts (next)) ++next;
      int at= n;
      while (at >= 0 && !layout.insertion (at, n)) --at;
      if (next < 100 && at >= 0) {
        insert (p * 1 * at, tree (TUPLE, next-n));
        go_to (p * path (1, at, 0));
      }
    }
    else {
      int previous= n-1;
      while (previous >= 0 && !layout.accepts (previous)) --previous;
      int at= previous;
      while (at >= 0 && !layout.insertion (at, previous)) --at;
      bool empty= at >= 0;
      for (int i= std::max (0, at); empty && i<at+n-previous; ++i)
        empty= is_empty (st[1][i]);
      if (empty) {
        remove (p * 1 * at, n-previous);
        go_to (p * path (0, N(st[0]->label)));
      }
    }
    return true;
  }
  if (key == "return" || key == "enter") {
    if (prepared && st[2] != "") {
      if (name || !(p * 1 <= tp)) return true;
      int index= (tp / (p * 1))->item;
      if (st[1][index] != "") {
        set_message ("Choose an empty argument for the selection", "Macro input");
        return true;
      }
      tree selected= st[2];
      const auto context= macro_invocation_context ();
      if (!context) {
        set_message ("Macro input is awaiting layout", "Macro input");
        return true;
      }
      assign (p * 2, "");
      assign (p * 1 * index, selected);
      go_to (prepare_command_boundary (p, p * path (1, index), false, *context));
    }
    else activate_hybrid (false);
    return true;
  }
  return false;
}

bool
edit_dynamic_rep::activate_latex () {
  return activate_latex (true);
}

bool
edit_dynamic_rep::activate_latex (bool report_error) {
  return activate_latex (report_error, {});
}

bool
edit_dynamic_rep::activate_latex (bool report_error, const std::optional<command>& resolved) {
  path p= search_upwards (LATEX);
  if (is_nil (p)) p= search_upwards (HYBRID);
  if (is_nil (p)) return false;
  tree st= subtree (et, p);
  if (is_atomic (st[0])) {
    if (is_func (subtree (et, path_up (p)), INACTIVE))
      p= path_up (p);
    string  s= st[0]->label, help;
    command cmd;
    const bool found= resolved ? (cmd= *resolved, !is_nil (cmd)) :
      kbd_get_command (s, help, cmd);
    if (found) {
      const auto context= macro_invocation_context ();
      if (!context) {
        set_message ("Macro input is awaiting layout", "Macro input");
        return false;
      }
      macro_edit_scope invocation (macro_environment, context);
      cmd= native_latex_prepare_command (cmd, context);
      cut (p * 0, p * 1);
      cmd ();
      if (N(st) == 2) insert_tree (copy (st[1]));
      return true;
    }
    if (report_error)
      set_message ("Error: not a command name",
                   "activate latex command");
  }
  return false;
}

void
edit_dynamic_rep::activate_hybrid (bool with_args_hint) {
  activate_hybrid (with_args_hint, {});
}

bool
edit_dynamic_rep::pending_activation_current () {
  if (!pending_activation) return false;
  const auto& request= *pending_activation;
  if (buf->actor->source_epoch () == request.source_epoch &&
      tp == request.caret && selection_get_start () == request.first &&
      selection_get_end () == request.last) return true;
  pending_activation.reset ();
  return false;
}

void
edit_dynamic_rep::prepare_pending_hybrid_activation () {
  if (!pending_activation_current () || pending_activation->queued ||
      !macro_invocation_context ()) return;
  const auto token= pending_activation->token;
  // Layout must finish publishing its boxes before a new source transaction.
  pending_activation->queued= true;
  buf->actor->queue_hybrid_activation (runtime_view_id, token);
}

void
edit_dynamic_rep::resume_pending_hybrid_activation (std::uint64_t token) {
  if (!pending_activation || pending_activation->token != token) return;
  pending_activation->queued= false;
  if (!pending_activation_current () || !macro_invocation_context ()) return;
  auto request= std::move (*pending_activation);
  pending_activation.reset ();
  before_menu_action ();
  try {
    activate_hybrid (request.with_args_hint, request.resolved);
    after_menu_action ();
  }
  catch (...) {
    cancel_menu_action ();
    throw;
  }
}

void
edit_dynamic_rep::activate_hybrid (bool with_args_hint, const std::optional<command>& resolved) {
  const auto context= macro_invocation_context ();
  if (!context) {
    if (buf->actor && (!is_nil (search_upwards (HYBRID)) ||
                       !is_nil (search_upwards (LATEX)))) {
      pending_activation= pending_hybrid_activation {
        ++next_activation_token, buf->actor->source_epoch (),
        copy (tp), copy (selection_get_start ()), copy (selection_get_end ()),
        with_args_hint, false, resolved};
      notify_change (THE_CURSOR);
    }
    set_message ("Macro input is awaiting layout", "Macro input");
    return;
  }
  pending_activation.reset ();
  macro_edit_scope invocation (macro_environment, context);
  const athena::contracts::editing_query query {context->contract_context ()};
  path hp= search_upwards (HYBRID);
  if (!is_nil (hp)) {
    tree input= subtree (et, hp);
    if (hybrid_owner.operator-> () != input.operator-> ()) {
      finish_hybrid ();
      hybrid_owner= input;
    }
    if (N(input) == 3 && is_tuple (input[1])) {
      if (input[2] != "") return;
      tree_label label= make_tree_label (input[0]->label);
      tree f= context->effective_value (input[0]->label);
      const auto signature= source_signature (input[0]->label, *context);
      const bool valid_arguments= signature ?
        (signature->kind == athena::contracts::definition_kind::variadic_macro ||
         (signature->kind == athena::contracts::definition_kind::macro &&
          N(input[1]) == int (signature->parameters.size ()))) :
        !((!is_func (f, MACRO) && !is_func (f, XMACRO) &&
           !query.contains (input[0]->label)) ||
          (is_func (f, MACRO) && N(input[1]) != N(f)-1) ||
          (!is_func (f, MACRO) && !query.describe (label).layout.accepts (N(input[1]))));
      if (!valid_arguments) {
        set_message ("Invalid macro arguments", "Macro input");
        return;
      }
      tree result (label, A(input[1]));
      const auto* declaration= athena::enunciation::standard_registry ().legacy (
        std::string (input[0]->label.data (), N(input[0]->label)));
      if (declaration && !in_source ()) {
        athena::enunciation::conversion_options options;
        if (!declaration->numbering_preference.empty ())
          options.numbering_preferences[declaration->numbering_preference]=
            get_preference (string (declaration->numbering_preference.c_str ())) == "on";
        auto converted= athena::enunciation::convert_detached_source (result, options);
        if (converted.diagnostics.empty ()) result= converted.source;
      }
      if (is_func (subtree (et, path_up (hp)), INACTIVE)) hp= path_up (hp);
      assign (hp, result);
      go_to (prepare_command_boundary (hp, hp, true, *context));
      correct (path_up (hp));
      finish_hybrid (true);
      return;
    }
    if (N(input) == 2 && prepare_hybrid ()) {
      input= subtree (et, hp);
      if (input[2] == "") activate_hybrid (with_args_hint);
      return;
    }
  }
  if (activate_latex (false, resolved)) { finish_hybrid (true); return; }
  set_message ("", "");
  path p= search_upwards (HYBRID);
  if (is_nil (p)) return;
  tree st= subtree (et, p);
  if (is_compound (st[0])) return;
  if (is_func (subtree (et, path_up (p)), INACTIVE))
    p= path_up (p);

  // activate macro argument
  string name= st[0]->label;
  path mp= search_upwards (MACRO);
  if (is_nil (mp)) mp= search_upwards ("edit-macro");
  if (!is_nil (mp)) {
    tree mt= subtree (et, mp);
    int i, n= N(mt)-1;
    for (i=0; i<n; i++)
      if (mt[i] == name) {
        if (N(st) == 2) {
          set_message ("This macro argument cannot wrap the selection", "Macro input");
          return;
        }
        assign (p, tree (ARG, copy (name)));
        go_to (prepare_command_boundary (p, p, false, *context));
        correct (path_up (p));
        finish_hybrid (true);
        return;
      }
  }

  // built-in primitives, macro applications and values
  const bool source= in_source ();
  tree f= context->effective_value (name);
  const auto signature= source_signature (name, *context);
  {
    source_edit_scope source_scope (source_mode_during_edit, source);
    if (signature ? signature->kind != athena::contracts::definition_kind::value :
        ((query.contains (name) && (f == UNINIT)) ||
         is_func (f, MACRO) || is_func (f, XMACRO))) {
      if (!make_compound (make_tree_label (name), -1, p)) return;
      if (N(st) == 2) insert_tree (st[1]);
    }
    else if (signature || f != UNINIT) {
      if (N(st) == 2) {
        set_message ("This value cannot wrap the selection", "Macro input");
        return;
      }
      assign (p, tree (VALUE, copy (name)));
      go_to (prepare_command_boundary (p, p, false, *context));
      correct (path_up (p));
    }
    else if (in_source ()) {
      if (!make_compound (make_tree_label (name), with_args_hint? 1: 0, p)) return;
      if (N(st) == 2) insert_tree (st[1]);
    }
    else set_message ("Error: unknown command",
                      "activate hybrid command");
  }
  if (f != UNINIT || query.contains (name) || in_source ()) finish_hybrid (true);
}

/******************************************************************************
* Other special tags (SYMBOL and COMPOUND)
******************************************************************************/

void
edit_dynamic_rep::activate_symbol () {
  path p= search_upwards (SYMBOL);
  if (is_nil (p)) return;
  tree st= subtree (et, p);
  if (is_func (subtree (et, path_up (p)), INACTIVE))
    p= path_up (p);
  string s= st[0]->label;
  if (is_int (s))
    assign (p, string (((char) as_int (s))));
  else {
    int i, n= N(s);
    for (i=0; i<n; i++)
      if ((s[i]=='<') || (s[i]=='>'))
        { s= ""; break; }
    assign (p, "<" * s * ">");
  }
  go_to (p * N(subtree (et, p)->label));
  correct (path_up (p));
}

/******************************************************************************
* Temporary fixes for block structures
******************************************************************************/

bool
edit_dynamic_rep::make_return_before () {
  bool flag;
  path q= tp;
  while (!is_document (subtree (et, path_up (q)))) q= path_up (q);
  const auto context= prepare_source_position (q * 0);
  flag= (N (subtree (et, path_up (q))) == (q->item+1)) ||
        (tp != prepare_command_boundary (q, q, false, *context));
  if (flag) {
    flag= insert_return ();
    go_to (prepare_command_boundary (q, q, false, *context));
  }
  return flag;
}

bool
edit_dynamic_rep::make_return_after () {
  path q= tp;
  while (!is_document (subtree (et, path_up (q)))) {
    q= path_up (q);
    if (!(rp < q)) return false;
  }
  tree doc= subtree (et, path_up (q));
  if (last_item (q) == N(doc)) {
    if (N(doc) == 0) return false;
    const path previous= path_dec (q);
    const auto context= prepare_source_position (previous * 0);
    go_to (prepare_command_boundary (previous, previous, false, *context));
    return insert_return ();
  }
  const auto context= prepare_source_position (q * 0);
  if (tp == prepare_command_boundary (q, q, true, *context)) return false;
  return insert_return ();
}

void
edit_dynamic_rep::ensure_trailing_proof_paragraph () {
  // A display equation at the end of a proof needs a following paragraph so
  // editing can continue inside the proof after the display.
  const auto& registry= athena::enunciation::standard_registry ();
  path p= path_up (tp);
  while (registry.kind_name (subtree (et, p)) != "proof") {
    if (p == rp) return;
    p= path_up (p);
  }
  if (N(tp) < N(p)+2) return;
  path q = head (tp, N(p)+2);
  tree st= subtree (et, path_up (q));
  if ((!is_document (st)) || (last_item (q) != (N(st)-1))) return;
  insert (path_inc (q), tree (DOCUMENT, ""));
}

/******************************************************************************
* Native whole-buffer folding and filtering.
* Observer-aware operations on the BufferActor's attached document tree.
******************************************************************************/

namespace {

bool dynamic_group (string tag, const char* group) {
  return athena::contracts::standard_node_groups ().contains (
    group, std::string_view (tag.data (), N (tag)));
}

string
dynamic_tag (tree t) {
  return is_atomic (t) ? string ("") : as_string (L(t));
}

int
dynamic_toggle_side (tree t) {
  string tag= dynamic_tag (t);
  if (dynamic_group (tag, "toggle-first-tag")) return 1;
  if (dynamic_group (tag, "toggle-second-tag")) return 2;
  return 0;
}

void
dynamic_toggle (tree t) {
  const string tag= dynamic_tag (t);
  if (dynamic_toggle_side (t) == 0) return;
  const auto next= athena::contracts::standard_node_groups ().alternate (
    std::string_view (tag.data (), N (tag)));
  (void) tree_assign_node (t, as_tree_label (string (next.c_str ())));
}

bool dynamic_alternative (string tag) {
  return dynamic_group (tag, "alternative-tag");
}
bool dynamic_unroll (string tag) {
  return dynamic_group (tag, "unroll-tag");
}
bool dynamic_expanded (string tag) {
  return dynamic_group (tag, "expanded-tag");
}
bool dynamic_switch (string tag) {
  return dynamic_alternative (tag) || dynamic_unroll (tag) ||
         dynamic_expanded (tag);
}
bool dynamic_hidden (tree t) {
  const string tag= dynamic_tag (t);
  return dynamic_group (tag, "hidden-tag");
}

void
dynamic_switch_set (tree t, int i, bool show, bool hidden, bool unroll) {
  if (i < 0 || i >= N(t)) return;
  tree child= t[i];
  const string hide= unroll ? "hidden*" : "hidden";
  if (show && hidden)
    (void) tree_assign_node (child, as_tree_label ("shown"));
  else if (!show && dynamic_tag (child) == "shown")
    (void) tree_assign_node (child, as_tree_label (hide));
  else if (show && dynamic_tag (child) != "shown")
    (void) tree_insert_node (child, 0, tree (as_tree_label ("shown")));
  else if (!show && !hidden)
    (void) tree_insert_node (child, 0, tree (as_tree_label (hide)));
}

void
dynamic_switch_set (tree t, int i, bool show) {
  if (i < 0 || i >= N(t)) return;
  dynamic_switch_set (t, i, show, dynamic_hidden (t[i]), dynamic_unroll (dynamic_tag (t)));
}

void
dynamic_switch_range (tree t, int first, int last, bool show) {
  for (int i= std::max (0, first); i < std::min (last, N(t)); ++i)
    dynamic_switch_set (t, i, show);
}

int
dynamic_switch_last_visible (tree t) {
  for (int i= N(t)-1; i >= 0; --i)
    if (!dynamic_hidden (t[i])) return i;
  return -1;
}

void
dynamic_operate_tree (tree t, const string& mode) {
  if (is_atomic (t)) return;
  const string tag= dynamic_tag (t);
  if (tag == "traversed") {
    if (N(t) == 0) return;
    dynamic_operate_tree (t[0], mode);
    if (mode == "unfold" || mode == "expand" || mode == "var-expand")
      (void) tree_remove_node (t, 0);
    return;
  }
  if (tag == "fold-back" || tag == "keep-folded") {
    if (N(t) != 0) dynamic_operate_tree (t[0], mode);
    return;
  }

  // Match the original post-order traversal: descendants before their wrapper.
  for (int i= 0; i < N(t); ++i)
    dynamic_operate_tree (t[i], mode);

  const int side= dynamic_toggle_side (t);
  if ((side == 1 && (mode == "unfold" || mode == "expand" ||
                     mode == "var-expand")) ||
      (side == 2 && (mode == "fold" || mode == "compress" ||
                     mode == "var-compress"))) {
    dynamic_toggle (t);
    return;
  }
  const string current= dynamic_tag (t);
  if (!dynamic_switch (current)) return;

  if (mode == "expand")
    dynamic_switch_range (t, 0, N(t), true);
  else if (mode == "compress") {
    dynamic_switch_set (t, 0, true);
    dynamic_switch_range (t, 1, N(t), false);
  }
  else if (mode == "var-expand") {
    if (dynamic_alternative (current))
      (void) tree_assign_node (t, as_tree_label ("expanded"));
    if (dynamic_alternative (current) || dynamic_unroll (current))
      dynamic_switch_range (t, 0, N(t), true);
  }
  else if (mode == "var-compress") {
    if (dynamic_expanded (current))
      (void) tree_assign_node (t, as_tree_label ("switch"));
    if (dynamic_expanded (current) || dynamic_unroll (current)) {
      dynamic_switch_set (t, 0, true);
      dynamic_switch_range (t, 1, N(t), false);
    }
  }
}

bool
dynamic_filter_removes (tree t, const string& mode) {
  const int side= dynamic_toggle_side (t);
  if (side == 1) return mode == "remove-folded" || mode == "keep-unfolded";
  if (side == 2) return mode == "remove-unfolded" || mode == "keep-folded";
  if (dynamic_switch (dynamic_tag (t))) {
    const bool folded= dynamic_switch_last_visible (t) == 0;
    return folded ? (mode == "remove-folded" || mode == "keep-unfolded") :
                    (mode == "remove-unfolded" || mode == "keep-folded");
  }
  return mode == "keep-folded" || mode == "keep-unfolded";
}

void
dynamic_filter_tree (tree t, const string& mode, bool parent_is_document) {
  if (is_atomic (t)) return;
  // A retained immediate child of a document is not recursively filtered
  // by a "keep" operation; remove modes still descend into it.
  if (parent_is_document &&
      (mode == "keep-folded" || mode == "keep-unfolded")) return;
  const bool is_doc= is_document (t);
  for (int i= 0; i < N(t); ) {
    tree child= t[i];
    if (is_doc && dynamic_filter_removes (child, mode)) {
      if (N(t) == 1) {
        (void) tree_assign (child, tree (""));
        ++i;
      }
      else
        (void) tree_remove (t, i, 1);
    }
    else {
      dynamic_filter_tree (child, mode, is_doc);
      ++i;
    }
  }
}

} // namespace

void
edit_dynamic_rep::show_cursor_if_hidden () {
  const path target= copy (tp);
  for (path p= rp; p < path_up (target); p= p * (target / p)->item) {
    const auto context= prepare_source_position (p * 0);
    if (context->effective_value (MODE) == "src" ||
        context->effective_value (PREAMBLE) == "true") return;
    tree t= subtree (et, p);
    const string tag= dynamic_tag (t);
    const auto member= [&] (const char* group) {
      return athena::contracts::group_contains (context->contract_context (), group,
        std::string_view (tag.data (), N(tag)));
    };
    const int child= (target / p)->item;
    if (member ("hidden-tag"))
      (void) tree_assign_node (t, as_tree_label ("shown"));
    else if ((member ("toggle-first-tag") && child == 1) ||
             (member ("toggle-second-tag") && child == 0)) {
      const auto alternate= athena::contracts::standard_node_groups ().alternate (
        std::string_view (tag.data (), N(tag)));
      if (alternate.empty ()) throw std::runtime_error ("Hidden toggle has no alternate: " +
        std::string (tag.data (), N(tag)));
      (void) tree_assign_node (t, as_tree_label (string (alternate.c_str ())));
    }
    else if (member ("switch-tag")) {
      const auto selected= prepare_source_position (p * child * 0);
      const string selected_tag= dynamic_tag (t[child]);
      if (!athena::contracts::group_contains (selected->contract_context (), "hidden-tag",
          std::string_view (selected_tag.data (), N(selected_tag)))) continue;
      const bool alternative= member ("alternative-tag");
      const bool unroll= member ("unroll-tag");
      const bool expanded= member ("expanded-tag");
      if (!alternative && !unroll && !expanded)
        throw std::runtime_error ("Hidden switch has no visibility policy: " +
          std::string (tag.data (), N(tag)));
      for (int i=0; i<N(t); ++i) {
        const auto entry= prepare_source_position (p * i * 0);
        const string child_tag= dynamic_tag (t[i]);
        const bool hidden= athena::contracts::group_contains (entry->contract_context (), "hidden-tag",
          std::string_view (child_tag.data (), N(child_tag)));
        const bool show= expanded || (unroll ? i <= child : i == child);
        dynamic_switch_set (t, i, show, hidden, unroll);
      }
    }
  }
}

void
native_dynamic_buffer_command (editor ed, string operation, string mode) {
  if (is_nil (ed)) return;
  tree body= ed->the_buffer ();
  if (is_atomic (body)) return;
  ed->start_editing ();
  if (operation == "operate")
    dynamic_operate_tree (body, mode);
  else if (operation == "filter")
    dynamic_filter_tree (body, mode, false);
  ed->end_editing ();
}
