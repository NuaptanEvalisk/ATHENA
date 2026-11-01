// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef ATHENA_LINK_MARKUP_HPP
#define ATHENA_LINK_MARKUP_HPP

#include "scheme.hpp"
#include <functional>

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
array<tree> native_complete_links (tree t);
list<string> native_link_constellation ();
tree native_get_link_locations (url here, tree t);
void native_register_link_locations (url here, tree locations);
bool native_resolve_link_id (string id);
bool native_resolve_link_id (string id, std::function<void(url)> completed);
object native_navigation_simplify (object items);
object native_link_list_to_navigation (object links);
object native_upward_navigation (object t);
bool native_link_may_follow (object t);
object native_navigation_filter (object items, object type, object position, bool jumpable);
list<string> native_navigation_types (object items);
list<string> native_navigation_xtypes (object items);
object native_navigation_first_xtype (object items, string type);
object native_automatic_link (object target, object options);
object native_link_enumeration (object items);
object native_navigation_document (tree style, object items);
void native_build_navigation_page (object items);
void native_navigation_follow (object items);
void native_link_follow_ids (list<string> ids, string event);
void native_link_execute_script (string source, bool trusted, object arguments);
void native_link_go_to_vertex (object vertex, object attributes);
void native_locus_link_follow ();
object native_link_process_url (url target);
void native_link_post_load (url target);

#endif
