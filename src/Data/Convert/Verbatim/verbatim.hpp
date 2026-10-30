// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#pragma once

#include "Data/NodeContracts/editing_query.hpp"
#include <optional>

// Source addresses are retained through argument and table formatting. A
// missing custom-node entry makes the whole read unavailable, not partial text.
std::optional<string> source_to_verbatim (
  tree, bool wrap, string encoding,
  const athena::contracts::source_contract_preparation&, path source= path ());

// Detached source excerpt, not macro expansion: primitive data fields are
// hidden, while custom macro arguments remain source text. No evaluator runs.
string source_text_excerpt (tree);

std::optional<string> search_metadata (
  tree, string kind, const athena::contracts::source_contract_preparation&);
