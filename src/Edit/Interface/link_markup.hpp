// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef ATHENA_LINK_MARKUP_HPP
#define ATHENA_LINK_MARKUP_HPP

#include "scheme.hpp"

object native_link_flatten (tree link);
object native_link_type (object link);
object native_link_attributes (object link);
object native_link_vertices (object link);
object native_link_source (object link);
object native_link_target (object link);
object native_vertex_id (object vertex);
object native_vertex_url (object vertex);
object native_vertex_script (object vertex);

string native_link_unique_id ();
void native_make_locus ();
object native_locus_id (tree locus);
void native_locus_set (string id, tree content);
void native_locus_insert_link (tree locus, tree link);
void native_locus_remove_link (tree link);
void native_locus_remove_all_links (tree locus, tree link);
list<tree> native_id_to_loci (string id);

bool native_navigation_bidirectional ();
bool native_navigation_external ();
bool native_navigation_link_pages ();
void native_navigation_toggle_bidirectional ();
void native_navigation_toggle_external ();
void native_navigation_toggle_link_pages ();
bool native_navigation_allow_type (string type);
void native_navigation_toggle_type (string type);
bool native_navigation_no_types ();
bool native_navigation_all_types ();
void native_navigation_allow_none ();
void native_navigation_allow_all ();
object native_ids_link_list (list<string> ids);
object native_filter_link_list (object items, string event);
object native_exact_link_list (tree t, bool filter);
object native_upward_link_list (object t, bool filter);
object native_complete_link_list (tree t, bool filter);
bool native_link_has_cardlink (list<string> ids);
list<string> native_link_active_ids (list<string> ids);
list<string> native_link_mouse_ids (list<string> ids);
list<tree> native_link_active_upwards (tree t);

#endif
