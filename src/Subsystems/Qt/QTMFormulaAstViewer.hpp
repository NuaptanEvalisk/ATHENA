/******************************************************************************
* MODULE     : QTMFormulaAstViewer.hpp
* DESCRIPTION: Generic tree graph viewer and formula AST entry point
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef QTMFORMULAASTVIEWER_HPP
#define QTMFORMULAASTVIEWER_HPP

#include "string.hpp"
#include "tree.hpp"

void ast_viewer_show_tree (tree value, string title);
void formula_ast_show ();

#endif // QTMFORMULAASTVIEWER_HPP
