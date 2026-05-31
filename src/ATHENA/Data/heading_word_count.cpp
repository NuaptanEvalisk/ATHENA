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
#include "analyze.hpp"
#include "converter.hpp"

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
         tag == "tmdoc-title" || tag == "tmweb-title";
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

int
athena_word_count_text (string s) {
  string utf= strict_cork_to_utf8 (s);
  int count= 0;
  bool in_word= false;
  for (int i=0; i<N(utf); ) {
    unsigned int code= decode_from_utf8 (utf, i);
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

int
athena_word_count_tree (tree t) {
  string text;
  athena_append_plain_text (t, text);
  return athena_word_count_text (text);
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
