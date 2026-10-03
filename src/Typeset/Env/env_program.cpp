/******************************************************************************
* MODULE     : env_program.cpp
* DESCRIPTION: Native presentation of property-backed program nodes
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* See the file LICENSE in the root directory.
******************************************************************************/

#include "program_presentation.hpp"

namespace {

string
text (const std::string& value) {
  return string (value.data (), static_cast<int> (value.size ()));
}

} // namespace

tree
native_program_macro (const tree& source) {
  if (!athena::program::is_canonical (source))
    return tree (_ERROR, "Malformed program");
  const std::string* language= athena::program::language (source);
  ASSERT (language != nullptr, "canonical program without language");

  // Match the existing code-block presentation, but keep language identity in
  // source metadata. `prog-language` is only an evaluation environment value;
  // it selects the KF6 syntax definition used by prog_language().
  tree body (ARG, "body");
  tree highlighted (
    WITH,
    tree ("mode"), tree ("prog"),
    tree ("prog-language"), tree (text (*language)),
    tree ("font-family"), tree ("rm"),
    body);
  tree presented= compound ("pseudo-code", tree (DOCUMENT, highlighted));
  return tree (MACRO, "body", tree (DOCUMENT, presented));
}
