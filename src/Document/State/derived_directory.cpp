
/******************************************************************************
* MODULE     : derived_directory.cpp
* DESCRIPTION: Generate detached directory views without editing source
* COPYRIGHT  : (C) 1999  Joris van der Hoeven
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "Document/State/derived_directory.hpp"
#include "analyze.hpp"
#include "convert.hpp"
#include "locale.hpp"
#include "node_metadata.hpp"
#include <algorithm>
#include <locale>
#include <stdexcept>
#include <vector>

#ifdef OS_MINGW
#include <winnls.h>
#endif

namespace athena::derived {

tree source_declarations (tree source) {
  if (is_atomic (source)) return source;
  if (is_directory (source)) {
    if (source[N(source)-1] == tree (DOCUMENT, "") &&
        !node::contains_metadata (source[N(source)-1])) return source;
    tree result (source, N(source));
    for (int i=0; i<N(source)-1; ++i) result[i]= source[i];
    result[N(source)-1]= tree (DOCUMENT, "");
    node::copy_metadata (source, result);
    return result;
  }
  tree result= source;
  for (int i=0; i<N(source); ++i) {
    tree child= source_declarations (source[i]);
    if (strong_equal (child, source[i])) continue;
    if (strong_equal (result, source)) {
      result= tree (source, N(source));
      for (int j=0; j<N(source); ++j) result[j]= source[j];
      node::copy_metadata (source, result);
    }
    result[i]= child;
  }
  return result;
}

/******************************************************************************
* Removing labels
******************************************************************************/

// Labels in TOC, index or glossaries (list-of-anything) leads to redefinition
// and impede typesetting. This generates errors reported via intrusive popups.
// So we remove them.

static tree
remove_labels (tree t) {
  if (is_atomic (t)) return t;
  if (is_func (t, LABEL)) return "";
  int i, n= N(t);
  tree r (L(t));
  for (i=0; i<n; i++) {
    tree u= remove_labels (t[i]);
    if (!is_concat (t) || u != "") r << u;
  }
  node::copy_metadata (t, r);
  if (node::get (r)) return r;
  if (is_func (r, CONCAT, 0)) return "";
  if (is_func (r, CONCAT, 1)) return r[0];
  return r;
}

/******************************************************************************
* Automatically generate table of contents
******************************************************************************/

tree
table_of_contents (const tree& entries) {
  return remove_labels (node::content_projection (entries));
}

/******************************************************************************
* Automatically generate an index
******************************************************************************/

static string
index_name_sub (tree t, bool all, hashmap<string,tree>& followup) {
  if (is_atomic (t)) {
    string s= t->label, r;
    int i, n= N(s);
    for (i=0; i<n; i++)
      if (is_iso_alpha (s[i]) || is_digit (s[i]) || (s[i] == ' ') ||
          (all && (s[i] >= ' '))) r << s[i];
    return r;
  }
  else if (is_concat (t)) {
    string r;
    int i, n= N(t);
    for (i=0; i<n; i++)
      r << index_name_sub (t[i], all, followup);
    return r;
  }
  else if (is_tuple (t)) {
    string r;
    int i, j, n= N(t);
    for (i=0; i<n; i++) {
      if (i!=0) r << "\t";
      string s= index_name_sub (t[i], false, followup);
      if (s == "") s= index_name_sub (t[i], true, followup);
      tree u= copy (followup [s]);
      for (j=0; j<N(u); j++)
        if (u[j] == t[i]) break;
      if (j == N(u)) { u << t[i]; followup (s)= u; }
      r << s;
      if (j != 0) r << "\n" << as_string (j);
    }
    return r;
  }
  else if (all && is_func (t, WITH))
    return index_name_sub (t[N(t)-1], all, followup);
  else return "";
}

static string
index_name (tree t, hashmap<string,tree>& followup) {
  if (is_func (t, TUPLE, 2)) t= t[0];
  else if (is_func (t, TUPLE, 3)) t= t[0];
  else if (is_func (t, TUPLE, 5)) {
    if (t[0] == "") t= t[3];
    else t= t[0];
  }
  if (!is_tuple (t)) t= tuple (t);
  string r= index_name_sub (t, false, followup);
  string l= locase_all (r);
  for (int i=0; i<N(r); i++)
    if (is_iso_upcase (r[i])) {
      while (i<N(l) && l[i]!='\t') i++;
      l= l(0,i) * "*" * l(i,N(l));
      break;
    }
  return l;
}

