/******************************************************************************
* MODULE     : qt_resvg_fonts.hpp
* DESCRIPTION: Owner-scoped ATHENA font injection for resvg
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#pragma once

#include <cstdint>

class ResvgOptions;

// Returns the ATHENA font-catalog generation used by resvg on this platform.
// Desktop resvg keeps its historical system-font scan; iPadOS instead injects
// explicit physical files from ATHENA's font catalog.
std::uint64_t athena_resvg_font_generation ();
void athena_configure_resvg_fonts (ResvgOptions& options);
