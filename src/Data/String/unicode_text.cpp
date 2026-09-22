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
#include <unicode/utext.h>
#include <unicode/utf8.h>
#include <unicode/utf16.h>
#include <limits>
#include <stdexcept>
#include <string>

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
} // namespace athena::text
