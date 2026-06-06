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

int
athena_character_count_text (string s) {
  string utf= strict_cork_to_utf8 (s);
  int count= 0;
  for (int i=0; i<N(utf); ) {
    (void) decode_from_utf8 (utf, i);
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

static bool
athena_enunciation_tree (tree t) {
  if (!is_compound (t)) return false;
  string s= athena_tree_tag (t);
  if (ends (s, "*")) s= s (0, N(s) - 1);
  return s == "theorem" || s == "lemma" || s == "corollary" ||
         s == "proposition" || s == "axiom" || s == "definition" ||
         s == "notation" || s == "convention" || s == "conjecture" ||
         s == "law" || s == "remark" || s == "note" ||
         s == "example" || s == "warning" || s == "disambiguation" ||
         s == "acknowledgments" || s == "exercise" ||
         s == "problem" || s == "question" || s == "solution" ||
         s == "answer" || s == "proof" || s == "proof-alternative" ||
         s == "proof-standard" || s == "proof-of" || s == "quote-env";
}

static tree
athena_enunciation_body (tree t) {
  string s= athena_tree_tag (t);
  if ((s == "proof-of" || s == "proof-of*") && N(t) >= 3) return t[2];
  if (N(t) >= 2) return t[1];
  return "";
}

int
athena_enunciation_word_count_at (tree doc, path p) {
  path q= p;
  if (!has_subtree (doc, q)) q= path_up (q);
  while (!is_nil (q) && has_subtree (doc, q)) {
    tree t= subtree (doc, q);
    if (athena_enunciation_tree (t))
      return athena_word_count_tree (athena_enunciation_body (t));
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
