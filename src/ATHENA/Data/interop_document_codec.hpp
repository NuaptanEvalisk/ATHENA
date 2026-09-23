/******************************************************************************
* MODULE     : interop_document_codec.hpp
* DESCRIPTION: Lossless structured document values at the AUDMAP ownership boundary
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#pragma once
#include "../Interop/value.hpp"
#include <cstddef>

class tree;

namespace athena::interop {
struct document_codec_limits {
  std::size_t nodes= 1000000;
  std::size_t bytes= 64 * 1024 * 1024;
  std::size_t depth= 256;
};

// Atoms: {"text": UTF8}. RAW_DATA's single child is {"raw": BIN}.
// Compounds: {"tag": UTF8, "children": [...]}. Ordinary text/tag data never
// uses a legacy Cork fallback in AUDMAP protocol v2.
// These functions run on the native tree owner. Only value crosses threads.
value document_node_to_value (const tree&, document_codec_limits = {});
tree document_node_from_value (const value&, document_codec_limits = {});
} // namespace athena::interop
