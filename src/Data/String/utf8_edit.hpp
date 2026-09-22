/******************************************************************************
* MODULE     : utf8_edit.hpp
* DESCRIPTION: Native editor UTF-8 byte positions at ICU grapheme boundaries
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#pragma once

#include "string.hpp"

// Text only, never RAW_DATA or a legacy Cork atom. Positions remain byte offsets.
// The owning thread retains a bounded cache of COW text revisions and private
// ICU iterators; navigation does not copy a document or rescan each prefix.
bool utf8_grapheme_boundary (const string& text, int byte);
int utf8_grapheme_next (const string& text, int byte);
int utf8_grapheme_previous (const string& text, int byte);
int utf8_grapheme_snap (const string& text, int byte, bool forwards);
