
/******************************************************************************
* MODULE     : edit_search.cpp
* DESCRIPTION: search and query replace
* COPYRIGHT  : (C) 1999  Joris van der Hoeven
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "Editor/Selection/edit_replace.hpp"
#include "Editor/Commands/edit_interface.hpp"
#include "Document/NodeContracts/execution_state.hpp"
#include "Document/NodeContracts/editing_query.hpp"
#include "analyze.hpp"
#include "App/Runtime/tm_data.hpp"
#include "tree_search.hpp"
#include "Typesetting/Env/local_environment.hpp"
#include "utf8_edit.hpp"
#include "unicode_text.hpp"
#include "Domains/Knowledge/enunciation_model.hpp"
#include "Document/NodeContracts/builtin_contracts.hpp"
#include "Document/Tree/cursor_navigation.hpp"
#include <algorithm>
#include <stdexcept>
#include <vector>

/******************************************************************************
* Constructor and destructor
******************************************************************************/

edit_replace_rep::edit_replace_rep ():
  document_search_index (0) {}
edit_replace_rep::~edit_replace_rep () {}

/******************************************************************************
* Structural search routines
******************************************************************************/

bool
edit_replace_rep::inside (string what) {
  return inside (make_tree_label (what));
}

path
edit_replace_rep::search_upwards (string what) {
  return search_upwards (make_tree_label (what));
}

bool
edit_replace_rep::inside (tree_label l) {
  return !is_nil (search_upwards (l));
}

path
edit_replace_rep::search_upwards (tree_label l) {
  path p= path_up (tp, 2);
  while (!is_func (subtree (et, p), l)) {
    if (p == rp) return path ();
    p= path_up (p);
  }
  return p;
}

path
edit_replace_rep::search_parent_upwards (tree_label l, int& last) {
  path p= path_up (tp);
  while (!is_func (subtree (et, path_up (p)), l)) {
    p= path_up (p);
    if (p == rp) {
      last= -1;
      return path ();
    }
  }
  last= last_item (p);
  return path_up (p);
}

path
edit_replace_rep::search_parent_upwards (tree_label l) {
  int last;
  path p= search_parent_upwards (l, last);
  return p * last;
}

bool
edit_replace_rep::inside_with (string var, string val) {
  return !is_nil (search_upwards_with (var, val));
}

path
edit_replace_rep::search_upwards_with (string var, string val) {
  path p= path_up (tp);
  while (true) {
    p= path_up (p);
    if (p == rp) return path ();
    tree st= subtree (et, p);
    if (is_func (st, WITH) && (st[0] == var) && (st[1] == val)) return p;
  }
}

string
edit_replace_rep::inside_which (tree t) {
  path p= search_upwards_in_set (t);
  if ((p == rp) || is_nil (p)) return "";
  tree st= subtree (et, p);
  if (is_func (st, COMPOUND)) return as_string (st[0]);
  else return as_string (L(st));
}

path
edit_replace_rep::search_upwards_in_set (tree t) {
  if (!is_tuple (t)) return path ();
  int i, n=N(t);
  path p= path_up (tp);
  while (true) {
    p= path_up (p);
    if (p == rp) return path ();
    tree st= subtree (et, p);
    for (i=0; i<n; i++) {
      if (is_atomic (t[i])) {
        string s= t[i]->label;
        if (is_quoted (s)) s= raw_unquote (s);
        if (athena::contracts::builtin_label (s)) {
          tree_label l= as_tree_label (s);
          if (is_func (st, l)) return p;
        }
        else if (is_compound (st, s)) return p;
      }
      else if (is_func (st, L(t))) return p;
    }
  }
}

path
edit_replace_rep::search_previous_compound (path init, string which) {
  path p= init;
  source_search_preparation preparation;
  while (true) {
    if (p == rp) return init;
    if (last_item (p) == 0) p= path_up (p);
    else {
      p= path_dec (p);
      while (true) {
        tree st= subtree (et, p);
        if (arity (st) == 0) break;
        p= p * (N(st)-1);
      }
    }
    if (is_compound (subtree (et, p), which) && search_source_accessible (p, preparation))
      return p;
  }
}

