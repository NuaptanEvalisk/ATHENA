/******************************************************************************
* MODULE     : namespace_ontology.hpp
* DESCRIPTION: Cached namespace ontology maintained by a background worker
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
*******************************************************************************/

#ifndef ATHENA_NAMESPACE_ONTOLOGY_HPP
#define ATHENA_NAMESPACE_ONTOLOGY_HPP

#include "namespaces.hpp"

enum athena_namespace_ontology_status {
  athena_namespace_ontology_inactive,
  athena_namespace_ontology_building,
  athena_namespace_ontology_ready,
  athena_namespace_ontology_failed
};

void athena_namespace_ontology_start (url vault_root, url namespace_db);
void athena_namespace_ontology_stop ();

// Schedule a refresh. A forced file refresh revisits every vault directory.
void athena_namespace_ontology_invalidate (bool force_files= false);
bool athena_namespace_ontology_refresh (bool force_files, string& error);
athena_namespace_ontology_status
athena_namespace_ontology_get_status (string& error);

bool athena_namespace_ontology_namespaces (
  namespace_records<athena_namespace_definition>& out);
bool athena_namespace_ontology_namespace (string name,
                                          std::shared_ptr<const athena_namespace_definition>& out);
bool athena_namespace_ontology_relations (
  namespace_records<athena_namespace_relation>& out);
bool athena_namespace_ontology_members (
  string name, namespace_records<athena_namespace_match>& out, string& error,
  std::shared_ptr<const athena_namespace_definition>* definition = nullptr);
bool athena_namespace_ontology_children (
  string name, bool simplified, namespace_records<string>& visible,
  namespace_records<string>& folded,
  string& error);

#endif // ATHENA_NAMESPACE_ONTOLOGY_HPP
