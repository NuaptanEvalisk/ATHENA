/******************************************************************************
* MODULE     : selection_autoscroll.hpp
* DESCRIPTION: bounded edge scrolling while extending mouse selections
* COPYRIGHT  : (C) 2026  Nuaptan
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef SELECTION_AUTOSCROLL_H
#define SELECTION_AUTOSCROLL_H

#include "basic.hpp"

SI selection_autoscroll_delta (SI position, SI lower, SI upper,
                               SI edge_width, SI maximum_step);

#endif // defined SELECTION_AUTOSCROLL_H
