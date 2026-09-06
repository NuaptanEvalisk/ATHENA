/******************************************************************************
* MODULE     : QTMVaultPreviewBuilder.hpp
* DESCRIPTION: Vault preview tree builders
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef QTMVAULTPREVIEWBUILDER_HPP
#define QTMVAULTPREVIEWBUILDER_HPP

#include "path.hpp"
#include "tree.hpp"
#include "url.hpp"

tree apply_vault_preferred_font_to_preview (tree body);
tree import_body (url file);
tree import_body_for_preview (url file);
tree build_preview_from_body (tree body, path focus, int* firstOut= nullptr,
                              int* lastOut= nullptr);
tree build_preview_from_anchor_range (tree body, path upper, path lower,
                                      int* firstOut= nullptr,
                                      int* lastOut= nullptr,
                                      bool detached= true);

#endif // QTMVAULTPREVIEWBUILDER_HPP
