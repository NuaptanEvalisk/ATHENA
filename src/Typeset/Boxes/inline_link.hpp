/******************************************************************************
* MODULE     : inline_link.hpp
* DESCRIPTION: Semantic links retained when transparent text boxes are shaped
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#pragma once

#include "boxes.hpp"

struct inline_link_info {
  list<string> ids;
  string reference;
  string anchor;
  SI outline_pixel= 0;
};

// Only wrappers with unchanged geometry and transparent source navigation may
// opt in. Other modifiers remain boxes; a shaper must not infer their meaning.
class inline_link_source {
public:
  virtual ~inline_link_source () = default;
  virtual bool inline_link (box& body, inline_link_info& link) = 0;
};
