// Copyright (C) 2006 Joris van der Hoeven, 2026 ATHENA contributors.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Editor/Commands/link_markup.hpp"
#include "native_interfaces.hpp"
#include "Editor/editor.hpp"
#include "link.hpp"
#include "tree_traverse.hpp"
#include <QUuid>
#include <stdexcept>

string native_link_unique_id () {
  const QByteArray id= QUuid::createUuid ().toString (QUuid::WithoutBraces).toUtf8 ();
  return "+" * string (id.constData (), id.size ());
}

void native_make_locus () {
  editor ed= get_current_editor ();
  const bool selected= ed->selection_active_any ();
  const tree body= selected ? ed->selection_get () : tree ("");
  const tree wrapper (LOCUS, tree (ID, native_link_unique_id ()), body);
  const path end= ed->command_tree_end (wrapper, path (1));
  if (selected) ed->selection_cut ("null");
  ed->insert_tree (wrapper, end);
}


void native_locus_set (string id, tree content) {
  for (list<tree> nodes= get_trees (id); !is_nil (nodes); nodes= nodes->next)
    tree_assign (nodes->item, content);
}

void native_locus_insert_link (tree locus, tree link) {
  tree_insert (locus, N(locus)-1, tree (TUPLE, copy (link)));
}

void native_locus_remove_link (tree link) {
  const path ip= obtain_ip (link);
  if (!ip_attached (ip) || is_nil (ip))
    throw std::invalid_argument ("Link removal requires an attached node");
  tree root= get_current_editor ()->the_root ();
  tree parent= subtree (root, reverse (ip->next));
  tree_remove (parent, ip->item, 1);
}

void native_locus_remove_all_links (tree locus, tree link) {
  for (int i= N(locus)-2; i >= 0; --i)
    if (locus[i] == link) tree_remove (locus, i, 1);
}

list<tree> native_id_to_loci (string id) {
  list<tree> result;
  for (list<tree> nodes= get_trees (id); !is_nil (nodes); nodes= nodes->next) {
    const path ip= obtain_ip (nodes->item);
    if (!ip_attached (ip) || is_nil (ip)) continue;
    tree root= get_current_editor ()->the_root ();
    tree parent= subtree (root, reverse (ip->next));
    if (is_func (parent, LOCUS)) result= list<tree> (parent, result);
  }
  return reverse (result);
}
