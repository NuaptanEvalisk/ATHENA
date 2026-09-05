/******************************************************************************
* MODULE     : websites_selector.cpp
* DESCRIPTION: Website selector evaluation
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "ATHENA/Data/websites_internal.hpp"

namespace athena_websites {

std::string
file_rel_from_url (url file) {
  url rel = delta (vault_get_root () * url (""), file);
  return clean_relative (tm_to_std (as_unix_string (rel)));
}

std::set<std::string>
all_document_rels (const fs::path& root) {
  std::set<std::string> out;
  for (const fs::path& doc: scan_documents (root)) {
    fs::path rel = doc.lexically_relative (root);
    if (!rel.empty () && is_doc_path (rel))
      out.insert (generic_path (rel));
  }
  return out;
}

void
set_union_into (std::set<std::string>& a, const std::set<std::string>& b) {
  a.insert (b.begin (), b.end ());
}

std::set<std::string>
set_intersection_of (const std::set<std::string>& a,
                     const std::set<std::string>& b) {
  std::set<std::string> out;
  std::set_intersection (a.begin (), a.end (), b.begin (), b.end (),
                         std::inserter (out, out.begin ()));
  return out;
}

std::set<std::string>
set_difference_of (const std::set<std::string>& a,
                   const std::set<std::string>& b) {
  std::set<std::string> out;
  std::set_difference (a.begin (), a.end (), b.begin (), b.end (),
                       std::inserter (out, out.begin ()));
  return out;
}

std::set<std::string>
path_selector_files (const fs::path& root, const std::string& path) {
  std::set<std::string> out;
  std::string rel = clean_relative (path);
  fs::path abs = (root / rel).lexically_normal ();
  if (fs::is_regular_file (abs) && is_doc_path (abs)) {
    out.insert (generic_path (fs::path (rel)));
    return out;
  }
  for (const std::string& doc: all_document_rels (root)) {
    if (rel.empty () || doc == rel || starts_with (doc, rel + "/"))
      out.insert (doc);
  }
  return out;
}

std::map<std::string,std::vector<std::string> >
namespace_children () {
  std::map<std::string,athena_namespace_definition> all;
  for (const athena_namespace_definition& ns: athena_namespaces_list ())
    all[tm_to_std (ns.name)] = ns;

  std::set<std::string> denied;
  for (const athena_namespace_relation& r: athena_namespace_relations_list ()) {
    if (r.decision == "deny")
      denied.insert (tm_to_std (r.parent) + "\n" + tm_to_std (r.child));
  }

  std::map<std::string,std::vector<std::string> > children;
  for (const auto& item: all) {
    const std::string& child_name = item.first;
    const athena_namespace_definition& child = item.second;
    for (int i=0; i<(int) child.parents.size (); i++) {
      std::string parent = tm_to_std (child.parents[i]);
      if (all.count (parent) != 0 &&
          denied.count (parent + "\n" + child_name) == 0)
        children[parent].push_back (child_name);
    }
    for (int i=0; i<(int) child.derived_parents.size (); i++) {
      std::string parent = tm_to_std (child.derived_parents[i]);
      if (all.count (parent) != 0 &&
          denied.count (parent + "\n" + child_name) == 0)
        children[parent].push_back (child_name);
    }
  }

  for (const athena_namespace_relation& r: athena_namespace_relations_list ()) {
    std::string parent = tm_to_std (r.parent);
    std::string child = tm_to_std (r.child);
    if (all.count (parent) == 0 || all.count (child) == 0) continue;
    std::vector<std::string>& kids = children[parent];
    kids.erase (std::remove (kids.begin (), kids.end (), child), kids.end ());
    if (r.decision == "allow") kids.push_back (child);
  }

  for (auto& item: children) {
    std::sort (item.second.begin (), item.second.end ());
    item.second.erase (std::unique (item.second.begin (), item.second.end ()),
                       item.second.end ());
  }
  return children;
}

std::set<std::string>
namespace_descendants_inclusive (const std::string& root) {
  std::map<std::string,std::vector<std::string> > children =
    namespace_children ();
  std::set<std::string> seen;
  std::vector<std::string> pending;
  pending.push_back (root);
  while (!pending.empty ()) {
    std::string current = pending.back ();
    pending.pop_back ();
    if (seen.count (current) != 0) continue;
    seen.insert (current);
    for (const std::string& child: children[current])
      pending.push_back (child);
  }
  return seen;
}

std::set<std::string>
namespace_selector_files (const std::string& name) {
  std::set<std::string> out;
  for (const std::string& ns_name: namespace_descendants_inclusive (name)) {
    std::shared_ptr<const athena_namespace_definition> ns;
    if (!athena_namespace_get (std_to_tm (ns_name), ns)) continue;
    if (ns->kind == "abstract") continue;
    string error;
    namespace_records<athena_namespace_match> members =
      athena_namespace_members (std_to_tm (ns_name), error);
    for (const athena_namespace_match& match: members)
      out.insert (file_rel_from_url (match.file_url ()));
  }
  return out;
}

std::set<std::string>
eval_selector (const athena_website_selector& selector, const fs::path& root,
               const std::set<std::string>& universe) {
  if (selector.op == "path") return path_selector_files (root, selector.value);
  if (selector.op == "namespace") return namespace_selector_files (selector.value);
  if (selector.op == "not") {
    if (selector.children.empty ()) return universe;
    return set_difference_of (universe,
                              eval_selector (selector.children[0], root,
                                             universe));
  }
  if (selector.children.empty ()) return std::set<std::string> ();

  std::string combine_op = selector.op;
  if (combine_op == "nand") combine_op = "and";
  if (combine_op == "nor") combine_op = "or";

  std::set<std::string> out =
    eval_selector (selector.children[0], root, universe);
  for (size_t i=1; i<selector.children.size (); i++) {
    std::set<std::string> rhs =
      eval_selector (selector.children[i], root, universe);
    if (combine_op == "and") out = set_intersection_of (out, rhs);
    else if (combine_op == "xor") {
      std::set<std::string> both = set_intersection_of (out, rhs);
      set_union_into (out, rhs);
      out = set_difference_of (out, both);
    }
    else {
      set_union_into (out, rhs);
    }
  }
  if (selector.op == "nand")
    out = set_difference_of (universe, out);
  else if (selector.op == "nor")
    out = set_difference_of (universe, out);
  return out;
}

} // namespace athena_websites
