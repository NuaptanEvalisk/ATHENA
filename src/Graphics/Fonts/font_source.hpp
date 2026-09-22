/******************************************************************************
* MODULE     : font_source.hpp
* DESCRIPTION: Explicit physical font identity shared by shaping and export
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#pragma once

#include <string>

namespace athena::text {

struct font_file_source {
  std::string file_utf8; // Absolute system path, not a family-name lookup key.
  long face_index= 0;    // FreeType collection/named-instance index.
};

struct physical_font_source {
  font_file_source file;
  int point_size= 0;
  int horizontal_dpi= 0, vertical_dpi= 0;
};

} // namespace athena::text
