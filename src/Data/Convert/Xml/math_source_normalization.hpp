/******************************************************************************
* MODULE     : math_source_normalization.hpp
* DESCRIPTION: Canonical mathematics at external-format import boundaries
* COPYRIGHT  : (C) 2026 ATHENA contributors
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
******************************************************************************/
#pragma once

#include "tree.hpp"

namespace athena::document {
// External converters may still produce legacy mathematical constructors.
// Normalize those detached results, rejecting ambiguity. Never call from the
// native v3 reader or exporter, which require canonical input already.
tree normalize_imported_math (const tree& source);
}
