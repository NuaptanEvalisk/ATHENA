/******************************************************************************
* MODULE     : namespaces_private.hpp
* DESCRIPTION: Private helpers for ATHENA vault namespace implementation
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
*******************************************************************************/

#ifndef ATHENA_NAMESPACES_PRIVATE_HPP
#define ATHENA_NAMESPACES_PRIVATE_HPP

#include "namespaces.hpp"

#include "convert.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <tuple>
#include <vector>

namespace athena_namespaces {

inline std::string
tm_to_std (const string& s) {
  return std::string (s.data (), (size_t) N(s));
}

inline string
std_to_tm (const std::string& s) {
  return string (s.c_str ());
}

inline std::string
trim_std (const std::string& s) {
  size_t a= 0, b= s.size ();
  while (a < b && std::isspace ((unsigned char) s[a])) a++;
  while (b > a && std::isspace ((unsigned char) s[b - 1])) b--;
  return s.substr (a, b - a);
}

inline string
join_list (const std::vector<string>& s) {
  string out= "";
  for (int i=0; i<(int) s.size (); i++) {
    if (i != 0) out << ", ";
    out << s[i];
  }
  return out;
}

inline bool
has_string (const array<string>& xs, string x) {
  for (int i=0; i<N(xs); i++)
    if (xs[i] == x) return true;
  return false;
}

inline bool
has_string (const std::vector<string>& xs, const string& x) {
  return std::find (xs.begin (), xs.end (), x) != xs.end ();
}

inline string
canonical_kind (string kind) {
  if (kind == "abstract" || kind == "semi-concrete" || kind == "concrete")
    return kind;
  return "concrete";
}

enum ns_field_type {
  ns_string_field,
  ns_word_field,
  ns_char_field,
  ns_int_field,
  ns_pos_int_field,
  ns_roman_field
};

struct template_token {
  bool          field;
  std::string   literal;
  ns_field_type type;
};

struct child_template_position {
  size_t tok;
  size_t off;
};

struct field_fragment {
  bool        child;
  int         child_index;
  std::string literal;
};

struct parent_field_expr {
  ns_field_type type;
  std::vector<field_fragment> parts;
};

struct derivation_result {
  bool changed= false;
  std::vector<parent_field_expr> fields;
};

extern "C" {
enum AthenaNsFieldType {
  ATHENA_NS_STRING,
  ATHENA_NS_WORD,
  ATHENA_NS_CHAR,
  ATHENA_NS_INT,
  ATHENA_NS_POS_INT,
  ATHENA_NS_ROMAN
};

typedef struct {
  const char* text;
  int         type;
  long long   integer;
  int         roman;
} AthenaNsField;
}

typedef int (*ns_compare_fn) (int, const AthenaNsField*, const AthenaNsField*);

const char* field_type_name (ns_field_type t);
int parse_roman_value (std::string_view s);
bool field_value_satisfies_type (ns_field_type type, const std::string& value);
bool parse_template (string templ, std::vector<template_token>& out,
                     string& error);
bool parse_template_std (const std::string& templ,
                         std::vector<template_token>& out,
                         std::string& error);
std::string template_to_std (const std::vector<template_token>& toks);
bool match_stem (const athena_namespace_definition& ns, const std::string& stem,
                 athena_namespace_match& out, string& error);
bool match_stem_std (const std::string& templ, const std::string& stem,
                     std::vector<std::string>& captures,
                     std::vector<std::string>& capture_types,
                     bool& ambiguous, std::string& error);
bool template_derivation_mapping (string child_template, string parent_template,
                                  bool require_changed,
                                  derivation_result& result, string& error);
bool template_derives_from (string child_template, string parent_template,
                            bool& derives, string& error);
bool template_derives_from_std (const std::string& child_template,
                                const std::string& parent_template,
                                bool& derives, std::string& error);
bool subproduct_candidate_from_order (string first_template,
                                      string second_template,
                                      bool aggressive_string,
                                      string& suggestion, string& error);

struct compiled_sorter;
using sorter_handle= std::shared_ptr<const compiled_sorter>;
sorter_handle load_sorter (string sorter_path, string& error);
void sort_namespace_members (const sorter_handle& sorter,
                              namespace_records<athena_namespace_match>& members);

bool refresh_derived_parents_if_needed (bool force, bool& changed,
                                        string& error);
bool load_namespace_snapshot_from_db (
  std::vector<athena_namespace_definition>& namespaces,
  std::vector<athena_namespace_relation>& relations, string& error);

} // namespace athena_namespaces

#endif // ATHENA_NAMESPACES_PRIVATE_HPP