path
edit_replace_rep::search_next_compound (path init, string which) {
  path p= init;
  source_search_preparation preparation;
  while (true) {
    if (p == rp) return init;
    if (last_item (p) == (N (subtree (et, path_up (p))) - 1)) p= path_up (p);
    else {
      p= path_inc (p);
      while (true) {
        tree st= subtree (et, p);
        if (arity (st) == 0) break;
        p= p * 0;
      }
    }
    if (is_compound (subtree (et, p), which) && search_source_accessible (p, preparation))
      return p;
  }
}

/******************************************************************************
* Test whether we found a match
******************************************************************************/

static bool
test_match (tree t, tree pat) {
  // FIXME: we use empty strings for wildcards now
  // we should support real wildcards and regular expressions later
  if (pat == "") return true;
  else if (is_atomic (t) || is_atomic (pat)) return t == pat;
  else if ((L(t) != L(pat)) || (N(t) != N(pat))) return false;
  else {
    int i, n= N(t);
    for (i=0; i<n; i++)
      if (!test_match (t[i], pat[i]))
        return false;
    return true;
  }
}

path
edit_replace_rep::test_sub (path p, tree t) {
  // cout << "Test " << subtree (et, path_up (p))
  //      << " :: " << t << " at " << p << "\n";
  if (is_concat (t) && (N(t) > 1)) {
    if (N(p) <= 1) return p;
    tree st= subtree (et, path_up (p, 2));
    if (!is_concat (st)) return p;
    int i, l= last_item (path_up (p)), n= N(t);
    if (N(st) < (n + l)) return p;
    const path first= path_up (p);
    const auto entry= prepare_source_position (first * 0);
    const path first_end= prepare_command_boundary (first, first, false, *entry);
    if ((t[0] != "") && (p == first_end)) return p;
    if (test_sub (p, t[0]) != first_end) return p;
    for (i=1; i<n-1; i++)
      if (!test_match (st[l+i], t[i]))
        return p;
    const path last= path_up (p, 2) * (l+n-1);
    const auto last_entry= prepare_source_position (last * 0);
    path r= prepare_command_boundary (last, last, true, *last_entry);
    path q= test_sub (r, t[n-1]);
    if (q == r) return p;
    return q;
  }
  else {
    tree st= subtree (et, path_up (p));
    if (is_compound (t)) {
      if (!is_compound (st)) return p;
      // cout << "Test with " << st << "\n";
      if (last_item (p) != 0) return p;
      if (!test_match (st, t)) return p;
      return path_inc (p);
    }
    else {
      if (is_compound (st)) return p;
      int l= last_item (p);
      // cout << "Test with " << st->label << " at " << l << "\n";
      if (N(st->label) < (N(t->label) + l)) return p;
      if (st->label (l, l + N(t->label)) != t->label) return p;
      if (!utf8_grapheme_boundary (st->label, l) ||
          !utf8_grapheme_boundary (st->label, l + N(t->label))) return p;
      return path_add (p, N (t->label));
    }
  }
}

std::shared_ptr<const evaluation_environment_checkpoint>
edit_replace_rep::prepare_search_target (path p, source_search_preparation& preparation) {
  const path leaf= path_up (p);
  if (preparation.selected && preparation.leaf == leaf && is_atomic (subtree (et, leaf)))
    return preparation.selected;
  auto context= prepare_source_position (p, preparation.root);
  preparation.leaf= leaf;
  preparation.selected= context;
  return context;
}

void
edit_replace_rep::source_mode_language (
    const evaluation_environment_checkpoint& context, string& mode, string& language) {
  mode= as_string (context.effective_value (MODE));
  language= as_string (context.effective_value (MODE_LANGUAGE (mode)));
}

std::shared_ptr<const evaluation_environment_checkpoint>
edit_replace_rep::prepare_search_entry (path source, source_search_preparation& preparation) {
  if (!preparation.traversal || preparation.traversal_source != source) {
    preparation.traversal= prepare_search_target (source * 0, preparation);
    preparation.traversal_source= source;
  }
  return preparation.traversal;
}

