
/******************************************************************************
* MODULE     : latex_picture_fallback.hpp
* DESCRIPTION: picture fallback for unsupported LaTeX imports
* COPYRIGHT  : (C) 2013  François Poulain, Joris van der Hoeven
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef LATEX_PICTURE_FALLBACK_HPP
#define LATEX_PICTURE_FALLBACK_HPP

#include "tree.hpp"

tree latex_fallback_on_pictures (string source, tree parsed);
void set_latex_command (string command);

#endif // LATEX_PICTURE_FALLBACK_HPP
