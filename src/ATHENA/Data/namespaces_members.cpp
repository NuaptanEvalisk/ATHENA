/******************************************************************************
* MODULE     : namespaces_members.cpp
* DESCRIPTION: Member collection for ATHENA vault namespaces
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
*******************************************************************************/

#include "namespaces_private.hpp"

#include "file.hpp"
#include "namespace_ontology.hpp"
#include "vault.hpp"

namespace athena_namespaces {

static std::string
stem_for_file (url u) {
  std::string p= tm_to_std (concretize (u));
  std::filesystem::path fp (p);
  return fp.stem ().string ();
}

static bool
is_ath_file (url u) {
  return suffix (u) == "ath";
}

static bool
append_unique_namespace_match (std::vector<athena_namespace_match>& out,
                               std::map<std::string,bool>& seen,
                               const athena_namespace_match& m) {
  std::string key= tm_to_std (m.file_path);
  if (seen[key]) return false;
  seen[key]= true;
  out.push_back (m);
  return true;
}

static bool
namespace_is_child_of (const athena_namespace_definition& child,
                       string parent) {
  for (int i=0; i<(int) child.parents.size (); i++)
    if (child.parents[i] == parent) return true;
  for (int i=0; i<(int) child.derived_parents.size (); i++)
    if (child.derived_parents[i] == parent) return true;
  return false;
}

static bool
collect_direct_namespace_members (const athena_namespace_definition& ns,
                                  std::vector<athena_namespace_match>& out,
                                  std::map<std::string,bool>& seen,
                                  string& error) {
  if (ns.kind == "abstract") return true;

  std::vector<template_token> toks;
  if (!parse_template (ns.templ, toks, error)) return false;

  array<url> files= vault_get_all_files ();
  for (int i=0; i<N(files); i++) {
    if (!is_ath_file (files[i])) continue;
    athena_namespace_match m;
    string err;
    if (match_stem (ns, stem_for_file (files[i]), m, err)) {
      m.file_path= concretize (files[i]);
      append_unique_namespace_match (out, seen, m);
    }
    else if (err != "") {
      error= err;
      return false;
    }
  }
  return true;
}

static bool
collect_namespace_members (const athena_namespace_definition& ns,
                           bool include_descendants,
                           std::map<std::string,bool>& visiting,
                           std::map<std::string,bool>& seen,
                           std::vector<athena_namespace_match>& out,
                           string& error) {
  std::string key= tm_to_std (ns.name);
  if (visiting[key]) return true;
  visiting[key]= true;

  if (!collect_direct_namespace_members (ns, out, seen, error)) {
    visiting[key]= false;
    return false;
  }

  if (include_descendants) {
    for (const athena_namespace_definition& child: athena_namespaces_list ()) {
      if (!namespace_is_child_of (child, ns.name)) continue;
      if (!collect_namespace_members (child, true, visiting, seen, out, error)) {
        visiting[key]= false;
        return false;
      }
    }
  }

  visiting[key]= false;
  return true;
}

} // namespace athena_namespaces

using namespace athena_namespaces;

namespace_records<athena_namespace_match>
athena_namespace_members (string name, string& error) {
  namespace_records<athena_namespace_match> out;
  if (!vault_active ()) {
    error= "No active vault.";
    return out;
  }
  std::shared_ptr<const athena_namespace_definition> ns;
  bool cached= athena_namespace_ontology_members (name, out, error, &ns);
  if (!cached && !athena_namespace_get (name, ns)) {
    error= "Unknown namespace: " * name;
    return out;
  }
  if (!cached) {
    error= "";
    std::map<std::string,bool> visiting;
    std::map<std::string,bool> seen;
    std::vector<athena_namespace_match> local;
    collect_namespace_members (*ns, ns->kind == "abstract", visiting, seen,
                               local, error);
    out= namespace_records<athena_namespace_match> (std::move (local));
    if (error != "") return out;
  }

  if (ns->sorter_trivial) return out;
  if (ns->sorter_path != "") {
    string sort_error;
    sorter_handle sorter= load_sorter (ns->sorter_path, sort_error);
    if (sort_error != "") error= sort_error;
    sort_namespace_members (sorter, out);
  }
  else {
    out.stable_sort ([] (const athena_namespace_match& a,
                        const athena_namespace_match& b) {
      return std::strcmp (a.stem.c_str (), b.stem.c_str ()) < 0;
    });
  }
  return out;
}
