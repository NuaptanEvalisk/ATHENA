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

namespace athena::document {
inline constexpr const char* clipboard_mime= "application/x-athena-selection+xml";
// The fragment contains the existing (texmacs, content, mode, language) tuple.
// Codec state and decoded trees belong to the caller, never to a shared cache.
std::string write_clipboard_xml (const tree& selection);
tree read_clipboard_xml (std::string_view bytes);
}
