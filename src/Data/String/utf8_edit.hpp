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
#include "array.hpp"

// Text only, never RAW_DATA or a legacy Cork atom. Positions remain byte offsets.
// The owning thread retains a bounded cache of COW text revisions and private
// ICU iterators; navigation does not copy a document or rescan each prefix.
bool utf8_grapheme_boundary (const string& text, int byte);
int utf8_grapheme_next (const string& text, int byte);
int utf8_grapheme_previous (const string& text, int byte);
int utf8_grapheme_snap (const string& text, int byte, bool forwards);

// Explicit native-byte / Scheme-character boundaries, never encoding guesses.
string utf8_text (string text);
int utf8_byte_length (string text);
int utf8_byte_to_character (string text, int byte);
int utf8_character_to_byte (string text, int character);
string utf8_byte_slice (string text, int begin, int end);
int utf8_grapheme_count (string text);
string utf8_forward_access (string text, int index);
string utf8_backward_access (string text, int index);
array<string> utf8_graphemes (string text);
