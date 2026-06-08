
/******************************************************************************
* MODULE     : free_type.cpp
* DESCRIPTION: Interface with Free Type II
* COPYRIGHT  : (C) 2003  Joris van der Hoeven
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "free_type.hpp"

static bool ft_initialized= false;
static bool ft_error      = true;

FT_Library ft_library;

FT_Error (*ft_init_freetype)  (FT_Library     *alibrary);
FT_Error (*ft_new_face)       (FT_Library     library,
			       const char*    filepathname,
			       FT_Long        face_index,
			       FT_Face*       aface);
FT_Error (*ft_new_memory_face) (FT_Library library,
            const FT_Byte* file_base,
            FT_Long        file_size,
            FT_Long        face_index,
            FT_Face*       aface);
FT_Error (*ft_select_charmap) (FT_Face        face,
			       FT_Encoding    encoding);
FT_Error (*ft_set_char_size)  (FT_Face        face,
			       FT_F26Dot6     char_width,
			       FT_F26Dot6     char_height,
			       FT_UInt        horz_resolution,
			       FT_UInt        vert_resolution);
FT_UInt  (*ft_get_char_index) (FT_Face        face,
			       FT_ULong       charcode);
FT_Error (*ft_load_glyph)     (FT_Face        face,
			       FT_UInt        glyph_index,
			       FT_Int         load_flags);
FT_Error (*ft_render_glyph)   (FT_GlyphSlot   slot,
			       FT_Render_Mode render_mode);
FT_Error (*ft_get_kerning)    (FT_Face        face,
                               FT_UInt        left_glyph,
                               FT_UInt        right_glyph,
                               FT_UInt        kern_mode,
                               FT_Vector      *akerning);
FT_Error (*ft_done_face)      (FT_Face        face);

typedef FT_Error (*glyph_renderer) (FT_GlyphSlot, FT_Render_Mode);

bool
ft_initialize () {
  if (ft_initialized) return ft_error;
  ft_initialized= true;
  ft_init_freetype = FT_Init_FreeType;
  ft_new_face      = FT_New_Face;
  ft_new_memory_face = FT_New_Memory_Face;
  ft_select_charmap= FT_Select_Charmap;
  ft_set_char_size = FT_Set_Char_Size;
  ft_get_char_index= FT_Get_Char_Index;
  ft_load_glyph    = FT_Load_Glyph;
  ft_render_glyph  = (glyph_renderer) ((void*) FT_Render_Glyph);
  ft_get_kerning   = FT_Get_Kerning;
  ft_done_face     = FT_Done_Face;
  if (ft_init_freetype (&ft_library)) return true;
  ft_error= false;
  return false;
}

bool
ft_present () {
  return !ft_initialize ();
}
