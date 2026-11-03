// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#pragma once

#include "tree.hpp"
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace athena::node { struct property; }

namespace athena::contracts {

enum class field_role { data, content, inline_content, body };
enum class field_value { source, atom };
enum class search_projection { hidden, content, atomic };
enum class property_editor { none, title_slot };
enum class property_type {
  string, boolean, integer, real, list, dictionary, reference, rich_text
};

struct child_field {
  std::string name;
  field_role role;
  field_value value;
  search_projection search;
  bool editable;
  std::map<std::string, std::string> environment;
};

struct property_field {
  property_type type;
  bool required= false;
  std::optional<property_type> element_type;
  std::optional<property_type> accepted_legacy_type;
  property_editor editor= property_editor::none;
  bool accepts_type (const node::property&) const;
};

property_type type_of (const node::property&);

struct native_node_fields {
  std::string name;
  std::string display_name;
  bool math= false, inner_border= false;
  int optional= 0;
  int repeat= 0, minimum_repetitions= 0;
  int editing_minimum= 0;
  bool repeat_first= false;
  std::vector<child_field> children;
  std::map<std::string, property_field, std::less<>> properties;

  bool accepts_arity (int size) const;
  int minimum_arity () const;
  int descriptor_index (int index, int arity) const;
  const child_field* child (int index, int arity) const;
  int child_index (std::string_view field, int arity, int occurrence= 0) const;
  const property_field* property (std::string_view field) const;
};

// Built once from embedded JSON, including in headless/cold codec paths.
// Core schema is part of the binary; loading it never evaluates a style.
const native_node_fields* native_fields (tree_label label);
bool native_field_shape (const tree& value);
// Checks declared property types/presence, not domain constraints or the
// recursive validity of rich text/extension values owned by the source codec.
bool native_property_shape (const tree& value);
// Fixed-position fields for constructors/edit policies. Repeated layouts need
// child_index with the actual node arity so suffix fields cannot be guessed.
int native_field_index (tree_label label, std::string_view field);

} // namespace athena::contracts