bool
edit_replace_rep::search_source_accessible (path source, int child,
    athena::contracts::access_policy access, source_search_preparation& preparation) {
  const tree node= subtree (et, source);
  if (is_atomic (node) || child < 0 || child >= N(node)) return false;
  if (access == athena::contracts::access_policy::source) return true;
  const auto context= prepare_search_entry (source, preparation);
  return athena::contracts::editing_query {
    context->contract_context (), access}.accessible (node, child);
}

bool
edit_replace_rep::search_source_accessible (path source, source_search_preparation& preparation) {
  path parent= rp;
  for (path relative= source / rp; !is_nil (relative); relative= relative->next) {
    if (!search_source_accessible (parent, relative->item,
          athena::contracts::access_policy::normal, preparation)) return false;
    parent= parent * relative->item;
  }
  return true;
}

/******************************************************************************
* Traversal of the edit tree
******************************************************************************/

void
edit_replace_rep::step_ascend (bool forward, athena::contracts::access_policy access,
                              source_search_preparation& preparation) {
  // cout << "Step ascend at " << search_at << "\n";
  search_at= path_add (path_up (search_at), forward? 1: -1);
  tree st;
  int  l;
  while (true) {
    st= subtree (et, path_up (search_at));
    l = last_item (search_at);
    // cout << "  st= " << st << "\n";
    // cout << "  l = " << l << "\n";
    if ((l<0) || (l>=N(st)) ||
        search_source_accessible (path_up (search_at), l, access, preparation)) break;
    search_at= path_add (search_at, forward? 1: -1);
  }

  if (forward) {
    if (l == N(st)) {
      if (is_atom (search_at / rp)) search_at= rp;
      else step_ascend (forward, access, preparation);
    }
    else step_descend (forward);
  }
  else {
    if (l == -1) {
      if (is_atom (search_at / rp)) search_at= rp;
      else step_ascend (forward, access, preparation);
    }
    else step_descend (forward);
  }
}

void
edit_replace_rep::step_descend (bool forward) {
  // cout << "Step descend at " << search_at << "\n";
  tree st= subtree (et, search_at);
  // cout << "  st= " << st << "\n";
  int last= (is_atomic (st)? N(st->label): N(st)-1);
  search_at= search_at * (forward? 0: last);
  if (is_format (st))
    step_descend (forward);
}

void
edit_replace_rep::step_horizontal (bool forward, athena::contracts::access_policy access) {
  source_search_preparation preparation;
  step_horizontal (forward, access, preparation);
}

void
edit_replace_rep::step_horizontal (bool forward, athena::contracts::access_policy access,
                                  source_search_preparation& preparation) {
  // cout << "Step horizontal at " << search_at << "\n";
  if (search_at == rp) step_descend (forward);
  else {
    tree st  = subtree (et, path_up (search_at));
    int  l   = last_item (search_at);

    // cout << "  st= " << st << "\n";
    if (forward) {
      if (l == right_index (st)) step_ascend (forward, access, preparation);
      else {
        if (is_atomic (st)) {
          search_at= path_up (search_at) * utf8_grapheme_next (st->label, l);
        }
        else {
          int i;
          for (i=l; i<N(st); i++)
            if (search_source_accessible (path_up (search_at), i, access, preparation)) {
              search_at= path_up (search_at) * i;
              step_descend (forward);
              return;
            }
          step_ascend (forward, access, preparation);
        }
      }
    }
    else {
      if (l == 0) step_ascend (forward, access, preparation);
      else {
        if (is_atomic (st)) {
          search_at= path_up (search_at) * utf8_grapheme_previous (st->label, l);
        }
        else {
          int i;
          for (i=l; i>=0; i--)
            if (search_source_accessible (path_up (search_at), i, access, preparation)) {
              search_at= path_up (search_at) * i;
              step_descend (forward);
              return;
            }
          step_ascend (forward, access, preparation);
        }
      }
    }
  }
}

