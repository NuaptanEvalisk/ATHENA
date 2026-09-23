/******************************************************************************
* MODULE     : utf8_line.hpp
* DESCRIPTION: Editable multi-font Unicode line boxes with bidi caret affinity
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#pragma once

#include "boxes.hpp"
#include "font.hpp"
#include "font_selection.hpp"

// Box paths use (relative UTF-8 byte, caret_affinity). Source tree paths remain
// ordinary absolute byte positions. Callers which retain only a tree path get
// the downstream affinity; editor cursor state must retain affinity separately.
// The nominal font supplies logical style/vertical metrics, not fallback glyphs.
box utf8_line_box (path ip, std::shared_ptr<athena::text::font_paragraph> paragraph,
                   int begin, int end, font nominal, pencil pen,
                   const athena::text::shaping_options& options= {},
                   brush background= brush (false), double horizontal_scale= 1.0);

// Join adjacent text and explicit zero-width source markers without losing
// their individual tree positions. Unsupported paint/effect combinations return
// a null box; callers must retain their original boxes and wrappers unchanged.
bool is_utf8_line_box (box b);
box join_utf8_line_boxes (path ip, array<box> pieces, array<bool> markers);

// Reassemble selected word fragments after line breaking/justification. Sources
// must share the same paragraph analysis; unrelated wrappers are left intact.
void reassemble_utf8_line (array<box>& pieces, array<SI>& spaces);
