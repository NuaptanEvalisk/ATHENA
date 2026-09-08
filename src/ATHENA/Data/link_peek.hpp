/******************************************************************************
* MODULE     : link_peek.hpp
* DESCRIPTION: Read-only source documents for canvas link previews
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
******************************************************************************/

#ifndef ATHENA_LINK_PEEK_HPP
#define ATHENA_LINK_PEEK_HPP

#include "tree.hpp"
#include "url.hpp"

bool athena_link_peek_target (string target);
// Called on the requesting editor's owner. Never opens or changes a GUI buffer.
tree athena_link_peek_document (string target, url& source);
tree athena_link_peek_range (tree document, string begin, string end);

#endif
