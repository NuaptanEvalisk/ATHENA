/******************************************************************************
* MODULE     : heading_word_count.hpp
* DESCRIPTION: Heading hierarchy and word-count helpers
* COPYRIGHT  : (C) 2026  Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef HEADING_WORD_COUNT_HPP
#define HEADING_WORD_COUNT_HPP

#include "array.hpp"
#include "path.hpp"
#include "tree.hpp"

struct heading_word_count_entry {
  int    level;
  string title;
  int    words;
  path   tree_path;
};

int   athena_heading_level (tree t);
bool  athena_heading_title_tree (tree t);
bool  athena_heading_skip_text (tree t);
int   athena_word_count_text (string s);
int   athena_word_count_tree (tree t);
string athena_heading_title (tree t);
array<heading_word_count_entry> athena_heading_word_count_entries (
  tree doc, path root_path= path ());

#endif // HEADING_WORD_COUNT_HPP
