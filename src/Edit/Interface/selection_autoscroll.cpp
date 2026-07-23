/******************************************************************************
* MODULE     : selection_autoscroll.cpp
* DESCRIPTION: bounded edge scrolling while extending mouse selections
* COPYRIGHT  : (C) 2026  Nuaptan
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "selection_autoscroll.hpp"

#include <algorithm>

SI
selection_autoscroll_delta (SI position, SI lower, SI upper,
                            SI edge_width, SI maximum_step) {
  edge_width= std::max ((SI) 0, edge_width);
  maximum_step= std::max ((SI) 0, maximum_step);

  SI delta= 0;
  if (position < lower + edge_width)
    delta= position - (lower + edge_width);
  else if (position > upper - edge_width)
    delta= position - (upper - edge_width);

  return std::clamp (delta, -maximum_step, maximum_step);
}
