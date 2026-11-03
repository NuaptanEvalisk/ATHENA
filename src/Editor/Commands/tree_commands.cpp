// Copyright (C) 2002 Joris van der Hoeven, 2026 ATHENA contributors.
// GPL-3.0-or-later.
#include "Editor/Commands/tree_commands.hpp"
#include "Editor/editor.hpp"

std::optional<tree> tree_command_outer (tree source) {
  const path ip= obtain_ip (source);
  if (!ip_attached (ip)) return std::nullopt;
  const path p= reverse (ip);
  editor ed= get_current_editor ();
  if (p == ed->the_buffer_path () || is_nil (p)) return std::nullopt;
  return ed->the_subtree (path_up (p));
}

std::optional<tree> tree_command_down (tree source) {
  const path ip= obtain_ip (source);
  if (!ip_attached (ip)) return std::nullopt;
  const path p= reverse (ip);
  editor ed= get_current_editor ();
  const path q= path_up (ed->the_path ());
  if (is_nil (q) || !(p <= path_up (q))) return std::nullopt;
  return ed->the_subtree (head (q, N(p)+1));
}

bool tree_command_cursor_inside (tree source) {
  const path ip= obtain_ip (source);
  if (!ip_attached (ip)) return false;
  editor ed= get_current_editor ();
  const path p= path_up (ed->the_path ()), q= reverse (ip);
  return q <= p && (is_atomic (ed->the_subtree (p)) || N(p)>N(q));
}
