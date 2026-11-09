#pragma once
#include "node_metadata.hpp"
#include <vector>
#include <functional>
#include <stdexcept>
#include <set>

namespace athena::slides {
struct source_page_sequence {
  std::vector<tree> pages;
  std::vector<tree> context;
};
inline void collect_source_pages (const tree& t, source_page_sequence& result) {
  if (is_func (t, SLIDE_PAGE, 1)) result.pages.push_back (t);
  else if (is_document (t))
    for (int i=0; i<N(t); ++i) collect_source_pages (t[i], result);
  else result.context.push_back (t);
}
inline source_page_sequence source_pages (const tree& t) {
  source_page_sequence result;
  collect_source_pages (t, result);
  return result;
}

inline std::string kind (const tree& t) {
  if (!is_func (t, SLIDE_PAGE, 1)) return {};
  const auto* meta= node::get (t);
  if (!meta) return {};
  auto it= meta->properties.find ("kind");
  if (it == meta->properties.end ()) return {};
  const auto* value= std::get_if<std::string> (&it->second.data);
  return value ? *value : std::string ();
}
using reader= std::function<tree (string)>;
struct field { std::string key, label, shared; };
struct page_type {
  std::string id, label, layout;
  std::vector<field> fields;
};
inline std::string text (const tree& t) {
  if (!is_atomic (t)) throw std::runtime_error ("Slide declaration expects text");
  return std::string (t->label.data (), N(t->label));
}
inline page_type definition (const std::string& id, const reader& read) {
  tree t= read ("slide-page-type-" * string (id.c_str ()));
  if (!is_func (t, TUPLE, 3) || !is_func (t[2], TUPLE))
    throw std::runtime_error ("Missing or invalid slide page declaration: " + id);
  page_type result {id, text (t[0]), text (t[1]), {}};
  std::set<std::string> keys;
  for (int i=0; i<N(t[2]); ++i) {
    tree f= t[2][i];
    if (!is_func (f, TUPLE, 3))
      throw std::runtime_error ("Invalid field declaration in slide page: " + id);
    field value {text (f[0]), text (f[1]), text (f[2])};
    if (value.key.empty () || value.key.find (':') != std::string::npos ||
        value.key == "kind" || value.key == "background" || !keys.insert (value.key).second)
      throw std::runtime_error ("Invalid or duplicate slide field: " + value.key);
    result.fields.push_back (std::move (value));
  }
  return result;
}
inline std::vector<page_type> types (const reader& read) {
  std::vector<page_type> result;
  tree ids= read ("slide-page-types");
  if (!is_func (ids, TUPLE)) return result;
  for (int i=0; i<N(ids); ++i) result.push_back (definition (text (ids[i]), read));
  return result;
}
inline std::vector<std::string> fields (const tree& t, const reader& read) {
  std::vector<std::string> result;
  if (kind (t).empty ()) return result;
  for (const auto& f: definition (kind (t), read).fields) result.push_back (f.key);
  return result;
}
inline tree value (const tree& source, const field& f, const reader& read) {
  if (!f.shared.empty ()) {
    tree v= read ("slide-shared-" * string (f.shared.c_str ()));
    return v == tree (UNINIT) ? tree ("") : v;
  }
  const auto* metadata= node::get (source);
  if (metadata) {
    auto it= metadata->properties.find (f.key);
    if (it != metadata->properties.end ())
      if (const auto* rich= std::get_if<node::rich_text> (&it->second.data))
        return rich->content;
  }
  return tree ("");
}
}
