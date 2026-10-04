/******************************************************************************
* MODULE     : heading_word_count.cpp
* DESCRIPTION: Heading hierarchy and word-count helpers
* COPYRIGHT  : (C) 2026  Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "heading_word_count.hpp"
#include "enunciation_model.hpp"
#include "analyze.hpp"
#include "unicode_text.hpp"
#include <unicode/utf8.h>
#include <stdexcept>

static string
athena_tree_tag (tree t) {
  if (!is_compound (t)) return "";
  return as_string (L(t));
}

int
athena_heading_level (tree t) {
  if (is_atomic (t) || N(t) < 1) return 0;
  string s= athena_tree_tag (t);
  if (ends (s, "*")) s= s (0, N(s) - 1);
  if (s == "part") return 1;
  if (s == "chapter" || s == "appendix" ||
      s == "prologue" || s == "epilogue")
    return 2;
  if (s == "section") return 3;
  if (s == "subsection") return 4;
  if (s == "subsubsection") return 5;
  if (s == "paragraph") return 6;
  if (s == "subparagraph") return 7;
  return 0;
}

bool
athena_heading_title_tree (tree t) {
  string tag= athena_tree_tag (t);
  return tag == "title" || tag == "doc-title" ||
         tag == "tmdoc-title";
}

bool
athena_heading_skip_text (tree t) {
  string tag= athena_tree_tag (t);
  return tag == "label" || tag == "reference" || tag == "pageref" ||
         tag == "image" || tag == "include" || tag == "bibliography" ||
         tag == "folded-hidden";
}

static bool
athena_cjk_codepoint (unsigned int code) {
  return (code >= 0x3400 && code <= 0x9fff) ||
         (code >= 0xf900 && code <= 0xfaff) ||
         (code >= 0x3040 && code <= 0x30ff) ||
         (code >= 0xac00 && code <= 0xd7af);
}

static bool
athena_ascii_word_codepoint (unsigned int code) {
  return code < 128 &&
         (is_iso_alpha ((char) code) || is_numeric ((char) code));
}

static unsigned int
athena_utf8_codepoint (string s, int& offset) {
  UChar32 code= 0;
  U8_NEXT (s.data (), offset, N(s), code);
  if (code < 0) throw std::invalid_argument ("Invalid UTF-8 text");
  return (unsigned int) code;
}

int
athena_word_count_text (string s) {
  athena::text::require_utf8 (
    std::string_view (s.data (), static_cast<std::size_t> (N(s))));
  int count= 0;
  bool in_word= false;
  for (int i=0; i<N(s); ) {
    unsigned int code= athena_utf8_codepoint (s, i);
    if (athena_cjk_codepoint (code)) {
      if (in_word) in_word= false;
      count++;
    }
    else if (athena_ascii_word_codepoint (code) || code >= 128) {
      if (!in_word) {
        count++;
        in_word= true;
      }
    }
    else if (code != '\'' && code != 0x2019) {
      in_word= false;
    }
  }
  return count;
}

static void
athena_append_plain_text (tree t, string& out) {
  if (is_atomic (t)) {
    if (N(t->label) != 0) {
      if (N(out) != 0) out << " ";
      out << t->label;
    }
    return;
  }
  if (!is_compound (t) || athena_heading_skip_text (t)) return;
  for (int i=0; i<N(t); i++) athena_append_plain_text (t[i], out);
}

string
athena_plain_text_projection (tree t) {
  string result;
  athena_append_plain_text (t, result);
  return result;
}

namespace {

bool
athena_section_heading_tag (string tag) {
  if (ends (tag, "*")) tag= tag (0, N(tag) - 1);
  return tag == "part" || tag == "chapter" || tag == "appendix" ||
         tag == "section" || tag == "subsection" ||
         tag == "subsubsection" || tag == "paragraph" ||
         tag == "subparagraph";
}

string
athena_section_indent (string tag, bool short_style) {
  if (ends (tag, "*")) tag= tag (0, N(tag) - 1);
  int spaces= 0;
  if (tag == "section") spaces= 3;
  else if (tag == "subsection") spaces= 6;
  else if (tag == "subsubsection" || tag == "paragraph" ||
           tag == "subparagraph") spaces= 9;
  if (short_style && spaces >= 3) spaces-= 3;
  string result;
  for (int i=0; i<spaces; ++i) result << " ";
  return result;
}

bool
athena_automatic_section_tag (string tag) {
  return tag == "table-of-contents" || tag == "the-index" ||
         tag == "the-glossary" || tag == "list-of-figures" ||
         tag == "list-of-tables";
}

string
athena_automatic_section_title (string tag) {
  if (tag == "the-index") return "Index";
  if (tag == "the-glossary") return "Glossary";
  return upcase_first (replace (tag, "-", " "));
}

} // namespace

string
athena_section_title (tree t, bool indent, bool short_style) {
  if (is_atomic (t) || !is_compound (t)) return "no title";
  string tag= as_string (L(t));
  if (athena_section_heading_tag (tag) && N(t) > 0) {
    string title= downgrade_math_letters (athena_plain_text_projection (t[0]));
    return (indent ? athena_section_indent (tag, short_style) : string ("")) *
           title;
  }
  if (tag == "prologue" || tag == "epilogue" ||
      athena_automatic_section_tag (tag))
    return athena_automatic_section_title (tag);
  if (is_func (t, CONCAT)) {
    for (int i=0; i<N(t); ++i) {
      string title= athena_section_title (t[i], indent, short_style);
      if (title != "no title") return title;
    }
    return "no title";
  }
  if (tag == "shared" && N(t) == 3 && is_func (t[2], DOCUMENT) && N(t[2]) > 0)
    return athena_section_title (t[2][0], indent, short_style);
  return "no title";
}

int
athena_word_count_tree (tree t) {
  string text= athena_plain_text_projection (t);
  return athena_word_count_text (text);
}

int
athena_character_count_text (string s) {
  athena::text::require_utf8 (
    std::string_view (s.data (), static_cast<std::size_t> (N(s))));
  int count= 0;
  for (int i=0; i<N(s); ) {
    (void) athena_utf8_codepoint (s, i);
    count++;
  }
  return count;
}

static void
athena_append_plain_text_lines (tree t, string& out) {
  if (is_atomic (t)) {
    if (N(t->label) != 0) {
      if (N(out) != 0 && !ends (out, "\n")) out << " ";
      out << t->label;
    }
    return;
  }
  if (!is_compound (t) || athena_heading_skip_text (t)) return;
  if (is_func (t, DOCUMENT)) {
    for (int i=0; i<N(t); i++) {
      if (i != 0 && N(out) != 0 && !ends (out, "\n")) out << "\n";
      athena_append_plain_text_lines (t[i], out);
    }
    return;
  }
  for (int i=0; i<N(t); i++) athena_append_plain_text_lines (t[i], out);
}

static int
athena_line_count_text (string s) {
  if (N(s) == 0) return 0;
  int count= 1;
  for (int i=0; i<N(s); i++)
    if (s[i] == '\n') count++;
  return count;
}

athena_document_statistics
athena_document_statistics_tree (tree t) {
  athena_document_statistics stats;
  string words_text;
  athena_append_plain_text (t, words_text);
  stats.words= athena_word_count_text (words_text);
  stats.characters= athena_character_count_text (words_text);
  string lines_text;
  athena_append_plain_text_lines (t, lines_text);
  stats.lines= athena_line_count_text (lines_text);
  return stats;
}

int
athena_enunciation_word_count_at (tree doc, path p) {
  path q= p;
  if (!has_subtree (doc, q)) q= path_up (q);
  while (!is_nil (q) && has_subtree (doc, q)) {
    tree t= subtree (doc, q);
    int body= athena::enunciation::standard_registry ().body_index (t);
    if (body >= 0) return athena_word_count_tree (t[body]);
    q= path_up (q);
  }
  return 0;
}

static string
athena_int_string (int i) {
  return as_string (i);
}

string
athena_expand_statistics_format (string format,
  athena_document_statistics stats, int heading_words, int block_words) {
  string out;
  for (int i=0; i<N(format); i++) {
    if (format[i] != '%' || i + 1 >= N(format)) {
      out << format[i];
      continue;
    }
    char c= format[++i];
    if (c == '%') out << "%";
    else if (c == 'w') out << athena_int_string (stats.words);
    else if (c == 'c') out << athena_int_string (stats.characters);
    else if (c == 'l') out << athena_int_string (stats.lines);
    else if (c == 'h') out << athena_int_string (heading_words);
    else if (c == 's') out << athena_int_string (block_words);
    else {
      out << "%";
      out << c;
    }
  }
  return out;
}

string
athena_heading_title (tree t) {
  if (is_compound (t) && N(t) > 0) {
    string title;
    athena_append_plain_text (t[0], title);
    if (N(title) != 0) return title;
  }
  string fallback;
  athena_append_plain_text (t, fallback);
  return N(fallback) == 0 ? string ("Untitled") : fallback;
}

class athena_heading_word_count_builder {
public:
  athena_heading_word_count_builder (
    array<heading_word_count_entry>& entries2, path root_path2)
    : entries (entries2), root_path (root_path2) {}

  void scan (tree t, path rel= path ()) {
    if (is_atomic (t)) {
      add_words (athena_word_count_text (t->label));
      return;
    }
    if (!is_compound (t) || athena_heading_skip_text (t)) return;

    if (athena_heading_title_tree (t)) {
      heading_word_count_entry entry;
      entry.level= 0;
      entry.title= athena_heading_title (t);
      entry.words= 0;
      entry.tree_path= root_path * rel;
      entries << entry;
      return;
    }

    int level= athena_heading_level (t);
    if (level > 0) {
      while (N(open_indexes) > 0 &&
             entries[open_indexes[N(open_indexes) - 1]].level >= level)
        open_indexes= range (open_indexes, 0, N(open_indexes) - 1);

      heading_word_count_entry entry;
      entry.level= level;
      entry.title= athena_heading_title (t);
      entry.words= 0;
      entry.tree_path= root_path * rel;
      entries << entry;
      open_indexes << (N(entries) - 1);
      return;
    }

    for (int i=0; i<N(t); i++) scan (t[i], rel * i);
  }

private:
  void add_words (int words) {
    if (words <= 0) return;
    for (int i=0; i<N(open_indexes); i++)
      entries[open_indexes[i]].words += words;
  }

  array<heading_word_count_entry>& entries;
  array<int> open_indexes;
  path root_path;
};

array<heading_word_count_entry>
athena_heading_word_count_entries (tree doc, path root_path) {
  array<heading_word_count_entry> entries;
  athena_heading_word_count_builder builder (entries, root_path);
  builder.scan (doc);
  return entries;
}
