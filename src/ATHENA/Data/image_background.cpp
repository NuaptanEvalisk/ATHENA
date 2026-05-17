/******************************************************************************
* MODULE     : image_background.cpp
* DESCRIPTION: Image background processing helpers
* COPYRIGHT  : (C) 2026  Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "ATHENA/Data/image_background.hpp"

#include "scheme.hpp"

#include <MagickWand/MagickWand.h>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace {

static std::string
to_std (string s) {
  return std::string (as_charp (s), N(s));
}

static string
to_tm (const std::string& s) {
  return string (s.c_str ());
}

static std::string
lower_suffix (url u) {
  std::string ext= to_std (suffix (u));
  std::transform (ext.begin (), ext.end (), ext.begin (),
                  [] (unsigned char c) { return (char) std::tolower (c); });
  return ext;
}

static bool
preference_on (const string& name) {
  return get_preference (name, "off") == "on";
}

static void
ensure_magick_wand () {
  static bool initialized= false;
  if (!initialized) {
    MagickWandGenesis ();
    initialized= true;
  }
}

static std::string
magick_exception (MagickWand* wand) {
  ExceptionType severity;
  char* description= MagickGetException (wand, &severity);
  std::string out= description == nullptr ? "ImageMagick operation failed"
                                          : description;
  if (description != nullptr) description= (char*) MagickRelinquishMemory (description);
  return out;
}

static bool
near_white (PixelWand* pixel) {
  const Quantum fuzz= (Quantum) (0.06 * QuantumRange);
  return PixelGetRedQuantum (pixel) + fuzz >= QuantumRange &&
         PixelGetGreenQuantum (pixel) + fuzz >= QuantumRange &&
         PixelGetBlueQuantum (pixel) + fuzz >= QuantumRange;
}

static bool
has_white_corner (MagickWand* wand) {
  size_t w= MagickGetImageWidth (wand);
  size_t h= MagickGetImageHeight (wand);
  if (w == 0 || h == 0) return false;

  PixelWand* pixel= NewPixelWand ();
  bool result= false;
  const ssize_t xs[2]= {0, (ssize_t) w - 1};
  const ssize_t ys[2]= {0, (ssize_t) h - 1};
  for (ssize_t x: xs)
    for (ssize_t y: ys)
      if (MagickGetImagePixelColor (wand, x, y, pixel) != MagickFalse &&
          near_white (pixel))
        result= true;
  DestroyPixelWand (pixel);
  return result;
}

} // namespace

bool
image_auto_remove_background_enabled () {
  return preference_on ("image auto remove background");
}

bool
image_remove_white_background_png (url image, string& error) {
  if (lower_suffix (image) != "png") {
    error= "Remove background supports PNG images only.";
    return false;
  }

  fs::path path= fs::path (to_std (concretize (image)));
  if (!fs::is_regular_file (path)) {
    error= "Image file does not exist: " * to_tm (path.string ());
    return false;
  }

  ensure_magick_wand ();

  MagickWand* wand= NewMagickWand ();
  std::string filename= path.string ();
  if (MagickReadImage (wand, filename.c_str ()) == MagickFalse) {
    error= to_tm (magick_exception (wand));
    DestroyMagickWand (wand);
    return false;
  }

  if (!has_white_corner (wand)) {
    DestroyMagickWand (wand);
    return true;
  }

  PixelWand* white= NewPixelWand ();
  PixelSetColor (white, "white");
  MagickSetImageAlphaChannel (wand, ActivateAlphaChannel);
  MagickSetImageBackgroundColor (wand, white);
  MagickSetImageFuzz (wand, 0.06 * QuantumRange);
  if (MagickTransparentPaintImage (wand, white, 0.0, 0.06 * QuantumRange,
                                   MagickFalse) == MagickFalse) {
    error= to_tm (magick_exception (wand));
    DestroyPixelWand (white);
    DestroyMagickWand (wand);
    return false;
  }

  MagickSetImageFormat (wand, "PNG32");
  if (MagickWriteImage (wand, filename.c_str ()) == MagickFalse) {
    error= to_tm (magick_exception (wand));
    DestroyPixelWand (white);
    DestroyMagickWand (wand);
    return false;
  }

  DestroyPixelWand (white);
  DestroyMagickWand (wand);
  return true;
}
