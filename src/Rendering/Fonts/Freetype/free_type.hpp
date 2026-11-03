/******************************************************************************
* MODULE     : free_type.hpp
* DESCRIPTION: Interface with Free Type II
* COPYRIGHT  : (C) 2003  Joris van der Hoeven
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef FREE_TYPE_H
#define FREE_TYPE_H
#include "tree.hpp"

#include <ft2build.h>
#include FT_FREETYPE_H

bool ft_initialize ();
bool ft_present ();
FT_Library current_ft_library ();

inline constexpr auto ft_new_face= &FT_New_Face;
inline constexpr auto ft_new_memory_face= &FT_New_Memory_Face;
inline constexpr auto ft_select_charmap= &FT_Select_Charmap;
inline constexpr auto ft_set_char_size= &FT_Set_Char_Size;
inline constexpr auto ft_get_char_index= &FT_Get_Char_Index;
inline constexpr auto ft_load_glyph= &FT_Load_Glyph;
inline constexpr auto ft_render_glyph= &FT_Render_Glyph;
inline constexpr auto ft_get_kerning= &FT_Get_Kerning;
inline constexpr auto ft_done_face= &FT_Done_Face;

#endif // FREE_TYPE_H
