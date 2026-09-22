/******************************************************************************
* MODULE     : unicode_text.cpp
* DESCRIPTION: ICU-backed Unicode validation, index conversion and segmentation
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#include "unicode_text.hpp"

#include <unicode/ubrk.h>
#include <unicode/ubidi.h>
#include <unicode/ustring.h>
#include <unicode/utext.h>
#include <unicode/utf8.h>
#include <unicode/utf16.h>
#include <pango/pango.h>
#include <limits>
#include <stdexcept>
#include <string>
#include <algorithm>

namespace athena::text {
namespace {
int32_t length (std::string_view text) {
  if (text.size () > std::size_t (std::numeric_limits<int32_t>::max ()))
    throw std::length_error ("Unicode text exceeds ICU index range");
  return static_cast<int32_t> (text.size ());
}

void position (std::string_view text, std::size_t byte) {
  (void) length (text);
  if (byte > text.size ()) throw std::out_of_range ("UTF-8 byte position");
  if (!scalar_boundary (text, byte))
    throw std::invalid_argument ("Position splits a UTF-8 scalar");
}

UChar32 read (std::string_view text, int32_t& at) {
  UChar32 scalar;
  U8_NEXT (text.data (), at, static_cast<int32_t> (text.size ()), scalar);
  if (scalar < 0) throw std::invalid_argument ("Malformed UTF-8 text");
  return scalar;
}

std::size_t to_units (std::string_view text, std::size_t byte, bool utf16) {
  require_utf8 (text);
  position (text, byte);
  std::size_t result= 0;
  int32_t at= 0;
  while (std::size_t (at) < byte) {
    auto scalar= read (text, at);
    result+= utf16 ? U16_LENGTH (scalar) : 1;
  }
  return result;
}

std::size_t from_units (std::string_view text, std::size_t units, bool utf16) {
  require_utf8 (text);
  int32_t at= 0;
  std::size_t used= 0;
  while (used < units && std::size_t (at) < text.size ()) {
    auto scalar= read (text, at);
    auto width= utf16 ? U16_LENGTH (scalar) : 1;
    if (std::size_t (width) > units - used)
      throw std::invalid_argument ("Position splits a UTF-16 surrogate pair");
    used+= width;
  }
  if (used != units) throw std::out_of_range ("Unicode character position");
  return static_cast<std::size_t> (at);
}

void checked (UErrorCode status) {
  if (U_FAILURE (status))
    throw std::runtime_error (std::string ("ICU: ") + u_errorName (status));
}
} // namespace

bool valid_utf8 (std::string_view text) noexcept {
  if (text.size () > std::size_t (std::numeric_limits<int32_t>::max ()))
    return false;
  int32_t at= 0, size= static_cast<int32_t> (text.size ());
  while (at < size) {
    UChar32 scalar;
    U8_NEXT (text.data (), at, size, scalar);
    if (scalar < 0) return false;
  }
  return true;
}

void require_utf8 (std::string_view text) {
  (void) length (text);
  if (!valid_utf8 (text)) throw std::invalid_argument ("Malformed UTF-8 text");
}

bool scalar_boundary (std::string_view text, std::size_t byte) noexcept {
  return byte <= text.size () &&
    (byte == text.size () || !U8_IS_TRAIL (text[byte]));
}

std::size_t next_scalar (std::string_view text, std::size_t byte) {
  position (text, byte);
  if (byte == text.size ()) return byte;
  int32_t at= static_cast<int32_t> (byte);
  (void) read (text, at);
  return at;
}

std::size_t previous_scalar (std::string_view text, std::size_t byte) {
  position (text, byte);
  if (byte == 0) return byte;
  int32_t at= static_cast<int32_t> (byte);
  UChar32 scalar;
  U8_PREV (text.data (), 0, at, scalar);
  if (scalar < 0) throw std::invalid_argument ("Malformed UTF-8 text");
  return at;
}

std::size_t byte_to_utf16 (std::string_view s, std::size_t i) {
  return to_units (s, i, true);
}
std::size_t utf16_to_byte (std::string_view s, std::size_t i) {
  return from_units (s, i, true);
}
std::size_t byte_to_codepoint (std::string_view s, std::size_t i) {
  return to_units (s, i, false);
}
std::size_t codepoint_to_byte (std::string_view s, std::size_t i) {
  return from_units (s, i, false);
}

struct grapheme_cursor::implementation {
  std::string_view text;
  UBreakIterator* iterator= nullptr;

  explicit implementation (std::string_view bytes): text (bytes) {
    require_utf8 (bytes);
    UErrorCode status= U_ZERO_ERROR;
    UText* input= utext_openUTF8 (nullptr, bytes.empty () ? "" : bytes.data (),
                                 length (bytes), &status);
    if (U_FAILURE (status)) {
      utext_close (input);
      checked (status);
    }
    iterator= ubrk_open (UBRK_CHARACTER, "root", nullptr, 0, &status);
    if (U_SUCCESS (status)) ubrk_setUText (iterator, input, &status);
    // ubrk_setUText clones the UText wrapper, not its underlying bytes.
    utext_close (input);
    if (U_FAILURE (status)) {
      ubrk_close (iterator);
      iterator= nullptr;
      checked (status);
    }
  }
  ~implementation () { ubrk_close (iterator); }
};

grapheme_cursor::grapheme_cursor (std::string_view text):
  impl_ (std::make_unique<implementation> (text)) {}
grapheme_cursor::~grapheme_cursor () = default;

void grapheme_cursor::reset (std::string_view text) {
  auto replacement= std::make_unique<implementation> (text);
  impl_= std::move (replacement);
}

bool grapheme_cursor::boundary (std::size_t byte) {
  position (impl_->text, byte);
  return ubrk_isBoundary (impl_->iterator, static_cast<int32_t> (byte));
}

std::size_t grapheme_cursor::next (std::size_t byte) {
  position (impl_->text, byte);
  int32_t result= ubrk_following (impl_->iterator, static_cast<int32_t> (byte));
  return result == UBRK_DONE ? impl_->text.size () : std::size_t (result);
}

std::size_t grapheme_cursor::previous (std::size_t byte) {
  position (impl_->text, byte);
  int32_t result= ubrk_preceding (impl_->iterator, static_cast<int32_t> (byte));
  return result == UBRK_DONE ? 0 : std::size_t (result);
}

struct unicode_paragraph::implementation {
  struct index { int32_t byte, units; };
  struct script_range { std::size_t begin, end; std::string tag; };
  std::string_view source;
  std::vector<index> indices;
  std::vector<UChar> utf16;
  std::vector<line_break> line_breaks;
  std::vector<script_range> scripts;
  std::unique_ptr<UBiDi, decltype (&ubidi_close)> paragraph {nullptr, ubidi_close};
  std::unique_ptr<UBiDi, decltype (&ubidi_close)> current_line {nullptr, ubidi_close};
  std::unique_ptr<grapheme_cursor> graphemes;
  std::uint8_t base= 0;

  implementation (std::string_view text, paragraph_direction direction,
                  std::string_view locale, std::size_t max_bytes): source (text) {
    if (text.size () > max_bytes)
      throw std::length_error ("Paragraph exceeds UTF-8 input budget");
    require_utf8 (text);
    if (locale.size () > 255 || locale.find ('\0') != std::string_view::npos)
      throw std::invalid_argument ("Invalid paragraph locale");
    UBiDiLevel requested;
    switch (direction) {
      case paragraph_direction::automatic_ltr: requested= UBIDI_DEFAULT_LTR; break;
      case paragraph_direction::automatic_rtl: requested= UBIDI_DEFAULT_RTL; break;
      case paragraph_direction::ltr: requested= 0; break;
      case paragraph_direction::rtl: requested= 1; break;
      default: throw std::invalid_argument ("Invalid paragraph direction");
    }
    const char* bytes= text.empty () ? "" : text.data ();
    UErrorCode status= U_ZERO_ERROR;
    int32_t units= 0;
    u_strFromUTF8 (nullptr, 0, &units, bytes, length (text), &status);
    if (status == U_BUFFER_OVERFLOW_ERROR) status= U_ZERO_ERROR;
    checked (status);
    utf16.resize (std::max (1, units));
    u_strFromUTF8 (utf16.data (), units, nullptr, bytes, length (text), &status);
    checked (status);
    status= U_ZERO_ERROR;

    indices.push_back ({0, 0});
    int32_t byte= 0;
    int32_t unit= 0;
    while (byte < length (text)) {
      const UChar32 scalar= read (text, byte);
      unit+= U16_LENGTH (scalar);
      indices.push_back ({byte, unit});
    }
    paragraph.reset (ubidi_openSized (units, 0, &status));
    checked (status);
    ubidi_setPara (paragraph.get (), utf16.data (), units, requested, nullptr, &status);
    checked (status);
    if (ubidi_countParagraphs (paragraph.get ()) > 1)
      throw std::invalid_argument ("Text contains more than one Unicode paragraph");
    base= ubidi_getParaLevel (paragraph.get ());
    current_line.reset (ubidi_open ());
    if (!current_line) throw std::bad_alloc ();
    graphemes= std::make_unique<grapheme_cursor> (text);

    const std::string language (locale);
    std::unique_ptr<UBreakIterator, decltype (&ubrk_close)> breaker (
      ubrk_open (UBRK_LINE, language.c_str (), nullptr, 0, &status), ubrk_close);
    checked (status);
    std::unique_ptr<UText, decltype (&utext_close)> input (
      utext_openUTF8 (nullptr, bytes, length (text), &status), utext_close);
    checked (status);
    ubrk_setUText (breaker.get (), input.get (), &status);
    checked (status);
    for (int32_t at= ubrk_first (breaker.get ());
         (at= ubrk_next (breaker.get ())) != UBRK_DONE;) {
      const auto offset= static_cast<std::size_t> (at);
      const auto rule= ubrk_getRuleStatus (breaker.get ());
      const bool hard= rule >= UBRK_LINE_HARD && rule < UBRK_LINE_HARD_LIMIT;
      // UAX #14 candidates must also satisfy our editing boundary contract.
      if (graphemes->boundary (offset)) line_breaks.push_back ({offset, hard});
      else if (hard)
        throw std::runtime_error ("ICU hard line break splits a grapheme");
    }
    if (!text.empty ()) {
      // Pango handles inherited/common script and paired punctuation; do not
      // replace that context-sensitive algorithm with per-scalar script tests.
      std::unique_ptr<PangoScriptIter, decltype (&pango_script_iter_free)> iter (
        pango_script_iter_new (bytes, length (text)), pango_script_iter_free);
      do {
        const char *start, *limit;
        PangoScript script;
        pango_script_iter_get_range (iter.get (), &start, &limit, &script);
        const auto iso= g_unicode_script_to_iso15924 (static_cast<GUnicodeScript> (script));
        if (iso == 0) throw std::runtime_error ("Pango returned an invalid script");
        const char tag[]= {static_cast<char> (iso >> 24),
                          static_cast<char> ((iso >> 16) & 0xff),
                          static_cast<char> ((iso >> 8) & 0xff),
                          static_cast<char> (iso & 0xff)};
        scripts.push_back ({static_cast<std::size_t> (start - bytes),
                            static_cast<std::size_t> (limit - bytes),
                            std::string (tag, 4)});
      } while (pango_script_iter_next (iter.get ()));
    }
  }
};

unicode_paragraph::unicode_paragraph (std::string_view text,
    paragraph_direction direction, std::string_view locale, std::size_t max_bytes):
  impl_ (std::make_unique<implementation> (text, direction, locale, max_bytes)) {}
unicode_paragraph::~unicode_paragraph () = default;

std::uint8_t unicode_paragraph::base_level () const { return impl_->base; }
std::string_view unicode_paragraph::source () const { return impl_->source; }
const std::vector<line_break>& unicode_paragraph::breaks () const {
  return impl_->line_breaks;
}

std::size_t unicode_paragraph::byte_to_utf16 (std::size_t byte) const {
  const auto& indices= impl_->indices;
  const auto at= std::lower_bound (indices.begin (), indices.end (), byte,
    [] (const implementation::index& i, std::size_t b) { return i.byte < b; });
  if (at == indices.end ()) throw std::out_of_range ("Paragraph byte offset");
  if (at->byte != byte) throw std::invalid_argument ("Position splits UTF-8");
  return at->units;
}

std::size_t unicode_paragraph::utf16_to_byte (std::size_t units) const {
  const auto& indices= impl_->indices;
  const auto at= std::lower_bound (indices.begin (), indices.end (), units,
    [] (const implementation::index& i, std::size_t u) { return i.units < u; });
  if (at == indices.end ()) throw std::out_of_range ("Paragraph UTF-16 offset");
  if (at->units != units) throw std::invalid_argument ("Position splits a surrogate");
  return at->byte;
}

std::vector<bidi_run> unicode_paragraph::line (std::size_t begin, std::size_t end) {
  if (begin > end) throw std::invalid_argument ("Inverted paragraph line range");
  const auto first= byte_to_utf16 (begin), last= byte_to_utf16 (end);
  if (!impl_->graphemes->boundary (begin) || !impl_->graphemes->boundary (end))
    throw std::invalid_argument ("Paragraph line splits a grapheme");
  const auto& breaks= impl_->line_breaks;
  auto at= std::upper_bound (breaks.begin (), breaks.end (), begin,
    [] (std::size_t b, const line_break& item) { return b < item.byte; });
  for (; at != breaks.end () && at->byte < end; ++at)
    if (at->mandatory) throw std::invalid_argument ("Line crosses a hard break");
  if (begin == end) return {};

  UErrorCode status= U_ZERO_ERROR;
  UBiDi* line= impl_->current_line.get ();
  ubidi_setLine (impl_->paragraph.get (), first, last, line, &status);
  checked (status);
  const int32_t count= ubidi_countRuns (line, &status);
  checked (status);
  std::vector<bidi_run> result;
  result.reserve (count);
  for (int32_t i= 0; i < count; ++i) {
    int32_t start, length;
    ubidi_getVisualRun (line, i, &start, &length);
    result.push_back ({utf16_to_byte (first + start),
                       utf16_to_byte (first + start + length),
                       ubidi_getLevelAt (line, start)});
  }
  return result;
}

std::vector<shaping_item> unicode_paragraph::items (std::size_t begin,
                                                  std::size_t end) {
  const auto runs= line (begin, end);
  std::vector<shaping_item> result;
  for (const auto& run: runs) {
    const auto first= result.size ();
    auto script= std::lower_bound (impl_->scripts.begin (), impl_->scripts.end (),
      run.begin, [] (const implementation::script_range& s, std::size_t byte) {
        return s.end <= byte;
      });
    for (; script != impl_->scripts.end () && script->begin < run.end; ++script)
      result.push_back ({{std::max (run.begin, script->begin),
                          std::min (run.end, script->end), run.level}, script->tag});
    // Bidi runs are already visual. Sub-items of an RTL run reverse their
    // order, not the logical byte range or the source text inside each item.
    if (run.right_to_left ())
      std::reverse (result.begin () + first, result.end ());
  }
  return result;
}
} // namespace athena::text
