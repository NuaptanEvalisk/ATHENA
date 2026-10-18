/******************************************************************************
* MODULE     : math_v2_to_v3.hpp
* DESCRIPTION: Detached source-tree migration; never used by the v3 reader
* COPYRIGHT  : (C) 2026 ATHENA contributors
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
******************************************************************************/
#pragma once

#include "tree.hpp"
#include <string>
#include <vector>

namespace athena::document {

struct math_v3_issue {
  std::vector<int> old_path;
  std::string code, detail;
};

struct math_v3_migration {
  tree document;
  std::vector<math_v3_issue> warnings;
  // Old node identities which no longer own a v3 source object. The whole
  // Vault upgrader must prove they have no live references before publication.
  std::vector<std::string> removed_identities;
  std::size_t delimiters= 0;
  std::size_t scripts= 0;
  std::size_t operators= 0;
  std::size_t math_blocks= 0;
  std::size_t grids= 0;
  std::size_t math_tokens= 0;
};

// A migration-only adapter from *decoded XML v2 native tree* to v3 native
// structure. This is NOT an input path for normal v3 documents. `strict` turns
// ambiguity warnings into errors; invariant violations always cause errors.
math_v3_migration migrate_math_v2_tree (const tree& source, bool strict= false);

// Whole-Vault migration must not mistake labels interned by the legacy reader
// for declarations available to a fresh v3 process. Includes rich metadata.
void validate_cold_v3_migration (const tree& source);

} // namespace athena::document
