// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#include "command_value.hpp"

template<>
double command_argument<double> (const command_value& value, const char* diagnostic) {
  if (const auto* number= std::get_if<double> (&value)) return *number;
  if (const auto* number= std::get_if<int> (&value)) return *number;
  throw std::invalid_argument (diagnostic);
}

template<>
string command_argument<string> (const command_value& value, const char* diagnostic) {
  if (const auto* text= std::get_if<string> (&value)) return *text;
  if (const auto* source= std::get_if<tree> (&value))
    if (is_atomic (*source)) return (*source)->label;
  throw std::invalid_argument (diagnostic);
}

template<>
url command_argument<url> (const command_value& value, const char* diagnostic) {
  if (const auto* location= std::get_if<url> (&value)) return *location;
  const string text= command_argument<string> (value, diagnostic);
#ifdef OS_MINGW
  return url_system (text);
#else
  return url (text);
#endif
}

tree command_content (const command_value& value, const char* diagnostic) {
  if (const auto* source= std::get_if<tree> (&value)) return *source;
  if (const auto* text= std::get_if<string> (&value)) return tree (*text);
  throw std::invalid_argument (diagnostic);
}

bool command_truth (const command_value& value) {
  const auto* boolean= std::get_if<bool> (&value);
  return boolean == nullptr || *boolean;
}
bool command_equal (const command_value& a, const command_value& b) { return a == b; }
bool command_is_tree (const command_value& value) {
  return std::holds_alternative<tree> (value);
}
bool command_is_content (const command_value& value) {
  return command_is_tree (value) || std::holds_alternative<string> (value);
}
bool command_is_url (const command_value& value) {
  return std::holds_alternative<url> (value) || std::holds_alternative<string> (value);
}
bool command_is_modification (const command_value& value) {
  return std::holds_alternative<modification> (value);
}
bool command_is_patch (const command_value& value) {
  return std::holds_alternative<patch> (value);
}