void
edit_replace_rep::next_match (bool forward) {
  // cout << "Next match at " << search_at << "\n";
  source_search_preparation preparation;
  while (true) {
    if (search_at == rp) {
      set_selection (tp, tp);
      notify_change (THE_SELECTION);
      return;
    }
    search_end= test_sub (search_at, search_what);
    if (search_end != search_at) {
      const auto context= prepare_search_target (search_at, preparation);
      string mode, language;
      source_mode_language (*context, mode, language);
      if (search_mode == mode && search_lan == language) {
        go_to (copy (search_end));
        set_selection (search_at, search_end);
        notify_change (THE_SELECTION);
        return;
      }
    }
    auto access= athena::contracts::access_policy::hidden;
    if (get_init_string (MODE) == "src" || inside ("show-preamble"))
      access= athena::contracts::access_policy::source;
    step_horizontal (forward, access, preparation);
  }
}

/******************************************************************************
* Searching
******************************************************************************/

void
edit_replace_rep::search_start (bool flag) {
  string r ("forward search");
  if (!flag) r= "backward search";

  source_search_preparation preparation;
  const auto context= prepare_search_target (tp, preparation);
  source_mode_language (*context, search_mode, search_lan);
  forward     = flag;
  search_at   = tp;
  search_what = tree ("");
  where_stack = list<path> ();
  what_stack  = tree ("");
  set_input_mode (INPUT_SEARCH);
  set_message ("Searching", r);
}

void
edit_replace_rep::search_next (bool forward) {
  string r ("forward search");
  if (!forward) r= "backward search";
  string w= as_string (search_what);
  if (is_compound (search_what)) w= "compound expression";

  next_match (forward);
  if (search_at == rp) {
    set_message (concat ("No more matches for ", verbatim (w)), r);
    beep ();
  }
  else set_message (concat ("Searching ", verbatim (w)), r);
}

void
edit_replace_rep::search_next (tree what, bool forward, bool step) {
  where_stack= list<path> (copy (search_at), where_stack);
  what_stack = tuple (copy (search_what), what_stack);
  search_what= copy (what);
  if (step) step_horizontal (forward);
  search_next (forward);
}

void
edit_replace_rep::search_stop () {
  tree t= tuple ("texmacs", search_what, search_mode, search_lan);
  set_input_normal ();
  if (search_what != "")
    selection_raw_set ("search", t);
}

void
edit_replace_rep::search_button_next () {
  set_input_mode (INPUT_SEARCH);
  search_next (search_what, forward, true);
}

bool
edit_replace_rep::search_keypress (string s) {
  set_message ("", "");
  if (s == "space") s= " ";
  if (N(s) > 0 && athena::text::valid_utf8 ({s.data (), std::size_t (N(s))}) &&
      utf8_grapheme_next (s, 0) == N(s)) {
    if (is_atomic (search_what))
      search_next (as_string (search_what) * s, forward, false);
  }
  else {
    if ((s == "left") || (s == "right") ||
        (s == "up") || (s == "down") ||
        (s == "pageup") || (s == "pagedown") ||
        (s == "begin") || (s == "end")) {
      search_stop ();
      return false;      
    }
    else if ((s == "C-c") || (s == "C-g"))
      search_stop ();
    else if ((s == "next") || (s == "previous")) {
      if (search_what == "") {
        tree t= selection_raw_get ("search");
        if (is_tuple (t, "texmacs", 3) &&
            (t[1] != "") &&
            (t[2] == search_mode) &&
            (t[3] == search_lan))
          search_next (t[1], s != "previous", true);
      }
      else search_next (search_what, s != "previous", true);
    }
    else if ((s == "delete") || (s == "backspace")) {
      if (is_nil (where_stack))
        search_stop ();
      else if (is_atom (where_stack)) {
        go_to (where_stack->item);
        search_stop ();
      }
      else {
        search_at  = where_stack->item;
        where_stack= where_stack->next;
        search_what= what_stack[0];
        what_stack = what_stack[1];
        search_next (forward);
      }
    }
    else if ((s == "C-left") || (s == "C-right")) {
      // FIXME: integrate better with general searching mechanism
      path p= path_up (search_at);
      while ((p != rp) && (!is_extension (subtree (et, path_up (p)))))
        p= path_up (p);
      if (p == rp) return true;
      path r= path_up (p);
      string w= as_string (L (subtree (et, r)));
      path q= (s == "C-right") ?
        search_next_compound (r, w) :
        search_previous_compound (r, w);
      if (q == r) {
        set_message ("No more matches", "search similar structure");
        beep ();
      }
      else {
        q= q * min (N (subtree (et, q)) - 1, last_item (p));
        const auto entry= prepare_source_position (q * 0);
        search_at= prepare_command_boundary (q, q, false, *entry);
        go_to (copy (search_at));
      }
    }
  }
  return true;
}

