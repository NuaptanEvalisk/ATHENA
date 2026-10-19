/******************************************************************************
* DESCRIPTION: Cursor-only layouts for vacant native script slots
* COPYRIGHT  : (C) 2026 ATHENA contributors
* This software falls under the GNU general public license version 3 or later.
******************************************************************************/
#ifndef ATHENA_NATIVE_SCRIPT_CURSOR_HPP
#define ATHENA_NATIVE_SCRIPT_CURSOR_HPP

#include "boxes.hpp"

// Each non-null layout contains the visible scripts plus one vacant slot.
// These layouts supply cursor geometry only; they never contribute ink/size.
box native_script_cursor_box (path ip, box visible, array<box> layouts,
                              path base_position);

#endif
