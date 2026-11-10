#pragma once

#include "tree.hpp"
#include "hashmap.hpp"

namespace athena::derived {
// Inputs are read-only. Results retain semantic properties, but not source
// identities or label definitions. No function accesses editor state.
tree table_of_contents (const tree& entries);
tree index (const tree& entries, hashmap<string,tree> references,
            string language, string break_style);
tree glossary (const tree& entries);
bool is_directory (tree declaration);
string directory_kind (tree declaration);
tree directory (tree declaration, const tree& entries,
                hashmap<string,tree> references, string language, string break_style);
}
