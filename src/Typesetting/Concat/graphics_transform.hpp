/******************************************************************************
* MODULE     : graphics_transform.hpp
* DESCRIPTION: Shared parsing of structured 2D graphics transformations
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
******************************************************************************/

#ifndef GRAPHICS_TRANSFORM_HPP
#define GRAPHICS_TRANSFORM_HPP

#include "frame.hpp"
#include "tree.hpp"

bool is_transformation (tree t);
frame get_transformation (tree t);

#endif // defined GRAPHICS_TRANSFORM_HPP
