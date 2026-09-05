/******************************************************************************
* MODULE     : namespaces_info.cpp
* DESCRIPTION: TMFS info page rendering for ATHENA namespaces
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
*******************************************************************************/

#include "namespaces_private.hpp"

#include "ATHENA/Data/new_buffer.hpp"
#include "converter.hpp"
#include "file.hpp"
#include "scheme.hpp"
#include "vault.hpp"

namespace athena_namespaces {

static std::string
percent_decode (const std::string& s) {
  std::string out;
  for (size_t i=0; i<s.size (); i++) {
    if (s[i] == '%' && i + 2 < s.size ()) {
      int hi= std::isxdigit ((unsigned char) s[i + 1]) ?
              std::toupper ((unsigned char) s[i + 1]) : -1;
      int lo= std::isxdigit ((unsigned char) s[i + 2]) ?
              std::toupper ((unsigned char) s[i + 2]) : -1;
      if (hi >= 0 && lo >= 0) {
        hi= hi <= '9' ? hi - '0' : hi - 'A' + 10;
        lo= lo <= '9' ? lo - '0' : lo - 'A' + 10;
        out.push_back ((char) ((hi << 4) | lo));
        i += 2;
        continue;
      }
    }
    out.push_back (s[i]);
  }
  return out;
}

static std::vector<string>
split_tmfs_path (string path) {
  std::vector<string> out;
  std::string s= tm_to_std (path);
  size_t start= 0;
  while (start <= s.size ()) {
    size_t p= s.find ('/', start);
    std::string item= percent_decode (
      s.substr (start, p == std::string::npos ? std::string::npos : p - start));
    if (!item.empty ()) out.push_back (std_to_tm (item));
    if (p == std::string::npos) break;
    start= p + 1;
  }
  return out;
}

static tree
text (const std::string& s) {
  return tree (std_to_tm (s));
}

static tree
line (const std::string& s) {
  return compound ("paragraph*", text (s));
}

static tree
line_tm (string s) {
  return compound ("paragraph*", tree (s));
}

static tree
link_to_url (string label, url target) {
  return compound ("hlink", tree (label), tree (concretize (target)));
}

static tree
namespace_link (string label, string tmfs_path) {
  return compound ("hlink", tree (label), tree ("tmfs://ns/" * tmfs_path));
}

static string
join_path (const std::vector<string>& path, bool technical) {
  string out= "";
  for (size_t i=0; i<path.size (); i++) {
    if (i != 0) out << "/";
    if (technical && i + 1 == path.size ()) out << "!";
    out << path[i];
  }
  return out;
}

static tree
document_for_body (tree body, bool use_vault_font= false) {
  tree doc (DOCUMENT);
  doc << compound ("TeXmacs", TEXMACS_COMPAT_VERSION);
  doc << compound ("style", tuple ("generic"));
  doc << compound ("body", body);
  if (use_vault_font) {
    string font= get_preference ("vault preferred font", "");
    if (font != "") {
      tree initial (COLLECTION);
      initial << compound ("associate", "font", font);
      initial << compound ("associate", "font-family", "rm");
      doc << compound ("initial", initial);
    }
  }
  return doc;
}

static tree
error_page (const std::string& title, const std::string& message) {
  tree body (DOCUMENT);
  body << compound ("section*", text (title));
  body << line (message);
  return document_for_body (body);
}

static tree
members_tree (const std::vector<athena_namespace_match>& members) {
  tree body (DOCUMENT);
  if (members.empty ()) {
    body << line ("No matching .ath files.");
    return body;
  }
  for (const athena_namespace_match& m: members) {
    tree link= link_to_url (m.stem, m.file);
    body << compound ("paragraph*", link);
  }
  return body;
}

static array<string>
namespace_children (string name) {
  array<string> out;
  for (const athena_namespace_definition& ns: athena_namespaces_list ()) {
    if (has_string (ns.parents, name) || has_string (ns.derived_parents, name))
      out << ns.name;
  }
  return out;
}

static array<string>
namespace_all_parents (const athena_namespace_definition& ns) {
  array<string> out;
  for (int i=0; i<N(ns.parents); i++)
    if (!has_string (out, ns.parents[i])) out << ns.parents[i];
  for (int i=0; i<N(ns.derived_parents); i++)
    if (!has_string (out, ns.derived_parents[i])) out << ns.derived_parents[i];
  return out;
}

static tree
namespace_list_tree (const array<string>& names) {
  tree body (DOCUMENT);
  if (N(names) == 0) {
    body << line ("<none>");
    return body;
  }
  for (int i=0; i<N(names); i++)
    body << compound ("paragraph*", namespace_link (names[i], names[i]));
  return body;
}

static url
namespace_path_url (string path) {
  std::filesystem::path p (tm_to_std (path));
  if (p.is_absolute ()) return url_system (path);
  return vault_get_root () * url (path);
}

static tree
sorter_tree (const athena_namespace_definition& ns) {
  if (ns.kind == "abstract") return tree ("<none>");
  if (ns.sorter_trivial) return tree ("trivial");
  if (ns.sorter_path == "") return tree ("<none>");
  return link_to_url (ns.sorter_path, namespace_path_url (ns.sorter_path));
}

static bool
is_ns_tag (tree t, const char* name) {
  string s= std_to_tm (std::string ("<") + name + ">");
  return (is_atomic (t) && t->label == s) || is_compound (t, name, 0);
}

static tree namespace_dynamic_tree (
  const athena_namespace_definition& ns,
  const std::vector<string>& path,
  const std::vector<athena_namespace_match>& members,
  tree t);

static bool
namespace_homepage_needs_members (tree t) {
  if (is_ns_tag (t, "ns-matches")) return true;
  if (is_atomic (t)) return false;
  for (int i=0; i<N(t); i++)
    if (namespace_homepage_needs_members (t[i])) return true;
  return false;
}

static tree
namespace_dynamic_replacement (const athena_namespace_definition& ns,
                               const std::vector<string>& path,
                               const std::vector<athena_namespace_match>& members,
                               tree t, bool& replaced) {
  replaced= true;
  if (is_ns_tag (t, "ns-name")) return tree (ns.name);
  if (is_ns_tag (t, "ns-type")) return tree (ns.kind);
  if (is_ns_tag (t, "ns-sorting-algo")) return sorter_tree (ns);
  if (is_ns_tag (t, "ns-matches")) return members_tree (members);
  if (is_ns_tag (t, "ns-children"))
    return namespace_list_tree (namespace_children (ns.name));
  if (is_ns_tag (t, "ns-parents"))
    return namespace_list_tree (namespace_all_parents (ns));
  if (is_ns_tag (t, "ns-filename-template"))
    return ns.templ == "" ? tree ("<none>") : compound ("code", tree (ns.templ));
  if (is_ns_tag (t, "ns-summary-link"))
    return namespace_link ("Technical summary", join_path (path, true));
  replaced= false;
  return t;
}

static tree
namespace_dynamic_tree (const athena_namespace_definition& ns,
                        const std::vector<string>& path,
                        const std::vector<athena_namespace_match>& members,
                        tree t) {
  bool replaced= false;
  tree r= namespace_dynamic_replacement (ns, path, members, t, replaced);
  if (replaced || is_atomic (t)) return r;
  tree out (L(t));
  for (int i=0; i<N(t); i++)
    out << namespace_dynamic_tree (ns, path, members, t[i]);
  return out;
}

static bool
namespace_absolute_image_path (const string& path) {
  return path == "" || starts (path, "/") || starts (path, "~") ||
         starts (path, "$") || occurs ("://", path);
}

static string
namespace_rebase_image_path (const string& path, url source_dir) {
  if (namespace_absolute_image_path (path)) return path;
  url absolute= source_dir * url_unix (cork_to_utf8 (path));
  return utf8_to_cork (as_system_string (absolute));
}

static tree
namespace_rebase_homepage_images (tree t, url source_dir) {
  if (is_atomic (t)) return copy (t);

  tree out (L(t));
  for (int i=0; i<N(t); i++) {
    if (i == 0 && is_func (t, IMAGE) && is_atomic (t[i]))
      out << tree (namespace_rebase_image_path (t[i]->label, source_dir));
    else
      out << namespace_rebase_homepage_images (t[i], source_dir);
  }
  return out;
}

static bool
load_homepage (const athena_namespace_definition& ns,
               const std::vector<string>& path,
               tree& out) {
  if (ns.homepage_path == "") return false;
  url u= namespace_path_url (ns.homepage_path);
  string s;
  if (load_string (u, s, false)) return false;
  tree doc= import_loaded_tree (s, u, "texmacs");
  std::vector<athena_namespace_match> members;
  if (namespace_homepage_needs_members (doc)) {
    string error;
    members= athena_namespace_members (ns.name, error);
  }
  tree expanded= namespace_dynamic_tree (ns, path, members, doc);
  out= namespace_rebase_homepage_images (expanded, head (u));
  return true;
}

static tree
technical_summary_page (const athena_namespace_definition& ns,
                        const std::vector<athena_namespace_match>& members,
                        string error) {
  tree body (DOCUMENT);
  body << compound ("section*", tree ("Namespace " * ns.name));
  body << line_tm ("Kind: " * ns.kind);
  body << line_tm ("Template: " * (ns.templ == "" ? "<none>" : ns.templ));
  body << line_tm ("Initial content: " *
                   (ns.initial_content_path == "" ? string ("<none>") :
                    ns.initial_content_path));
  body << line_tm ("Homepage: " *
                   (ns.homepage_path == "" ? string ("<none>") :
                    ns.homepage_path));
  body << line_tm ("Parents: " *
                   (N(ns.parents) == 0 ? string ("<none>") :
                    join_list (ns.parents)));
  body << line_tm ("Derived parents: " *
                   (N(ns.derived_parents) == 0 ? string ("<none>") :
                    join_list (ns.derived_parents)));
  if (ns.sorter_trivial)
    body << line_tm ("Sorter: trivial");
  else if (ns.sorter_path != "")
    body << line_tm ("Sorter: " * ns.sorter_path);
  if (ns.style_path != "")
    body << line_tm ("Style: " * ns.style_path);
  if (error != "")
    body << line_tm ("Sorter warning: " * error);

  body << compound ("subsection*", tree ("Members"));
  tree ms= members_tree (members);
  for (int i=0; i<N(ms); i++) body << ms[i];
  return document_for_body (body, true);
}



} // namespace athena_namespaces

using namespace athena_namespaces;

tree
athena_namespace_info_page (string tmfs_name) {
  if (!vault_active ())
    return error_page ("Namespace", "No active vault.");

  std::vector<string> path= split_tmfs_path (tmfs_name);
  if (path.empty ())
    return error_page ("Namespace", "No namespace specified.");

  bool technical= false;
  if (N(path.back ()) > 0 && path.back ()[0] == '!') {
    technical= true;
    path.back ()= path.back () (1, N(path.back ()));
  }

  for (size_t i=0; i + 1<path.size (); i++) {
    string error;
    if (!athena_namespace_validate_relation (path[i], path[i + 1], true,
                                             error)) {
      return error_page ("Namespace Relation", tm_to_std (error));
    }
  }

  string name= path.back ();
  athena_namespace_definition ns;
  if (!athena_namespace_get (name, ns))
    return error_page ("Namespace", "Unknown namespace: " + tm_to_std (name));

  if (!technical) {
    tree homepage;
    if (load_homepage (ns, path, homepage)) return homepage;
  }
  string error;
  std::vector<athena_namespace_match> members=
    athena_namespace_members (name, error);
  return technical_summary_page (ns, members, error);
}
