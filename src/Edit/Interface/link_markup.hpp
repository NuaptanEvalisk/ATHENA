// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef ATHENA_LINK_MARKUP_HPP
#define ATHENA_LINK_MARKUP_HPP

#include "scheme.hpp"
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <vector>

struct native_link_record {
  string source;
  tree link;
};
using native_link_records= std::vector<native_link_record>;
native_link_records native_matching_links (list<string> ids, string event);

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
bool native_link_has_cardlink (list<string> ids);
list<string> native_link_active_ids (list<string> ids);
list<string> native_link_mouse_ids (list<string> ids);
list<tree> native_link_active_upwards (tree t);
array<tree> native_complete_links (tree t);
list<string> native_link_constellation ();
tree native_get_link_locations (url here, tree t);
void native_register_link_locations (url here, tree locations);
bool native_resolve_link_id (string id, std::set<std::string>& attempted,
                             std::function<void(url)> completed);
void native_link_follow_ids (list<string> ids, string event);
void native_link_execute_script (string source, bool trusted, object arguments);
void native_locus_link_follow ();
object native_link_process_url (url target);
void native_link_post_load (url target);
void native_link_go_to_url (url target, object optional_from);
void native_link_go_to_url (url target, std::optional<path> from= std::nullopt);
void native_link_go_to_url (string target, std::optional<path> from= std::nullopt);
void native_link_open_label (url target, string label);
void native_link_go_to_id (string id, object optional_from);
void native_link_go_to_id (string id, std::optional<path> from= std::nullopt);

#endif
