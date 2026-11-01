// Copyright (C) 2001 Joris van der Hoeven, 2026 ATHENA contributors.
// GPL-3.0-or-later.
#include "native_text_edit.hpp"
#include "native_interfaces.hpp"
#include "editor.hpp"
#include "Data/NodeContracts/node_groups.hpp"

bool text_node_in_group (tree t, const char* group) {
  const string tag= as_string (L(t));
  return athena::contracts::standard_node_groups ().contains (
    group, std::string (tag.data (), N(tag)));
}

string text_algorithm_root (string tag) {
  if (ends (tag, "*")) return text_algorithm_root (tag (0, N(tag)-1));
  if (starts (tag, "specified-")) return text_algorithm_root (tag (10, N(tag)));
  if (starts (tag, "named-")) return text_algorithm_root (tag (6, N(tag)));
  return tag;
}

bool text_algorithm_numbered (tree t) {
  const string tag= as_string (L(t)), root= text_algorithm_root (tag);
  return tag == root || tag == "specified-" * root;
}
bool text_algorithm_named (tree t) { return starts (as_string (L(t)), "named-"); }
bool text_algorithm_specified (tree t) {
  const string tag= as_string (L(t));
  return starts (tag, "specified-") || starts (tag, "named-specified-");
}

void text_algorithm_toggle_number (tree t) {
  string tag= text_algorithm_root (as_string (L(t)));
  if (text_algorithm_specified (t)) tag= "specified-" * tag;
  if (text_algorithm_numbered (t)) tag << "*";
  call ("variant-set", object (t), symbol_object (tag));
}

void text_algorithm_toggle_name (tree t) {
  const bool named= text_algorithm_named (t);
  string tag= text_algorithm_root (as_string (L(t)));
  if (text_algorithm_specified (t)) tag= "specified-" * tag;
  if (!named) tag= "named-" * tag;
  t= tree_assign_node (t, as_tree_label (tag));
  if (named) tree_remove (t, 0, 1);
  else {
    t= tree_insert (t, 0, tree (TUPLE, ""));
    get_current_editor ()->go_to_start (reverse (obtain_ip (t)) * 0);
  }
}

void text_algorithm_toggle_specification (tree t) {
  const bool specified= text_algorithm_specified (t);
  const bool named= text_algorithm_named (t);
  const bool numbered= text_algorithm_numbered (t);
  string tag= text_algorithm_root (as_string (L(t)));
  if (!specified) tag= "specified-" * tag;
  if (named) tag= "named-" * tag;
  else if (!numbered) tag << "*";
  t= tree_assign_node (t, as_tree_label (tag));
  if (specified) tree_remove (t, N(t)-2, 1);
  else {
    t= tree_insert (t, N(t)-1, tree (TUPLE, tree (DOCUMENT, "")));
    get_current_editor ()->go_to_start (reverse (obtain_ip (t)) * (N(t)-2));
  }
}
