/******************************************************************************
* MODULE     : data_art.hpp
* DESCRIPTION: Deterministic DataArt cover generation
* COPYRIGHT  : (C) 2026  Nuaptan F. Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef ATHENA_DATA_ART_HPP
#define ATHENA_DATA_ART_HPP

#include "string.hpp"
#include "tree.hpp"
#include "url.hpp"

// Returns the empty string on success and a diagnostic on failure.
string athena_data_art_generate (string seed, url output);

// Export a semantically complete snapshot through a real transient buffer.
// `document` contains the full buffer envelope (style/init/references/etc.) and
// `body` is the covered body to install in that transient buffer. Returns the
// empty string on success and a diagnostic on failure.
string athena_data_art_export_snapshot (
  url source, url output, tree document, tree body);

#endif // defined ATHENA_DATA_ART_HPP
