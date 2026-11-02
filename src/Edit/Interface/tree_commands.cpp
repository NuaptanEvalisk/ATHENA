// Copyright (C) 2002 Joris van der Hoeven, 2026 ATHENA contributors.
// GPL-3.0-or-later.
#include "tree_commands.hpp"
#include "tree_pattern.hpp"
#include "editor.hpp"
#include "native_interfaces.hpp"
#include <stdexcept>
#include <vector>

namespace {
int child_index (tree source, object selector) {
  if (is_int (selector)) return as_int (selector);
  if (selector == keyword_object ("first")) return 0;
  if (selector == keyword_object ("last")) return N(source)-1;
  for (int i=0; i<N(source); ++i)
    if (as_string (L(source[i])) == as_symbol (selector)) return i;
  return -1;
}
object pop_last (object& list) {
  std::vector<object> items;
  for (object p= list; !is_null (p); p= cdr (p)) items.push_back (car (p));
  if (items.empty ()) throw std::runtime_error ("Missing tree operation argument");
  object last= items.back ();
  items.pop_back ();
  list= null_object ();
  for (auto p= items.rbegin (); p != items.rend (); ++p) list= cons (*p, list);
  return last;
}
object source_path (object source) {
  if (!is_tree (source)) return object (false);
  tree t= as_tree (source);
  path ip= obtain_ip (t);
  return ip_attached (ip) ? object (reverse (ip)) : object (false);
}
bool matches (tree t, object predicate) {
  if (is_symbol (predicate)) return as_string (L(t)) == as_symbol (predicate);
  if (!is_list (predicate))
    throw std::invalid_argument ("Tree selector must be a tag or a list of tags");
  for (; !is_null (predicate); predicate= cdr (predicate))
    if (matches (t, car (predicate))) return true;
  return false;
}
tree set_subtree (tree source, object selectors, tree value) {
  if (is_null (selectors))
    return tree_active (source) ? tree_set_diff (source, value) : tree_assign (source, value);
  object selector= car (selectors);
  const bool simple= is_int (selector) || is_symbol (selector) ||
    selector == keyword_object ("first") || selector == keyword_object ("last");
  object target= tree_command_ref (object (source), simple ? list_object (selector) : selectors);
  if (!is_tree (target)) throw std::runtime_error ("tree-set: subtree does not exist");
  tree child= as_tree (target);
  if (!simple) {
    if (!tree_active (source)) throw std::runtime_error ("tree-set: detached selector result");
    return tree_set_diff (child, value);
  }
  if (!is_null (cdr (selectors)) || tree_active (source))
    return set_subtree (child, cdr (selectors), value);
  return tree_set (source, child_index (source, selector), value);
}
}

object tree_command_ref (object source, object selectors) {
  if (!is_tree (source)) return object (false);
  tree t= as_tree (source);
  for (; !is_null (selectors); selectors= cdr (selectors)) {
    object s= car (selectors);
    if (!is_int (s) && !is_symbol (s) && s != keyword_object ("first") && s != keyword_object ("last")) {
      object selected= native_tree_select (object (t), selectors);
      return is_null (selected) ? object (false) : car (selected);
    }
    int index= child_index (t, s);
    if (is_atomic (t) || index < 0 || index >= N(t)) return object (false);
    t= t[index];
  }
  return object (t);
}

object tree_command_path (object source, object selectors) {
  if (is_null (selectors)) return source_path (source);
  object prefix= selectors, last= pop_last (prefix);
  bool start= last == keyword_object ("start"), end= last == keyword_object ("end");
  if (!start && !end && !is_int (last))
    return source_path (tree_command_ref (source, selectors));
  object position= source_path (tree_command_ref (source, prefix));
  if (position == object (false)) return position;
  path p= as_path (position);
  if (is_int (last)) return object (p * as_int (last));
  editor ed= get_current_editor ();
  return start ? ed->source_tree_start (ed->the_root (), p) :
                 ed->source_tree_end (ed->the_root (), p);
}

tree tree_command_set (tree source, object arguments) {
  tree value= content_to_tree (pop_last (arguments));
  return set_subtree (source, arguments, value);
}

tree tree_command_insert (tree source, int index, object value) {
  if (is_string (value)) return tree_insert (source, index, as_string (value));
  if (!is_list (value)) throw std::runtime_error ("tree-insert: expected a string or list");
  tree children (TUPLE);
  for (; !is_null (value); value= cdr (value)) children << content_to_tree (car (value));
  return tree_insert (source, index, children);
}

bool tree_command_is (object source, object selectors) {
  object label= pop_last (selectors), target= tree_command_ref (source, selectors);
  return is_tree (target) && matches (as_tree (target), label);
}
bool tree_command_in (object source, object selectors) {
  return tree_command_is (source, selectors);
}

void tree_command_go_to (object source, object selectors) {
  editor ed= get_current_editor ();
  if (!is_null (selectors)) {
    object prefix= selectors, last= pop_last (prefix);
    if (last == keyword_object ("start") || last == keyword_object ("end")) {
      object target= source_path (tree_command_ref (source, prefix));
      if (target != object (false))
        ed->go_to (last == keyword_object ("start") ?
          ed->command_tree_start (ed->the_root (), as_path (target)) :
          ed->command_tree_end (ed->the_root (), as_path (target)));
      return;
    }
  }
  object target= tree_command_path (source, selectors);
  if (target != object (false)) ed->go_to (as_path (target));
}

void tree_command_select (object source, object selectors) {
  object target= tree_command_ref (source, selectors), position= source_path (target);
  if (position == object (false)) return;
  path p= as_path (position);
  get_current_editor ()->selection_set_paths (p * 0, p * right_index (as_tree (target)));
}
void tree_command_focus (object source, object selectors) {
  object position= source_path (tree_command_ref (source, selectors));
  if (position != object (false)) get_current_editor ()->manual_focus_set (as_path (position));
}

object tree_command_innermost (object predicate, object options) {
  editor ed= get_current_editor ();
  path p= path_up (ed->the_path ());
  if (is_null (options) && !is_nil (p)) p= path_up (p);
  while (ed->the_buffer_path () <= p) {
    tree t= ed->the_subtree (p);
    if (matches (t, predicate)) return object (t);
    if (p == ed->the_buffer_path ()) break;
    p= path_up (p);
  }
  return object (false);
}

object tree_command_outer (tree source) {
  object position= source_path (object (source));
  if (position == object (false)) return position;
  const path p= as_path (position);
  editor ed= get_current_editor ();
  if (p == ed->the_buffer_path () || is_nil (p)) return object (false);
  return object (ed->the_subtree (path_up (p)));
}

object tree_command_down (tree source) {
  object position= source_path (object (source));
  if (position == object (false)) return position;
  const path p= as_path (position);
  editor ed= get_current_editor ();
  const path q= path_up (ed->the_path ());
  if (is_nil (q) || !(p <= path_up (q))) return object (false);
  return object (ed->the_subtree (head (q, N(p)+1)));
}

bool tree_command_cursor_inside (tree source) {
  object position= source_path (object (source));
  if (position == object (false)) return false;
  editor ed= get_current_editor ();
  const path p= path_up (ed->the_path ()), q= as_path (position);
  return q <= p && (is_atomic (ed->the_subtree (p)) || N(p)>N(q));
}
