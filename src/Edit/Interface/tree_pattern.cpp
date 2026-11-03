// Native structural selection for style markup.
// Copyright (C) 2002-2004 Joris van der Hoeven, 2026 ATHENA contributors.
// GPL-3.0-or-later.
#include "tree_pattern.hpp"
#include "editor.hpp"
#include <algorithm>
#include <optional>
#include <utility>

namespace {
using kind= tree_selector::kind;
using bindings= std::vector<std::pair<string, tree>>;
struct selection { std::vector<int> route; tree value; bindings bound; };
using selections= std::vector<selection>;

bool bind (bindings& bound, string name, tree value) {
  for (const auto& entry: bound)
    if (entry.first == name) return entry.second == value;
  bound.emplace_back (name, value);
  return true;
}
void extend (selections& target, selections values) {
  target.insert (target.end (), std::make_move_iterator (values.begin ()),
                 std::make_move_iterator (values.end ()));
}
bool intersect (const selection& left, const selection& right, selection& joint) {
  if (left.route != right.route) return false;
  joint= left;
  for (const auto& entry: right.bound)
    if (!bind (joint.bound, entry.first, entry.second)) return false;
  return true;
}
std::optional<tree> navigate (tree value, string name) {
  const path ip= obtain_ip (value);
  if (ip_attached (ip)) {
    const path p= reverse (ip);
    path target= p;
    editor ed= get_current_editor ();
    if (name == "same" || name == "up") {
      if (is_nil (p)) return std::nullopt;
      if (name == "up") target= path_up (p);
    }
    else if (name == "down") {
      const path q= path_up (ed->the_path ());
      if (is_nil (q) || !(p <= path_up (q))) return std::nullopt;
      target= head (q, N(p)+1);
    }
    else if (name == "first" || name == "last") {
      if (is_atomic (value) || N(value) == 0) return std::nullopt;
      target= p * (name == "first" ? 0 : N(value)-1);
    }
    else {
      if (is_nil (p)) return std::nullopt;
      const int index= last_item (p) + (name == "next" ? 1 : -1);
      const tree parent= ed->the_subtree (path_up (p));
      if (index < 0 || index >= N(parent)) return std::nullopt;
      target= path_up (p) * index;
    }
    return ed->the_subtree (target);
  }
  if ((name == "first" || name == "last") && is_compound (value) && N(value))
    return value[name == "first" ? 0 : N(value)-1];
  return std::nullopt;
}

selections select_list (tree, const std::vector<tree_selector>&, std::size_t,
                        const bindings&);
selections select_one (tree, const tree_selector&, const bindings&);
selections continue_selection (const selections& values,
                                const std::vector<tree_selector>& patterns,
                                std::size_t offset) {
  selections result;
  for (const auto& value: values) {
    auto next= select_list (value.value, patterns, offset, value.bound);
    for (auto& child: next)
      child.route.insert (child.route.begin (), value.route.begin (), value.route.end ());
    extend (result, std::move (next));
  }
  return result;
}

selections children (tree value, const bindings& bound) {
  selections result;
  for (int i=0; i<N(value); ++i) result.push_back ({{i}, value[i], bound});
  return result;
}

selections select_one (tree value, const tree_selector& pattern, const bindings& bound) {
  if (pattern.operation == kind::capture) {
    bindings next= bound;
    if (bind (next, pattern.name, value)) return {{{}, value, std::move (next)}};
    return {};
  }
  if (pattern.operation == kind::sequence)
    return select_list (value, pattern.branches, 0, bound);
  if (pattern.operation == kind::unite) {
    selections result;
    for (const auto& branch: pattern.branches)
      extend (result, select_one (value, branch, bound));
    return result;
  }
  if (pattern.operation == kind::intersect || pattern.operation == kind::subtract) {
    if (pattern.branches.empty ()) return {};
    auto result= select_one (value, pattern.branches.front (), bound);
    for (std::size_t i=1; i<pattern.branches.size (); ++i) {
      const auto other= select_one (value, pattern.branches[i], bound);
      selections combined;
      for (const auto& left: result) {
        bool matched= false;
        for (const auto& right: other) {
          selection joint;
          if (intersect (left, right, joint)) {
            matched= true;
            if (pattern.operation == kind::intersect) combined.push_back (std::move (joint));
          }
        }
        if (pattern.operation == kind::subtract && !matched) combined.push_back (left);
      }
      result= std::move (combined);
    }
    return result;
  }
  if (pattern.operation == kind::navigate || pattern.operation == kind::descendants ||
      (pattern.operation == kind::depth && pattern.count != 1))
    return select_list (value, {pattern}, 0, bound);
  selections result;
  for (int i=0; i<N(value); ++i) {
    if ((pattern.operation == kind::depth && pattern.count == 1) ||
        (pattern.operation == kind::index && pattern.count == i) ||
        (pattern.operation == kind::tag && is_compound (value[i]) &&
         as_string (L(value[i])) == pattern.name))
      result.push_back ({{i}, value[i], bound});
  }
  return result;
}

selections select_list (tree value, const std::vector<tree_selector>& patterns,
                        std::size_t offset, const bindings& bound) {
  if (offset == patterns.size ()) return {{{}, value, bound}};
  const auto& pattern= patterns[offset];
  if (pattern.operation == kind::depth) {
    if (pattern.count < 0) return {};
    if (pattern.count == 0) return select_list (value, patterns, offset+1, bound);
    auto rest= std::vector<tree_selector> (patterns.begin ()+offset, patterns.end ());
    --rest[0].count;
    return continue_selection (children (value, bound), rest, 0);
  }
  if (pattern.operation == kind::descendants) {
    auto result= select_list (value, patterns, offset+1, bound);
    extend (result, continue_selection (children (value, bound), patterns, offset));
    return result;
  }
  if (pattern.operation == kind::navigate) {
    const auto target= navigate (value, pattern.name);
    return target ? select_list (*target, patterns, offset+1, bound) : selections {};
  }
  return continue_selection (select_one (value, pattern, bound), patterns, offset+1);
}
}

array<tree> native_tree_select (tree source, const std::vector<tree_selector>& selectors) {
  array<tree> result;
  for (const auto& selected: select_list (source, selectors, 0, {}))
    result << selected.value;
  return result;
}