static tree
index_value (tree t) {
  if (is_func (t, TUPLE, 2)) return t;
  else if (is_func (t, TUPLE, 3)) return tuple (t[2]);
  else if (is_func (t, TUPLE, 5)) {
    tree l= t[3], r= t[4];
    if (!is_tuple (l)) l= tuple (l);
    if (t[1] == "strong") r= compound ("strong", r);
    if (t[2] != "") r= tuple ("range", t[2], r);
    return tuple (l, r);
  }
  return "";
}

static void
insert_recursively (array<string>& a, string s, hashmap<string,tree>& h) {
  // cout << "Insert recursively \t" << s << "\n";
  int i= search_backwards ("\t", s);
  if (i != -1) {
    string r= s (0, i);
    if (!h->contains (r)) {
      tree u= h[s][0][0];
      h (r)= tuple (tuple (copy (u (0, N(u)-1)), ""));
      insert_recursively (a, s (0, i), h);
    }
  }
  a << s;
}

static void
make_entry (tree& D, tree t, hashmap<string,tree> refs, bool rec) {
  // cout << "Make entry " << t << "\n";
  int i, j, n= N(t);
  for (i=0; i<n; i++)
    if (is_func (t[i], TUPLE, 1)) {
      bool flag= true;
      for (j=0; j<n; j++)
        if (is_func (t[j], TUPLE, 2) && (t[i][0] == t[j][0]))
          flag= false;
      if (flag) D << t[i][0];
    }

  for (i=0; i<n; i++)
    if (is_func (t[i], TUPLE, 2) && is_tuple (t[i][1], "range", 2)) {
      bool flag= true;
      for (j=i+1; j<n; j++)
        if (is_func (t[j], TUPLE, 2) && is_tuple (t[j][1], "range", 2))
          if ((t[i][0] == t[j][0]) && (t[i][1][1] == t[j][1][1])) {
            t[i][1]= tree (CONCAT, t[i][1][2], "\25", t[j][1][2]);
            t[j]= "";
            flag= false;
            break;
          }
      if (flag) t[i][1]= tree (CONCAT, t[i][1][2], "\25?");
    }

  hashmap<tree,tree> h ("");
  hashmap<tree,string> last ("");
  for (i=0; i<n; i++)
    if (is_func (t[i], TUPLE, 2)) {
      tree l= t[i][0], r= t[i][1];
      string prev= "", next= "";
      if (is_func (r, PAGEREF, 1) &&
          is_atomic (r[0]) &&
          refs->contains (r[0]->label) &&
          is_func (refs[r[0]->label], TUPLE, 2) &&
          is_atomic (refs[r[0]->label][1])) {
        if (last->contains (l)) prev= last[l];
        next= refs[r[0]->label][1]->label;
        last (l)= next;
      }
      if (!h->contains (l)) h (l)= r;
      else {
        tree rr= h[l];
        if (rr == "") rr= r;
        else if (prev != "" && next == prev);
        else if (is_int (prev) && is_int (next) &&
                 as_int (next) == as_int (prev) + 1) {
          if (!is_concat (rr))
            rr= tree (CONCAT, rr, "\25", r);
          else if (is_concat (rr) && N(rr) >= 2 && rr[N(rr)-2] == "\25")
            rr[N(rr)-1]= r;
          else
            rr << "\25" << r;
        }
        else if (r != "") {
          if (!is_concat (rr)) rr= tree (CONCAT, rr);
          rr << ", " << r;
        }
        h (l)= rr;
      }
    }

  for (i=0; i<n; i++)
    if (is_func (t[i], TUPLE, 2)) {
      tree l= t[i][0];
      if (h->contains (l)) {
        int k= N(l);
        tree e;
        if (rec) {
          e= compound ("index+" * as_string (k));
          if (h[l] == "") e= compound ("index+" * as_string (k) * "*");
          for (int ch=0; ch<k; ch++) e << copy (l[ch]);
          if (h[l] != "") e << h[l];
        }
        else {
          e= compound ("index-" * as_string (k), copy (l[k-1]), h[l]);
          if (h[l] == "")
            e= compound ("index-" * as_string (k) * "*", copy (l[k-1]));
        }
        D << e;
        h->reset (l);
      }
    }
}

