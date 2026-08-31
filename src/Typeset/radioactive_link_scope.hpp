/******************************************************************************
* MODULE     : radioactive_link_scope.hpp
* DESCRIPTION: Structural scopes excluded from radioactive link matching
* COPYRIGHT  : (C) 2026  Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef RADIOACTIVE_LINK_SCOPE_H
#define RADIOACTIVE_LINK_SCOPE_H

#include "string.hpp"

inline bool
athena_suppresses_radioactive_links (string name) {
  return name == "doc-title" ||
         name == "heading-fold-title" ||
         name == "table-of-contents" ||
         name == "table-of-contents*" ||
         name == "screen-folded-table-of-contents" ||
         name == "screen-unfolded-table-of-contents" ||
         name == "screen-folded-table-of-contents*" ||
         name == "screen-unfolded-table-of-contents*" ||
         name == "render-table-of-contents" ||
         name == "render-folded-table-of-contents" ||
         name == "render-unfolded-table-of-contents";
}

inline bool
athena_allows_radioactive_link_path (bool accessible, bool transcluded) {
  return accessible || transcluded;
}

#endif // defined RADIOACTIVE_LINK_SCOPE_H
