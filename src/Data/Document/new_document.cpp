
/******************************************************************************
* MODULE     : new_document.cpp
* DESCRIPTION: Selection and lifetime of per-buffer edit trees
* COPYRIGHT  : (C) 1999-2011  Joris van der Hoeven
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "new_document.hpp"

/******************************************************************************
* Management of edit-tree ownership contexts
******************************************************************************/

static thread_local tree* thread_document_tree= nullptr;

tree
make_document_tree () {
  tree document= tuple (tree (DOCUMENT, ""));
  attach_ip (document, path ());
  return document;
}

tree&
current_document_tree () noexcept {
  static tree default_document_tree= make_document_tree ();
  return thread_document_tree == nullptr ? default_document_tree
                                         : *thread_document_tree;
}

tree*
swap_current_document_tree (tree* document) noexcept {
  tree* previous= thread_document_tree;
  thread_document_tree= document;
  return previous;
}

void
reset_document_tree (tree& document) {
  clean_observers (document);
  document= make_document_tree ();
}

void
set_document (tree& document, path rp, tree t) {
  with_document_tree scope (&document);
  assign (subtree (document, rp), copy (t));
}
