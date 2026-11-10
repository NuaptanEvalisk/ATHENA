/******************************************************************************
* MODULE     : edit_process.cpp
* DESCRIPTION: Editor entry points for generated directories
* COPYRIGHT  : (C) 1999 Joris van der Hoeven, (C) 2026 Nuaptan F. Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "Editor/Process/edit_process.hpp"
#include "App/Runtime/tm_buffer.hpp"
#include "Document/State/derived_directory.hpp"

edit_process_rep::edit_process_rep () {}
edit_process_rep::~edit_process_rep () {}

void
edit_process_rep::generate_table_of_contents (string channel) {
  tree result= athena::derived::table_of_contents (buf->data->aux[channel]);
  if (N(result) > 0) insert_tree (result);
}

void
edit_process_rep::generate_index (string channel) {
  tree result= athena::derived::index (buf->data->aux[channel], buf->data->ref,
    get_init_string ("language"), get_init_string ("index-break-style"));
  if (N(result) > 0) insert_tree (result);
}

void
edit_process_rep::generate_glossary (string channel) {
  tree result= athena::derived::glossary (buf->data->aux[channel]);
  if (N(result) > 0) insert_tree (result);
}

void
edit_process_rep::generate_aux_recursively (string which, tree st, path p) {
  if (which == "index") which= "the-index";
  if (which == "glossary") which= "the-glossary";
  for (int i=0; i<N(st); ++i) {
    tree t= st[i];
    if (!athena::derived::is_directory (t)) {
      if (is_compound (t)) generate_aux_recursively (which, t, p * i);
      continue;
    }
    const string kind= athena::derived::directory_kind (t);
    if (which != "" && which != kind) continue;
    tree result= athena::derived::directory (t, buf->data->aux[as_string (t[0])],
      buf->data->ref, get_init_string ("language"), get_init_string ("index-break-style"));
    if (N(result) == 0) result= tree (DOCUMENT, "");
    assign (p * path (i, N(t)-1), result);
    if (kind == "table-of-contents") toc_fold_set_at (p * i, false);
  }
}

void
edit_process_rep::generate_aux (string which) {
  typeset_invalidate_all ();
  typeset_forced ();
  generate_aux_recursively (which, subtree (et, rp), rp);
  init_update ();
}

bool
edit_process_rep::get_save_aux () {
  return as_bool (get_init_string (SAVE_AUX));
}
