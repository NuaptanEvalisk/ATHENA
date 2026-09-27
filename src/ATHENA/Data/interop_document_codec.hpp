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
// The v2 encoder rejects node metadata rather than silently discarding it.
// These functions run on the native tree owner. Only value crosses threads.
value document_node_to_value (const tree&, document_codec_limits = {});
tree document_node_from_value (const value&, document_codec_limits = {});

// AUDMAP document-model v3 boundary. Nodes retain text/tag/children and add
// optional persistent id and properties fields. Connection-scoped AUDMAP
// handles remain a separate transport identity and are never encoded here.
// RAW_DATA's child remains {"raw": BIN}, with optional id and properties.
// JSON transports must represent binary explicitly at their own boundary.
// Properties use the typed schema documented in the implementation and share
// the node/depth/byte limits, including rich text.
// Wire parsers must reject duplicate object members before constructing value;
// this API can only validate the members retained in the parsed object.
value document_node_to_value_v3 (const tree&, document_codec_limits = {});
tree document_node_from_value_v3 (const value&, document_codec_limits = {});
} // namespace athena::interop
