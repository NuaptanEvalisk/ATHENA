/******************************************************************************
* MODULE     : unicode_text_test.cpp
* DESCRIPTION: UTF-8 indexing and Unicode grapheme boundary regression tests
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#include "unicode_text.hpp"
#include <future>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace athena::text;
void require (bool value) {
  if (!value) throw std::runtime_error ("Unicode assertion failed");
}
template<class F> void rejects (F action) {
  bool caught= false;
  try { action (); }
  catch (const std::invalid_argument&) { caught= true; }
  catch (const std::out_of_range&) { caught= true; }
  require (caught);
}

void boundaries () {
  // ASCII, combining accent, CJK, emoji with modifier, ZWJ and flag sequences.
  std::vector<std::string> clusters= {
    "A", u8"e\u0301", u8"\u4e2d", u8"\U0001f44d\U0001f3fd",
    u8"\U0001f469\u200d\U0001f4bb", u8"\U0001f1e8\U0001f1f3", "\r\n"};
  std::string text;
  std::vector<std::size_t> offsets {0};
  for (const auto& cluster: clusters) {
    text+= cluster;
    offsets.push_back (text.size ());
  }
  grapheme_cursor cursor (text);
  for (std::size_t i= 0; i < clusters.size (); ++i) {
    require (cursor.boundary (offsets[i]));
    require (cursor.next (offsets[i]) == offsets[i + 1]);
    require (cursor.previous (offsets[i + 1]) == offsets[i]);
  }
  require (!cursor.boundary (2)); // Between e and its combining accent.
  require (cursor.next (2) == offsets[2]);
  require (cursor.previous (2) == offsets[1]);
  require (cursor.next (text.size ()) == text.size ());
  require (cursor.previous (0) == 0);
  rejects ([&] { cursor.next (3); }); // Inside the combining accent's UTF-8.
  rejects ([&] { cursor.reset ("\xff"); });
  require (cursor.next (0) == 1); // Failed rebind did not destroy the old state.
  cursor.reset ({});
  require (cursor.boundary (0) && cursor.next (0) == 0 && cursor.previous (0) == 0);
}

int main () {
  try {
    require (valid_utf8 ({}));
    require (valid_utf8 (std::string ("a\0b", 3)));
    for (const std::string bad: {"\x80", "\xc0\x80", "\xed\xa0\x80",
                                "\xf4\x90\x80\x80", "\xe4\xb8"}) {
      require (!valid_utf8 (bad));
      rejects ([&] { require_utf8 (bad); });
    }
    const std::string sample= u8"A\u4e2d\U0001f600e\u0301";
    const std::vector<std::size_t> bytes {0, 1, 4, 8, 9, 11};
    const std::vector<std::size_t> units {0, 1, 2, 4, 5, 6};
    for (std::size_t i= 0; i < bytes.size (); ++i) {
      require (byte_to_utf16 (sample, bytes[i]) == units[i]);
      require (utf16_to_byte (sample, units[i]) == bytes[i]);
      require (byte_to_codepoint (sample, bytes[i]) == i);
      require (codepoint_to_byte (sample, i) == bytes[i]);
      if (i + 1 < bytes.size ()) require (next_scalar (sample, bytes[i]) == bytes[i+1]);
      if (i != 0) require (previous_scalar (sample, bytes[i]) == bytes[i-1]);
    }
    rejects ([&] { utf16_to_byte (sample, 3); });
    rejects ([&] { byte_to_utf16 (sample, 2); });
    rejects ([&] { codepoint_to_byte (sample, 6); });
    boundaries ();
    std::vector<std::future<void>> workers;
    for (int i= 0; i < 4; ++i)
      workers.push_back (std::async (std::launch::async, boundaries));
    for (auto& worker: workers) worker.get ();
    std::cout << "UTF-8 and grapheme checks passed\n";
  }
  catch (const std::exception& e) {
    std::cerr << e.what () << '\n';
    return 1;
  }
}
