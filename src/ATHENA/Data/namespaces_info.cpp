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
document_for_body (tree body) {
  tree doc (DOCUMENT);
  doc << compound ("TeXmacs", TEXMACS_COMPAT_VERSION);
  doc << compound ("style", tuple ("generic"));
  doc << compound ("body", body);
  return doc;
}

static tree
error_page (const std::string& title, const std::string& message) {
  tree body (DOCUMENT);
  body << compound ("section*", text (title));
  body << line (message);
  return document_for_body (body);
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

  string error;
  std::vector<athena_namespace_match> members=
    athena_namespace_members (name, error);

  tree body (DOCUMENT);
  body << compound ("section*", tree ("Namespace " * name));
  body << line_tm ("Kind: " * ns.kind);
  body << line_tm ("Template: " * (ns.templ == "" ? "<none>" : ns.templ));
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
  if (members.empty ()) body << line ("No matching .ath files.");
  for (const athena_namespace_match& m: members) {
    tree link= compound ("hlink", tree (m.stem), tree (concretize (m.file)));
    tree c (CONCAT);
    c << link;
    if (N(m.captures) > 0) {
      c << tree ("  ");
      c << tree ("[");
      for (int i=0; i<N(m.captures); i++) {
        if (i != 0) c << tree (", ");
        c << tree (m.capture_types[i] * "=" * m.captures[i]);
      }
      c << tree ("]");
    }
    if (m.ambiguous) c << tree ("  (ambiguous)");
    body << compound ("paragraph*", c);
  }

  return document_for_body (body);
}
