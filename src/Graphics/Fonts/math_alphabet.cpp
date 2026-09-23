/******************************************************************************
* MODULE     : math_alphabet.cpp
* DESCRIPTION: ICU-backed mathematical alphabet glyph selection
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#include "math_alphabet.hpp"
#include <unicode/uchar.h>
#include <unicode/unorm2.h>
#include <unicode/utf16.h>
#include <map>
#include <stdexcept>
#include <string_view>

namespace athena::text {
namespace {
math_alphabet named_alphabet (std::string_view name) {
  constexpr std::string_view prefix= "MATHEMATICAL ";
  if (name.substr (0, prefix.size ()) == prefix) name.remove_prefix (prefix.size ());
  // U+210E fills the reserved mathematical italic small h position.
  if (name == "PLANCK CONSTANT") return math_alphabet::italic;
  const std::pair<std::string_view, math_alphabet> styles[]= {
    {"SANS-SERIF BOLD ITALIC ", math_alphabet::bold_italic_sans},
    {"SANS-SERIF BOLD ", math_alphabet::bold_sans},
    {"SANS-SERIF ITALIC ", math_alphabet::italic_sans},
    {"SANS-SERIF ", math_alphabet::sans},
    {"BOLD ITALIC ", math_alphabet::bold_italic},
    {"BOLD SCRIPT ", math_alphabet::bold_script},
    {"BOLD FRAKTUR ", math_alphabet::bold_fraktur},
    {"BOLD ", math_alphabet::bold}, {"ITALIC ", math_alphabet::italic},
    {"SCRIPT ", math_alphabet::script}, {"FRAKTUR ", math_alphabet::fraktur},
    {"BLACK-LETTER ", math_alphabet::fraktur},
    {"DOUBLE-STRUCK ", math_alphabet::double_struck},
    {"MONOSPACE ", math_alphabet::monospace}};
  if (name.substr (0, 21) == "DOUBLE-STRUCK ITALIC ") return math_alphabet::normal;
  for (const auto& style: styles)
    if (name.substr (0, style.first.size ()) == style.first) return style.second;
  return math_alphabet::normal;
}

struct alphabet_data {
  std::map<std::pair<math_alphabet, char32_t>, char32_t> variants;
  std::map<char32_t, math_alphabet> styles;
  alphabet_data () {
    UErrorCode status= U_ZERO_ERROR;
    const auto* normalization= unorm2_getNFKCInstance (&status);
    if (U_FAILURE (status)) throw std::runtime_error ("Cannot read Unicode math data");
    for (auto range: {std::pair<UChar32,UChar32> {0x2100, 0x214f}, {0x1d400, 0x1d7ff}}) {
      for (UChar32 c= range.first; c<=range.second; ++c) {
        if (u_getIntPropertyValue (c, UCHAR_DECOMPOSITION_TYPE) != U_DT_FONT) continue;
        char name[160];
        const int length= u_charName (c, U_UNICODE_CHAR_NAME, name, sizeof (name), &status);
        if (U_FAILURE (status)) throw std::runtime_error ("Cannot read Unicode math name");
        const auto style= named_alphabet ({name, static_cast<std::size_t> (length)});
        if (style == math_alphabet::normal) continue;
        UChar decomposition[8];
        const int count= unorm2_getRawDecomposition (normalization, c, decomposition, 8, &status);
        if (U_FAILURE (status)) throw std::runtime_error ("Cannot read Unicode math decomposition");
        if (count <= 0) continue;
        int offset= 0;
        UChar32 base;
        U16_NEXT (decomposition, offset, count, base);
        if (base < 0 || offset != count) continue;
        styles.emplace (c, style);
        // Mathematical Alphanumeric Symbols take precedence over historical
        // duplicates, while reserved holes use their Letterlike Symbols entry.
        variants[{style, static_cast<char32_t> (base)}]= c;
      }
    }
  }
};

const alphabet_data& data () {
  static const alphabet_data value;
  return value;
}
}

char32_t math_variant_character (char32_t character, math_alphabet alphabet) {
  if (alphabet == math_alphabet::normal) return character;
  const auto& table= data ();
  if (table.styles.count (character)) return character;
  const auto found= table.variants.find ({alphabet, character});
  return found == table.variants.end () ? character : found->second;
}

math_alphabet math_character_alphabet (char32_t character) {
  const auto& table= data ().styles;
  const auto found= table.find (character);
  return found == table.end () ? math_alphabet::normal : found->second;
}
} // namespace athena::text
