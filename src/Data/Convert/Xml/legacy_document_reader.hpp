/******************************************************************************
* MODULE     : legacy_document_reader.hpp
* DESCRIPTION: Bounded read-only entry points for legacy native serializations
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#pragma once
#include "athena_document_xml.hpp"

namespace athena::document {
tree read_legacy_markup (std::string_view, codec_limits = {});
tree read_legacy_scheme (std::string_view, codec_limits = {});
} // namespace athena::document