/******************************************************************************
* Native document search
******************************************************************************/

void
edit_replace_rep::document_search_select (int index) {
  const auto& hit= document_search_hits[index];
  selection_cancel ();
  if (hit.field == "") {
    const auto selected= prepare_source_position (hit.begin);
    with_selection_context ([&] {
      selection_set_range_set (range_set (hit.begin, hit.end));
    }, selected);
    go_to (copy (hit.begin));
  }
  else {
    // Metadata has no editable body path. Navigate to its owner without
    // manufacturing a body selection that typing could accidentally replace.
    go_to (copy (hit.begin));
    set_message ("Match in " * hit.field * " " * as_string (hit.field_index + 1),
                 "Search");
  }
  document_search_index= index + 1;
  document_search_reference= copy (tp);
}

int
edit_replace_rep::document_search (tree what, bool case_insensitive) {
  document_search_hit previous;
  const bool retain= what == document_search_query &&
    case_insensitive == document_search_ignore_case && document_search_index > 0 &&
    document_search_index <= (int) document_search_hits.size () &&
    tp == document_search_reference;
  if (retain) previous= document_search_hits[document_search_index - 1];
  document_search_hits.clear ();
  document_search_query= what;
  document_search_ignore_case= case_insensitive;
  document_search_reference= copy (tp);

  selection_cancel ();
  if (is_empty (what)) {
    cancel_alt_selection ("alternate");
    document_search_sels= range_set ();
    document_search_index= 0;
    go_to (copy (document_search_reference));
    return 0;
  }

  source_search_preparation preparation;
  const athena::contracts::source_contract_preparation prepare= [&] (path source) {
    const auto context= prepare_search_entry (source, preparation);
    return std::shared_ptr<const athena::contracts::contract_scope> (
      context, &context->contract_context ());
  };
  document_search_sels= search (prepare, athena::contracts::access_policy::normal,
    subtree (et, rp), what, rp, case_insensitive, 1000000);
  for (int i=0; i+1<N(document_search_sels); i+=2)
    document_search_hits.push_back ({document_search_sels[i], document_search_sels[i+1]});
  const auto collect_fields= [&] (auto&& visit, tree source, path p) -> void {
    if (athena::enunciation::is_enunciation (source) &&
        search_source_accessible (p, preparation)) {
      for (const char* field: {"name", "year", "attribution"}) {
        const auto values= athena::enunciation::title_fields (source, field);
        for (int i=0; i<(int) values.size (); ++i) {
          const auto matches= search (
            {athena::contracts::builtin_contracts (), athena::contracts::access_policy::normal},
            values[i], what, path (), case_insensitive, 1000000);
          if (N(matches) == 0) continue;
          const auto context= prepare_search_entry (p, preparation);
          const path anchor= p * cursor_navigation ({context->contract_context (),
            athena::contracts::access_policy::normal}).start (source);
          for (int j=0; j+1<N(matches); j+=2)
            document_search_hits.push_back ({anchor, anchor, field, i, matches[j], matches[j+1]});
        }
      }
    }
    for (int i=0; i<N(source); ++i)
      visit (visit, source[i], p * i);
  };
  collect_fields (collect_fields, subtree (et, rp), rp);
  std::stable_sort (document_search_hits.begin (), document_search_hits.end (),
    [] (const document_search_hit& a, const document_search_hit& b) {
      if (a.begin == b.begin) return a.field != "" && b.field == "";
      return path_less (a.begin, b.begin);
    });
  set_alt_selection ("alternate", document_search_sels);
  document_search_index= 0;
  if (document_search_hits.empty ()) {
    go_to (copy (document_search_reference));
    return 0;
  }

  int selected= 0;
  for (int i=0; i<(int) document_search_hits.size (); ++i)
    if (path_less_eq (document_search_reference, document_search_hits[i].end)) {
      selected= i; break;
    }
  if (retain)
    for (int i=0; i<(int) document_search_hits.size (); ++i) {
      const auto& hit= document_search_hits[i];
      if (hit.begin == previous.begin && hit.end == previous.end &&
          hit.field == previous.field && hit.field_index == previous.field_index &&
          hit.field_begin == previous.field_begin && hit.field_end == previous.field_end) {
        selected= i; break;
      }
    }
  document_search_select (selected);
  return document_search_hits.size ();
}

