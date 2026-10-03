/******************************************************************************
* MODULE     : program_model.hpp
* DESCRIPTION: Canonical property-backed program source nodes
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* See the file LICENSE in the root directory.
******************************************************************************/
#pragma once

#include "Kernel/Types/node_metadata.hpp"

#include <string>
#include <string_view>

namespace athena::program {

tree_label label ();
bool is_program (const tree& source);
bool is_canonical (const tree& source);
const std::string* language (const tree& source);

// Construct a detached canonical program. The language is source metadata, not
// a tag name: every programming language uses the same `program` node type.
tree create (std::string_view language, tree body= tree (DOCUMENT, ""));

} // namespace athena::program
