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
#include <algorithm>

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

void paragraphs () {
  const std::string mixed= u8"abc \u05d0\u05d1\u05d2 xyz";
  unicode_paragraph paragraph (mixed);
  require (paragraph.base_level () == 0);
  const auto runs= paragraph.line (0, mixed.size ());
  require (runs.size () == 3);
  require (runs[0].begin == 0 && runs[0].end == 4 && !runs[0].right_to_left ());
  require (runs[1].begin == 4 && runs[1].end == 10 && runs[1].right_to_left ());
  require (runs[2].begin == 10 && runs[2].end == 14 && !runs[2].right_to_left ());
  const auto mixed_items= paragraph.items (0, mixed.size ());
  require (mixed_items.size () >= 3);
  bool hebrew= false, latin= false;
  for (const auto& item: mixed_items) {
    if (item.script == "Hebr") hebrew= true;
    if (item.script == "Latn") latin= true;
  }
  require (hebrew && latin);
  const std::string parentheses= u8"a(\u03b1)b";
  unicode_paragraph paired (parentheses);
  const auto paired_items= paired.items (0, parentheses.size ());
  require (paired_items.size () == 3 && paired_items[0].script == "Latn" &&
           paired_items[0].run.end == 2 && paired_items[1].script == "Grek" &&
           paired_items[1].run.begin == 2 && paired_items[1].run.end == 4 &&
           paired_items[2].script == "Latn" && paired_items[2].run.begin == 4);
  const std::string rtl_scripts= u8"\u05d0\u05d1\u0627\u0628";
  unicode_paragraph scripts (rtl_scripts);
  const auto rtl_items= scripts.items (0, rtl_scripts.size ());
  require (rtl_items.size () == 2 && rtl_items[0].script == "Arab" &&
           rtl_items[0].run.begin == 4 && rtl_items[0].run.end == 8 &&
           rtl_items[1].script == "Hebr" && rtl_items[1].run.begin == 0 &&
           rtl_items[1].run.end == 4);
  const std::string rtl= u8"\u05d0\u05d1 12";
  unicode_paragraph rtl_paragraph (rtl);
  require (rtl_paragraph.base_level () == 1);
  const auto rtl_runs= rtl_paragraph.line (0, rtl.size ());
  require (rtl_runs.size () == 2 && rtl_runs[0].begin == 5 &&
           rtl_runs[0].end == 7 && rtl_runs[0].level == 2 &&
           rtl_runs[1].begin == 0 && rtl_runs[1].end == 5 && rtl_runs[1].level == 1);

  // Bidi is resolved for the whole paragraph, but trailing line whitespace
  // must be reset to the paragraph level rather than the next word's level.
  const std::string wrapped= u8"\u05d0\u05d1 abc   xyz";
  unicode_paragraph wrap (wrapped);
  const auto whole= wrap.line (0, wrapped.size ());
  require (whole.size () == 2 && whole[0].begin == 5 && whole[0].level == 2);
  const auto first= wrap.line (0, 11);
  require (first.size () == 3 && first[0].begin == 8 &&
           first[0].end == 11 && first[0].level == 1 &&
           first[1].begin == 5 && first[1].end == 8 && first[1].level == 2);
  const auto second= wrap.line (11, wrapped.size ());
  require (second.size () == 1 && second[0].begin == 11 &&
           second[0].end == wrapped.size ());

  for (const std::string text: {std::string (), std::string ("a\0b", 3),
         std::string (u8"A\U0001f600e\u0301\u4e2d"),
         std::string (u8"a \u2067\u05d0\u05d1\u2069 z"),
         std::string (u8"\U0001f469\u200d\U0001f4bb \u05d0")}) {
    unicode_paragraph analyzed (text);
    grapheme_cursor cursor (text);
    for (std::size_t byte= 0;; byte= next_scalar (text, byte)) {
      const auto unit= analyzed.byte_to_utf16 (byte);
      require (unit == byte_to_utf16 (text, byte));
      require (analyzed.utf16_to_byte (unit) == byte);
      if (byte == text.size ()) break;
    }
    for (auto b: analyzed.breaks ()) require (cursor.boundary (b.byte));
    auto visual= analyzed.line (0, text.size ());
    std::sort (visual.begin (), visual.end (),
      [] (const bidi_run& a, const bidi_run& b) { return a.begin < b.begin; });
    std::size_t previous= 0;
    for (auto run: visual) {
      require (run.begin == previous && run.end > run.begin &&
               scalar_boundary (text, run.begin) && scalar_boundary (text, run.end));
      previous= run.end;
    }
    require (previous == text.size ());
    auto items= analyzed.items (0, text.size ());
    std::sort (items.begin (), items.end (),
      [] (const shaping_item& a, const shaping_item& b) { return a.run.begin < b.run.begin; });
    previous= 0;
    for (const auto& item: items) {
      require (item.script.size () == 4 && item.run.begin == previous &&
               item.run.end > item.run.begin && scalar_boundary (text, item.run.end));
      previous= item.run.end;
    }
    require (previous == text.size ());
    require (analyzed.line (text.size (), text.size ()).empty ());
  }
  unicode_paragraph ascii ("abc def");
  require (ascii.breaks ().size () == 2 && ascii.breaks ()[0].byte == 4 &&
           !ascii.breaks ()[0].mandatory && ascii.breaks ()[1].byte == 7);
  const std::string cjk= u8"\u4e2d\u6587";
  unicode_paragraph chinese (cjk, paragraph_direction::automatic_ltr, "zh");
  require (chinese.breaks ().size () == 2 && chinese.breaks ()[0].byte == 3);
  const std::string separated= u8"a\u2028b";
  unicode_paragraph hard (separated);
  require (hard.breaks ()[0].byte == 4 && hard.breaks ()[0].mandatory);
  rejects ([&] { hard.line (0, separated.size ()); });
  require (!hard.line (0, 4).empty () && !hard.line (4, 5).empty ());
  unicode_paragraph crlf ("a\r\n");
  require (crlf.breaks ().size () == 1 && crlf.breaks ()[0].byte == 3 &&
           crlf.breaks ()[0].mandatory);
  rejects ([&] { crlf.line (0, 2); });

  unicode_paragraph neutral ("123", paragraph_direction::automatic_rtl);
  require (neutral.base_level () == 1);
  unicode_paragraph forced (rtl, paragraph_direction::ltr);
  require (forced.base_level () == 0);
  unicode_paragraph empty ({});
  require (empty.line (0, 0).empty () && empty.breaks ().empty ());
  const std::string accent= u8"e\u0301";
  unicode_paragraph combining (accent);
  rejects ([&] { combining.line (0, 1); });
  rejects ([&] { combining.byte_to_utf16 (2); });
  const std::string emoji= u8"\U0001f600";
  unicode_paragraph nonbmp (emoji);
  rejects ([&] { nonbmp.utf16_to_byte (1); });
  rejects ([&] { nonbmp.line (3, 1); });
  rejects ([&] { nonbmp.line (0, 5); });
  rejects ([&] { unicode_paragraph invalid ("\xff"); });
  rejects ([&] { unicode_paragraph multiple ("first\nsecond"); });
  rejects ([&] { unicode_paragraph locale ("a", paragraph_direction::ltr,
                                           std::string ("en\0US", 5)); });
  bool limited= false;
  try { unicode_paragraph too_big ("123", paragraph_direction::ltr, "root", 2); }
  catch (const std::length_error&) { limited= true; }
  require (limited);
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
    paragraphs ();
    std::vector<std::future<void>> workers;
    for (int i= 0; i < 4; ++i)
      workers.push_back (std::async (std::launch::async, [] {
        boundaries ();
        paragraphs ();
      }));
    for (auto& worker: workers) worker.get ();
    std::cout << "UTF-8, grapheme, paragraph bidi and script checks passed\n";
  }
  catch (const std::exception& e) {
    std::cerr << e.what () << '\n';
    return 1;
  }
}