bool
edit_replace_rep::document_search_navigate (bool forward, bool extreme) {
  document_search (document_search_query, document_search_ignore_case);
  const int count= document_search_hits.size ();
  if (count == 0) return false;
  const int index= extreme ? (forward ? count - 1 : 0)
    : (document_search_index - 1 + (forward ? 1 : count - 1)) % count;
  document_search_select (index);
  return true;
}

void
edit_replace_rep::document_search_clear () {
  cancel_alt_selection ("alternate");
  selection_cancel ();
  document_search_sels= range_set ();
  document_search_hits.clear ();
  document_search_index= 0;
  document_search_reference= path ();
  document_search_query= tree ("");
  set_message ("", "");
  set_center_message ("");
}

int
edit_replace_rep::document_search_current () {
  return document_search_index;
}

int
edit_replace_rep::document_search_total () {
  return document_search_hits.size ();
}

int
edit_replace_rep::document_replace (tree by, bool all) {
  if (buf != nullptr && buf->read_only) return -1;
  if (is_empty (document_search_query)) return 0;
  document_search (document_search_query, document_search_ignore_case);
  if (document_search_index == 0) return 0;

  const auto& current= document_search_hits[document_search_index - 1];
  if ((!all && current.field != "") || (all &&
      std::any_of (document_search_hits.begin (), document_search_hits.end (),
        [] (const document_search_hit& hit) { return hit.field != ""; }))) {
    set_message ("Edit structured field matches in their title slots", "Replace");
    return -3;
  }
  range_set hits= all ? document_search_sels : range_set (current.begin, current.end);
  source_search_preparation preparation;
  std::vector<std::shared_ptr<const evaluation_environment_checkpoint>> contexts;
  contexts.reserve (N(hits) / 2);
  for (int i=0; i<N(hits); i+=2)
    contexts.push_back (prepare_search_target (hits[i], preparation));
  // Track boundaries because cutting a match may normalize adjacent concats.
  // Process the original matches backwards; inserted text is never searched
  // again during this replacement operation.
  array<observer> positions;
  for (int i=0; i<N(hits); ++i)
    positions << position_new (hits[i]);
  start_editing ();
  try {
    for (int i=N(positions)-2; i>=0; i-=2) {
      path p= position_get (positions[i]);
      path q= position_get (positions[i+1]);
      with_selection_context ([&] {
        selection_set_range_set (range_set (p, q));
        selection_cut ("none");
        insert_tree (copy (by));
      }, contexts[i / 2]);
    }
    end_editing ();
  }
  catch (...) {
    for (int i=0; i<N(positions); ++i) position_delete (positions[i]);
    cancel_editing ();
    throw;
  }
  for (int i=0; i<N(positions); ++i) position_delete (positions[i]);
  document_search_reference= copy (tp);
  document_search (document_search_query, document_search_ignore_case);
  return N(hits) / 2;
}
