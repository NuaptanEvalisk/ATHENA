/******************************************************************************
* MODULE     : clipboard_xml.cpp
* DESCRIPTION: Validate clipboard envelopes around the native XML tree codec
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#include "clipboard_xml.hpp"

namespace athena::document {
namespace {
void validate_selection (const tree& selection) {
  if (!is_tuple (selection, "texmacs", 3) ||
      !is_atomic (selection[2]) || !is_atomic (selection[3]))
    throw codec_exception (codec_error::invalid_structure,
                           "Invalid ATHENA clipboard selection envelope");
}
}
std::string write_clipboard_xml (const tree& selection) {
  validate_selection (selection);
  return write_xml (selection, xml_kind::fragment);
}
tree read_clipboard_xml (std::string_view bytes) {
  tree selection= read_xml (bytes, xml_kind::fragment);
  validate_selection (selection);
  return selection;
}
}