tree
index (const tree& entries, hashmap<string,tree> R,
       string language, string break_style) {
  tree I= node::content_projection (entries);
  if (N(I)>0) {
    hashmap<string,tree> followup (TUPLE);
    int i, n= N(I);
    array<string> entry (n);
    for (i=0; i<n; i++)
      entry[i]= index_name (I[i], followup);
    std::vector<string> sorted;
    for (i=0; i<n; ++i) sorted.push_back (entry[i]);
#ifndef OS_MINGW
    const auto locale= get_std_locale (language);
    std::stable_sort (sorted.begin (), sorted.end (), [&] (string a, string b) {
      c_string aa (a), bb (b);
      return locale (std::string (aa), std::string (bb));
    });
#else
    string name= language_to_locale (language);
    name[2]= '-';
    const auto locale= texmacs_utf8_to_wide (name);
    std::stable_sort (sorted.begin (), sorted.end (), [&] (string a, string b) {
      const auto aa= texmacs_utf8_to_wide (a), bb= texmacs_utf8_to_wide (b);
      return CompareStringEx (locale.c_str (), SORT_STRINGSORT,
        aa.c_str (), static_cast<int> (aa.size ()),
        bb.c_str (), static_cast<int> (bb.size ()), nullptr, nullptr, 0) == CSTR_LESS_THAN;
    });
#endif
    for (i=0; i<n; ++i) entry[i]= sorted[i];

    hashmap<string,tree> h (TUPLE);
    for (i=0; i<n; i++) {
      string name = index_name  (I[i], followup);
      tree   value= index_value (I[i]);
      if (!h->contains (name)) h (name)= tuple (value);
      else h (name) << value;
    }

    array<string> new_entry;
    for (i=0; i<n; i++) {
      if ((i>0) && (entry[i] == entry[i-1])) continue;
      insert_recursively (new_entry, entry[i], h);
    }
    entry= new_entry;
    n= N(entry);

    bool rec= break_style == "recall";
    tree D (DOCUMENT);
    for (i=0; i<n; i++)
      make_entry (D, h (entry[i]), R, rec);
    return remove_labels (D);
  }
  return tree (DOCUMENT);
}

/******************************************************************************
* Automatically generate a glossary
******************************************************************************/

tree
glossary (const tree& entries) {
  tree G= node::content_projection (entries);
  if (N(G)>0) {
    int i, n= N(G);
    tree D (DOCUMENT);
    for (i=0; i<n; i++)
      if (is_func (G[i], TUPLE, 1)) D << G[i][0];
      else if (is_func (G[i], TUPLE, 3) && (G[i][0] == "normal")) {
        tree content= G[i][1];
        if (is_document (content) && N(content) == 1) content= content[0];;
        tree L= compound ("glossary-1", content, G[i][2]);
        D << L;
      }
      else if (is_func (G[i], TUPLE, 4) && (G[i][0] == "normal")) {
        tree content= G[i][1];
        if (is_document (content) && N(content) == 1) content= content[0];;
        tree L= compound ("glossary-2", content, G[i][2], G[i][3]);
        D << L;
      }
      else if (is_func (G[i], TUPLE, 3) && (G[i][0] == "dup")) {
        int j;
        for (j=0; j<N(D); j++)
          if ((is_compound (D[j], "glossary-1") ||
               is_compound (D[j], "glossary-2")) &&
              (D[j][0] == G[i][1]))
            {
              tree C= D[j][N(D[j])-1];
              if (!is_concat (C)) C= tree (CONCAT, C);
              C << ", ";
              C << G[i][2];
              D[j][N(D[j])-1]= C;
            }
      }
    return remove_labels (D);
  }
  return tree (DOCUMENT);
}

/******************************************************************************
* Recognize directory declarations
******************************************************************************/

bool
is_directory (tree t) {
  return
    is_compound (t, "table-of-contents", 2) ||
    is_compound (t, "table-of-contents*", 3) ||
    is_compound (t, "the-index", 2) ||
    is_compound (t, "the-index*", 3) ||
    is_compound (t, "the-glossary", 2) ||
    is_compound (t, "the-glossary*", 3) ||
    is_compound (t, "list-of-figures", 2) ||
    is_compound (t, "list-of-tables", 2);
}

string
directory_kind (tree t) {
  if (!is_directory (t)) return "";
  string name= as_string (L(t));
  if (name[N(name)-1] == '*') name= name (0, N(name)-1);
  return name;
}

tree
directory (tree declaration, const tree& entries,
           hashmap<string,tree> references, string language, string break_style) {
  const auto kind= directory_kind (declaration);
  if (kind == "table-of-contents") return table_of_contents (entries);
  if (kind == "the-index") return index (entries, references, language, break_style);
  if (kind == "") throw std::invalid_argument ("Not a generated directory declaration");
  return glossary (entries);
}
} // namespace athena::derived
