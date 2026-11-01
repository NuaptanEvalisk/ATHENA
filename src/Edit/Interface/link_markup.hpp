// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef ATHENA_LINK_MARKUP_HPP
#define ATHENA_LINK_MARKUP_HPP

#include "scheme.hpp"

object native_link_flatten (tree link);
object native_link_type (object link);
object native_link_attributes (object link);
object native_link_vertices (object link);
object native_link_source (object link);
object native_link_target (object link);
object native_vertex_id (object vertex);
object native_vertex_url (object vertex);
object native_vertex_script (object vertex);

string native_link_unique_id ();
void native_make_locus ();
object native_locus_id (tree locus);
void native_locus_set (string id, tree content);
void native_locus_insert_link (tree locus, tree link);
void native_locus_remove_link (tree link);
void native_locus_remove_all_links (tree locus, tree link);
list<tree> native_id_to_loci (string id);

#endif
