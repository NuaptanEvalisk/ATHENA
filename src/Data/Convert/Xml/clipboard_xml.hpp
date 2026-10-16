/******************************************************************************
* MODULE     : clipboard_xml.hpp
* DESCRIPTION: Versioned UTF-8 clipboard selections with binary-safe tree content
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#pragma once
#include "athena_document_xml.hpp"
#include <optional>

namespace athena::document {
inline constexpr const char* clipboard_mime= "application/x-athena-selection+xml";
inline constexpr const char* clipboard_source_version_key= "athena:clipboard-source-version";
// Runtime-only header metadata; the tuple's content/mode/language/move slots
// are unchanged. Helpers return detached snapshots and reject malformed headers
// or conflicting stamps. Removing a stamp does not authorize legacy encoding.
tree clipboard_with_version (const tree& selection, xml_format_version version);
std::optional<xml_format_version> clipboard_version (const tree& selection);
tree clipboard_without_version (const tree& selection);

// The runtime stamp is stripped before encoding. Explicit versions must match
// an existing stamp. Unmarked legacy selections retain metadata-based v1/v2
// selection, but never infer v3 from mathematical nodes or properties.
// Codec state and decoded trees belong to the caller, never to a shared cache.
std::string write_clipboard_xml (const tree& selection);
std::string write_clipboard_xml (const tree& selection, xml_format_version version);
// Strict envelope dispatch; the actual decoded version supplies the stamp.
tree read_clipboard_xml (std::string_view bytes);
// Call before any paste/cut-move/identity mutation. Both origin and target must
// represent the complete payload exactly; this never migrates source content.
void validate_clipboard_paste (const tree& selection, xml_format_version target);
}
