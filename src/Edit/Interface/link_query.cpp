// Copyright (C) 2006 Joris van der Hoeven, 2026 ATHENA contributors.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "link_markup.hpp"
#include "editor.hpp"
#include "link.hpp"
#include <algorithm>
#include <mutex>
#include <vector>

namespace {
std::mutex blocked_mutex;
std::vector<string> blocked_types;

bool contains (const std::vector<string>& values, string value) {
  return std::find (values.begin (), values.end (), value) != values.end ();
}

struct query_policy {
  bool bidirectional= native_navigation_bidirectional ();
  bool external= native_navigation_external ();
  std::vector<string> blocked;
  query_policy () {
    std::lock_guard<std::mutex> lock (blocked_mutex);
    blocked= blocked_types;
  }
  bool accepts (string source, string type, tree first, string event) const {
    if (!bidirectional &&
        (!is_func (first, ID, 1) || first[0] != source)) return false;
    if (contains (blocked, type)) return false;
    return event == "hover" || event == type ||
      (event == "click" && type != "focus" && type != "mouse-over" && type != "cardlink") ||
      (event == "double-click" && type == "cardlink");
  }
};

struct link_item {
  string source;
  tree link;
};
using link_items= std::vector<link_item>;

void collect_ids (list<string> ids, link_items& result) {
  for (; !is_nil (ids); ids= ids->next)
    for (list<tree> links= get_links (tree (ID, ids->item));
         !is_nil (links); links= links->next)
      result.push_back ({ids->item, links->item});
}

void collect_exact (tree t, bool filter, const query_policy& policy,
                    link_items& result) {
  link_items candidates;
  if (filter && !policy.external) {
    if (is_func (t, LOCUS) && N(t) >= 2 && is_func (t[0], ID, 1) && is_atomic (t[0][0]))
      for (int i= 1; i < N(t); ++i)
        if (is_func (t[i], LINK)) candidates.push_back ({t[0][0]->label, t[i]});
  }
  else collect_ids (get_ids (t), candidates);
  for (const auto& item: candidates)
    if (!filter || policy.accepts (item.source, item.link[0]->label, item.link[2], "hover"))
      result.push_back (item);
}

std::vector<tree> ancestors (tree t) {
  const path ip= obtain_ip (t);
  if (!ip_attached (ip)) return {};
  editor ed= get_current_editor ();
  tree root= ed->the_root ();
  const path stop= ed->the_buffer_path ();
  std::vector<tree> result;
  for (path p= reverse (ip); !is_nil (p); p= path_up (p)) {
    result.push_back (subtree (root, p));
    if (p == stop) break;
  }
  return result;
}

void collect_complete (tree t, bool filter, const query_policy& policy,
                       link_items& result) {
  collect_exact (t, filter, policy, result);
  if (is_compound (t))
    for (int i= 0; i < N(t); ++i) collect_complete (t[i], filter, policy, result);
}

object to_object (const link_items& items) {
  object result= null_object ();
  for (auto i= items.rbegin (); i != items.rend (); ++i)
    result= cons (cons (object (i->source), cdr (native_link_flatten (i->link))), result);
  return result;
}

list<string> distinct_sources (const link_items& items) {
  std::vector<string> seen;
  list<string> result;
  for (const auto& item: items)
    if (!contains (seen, item.source)) {
      seen.push_back (item.source);
      result= list<string> (item.source, result);
    }
  return reverse (result);
}
}

