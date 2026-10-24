/******************************************************************************
* MODULE     : totex_links.cpp
* DESCRIPTION: Native link and ATHENA navigation support for LaTeX export
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
*******************************************************************************/

#include "totex_internal.hpp"
#include "Edit/Interface/generic_editor_commands.hpp"
#include "ATHENA/Data/vault.hpp"
#include "ATHENA/Data/vault_map_sqlite.hpp"
#include "ATHENA/Data/transclusion_cache.hpp"
#include "Data/Convert/Generic/conversion_registry.hpp"
#include "file.hpp"
#include "converter.hpp"
#include <set>
#include <stdexcept>

namespace {

using namespace latex_export_internal;

scheme_tree
source_apply (string head, scheme_tree args) {
  scheme_tree out= stree_apply (head);
  if (stree_list (args))
    for (int i=0; i<N(args); ++i) out << args[i];
  return out;
}

scheme_tree
preserve_source (scheme_tree source) {
  scheme_tree records (TUPLE);
  records << stree_string (latex_export_athena_data_object (source));
  return latex_export_athena_data_inline (records);
}

scheme_tree
escape_hyperref_url (scheme_tree value) {
  if (string_atom (value)) {
    string out= atom_text (value);
    out= replace (out, "\\", "\\\\");
    out= replace (out, "#", "\\#");
    out= replace (out, "_", "\\_");
    return stree_string (out);
  }
  if (!stree_list (value)) return value;
  scheme_tree out (TUPLE);
  for (int i=0; i<N(value); ++i) out << escape_hyperref_url (value[i]);
  return out;
}

scheme_tree
hyperref_url (scheme_tree value) {
  return stree_string (latex_export_tt (escape_hyperref_url (value)));
}

scheme_tree
hlink_output (scheme_tree args) {
  if (!stree_list (args) || N(args) != 2) return tree ("#f");
  scheme_tree source= source_apply ("hlink", args);
  scheme_tree display= tmtex_convert (args[0]);
  scheme_tree destination= args[1];

  if (string_atom (destination)) {
    string target= atom_text (destination);
    if (starts (target, "tmfs://wikilink/")) {
      scheme_tree underline_args (TUPLE);
      underline_args << args[0];
      scheme_tree fallback= tmtex_convert (source_apply ("underline", underline_args));
      return latex_export_athena_data_wrap (source, fallback);
    }
    if (starts (target, "#")) {
      scheme_tree option= stree_apply ("!option");
      option << stree_string (target (1, N(target)));
      scheme_tree out= stree_apply ("hyperref");
      out << option << display;
      return out;
    }
  }

  scheme_tree out= stree_apply ("href");
  out << hyperref_url (destination) << display;
  return out;
}

bool
empty_cardlink_body (scheme_tree body) {
  return string_atom (body) && atom_text (body) == "";
}

scheme_tree
cardlink_default_body (scheme_tree destination) {
  return stree_string (generic_cardlink_default_link_body (
    scheme_tree_to_tree (destination)));
}

scheme_tree
cardlink_output (scheme_tree args) {
  if (!stree_list (args) || N(args) != 2) return tree ("#f");
  scheme_tree source= source_apply ("cardlink", args);
  scheme_tree display= empty_cardlink_body (args[0])
                        ? cardlink_default_body (args[1]) : args[0];
  scheme_tree link_args (TUPLE);
  link_args << display << args[1];
  return latex_export_athena_data_wrap (
    source, tmtex_convert (source_apply ("hlink", link_args)));
}

// Each recursive transclusion chain owns its database connection and ancestry.
// No editor cache or native tree is shared with another export thread.
struct transclusion_request {
  vault_context_handle vault= vault_capture_context ();
  AthenaVaultMapSqlite index;
  std::set<std::string> ancestry;
  transclusion_request () {
    if (!vault) throw std::runtime_error ("LaTeX transclusion export: no vault is open");
    std::string error;
    if (!index.open_read_only (vault->map_db, error))
      throw std::runtime_error ("LaTeX transclusion export: " + error);
  }
};
thread_local transclusion_request* active_transclusion= nullptr;

void
prepare_transcluded_content (tree& content, url directory) {
  if (is_atomic (content)) return;
  if (is_func (content, LABEL)) { content= tree (CONCAT); return; }
  if (is_func (content, IMAGE) && N(content) && is_atomic (content[0])) {
    string name= content[0]->label;
    if (name != "" && !starts (name, "/") && !starts (name, "~") &&
        !starts (name, "$") && !occurs ("://", name))
      content[0]= as_system_string (directory * url_unix (name));
  }
  for (int i=0; i<N(content); ++i) prepare_transcluded_content (content[i], directory);
}

scheme_tree
transclude_resolved (scheme_tree args, transclusion_request& request) {
  string id= atom_text (args[0]);
  std::string uuid (id.data (), N(id));
  if (!request.ancestry.insert (uuid).second)
    throw std::runtime_error ("LaTeX export: cyclic transclusion " + uuid);
  struct ancestry_scope {
    std::set<std::string>& values;
    std::string id;
    ~ancestry_scope () { values.erase (id); }
  } scope {request.ancestry, uuid};
  AthenaVaultMapNode location;
  bool found= false;
  std::string error;
  if (!request.index.get_node (uuid, location, found, error))
    throw std::runtime_error ("LaTeX transclusion export: " + error);
  if (!found) throw std::runtime_error ("LaTeX export: unresolved transclusion " + uuid);
  auto path= request.vault->root / location.path;
  url file= url_system (path.string ().c_str ());
  string data;
  if (load_string (file, data, false))
    throw std::runtime_error ("LaTeX export: cannot read transclusion source " + path.string ());
  tree document= conversion_read_document (data);
  tree content= athena_transclusion_source_range (extract (document, "body"),
    string (location.anchor_begin.c_str ()), string (location.anchor_end.c_str ()));
  if (content == UNINIT)
    throw std::runtime_error ("LaTeX export: transclusion anchors not found for " + uuid);
  prepare_transcluded_content (content, head (file));
  scheme_tree resolved= latex_export_prepare_v3_source (content);
  return latex_export_athena_data_wrap (
    source_apply ("transclude", args), tmtex_convert (resolved));
}

scheme_tree
transclude_output (scheme_tree args) {
  if (!stree_list (args) || N(args) != 4) return tree ("#f");
  if (active_transclusion) return transclude_resolved (args, *active_transclusion);
  transclusion_request request;
  struct request_scope {
    request_scope (transclusion_request& request) { active_transclusion= &request; }
    ~request_scope () { active_transclusion= nullptr; }
  } scope (request);
  return transclude_resolved (args, request);
}

scheme_tree
material_visible (scheme_tree rendered) {
  if (func_is (rendered, "hlink", 2) && string_atom (rendered[2]) &&
      starts (atom_text (rendered[2]), "tmfs://material/"))
    return tmtex_convert (rendered[1]);
  return tmtex_convert (rendered);
}

scheme_tree
material_citation_output (scheme_tree args) {
  scheme_tree source= source_apply ("material-citation", args);
  if (!stree_list (args) || N(args) != 2) return preserve_source (source);
  return latex_export_athena_data_wrap (source, material_visible (args[1]));
}

scheme_tree
referenced_materials_output (scheme_tree args) {
  scheme_tree source= source_apply ("referenced-materials", args);
  if (!stree_list (args) || N(args) != 3) return preserve_source (source);
  return latex_export_athena_data_wrap (source, tmtex_convert (args[2]));
}

} // namespace

scheme_tree
latex_export_link_dispatch (string key, scheme_tree args) {
  if (key == "hlink") return hlink_output (args);
  if (key == "cardlink") return cardlink_output (args);
  if (key == "transclude") return transclude_output (args);
  if (key == "material-citation") return material_citation_output (args);
  if (key == "referenced-materials") return referenced_materials_output (args);
  return tree ("#f");
}
