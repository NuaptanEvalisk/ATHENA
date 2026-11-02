// Native implementation of ATHENA's structural path pattern language.
// Copyright (C) 2002-2004 Joris van der Hoeven, 2026 ATHENA contributors.
// GPL-3.0-or-later.
#include "tree_pattern.hpp"
#include "editor.hpp"
#include "analyze.hpp"
#include <algorithm>
#include <vector>
#include <utility>

namespace {
using bindings= std::vector<std::pair<object, object>>;
using matches= std::vector<bindings>;
bool named (object x, const char* name) { return is_keyword (x) && keyword_name (x) == name; }
object append (object a, object b) {
  std::vector<object> prefix;
  for (; is_pair (a); a= cdr (a)) prefix.push_back (car (a));
  for (auto it= prefix.rbegin (); it!=prefix.rend (); ++it) b= cons (*it, b);
  return b;
}
bool compound_value (object value) {
  return is_pair (value) || (is_tree (value) && is_compound (as_tree (value)));
}
object components (object value) {
  if (!is_tree (value)) return value;
  tree t= as_tree (value);
  if (is_atomic (t)) return object (t->label);
  array<object> parts;
  parts << symbol_object (as_string (L(t)));
  for (int i=0; i<N(t); ++i) parts << object (t[i]);
  return as_list_object (parts);
}
bool content_equal (object a, object b) {
  if (is_tree (a) && is_tree (b)) return as_tree (a) == as_tree (b);
  if (is_tree (a)) return content_equal (components (a), b);
  if (is_tree (b)) return content_equal (a, components (b));
  if (is_pair (a) && is_pair (b)) return content_equal (car (a), car (b)) && content_equal (cdr (a), cdr (b));
  return a == b;
}
bool bind (bindings& env, object name, object value) {
  for (const auto& item: env) if (item.first == name) return item.second == value;
  env.emplace_back (name, value);
  return true;
}
object substitute (object value, const bindings& env) {
  if (!is_pair (value)) return value;
  if (car (value) == symbol_object ("quote") && is_pair (cdr (value)) && is_null (cddr (value))) {
    for (const auto& item: env) if (item.first == cadr (value)) return item.second;
    return value;
  }
  return cons (substitute (car (value), env), substitute (cdr (value), env));
}
void merge_matches (matches& destination, matches source) {
  destination.insert (destination.end (), std::make_move_iterator (source.begin ()), std::make_move_iterator (source.end ()));
}
matches match_sequence (object values, object patterns, const bindings& env);
matches match_each (object values, object patterns, const matches& alternatives) {
  matches result;
  for (const auto& env: alternatives) merge_matches (result, match_sequence (values, patterns, env));
  return result;
}
matches match_sequence (object values, object patterns, const bindings& env) {
  if (is_null (patterns)) return is_null (values) ? matches {env} : matches {};
  object pattern= car (patterns), rest= cdr (patterns);
  if (is_keyword (pattern)) {
    string name= keyword_name (pattern);
    if (name == "*") return {env};
    if (starts (name, "%") && is_int (name (1,N(name)))) {
      int count= as_int (name (1,N(name)));
      if (count<0) return {};
      for (int i=0; i<count; ++i) {
        if (!is_pair (values)) return {};
        values= cdr (values);
      }
      return match_sequence (values, rest, env);
    }
    if (!is_pair (values) || call (name, car (values)) == object (false)) return {};
    return match_sequence (cdr (values), rest, env);
  }
  if (is_pair (pattern)) {
    object operation= car (pattern), args= cdr (pattern);
    if (named (operation, "or")) {
      matches result;
      for (; is_pair (args); args= cdr (args)) {
        matches branch= match_sequence (values, cons (car (args), rest), env);
        if (branch.size () == 1 && branch.front ().empty ()) return branch;
        merge_matches (result, std::move (branch));
      }
      return result;
    }
    if (named (operation, "and")) {
      matches result {env};
      std::vector<object> terms;
      for (; is_pair (args); args= cdr (args)) terms.push_back (car (args));
      for (auto it= terms.rbegin (); it!=terms.rend (); ++it)
        result= match_each (values, cons (*it, rest), result);
      return result;
    }
    if (named (operation, "not")) {
      if (!is_pair (values) || !is_pair (args) || !is_null (cdr (args)) ||
          !match_sequence (values, append (args, rest), env).empty ()) return {};
      return match_sequence (cdr (values), rest, env);
    }
    if (named (operation, "repeat")) {
      if (is_null (args)) return {};
      matches result= match_sequence (values, rest, env);
      if (result.size () == 1 && result.front ().empty ()) return result;
      merge_matches (result, match_sequence (values, append (args, cons (pattern, rest)), env));
      return result;
    }
    if (named (operation, "group")) return match_sequence (values, append (args, rest), env);
    if (named (operation, "quote")) {
      if (!is_pair (values) || !is_pair (args) || !is_null (cdr (args)) || car (values) != car (args)) return {};
      return match_sequence (cdr (values), rest, env);
    }
    if (!is_pair (values)) return {};
    if (operation == symbol_object ("quote")) {
      bindings bound= env;
      return is_pair (args) && bind (bound, car (args), car (values)) ? match_sequence (cdr (values), rest, bound) : matches {};
    }
    object nested= components (car (values));
    if (is_string (nested)) nested= character_list_object (as_string (nested));
    if (!is_list (nested)) return {};
    return match_each (cdr (values), rest, match_sequence (nested, pattern, env));
  }
  if (!is_pair (values) || !content_equal (car (values), pattern)) return {};
  return match_sequence (cdr (values), rest, env);
}
struct selection { object route; object value; bindings env; };
using selections= std::vector<selection>;
void extend (selections& target, selections values) {
  target.insert (target.end (), std::make_move_iterator (values.begin ()), std::make_move_iterator (values.end ()));
}
selections select_list (object value, object patterns, const bindings& env);
selections select_one (object value, object pattern, const bindings& env);
selections continue_selection (const selections& values, object patterns) {
  selections result;
  for (const auto& item: values) {
    auto next= select_list (item.value, patterns, item.env);
    for (auto& child: next) child.route= append (item.route, child.route);
    extend (result, std::move (next));
  }
  return result;
}
bool intersection (const selection& a, const selection& b, selection& result) {
  if (a.route != b.route) return false;
  result= a;
  for (const auto& entry: b.env) if (!bind (result.env, entry.first, entry.second)) return false;
  return true;
}
selections select_one (object value, object pattern, const bindings& env) {
  if (is_pair (pattern)) {
    object operation= car (pattern), args= cdr (pattern);
    if (operation == symbol_object ("quote") && is_pair (args) && is_null (cdr (args))) {
      bindings bound= env;
      return bind (bound, car (args), value) ? selections {{null_object (), value, bound}} : selections {};
    }
    if (named (operation, "group")) return select_list (value, args, env);
    if (named (operation, "match")) {
      selections result;
      for (const auto& bound: match_sequence (list_object (value), args, {}))
        result.push_back ({null_object (), value, bound});
      return result;
    }
    if (named (operation, "replace")) {
      object replacement= substitute (car (args), env);
      return {{list_object (list_object (keyword_object ("replace"), replacement)), replacement, env}};
    }
    if (named (operation, "or")) {
      selections result;
      for (; is_pair (args); args= cdr (args)) extend (result, select_one (value, car (args), env));
      return result;
    }
    if (named (operation, "and") || named (operation, "and-not")) {
      if (!is_pair (args)) return {};
      selections result= select_one (value, car (args), env);
      for (args= cdr (args); is_pair (args); args= cdr (args)) {
        auto other= select_one (value, car (args), env);
        selections combined;
        for (const auto& a: result) {
          bool matched= false;
          for (const auto& b: other) {
            selection joint;
            if (intersection (a,b,joint)) {
              matched= true;
              if (named (operation, "and")) combined.push_back (std::move (joint));
            }
          }
          if (named (operation, "and-not") && !matched) combined.push_back (a);
        }
        result= std::move (combined);
      }
      return result;
    }
    if (!compound_value (value)) return {};
    object children= cdr (components (value));
    selections result;
    int begin=0, end=0;
    const bool range= named (operation, "range");
    if (range) {
      if (!is_pair (args) || !is_pair (cdr (args)) || !is_null (cddr (args))) return {};
      begin= as_int (car (args)); end= as_int (cadr (args));
    }
    else if (!named (operation, "exclude")) return {};
    for (int i=0; is_pair (children); ++i, children= cdr (children)) {
      object child= car (children);
      bool chosen= range && i>=begin && i<end;
      if (!range && compound_value (child)) {
        chosen= true;
        for (object excluded= args; is_pair (excluded); excluded= cdr (excluded))
          if (car (components (child)) == car (excluded)) chosen= false;
      }
      if (chosen) result.push_back ({list_object (object (i)), child, env});
    }
    return result;
  }
  if (is_keyword (pattern) && !named (pattern, "%1")) return select_list (value, list_object (pattern), env);
  if (!compound_value (value)) return {};
  selections result;
  object children= cdr (components (value));
  for (int i=0; is_pair (children); ++i, children= cdr (children)) {
    object child= car (children);
    if (named (pattern, "%1") || (is_int (pattern) && as_int (pattern)==i) ||
        (is_symbol (pattern) && compound_value (child) && car (components (child)) == pattern))
      result.push_back ({list_object (object (i)), child, env});
  }
  return result;
}
selections select_list (object value, object patterns, const bindings& env) {
  if (is_null (patterns)) return {{null_object (), value, env}};
  if (!is_pair (patterns)) return {};
  object pattern= car (patterns), rest= cdr (patterns);
  if (is_keyword (pattern)) {
    const string name= keyword_name (pattern);
    if (name == "%0") return select_list (value, rest, env);
    if (name == "*") {
      auto result= select_list (value, rest, env);
      extend (result, continue_selection (select_one (value, keyword_object ("%1"), env), patterns));
      return result;
    }
    if (starts (name, "%") && is_int (name(1,N(name)))) {
      int depth= as_int (name(1,N(name)));
      if (depth<0) return {};
      return continue_selection (select_one (value, keyword_object ("%1"), env), cons (keyword_object ("%" * as_string (depth-1)), rest));
    }
    if (name == "same" || name == "up" || name == "down" || name == "first" || name == "last" || name == "next" || name == "previous") {
      if (is_tree (value)) {
        tree t= as_tree (value);
        path ip= obtain_ip (t);
        if (ip_attached (ip)) {
          path p= reverse (ip), target= p;
          editor ed= get_current_editor ();
          if (name == "same" || name == "up") {
            if (is_nil (p)) return {};
            if (name == "up") target= path_up (p);
          }
          else if (name == "down") {
            path q= path_up (ed->the_path ());
            if (is_nil (q) || !(p <= path_up (q))) return {};
            target= head (q,N(p)+1);
          }
          else if (name == "first" || name == "last") {
            if (is_atomic (t) || N(t)==0) return {};
            target= p * (name == "first" ? 0 : N(t)-1);
          }
          else {
            if (is_nil (p)) return {};
            int index= last_item (p)+(name == "next" ? 1 : -1);
            tree parent= ed->the_subtree (path_up (p));
            if (index<0 || index>=N(parent)) return {};
            target= path_up (p)*index;
          }
          return select_list (object (ed->the_subtree (target)), rest, env);
        }
      }
      if ((name == "first" || name == "last") && compound_value (value)) {
        object children= cdr (components (value));
        if (!is_pair (children)) return {};
        if (name == "last") while (is_pair (cdr (children))) children= cdr (children);
        return select_list (car (children), rest, env);
      }
      return {};
    }
    // Unknown named grammars have no registrations after retiring Scheme extensions.
    return {};
  }
  return continue_selection (select_one (value, pattern, env), rest);
}
}

object native_tree_select (object source, object pattern) {
  array<object> result;
  for (const auto& selected: select_list (source, pattern, {})) result << selected.value;
  return as_list_object (result);
}
