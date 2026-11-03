/******************************************************************************
* MODULE     : image_background.hpp
* DESCRIPTION: Image background processing helpers
* COPYRIGHT  : (C) 2026  Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef IMAGE_BACKGROUND_HPP
#define IMAGE_BACKGROUND_HPP

#include "url.hpp"

bool image_remove_white_background_png (url image, string& error);
bool image_auto_remove_background_enabled ();

#endif // IMAGE_BACKGROUND_HPP
