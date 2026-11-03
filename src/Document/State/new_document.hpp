
/******************************************************************************
* MODULE     : new_document.hpp
* DESCRIPTION: Management of the global TeXmacs tree
* COPYRIGHT  : (C) 1999-2011  Joris van der Hoeven
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef NEW_DOCUMENT_H
#define NEW_DOCUMENT_H
#include "tree.hpp"
#include "path.hpp"

tree make_document_tree ();
tree& current_document_tree () noexcept;
tree* swap_current_document_tree (tree* document) noexcept;

class with_document_tree {
  tree* previous;

public:
  inline explicit with_document_tree (tree* document):
    previous (swap_current_document_tree (document)) {}
  inline ~with_document_tree () { swap_current_document_tree (previous); }

  with_document_tree (const with_document_tree&)= delete;
  with_document_tree& operator = (const with_document_tree&)= delete;
};

void reset_document_tree (tree& document);
void set_document (tree& document, path rp, tree t);

#endif // NEW_DOCUMENT_H
