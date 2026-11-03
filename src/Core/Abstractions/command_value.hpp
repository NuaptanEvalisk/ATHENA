// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#pragma once

#include "tree.hpp"
#include "path.hpp"
#include "url.hpp"
#include "patch.hpp"
#include <stdexcept>
#include <variant>

// Command signatures are defined in XML. Values stay on the caller's thread;
// actor mailboxes carry IDs and serialized transport payloads, never these values.
struct command_bytes { string data; };
struct command_syntax { tree data; };
inline bool operator == (const command_bytes& a, const command_bytes& b) {
  return a.data == b.data;
}
inline bool operator == (const command_syntax& a, const command_syntax& b) {
  return a.data == b.data;
}
using command_value= std::variant<std::monostate, bool, int, double, string,
  tree_label, tree, url, path, modification, patch, command_bytes, command_syntax,
  array<int>, array<double>, array<string>, array<tree>, array<url>,
  array<patch>, array<path>, list<string>, list<tree>>;
using command_arguments= array<command_value>;

template<class T>
T command_argument (const command_value& value, const char* diagnostic) {
  if (const auto* typed= std::get_if<T> (&value)) return *typed;
  throw std::invalid_argument (diagnostic);
}
template<> string command_argument<string> (const command_value&, const char*);
template<> double command_argument<double> (const command_value&, const char*);
template<> url command_argument<url> (const command_value&, const char*);
tree command_content (const command_value&, const char* diagnostic);
bool command_truth (const command_value&);
bool command_equal (const command_value&, const command_value&);

bool command_is_tree (const command_value&);
bool command_is_content (const command_value&);
bool command_is_url (const command_value&);
bool command_is_modification (const command_value&);
bool command_is_patch (const command_value&);
