// Copyright (C) 2006 Joris van der Hoeven, 2026 ATHENA contributors.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "link_markup.hpp"

namespace {

object flatten_child (tree t) {
  if (is_compound (t, "script")) {
    object args= null_object ();
    // Script arguments retain their attached tree identity, unlike the code.
    for (int i= N(t)-1; i >= 1; --i) args= cons (object (t[i]), args);
    return cons (symbol_object ("script"), cons (tree_to_stree (t[0]), args));
  }
  if (is_compound (t, "attr")) {
    object attributes= null_object ();
    for (int i= (N(t)/2)*2-2; i >= 0; i-= 2)
      attributes= cons (cons (tree_to_stree (t[i]), tree_to_stree (t[i+1])),
                        attributes);
    return attributes;
  }
  return tree_to_stree (t);
}

bool tagged (object value, string tag) {
  return is_list (value) && !is_null (value) &&
    car (value) == symbol_object (tag);
}

object flattened (object value) {
  return is_tree (value) ? native_link_flatten (as_tree (value)) : value;
}

object string_vertex (object value, string tag) {
  if (!tagged (value, tag)) return object (false);
  object tail= cdr (value);
  if (is_null (tail) || !is_null (cdr (tail)) || !is_string (car (tail)))
    return object (false);
  return car (tail);
}

} // namespace

object native_link_flatten (tree link) {
  object result= null_object ();
  for (int i= N(link)-1; i >= 0; --i)
    result= cons (flatten_child (link[i]), result);
  return cons (symbol_object (as_string (L(link))), result);
}

object native_link_type (object link) {
  link= flattened (link);
  return tagged (link, "link") ? cadr (link) : object (false);
}

object native_link_attributes (object link) {
  link= flattened (link);
  return tagged (link, "link") ? caddr (link) : object (false);
}

object native_link_vertices (object link) {
  link= flattened (link);
  return tagged (link, "link") ? cdr (cddr (link)) : object (false);
}

object native_link_source (object link) {
  return car (native_link_vertices (link));
}

object native_link_target (object link) {
  return cadr (native_link_vertices (link));
}

object native_vertex_id (object vertex) {
  return string_vertex (vertex, "id");
}

object native_vertex_url (object vertex) {
  return string_vertex (vertex, "url");
}

object native_vertex_script (object vertex) {
  return tagged (vertex, "script") ? cadr (vertex) : object (false);
}
