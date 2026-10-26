// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#include "native_fields.hpp"
#include "native_fields_data.hpp"
#include "node_metadata.hpp"
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>

namespace athena::contracts {
namespace {

field_role role (const std::string& name) {
  if (name == "data") return field_role::data;
  if (name == "content") return field_role::content;
  if (name == "inline") return field_role::inline_content;
  if (name == "body") return field_role::body;
  throw std::invalid_argument ("Unknown native field role: " + name);
}

property_type metadata_type (const std::string& name) {
  if (name == "string") return property_type::string;
  if (name == "boolean") return property_type::boolean;
  if (name == "integer") return property_type::integer;
  if (name == "real") return property_type::real;
  if (name == "list") return property_type::list;
  if (name == "dictionary") return property_type::dictionary;
  if (name == "reference") return property_type::reference;
  if (name == "rich-text") return property_type::rich_text;
  throw std::invalid_argument ("Unknown native property type: " + name);
}

const std::map<tree_label, native_node_fields>& registry () {
  static const auto values= [] {
    init_standard_tree_labels ();
    const auto data= nlohmann::json::parse (athena_native_fields_json);
    if (data.at ("version") != 1)
      throw std::invalid_argument ("Unsupported native field schema version");
    std::map<tree_label, native_node_fields> result;
    for (const auto& [name, value]: data.at ("nodes").items ()) {
      const string tag (name.data (), name.size ());
      if (!existing_tree_label (tag))
        throw std::invalid_argument ("Undeclared native node: " + name);
      native_node_fields node;
      node.name= name;
      node.display_name= value.value ("display-name", name);
      node.math= value.value ("math", false);
      node.inner_border= value.value ("inner-border", false);
      node.optional= value.value ("optional", 0);
      if (value.contains ("repeat")) {
        const auto& repetition= value.at ("repeat");
        node.repeat= repetition.at ("count").get<int> ();
        node.minimum_repetitions= repetition.value ("minimum", 0);
        const auto position= repetition.at ("position").get<std::string> ();
        if (position != "first" && position != "last")
          throw std::invalid_argument ("Unknown repetition position for " + name);
        node.repeat_first= position == "first";
        if (node.repeat <= 0 || node.minimum_repetitions < 0 || node.optional != 0)
          throw std::invalid_argument ("Invalid repetition for " + name);
      }
      std::set<std::string> field_names;
      for (const auto& entry: value.at ("fields")) {
        child_field field;
        field.name= entry.at ("name").get<std::string> ();
        if (!field_names.insert (field.name).second)
          throw std::invalid_argument ("Duplicate field " + name + "." + field.name);
        field.role= role (entry.at ("role").get<std::string> ());
        const auto type= entry.value ("type", std::string ("source"));
        if (type != "source" && type != "atom")
          throw std::invalid_argument ("Unknown native field type: " + type);
        field.value= type == "atom" ? field_value::atom : field_value::source;
        field.editable= entry.value ("editable", field.role != field_role::data);
        const auto projection= entry.value ("search", std::string (
          field.role == field_role::data ? "hidden" : "content"));
        if (projection == "hidden") field.search= search_projection::hidden;
        else if (projection == "content") field.search= search_projection::content;
        else if (projection == "atomic") field.search= search_projection::atomic;
        else throw std::invalid_argument ("Unknown search projection: " + projection);
        if (entry.contains ("environment"))
          field.environment= entry.at ("environment").get<decltype(field.environment)> ();
        node.children.push_back (std::move (field));
      }
      if (node.optional < 0 || node.optional > int (node.children.size ()) ||
          node.repeat > int (node.children.size ()))
        throw std::invalid_argument ("Invalid native field layout for " + name);
      node.editing_minimum= value.value ("editing-minimum", node.minimum_arity ());
      if (!node.accepts_arity (node.editing_minimum))
        throw std::invalid_argument ("Invalid editing minimum for " + name);
      if (value.contains ("properties"))
        for (const auto& [name, entry]: value.at ("properties").items ()) {
          property_field field;
          field.type= metadata_type (entry.at ("type").get<std::string> ());
          field.required= entry.value ("required", false);
          if (entry.contains ("element-type"))
            field.element_type= metadata_type (entry.at ("element-type").get<std::string> ());
          if (entry.contains ("accepted-legacy-type"))
            field.accepted_legacy_type= metadata_type (entry.at ("accepted-legacy-type").get<std::string> ());
          if (entry.contains ("editor")) {
            if (entry.at ("editor") != "title-slot")
              throw std::invalid_argument ("Unknown property editor for " + node.name + "." + name);
            field.editor= property_editor::title_slot;
          }
          node.properties.emplace (name, std::move (field));
        }
      result.emplace (as_tree_label (tag), std::move (node));
    }
    return result;
  } ();
  return values;
}
}

bool native_node_fields::accepts_arity (int size) const {
  if (size < minimum_arity ()) return false;
  return repeat ? (size - minimum_arity ()) % repeat == 0 :
    size <= int (children.size ());
}

int native_node_fields::minimum_arity () const {
  return int (children.size ()) - optional - repeat + minimum_repetitions * repeat;
}

int native_node_fields::descriptor_index (int index, int arity) const {
  if (index < 0 || index >= arity || !accepts_arity (arity)) return -1;
  if (!repeat) return index;
  const int fixed= int (children.size ()) - repeat;
  if (repeat_first)
    return index < arity - fixed ? index % repeat :
      repeat + index - (arity - fixed);
  return index < fixed ? index : fixed + (index - fixed) % repeat;
}

const child_field* native_node_fields::child (int index, int arity) const {
  const int descriptor= descriptor_index (index, arity);
  return descriptor < 0 ? nullptr : &children[descriptor];
}

int native_node_fields::child_index (std::string_view field, int arity, int occurrence) const {
  if (occurrence < 0 || !accepts_arity (arity)) return -1;
  const int count= int (children.size ());
  for (int i=0; i<count; ++i) {
    if (children[i].name != field) continue;
    const bool repeated= repeat && (repeat_first ? i < repeat : i >= count - repeat);
    if (!repeated && occurrence != 0) return -1;
    const int index= repeated ? i + occurrence * repeat :
      repeat && repeat_first ? arity - count + i : i;
    return descriptor_index (index, arity) == i ? index : -1;
  }
  return -1;
}

const property_field* native_node_fields::property (std::string_view field) const {
  const auto found= properties.find (field);
  return found == properties.end () ? nullptr : &found->second;
}

property_type type_of (const node::property& value) {
  return std::visit ([] (const auto& item) {
    using T= std::decay_t<decltype (item)>;
    if constexpr (std::is_same_v<T, std::string>) return property_type::string;
    else if constexpr (std::is_same_v<T, bool>) return property_type::boolean;
    else if constexpr (std::is_same_v<T, std::int64_t>) return property_type::integer;
    else if constexpr (std::is_same_v<T, double>) return property_type::real;
    else if constexpr (std::is_same_v<T, node::property::list>) return property_type::list;
    else if constexpr (std::is_same_v<T, node::property::dictionary>) return property_type::dictionary;
    else if constexpr (std::is_same_v<T, node::reference>) return property_type::reference;
    else return property_type::rich_text;
  }, value.data);
}

bool property_field::accepts_type (const node::property& value) const {
  const auto actual= type_of (value);
  return actual == type || (accepted_legacy_type && actual == *accepted_legacy_type);
}

const native_node_fields* native_fields (tree_label label) {
  const auto& values= registry ();
  const auto found= values.find (label);
  return found == values.end () ? nullptr : &found->second;
}

bool native_field_shape (const tree& value) {
  if (is_atomic (value)) return false;
  const auto* node= native_fields (L(value));
  if (!node || !node->accepts_arity (N(value))) return false;
  for (int i=0; i<N(value); ++i)
    if (node->child (i, N(value))->value == field_value::atom &&
        !is_atomic (value[i])) return false;
  return true;
}

bool native_property_shape (const tree& value) {
  const auto* fields= native_fields (L(value));
  if (!fields) return false;
  const auto* metadata= node::get (value);
  for (const auto& [name, field]: fields->properties) {
    const auto* property= metadata && metadata->properties.count (name) ?
      &metadata->properties.at (name) : nullptr;
    if (!property) {
      if (field.required) return false;
      continue;
    }
    if (!field.accepts_type (*property)) return false;
    if (field.element_type)
      if (const auto* list= std::get_if<node::property::list> (&property->data))
        for (const auto& item: *list)
          if (type_of (item) != *field.element_type) return false;
  }
  return true;
}

int native_field_index (tree_label label, std::string_view field) {
  const auto* node= native_fields (label);
  const int index= node && !node->repeat ?
    node->child_index (field, int (node->children.size ())) : -1;
  if (index < 0)
    throw std::invalid_argument ("Unknown native field: " + std::string (field));
  return index;
}

} // namespace athena::contracts
