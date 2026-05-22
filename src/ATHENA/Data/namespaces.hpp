/******************************************************************************
* MODULE     : namespaces.hpp
* DESCRIPTION: ATHENA vault namespace registry
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef ATHENA_NAMESPACES_HPP
#define ATHENA_NAMESPACES_HPP

#include "Database/database.hpp"
#include "tree.hpp"
#include "url.hpp"

#include <vector>

struct athena_namespace_definition {
  string  name;
  string  kind;
  string  templ;
  bool    sorter_trivial;
  string  sorter_path;
  string  style_path;
  strings parents;
  strings derived_parents;
};

struct athena_namespace_match {
  url     file;
  string  stem;
  strings captures;
  strings capture_types;
  bool    ambiguous;
};

struct athena_namespace_relation {
  string parent;
  string child;
  string decision;
  string source;
};

std::vector<athena_namespace_definition> athena_namespaces_list ();
bool athena_namespace_get (string name, athena_namespace_definition& out);
bool athena_namespace_save (const athena_namespace_definition& ns,
                            string& error);
bool athena_namespace_remove (string name, string& error);

std::vector<athena_namespace_relation> athena_namespace_relations_list ();
bool athena_namespace_relation_set (string parent, string child,
                                    string decision, string source,
                                    string& error);
bool athena_namespace_relation_remove (string parent, string child,
                                       string& error);
bool athena_namespace_validate_relation (string parent, string child,
                                         bool ask_user, string& error);

std::vector<athena_namespace_match> athena_namespace_members (string name,
                                                              string& error);
tree athena_namespace_info_page (string tmfs_name);

#endif // ATHENA_NAMESPACES_HPP
