// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#pragma once

#include "Data/NodeContracts/editing_query.hpp"
#include "hashmap.hpp"

// A compression dictionary belongs to the editor issuing the request. It must
// remain alive until that editor decompresses the returned placeholders.
class tree_compression {
  hashmap<string,tree> compressed;
  hashmap<tree,string> compressed_code;
public:
  tree compress (tree, const athena::contracts::source_contract_preparation&,
                 path source= path ());
  tree decompress (tree);
  string html (tree compressed, int mode);
  tree from_html (string, int mode);
};