bool native_navigation_bidirectional () {
  return get_preference ("bidirectional navigation", "off") == "on";
}
bool native_navigation_external () {
  return get_preference ("external navigation", "on") == "on";
}
bool native_navigation_link_pages () {
  return get_preference ("link pages", "on") == "on";
}
void native_navigation_toggle_bidirectional () {
  set_preference ("bidirectional navigation", native_navigation_bidirectional () ? "off" : "on");
}
void native_navigation_toggle_external () {
  set_preference ("external navigation", native_navigation_external () ? "off" : "on");
}
void native_navigation_toggle_link_pages () {
  set_preference ("link pages", native_navigation_link_pages () ? "off" : "on");
}
bool native_navigation_allow_type (string type) {
  std::lock_guard<std::mutex> lock (blocked_mutex);
  return !contains (blocked_types, type);
}
void native_navigation_toggle_type (string type) {
  std::lock_guard<std::mutex> lock (blocked_mutex);
  auto found= std::find (blocked_types.begin (), blocked_types.end (), type);
  if (found == blocked_types.end ()) blocked_types.push_back (type);
  else blocked_types.erase (found);
}
bool native_navigation_no_types () {
  list<string> types= all_link_types ();
  std::lock_guard<std::mutex> lock (blocked_mutex);
  for (; !is_nil (types); types= types->next)
    if (!contains (blocked_types, types->item)) return false;
  return true;
}
bool native_navigation_all_types () {
  std::lock_guard<std::mutex> lock (blocked_mutex);
  return blocked_types.empty ();
}
void native_navigation_allow_none () {
  list<string> types= all_link_types ();
  std::lock_guard<std::mutex> lock (blocked_mutex);
  for (; !is_nil (types); types= types->next)
    if (!contains (blocked_types, types->item)) blocked_types.push_back (types->item);
}
void native_navigation_allow_all () {
  std::lock_guard<std::mutex> lock (blocked_mutex);
  blocked_types.clear ();
}
object native_ids_link_list (list<string> ids) {
  link_items result;
  collect_ids (ids, result);
  return to_object (result);
}
object native_filter_link_list (object items, string event) {
  query_policy policy;
  array<object> result;
  for (; !is_null (items); items= cdr (items)) {
    object item= car (items);
    const tree first= content_to_tree (cadddr (item));
    if (policy.accepts (as_string (car (item)), as_string (cadr (item)), first, event))
      result << item;
  }
  return as_list_object (result);
}
object native_exact_link_list (tree t, bool filter) {
  query_policy policy;
  link_items result;
  collect_exact (t, filter, policy, result);
  return to_object (result);
}
object native_upward_link_list (object t, bool filter) {
  if (is_bool (t) && !as_bool (t)) return null_object ();
  query_policy policy;
  link_items result;
  for (tree node: ancestors (content_to_tree (t))) collect_exact (node, filter, policy, result);
  return to_object (result);
}
object native_complete_link_list (tree t, bool filter) {
  query_policy policy;
  link_items result;
  collect_complete (t, filter, policy, result);
  return to_object (result);
}
array<tree> native_complete_links (tree t) {
  query_policy policy;
  link_items items;
  collect_complete (t, false, policy, items);
  array<tree> result;
  for (const auto& item: items) result << item.link;
  return result;
}
bool native_link_has_cardlink (list<string> ids) {
  link_items items;
  collect_ids (ids, items);
  for (const auto& item: items) if (item.link[0] == "cardlink") return true;
  return false;
}
list<string> native_link_active_ids (list<string> ids) {
  query_policy policy;
  link_items items, active;
  collect_ids (ids, items);
  for (const auto& item: items)
    if (policy.accepts (item.source, item.link[0]->label, item.link[2], "hover"))
      active.push_back (item);
  return distinct_sources (active);
}
list<string> native_link_mouse_ids (list<string> ids) {
  link_items items, mouse;
  collect_ids (ids, items);
  for (const auto& item: items)
    if (item.link[0] != "focus") mouse.push_back (item);
  return distinct_sources (mouse);
}
list<tree> native_link_active_upwards (tree t) {
  const auto nodes= ancestors (t);
  query_policy policy;
  link_items active;
  for (tree node: nodes) collect_exact (node, true, policy, active);
  const list<string> ids= distinct_sources (active);
  list<tree> result;
  for (tree node: nodes) {
    bool found= false;
    for (list<string> own= get_ids (node); !is_nil (own) && !found; own= own->next)
      for (list<string> id= ids; !is_nil (id) && !found; id= id->next)
        found= own->item == id->item;
    if (found) result= list<tree> (node, result);
  }
  return reverse (result);
}
