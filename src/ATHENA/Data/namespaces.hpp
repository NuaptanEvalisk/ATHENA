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

#include "array.hpp"
#include "namespace_records.hpp"
#include "tree.hpp"
#include "url.hpp"
#include "vault.hpp"

#include <vector>

struct athena_namespace_definition {
  // Empty only for name-based create/upsert requests; persisted rows have UUIDs.
  string  uuid;
  string  name;
  string  kind;
  string  templ;
  bool    sorter_trivial= false;
  string  sorter_path;
  string  style_path;
  string  initial_content_path;
  string  homepage_path;
  std::vector<string> parents;
  std::vector<string> derived_parents;
};

struct athena_namespace_match {
  string  file_path;
  url file_url () const { return url_system (file_path); }
  string  stem;
  std::vector<string> captures;
  std::vector<string> capture_types;
  bool    ambiguous;
};

struct athena_namespace_relation {
  string parent;
  string child;
  string decision;
  string source;
};

struct athena_namespace_template_field {
  string placeholder;
  string type;
};

namespace_records<athena_namespace_definition> athena_namespaces_list ();
enum class namespace_query_status { ok, not_found, stale, error };

// Admission rejects a closed/replaced vault. Admitted operations keep the
// captured database path; they never switch to a newly active vault.
namespace_query_status athena_namespace_get (
  const vault_context_handle& context, string name,
  std::shared_ptr<const athena_namespace_definition>& out, string& error);
namespace_query_status athena_namespace_get_by_uuid (
  const vault_context_handle& context, string uuid,
  std::shared_ptr<const athena_namespace_definition>& out, string& error);
namespace_query_status athena_namespaces_list (
  const vault_context_handle& context,
  namespace_records<athena_namespace_definition>& out, string& error);
namespace_query_status athena_namespace_relations_list (
  const vault_context_handle& context,
  namespace_records<athena_namespace_relation>& out, string& error);
bool athena_namespace_get (
  string name, std::shared_ptr<const athena_namespace_definition>& out);
bool athena_namespace_save (const athena_namespace_definition& ns,
                            string& error);
bool athena_namespace_save (const vault_context_handle& context,
                            const athena_namespace_definition& ns,
                            string& error);
bool athena_namespace_create (const vault_context_handle& context,
                              const athena_namespace_definition& ns,
                              string& error);
bool athena_namespace_remove (string name, string& error);
namespace_query_status athena_namespace_remove_by_uuid (
  const vault_context_handle& context, string uuid, string& error);
bool athena_namespace_refresh_derived (string& error);

namespace_records<athena_namespace_relation> athena_namespace_relations_list ();
bool athena_namespace_relation_set (string parent, string child,
                                    string decision, string source,
                                    string& error);
bool athena_namespace_relation_remove (string parent, string child,
                                       string& error);
bool athena_namespace_validate_relation (string parent, string child,
                                         bool ask_user, string& error);
bool athena_namespace_template_derives (string child_template,
                                        string parent_template,
                                        bool& derives, string& error);
bool athena_namespace_suggest_subproduct_template (string first_template,
                                                   string second_template,
                                                   bool aggressive_string,
                                                   string& suggestion,
                                                   string& error);
bool athena_namespace_sorter_source (const athena_namespace_definition& ns,
                                     string& source, string& error);
bool athena_namespace_generate_product_sorter (
  const athena_namespace_definition& first,
  const athena_namespace_definition& second,
  string product_template, string& sorter_path, string& error);
bool athena_namespace_generate_product_sorter (
  const vault_context_handle& context,
  const athena_namespace_definition& first,
  const athena_namespace_definition& second,
  string product_template, string& sorter_path, string& error);
bool athena_namespace_generate_restricted_sorter (
  const athena_namespace_definition& parent,
  string product_template, string& sorter_path, string& error);
bool athena_namespace_generate_restricted_sorter (
  const vault_context_handle& context,
  const athena_namespace_definition& parent,
  string product_template, string& sorter_path, string& error);
namespace_query_status athena_namespace_rename_by_uuid (
  const vault_context_handle& context, string uuid, string name, string& error);
namespace_query_status athena_namespace_relation_write_by_uuid (
  const vault_context_handle& context, string parent_uuid, string child_uuid,
  string decision, string source, bool remove, string& error);

namespace_records<athena_namespace_match>
athena_namespace_members (string name, string& error);
namespace_records<athena_namespace_match>
athena_namespace_members (const vault_context_handle& context, string uuid,
                          string& error);
bool athena_namespace_match_stem (const athena_namespace_definition& ns,
                                  string stem, athena_namespace_match& match,
                                  string& error);
namespace_records<athena_namespace_definition>
athena_namespace_concrete_matches_stem (string stem, string& error);
std::vector<athena_namespace_template_field>
athena_namespace_template_fields (const athena_namespace_definition& ns,
                                  string& error);
bool athena_namespace_build_stem (const athena_namespace_definition& ns,
                                  const array<string>& values, string& stem,
                                  string& error);
tree athena_namespace_apply_style_to_tree (
  tree doc, const athena_namespace_definition& ns, string base_root,
  string& error);
bool athena_namespace_create_file (
  const athena_namespace_definition& ns, url target, string base_root,
  bool use_initial_content, string& error);
bool athena_namespace_create_plain_file (url target, string& error);
tree athena_namespace_info_page (string tmfs_name);

#endif // ATHENA_NAMESPACES_HPP
