/******************************************************************************
* MODULE     : native_tree_diff.cpp
* DESCRIPTION: Owner-local exact tree updates preserving native edit observers
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "native_interfaces.hpp"
#include "node_metadata.hpp"
#include "unicode_text.hpp"
#include "editor.hpp"
#include "new_view.hpp"

namespace {

bool inside_source (tree target, path source) {
  path ip= obtain_ip (target);
  return ip_attached (ip) && source <= reverse (ip);
}

bool contains_source (tree target, path source) {
  if (inside_source (target, source)) return true;
  if (is_atomic (target)) return false;
  for (int i= 0; i < N(target); ++i)
    if (contains_source (target[i], source)) return true;
  return false;
}

tree finish_header (tree source, const tree& header) {
  if (is_compound (source) && L(source) != L(header))
    source= tree_assign_node (source, L(header));
  if (!athena::node::equal_metadata (source, header))
    ::apply (source, mod_set_metadata (path (), header));
  return source;
}

tree update_text (tree source, const tree& target, const tree& header) {
  const string before= source->label, after= target->label;
  const std::string_view a (before.data (), N(before)), b (after.data (), N(after));
  athena::text::grapheme_cursor left (a), right (b);
  std::size_t prefix= 0;
  while (prefix < a.size () && prefix < b.size ()) {
    const auto x= left.next (prefix), y= right.next (prefix);
    if (a.substr (prefix, x-prefix) != b.substr (prefix, y-prefix)) break;
    prefix= x;
  }
  std::size_t a_end= a.size (), b_end= b.size ();
  while (a_end > prefix && b_end > prefix) {
    const auto x= left.previous (a_end), y= right.previous (b_end);
    if (a.substr (x, a_end-x) != b.substr (y, b_end-y)) break;
    a_end= x; b_end= y;
  }
  if (a_end > prefix) source= tree_remove (source, prefix, a_end-prefix);
  if (b_end > prefix) source= tree_insert (source, prefix, after (prefix, b_end));
  return finish_header (source, header);
}

tree update_tree (tree source, tree target) {
  if (source == target) return source;
  const path position= reverse (obtain_ip (source));
  const tree header= node_header (target);
  if (!admits_edit_observer (source) && inside_source (target, position)) {
    // Unwrapping an actual source descendant keeps its observers. The target
    // is still live, so obtain its path again after each removed ancestor.
    const path descendant= reverse (obtain_ip (target)) / position;
    if (!is_nil (descendant)) {
      // Unlike normalization, this is an explicit replacement by a selected
      // descendant. Record its exact header so undo restores both identities.
      const int child= descendant->item;
      ::apply (source, mod_remove_node (path (), child, node_header (source[child])));
      return update_tree (source, target);
    }
  }
  if (is_atomic (source) && is_atomic (target))
    return update_text (source, target, header);

  if (is_compound (source) && is_compound (target)) {
    int left= 0, right= 0;
    while (left < N(source) && left < N(target) && source[left] == target[left]) ++left;
    while (right < N(source)-left && right < N(target)-left &&
           source[N(source)-right-1] == target[N(target)-right-1]) ++right;
    if (left == N(source) && left == N(target)) return finish_header (source, header);
    if (left + right == N(source) && N(source) < N(target)) {
      tree added (TUPLE, N(target)-left-right);
      for (int i= 0; i < N(added); ++i) added[i]= target[left+i];
      source= tree_insert (source, left, added);
      return finish_header (source, header);
    }
    if (left + right == N(target) && N(source) > N(target) &&
        !admits_edit_observer (source)) {
      source= tree_remove (source, left, N(source)-N(target));
      return finish_header (source, header);
    }
  }

  if (is_compound (target) && !admits_edit_observer (source))
    for (int i= 0; i < N(target); ++i)
      if (contains_source (target[i], position)) {
        tree wrapper (L(target), N(target)-1);
        athena::node::copy_metadata (target, wrapper);
        for (int j= 0; j < i; ++j) wrapper[j]= target[j];
        for (int j= i+1; j < N(target); ++j) wrapper[j-1]= target[j];
        source= update_tree (source, target[i]);
        return tree_insert_node (source, i, wrapper);
      }
  return tree_assign (source, target);
}

} // namespace

tree tree_set_diff (tree source, tree target) {
  const path ip= obtain_ip (source);
  ASSERT (ip_attached (ip), "tree-set-diff requires an attached source");
  editor ed= get_current_editor ();
  ASSERT (ed->the_buffer_path () <= reverse (ip) &&
          strong_equal (ed->the_subtree (reverse (ip)), source),
          "tree-set-diff requires the source editor owner");
  return update_tree (source, target);
}
