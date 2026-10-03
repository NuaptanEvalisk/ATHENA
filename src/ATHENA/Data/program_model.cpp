/******************************************************************************
* MODULE     : program_model.cpp
* DESCRIPTION: Canonical property-backed program source nodes
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* See the file LICENSE in the root directory.
******************************************************************************/

#include "ATHENA/Data/program_model.hpp"

#include <stdexcept>

namespace athena::program {
namespace {

constexpr std::size_t maximum_language_bytes= 1024;

bool valid_language (std::string_view value) {
  return !value.empty () && value.size () <= maximum_language_bytes &&
         value.find ('\0') == std::string_view::npos;
}

} // namespace

tree_label
label () {
  static const tree_label value= make_tree_label ("program");
  return value;
}

bool
is_program (const tree& source) {
  return is_compound (source) && L(source) == label ();
}

const std::string*
language (const tree& source) {
  const auto* metadata= node::get (source);
  if (metadata == nullptr) return nullptr;
  const auto found= metadata->properties.find ("language");
  if (found == metadata->properties.end ()) return nullptr;
  return std::get_if<std::string> (&found->second.data);
}

bool
is_canonical (const tree& source) {
  const std::string* value= language (source);
  return is_program (source) && N(source) == 1 &&
         is_func (source[0], DOCUMENT) && value != nullptr &&
         valid_language (*value);
}

tree
create (std::string_view language_name, tree body) {
  if (!valid_language (language_name))
    throw std::invalid_argument ("Program language must be a non-empty name");
  if (!is_func (body, DOCUMENT)) body= tree (DOCUMENT, body);
  tree result (label (), std::move (body));
  node::metadata metadata;
  metadata.properties.emplace (
    "language", node::property (std::string (language_name)));
  node::set (result, metadata);
  return result;
}

} // namespace athena::program
