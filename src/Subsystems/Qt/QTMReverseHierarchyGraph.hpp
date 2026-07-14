/******************************************************************************
* MODULE     : QTMReverseHierarchyGraph.hpp
* DESCRIPTION: Reverse namespace hierarchy graph for the current document
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef QTMREVERSEHIERARCHYGRAPH_HPP
#define QTMREVERSEHIERARCHYGRAPH_HPP

#include "string.hpp"
#include "tree.hpp"

void reverse_hierarchy_graph_show ();
void reverse_hierarchy_graph_insert ();
tree reverse_hierarchy_graph_render (string size);
void direct_hierarchy_graph_show ();
void direct_hierarchy_graph_show_namespace (string name);
void global_hierarchy_graph_show ();
void local_reference_graph_show ();
void reference_graph_show ();
void hierarchy_graph_interactivity_changed ();

#endif // QTMREVERSEHIERARCHYGRAPH_HPP
