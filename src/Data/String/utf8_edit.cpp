/******************************************************************************
* MODULE     : utf8_edit.cpp
* DESCRIPTION: Thread-private ICU navigation over immutable native text revisions
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#include "utf8_edit.hpp"
#include "unicode_text.hpp"
#include <algorithm>
#include <array>
#include <memory>
#include <stdexcept>

namespace {
struct text_revision {
  string text;
  athena::text::grapheme_cursor cursor;
  explicit text_revision (const string& source):
    text (source), cursor (std::string_view (text.data (), N(text))) {}
  bool matches (const string& source) const {
    // Heap strings share immutable storage. Short independent values can be
    // compared cheaply; do not scan large equal-but-unrelated atoms on lookup.
    return N(text) == N(source) &&
      (text.data () == source.data () || (N(text) <= 32 && text == source));
  }
};

athena::text::grapheme_cursor& cursor_for (const string& text) {
  thread_local std::array<std::unique_ptr<text_revision>, 4> cache;
  for (std::size_t i= 0; i < cache.size (); ++i)
    if (cache[i] && cache[i]->matches (text)) {
      std::rotate (cache.begin (), cache.begin () + i, cache.begin () + i + 1);
      return cache[0]->cursor;
    }
  // Construct before evicting so invalid input cannot poison another revision.
  auto revision= std::make_unique<text_revision> (text);
  std::rotate (cache.begin (), cache.end () - 1, cache.end ());
  cache[0]= std::move (revision);
  return cache[0]->cursor;
}

void check_position (const string& text, int byte) {
  if (byte < 0 || byte > N(text))
    throw std::out_of_range ("UTF-8 editor position outside text");
}
}

bool utf8_grapheme_boundary (const string& text, int byte) {
  if (byte < 0 || byte > N(text)) return false;
  if (!athena::text::scalar_boundary ({text.data (), std::size_t (N(text))}, byte))
    return false;
  return cursor_for (text).boundary (byte);
}

int utf8_grapheme_next (const string& text, int byte) {
  check_position (text, byte);
  return static_cast<int> (cursor_for (text).next (byte));
}

int utf8_grapheme_previous (const string& text, int byte) {
  check_position (text, byte);
  return static_cast<int> (cursor_for (text).previous (byte));
}

int utf8_grapheme_snap (const string& text, int byte, bool forwards) {
  byte= std::clamp (byte, 0, N(text));
  auto& cursor= cursor_for (text);
  const int original= byte;
  const std::string_view source (text.data (), N(text));
  while (!athena::text::scalar_boundary (source, byte)) --byte;
  if (forwards && byte != original) return static_cast<int> (cursor.next (byte));
  if (cursor.boundary (byte)) return byte;
  return static_cast<int> (forwards ? cursor.next (byte) : cursor.previous (byte));
}

string utf8_text (string text) {
  athena::text::require_utf8 ({text.data (), std::size_t (N(text))});
  return text;
}

int utf8_byte_length (string text) { return N(utf8_text (text)); }

int utf8_byte_to_character (string text, int byte) {
  check_position (text, byte);
  return static_cast<int> (athena::text::byte_to_codepoint (
    {text.data (), std::size_t (N(text))}, byte));
}

int utf8_character_to_byte (string text, int character) {
  if (character < 0) throw std::out_of_range ("Negative Unicode character index");
  return static_cast<int> (athena::text::codepoint_to_byte (
    {text.data (), std::size_t (N(text))}, character));
}

string utf8_byte_slice (string text, int begin, int end) {
  check_position (text, begin);
  check_position (text, end);
  utf8_text (text);
  const std::string_view source (text.data (), N(text));
  if (begin > end || !athena::text::scalar_boundary (source, begin) ||
      !athena::text::scalar_boundary (source, end))
    throw std::invalid_argument ("UTF-8 slice splits a scalar or reverses its range");
  return text (begin, end);
}

int utf8_grapheme_count (string text) {
  auto& cursor= cursor_for (text);
  int count= 0;
  for (std::size_t at=0; at<std::size_t (N(text)); at=cursor.next (at)) ++count;
  return count;
}

string utf8_forward_access (string text, int index) {
  if (index < 0) throw std::out_of_range ("Negative grapheme index");
  auto& cursor= cursor_for (text);
  int at= 0;
  while (index-- > 0 && at < N(text)) at= cursor.next (at);
  return at == N(text) ? string ("") : text (at, cursor.next (at));
}

string utf8_backward_access (string text, int index) {
  if (index < 0) throw std::out_of_range ("Negative grapheme index");
  auto& cursor= cursor_for (text);
  int at= N(text);
  while (index-- > 0 && at > 0) at= cursor.previous (at);
  return at == 0 ? string ("") : text (cursor.previous (at), at);
}

array<string> utf8_graphemes (string text) {
  auto& cursor= cursor_for (text);
  array<string> result;
  for (int at=0; at<N(text);) {
    const int end= cursor.next (at);
    result << text (at, end);
    at= end;
  }
  return result;
}
