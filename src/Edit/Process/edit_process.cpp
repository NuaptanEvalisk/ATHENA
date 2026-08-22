
/******************************************************************************
* MODULE     : edit_process.cpp
* DESCRIPTION: incorporate automatically generated data into text
* COPYRIGHT  : (C) 1999  Joris van der Hoeven
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "Process/edit_process.hpp"
#include "analyze.hpp"
#include "tm_buffer.hpp"
#include "merge_sort.hpp"
#include "Sqlite3/sqlite3.hpp"
#include "file.hpp"
#include "convert.hpp"
#include "scheme.hpp"
#if __cplusplus >= 201103L
#  include "Editor/edit_typeset.hpp"
#  include "locale.hpp"
#  include <locale>
#endif

#ifdef OS_MINGW
#include <winnls.h>
#endif

/******************************************************************************
* Constructors and destructors
******************************************************************************/

edit_process_rep::edit_process_rep () {}
edit_process_rep::~edit_process_rep () {}

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
  if (is_func (r, CONCAT, 0)) return "";
  if (is_func (r, CONCAT, 1)) return r[0];
  return r;
}

/******************************************************************************
* Automatically generate table of contents
******************************************************************************/

void
edit_process_rep::generate_table_of_contents (string toc) {
  if (DEBUG_AUTO)
    debug_automatic << "Generating table of contents [" << toc << "]\n";
  tree toc_t= buf->data->aux[toc];
  if (buf->prj != NULL) toc_t= copy (buf->prj->data->aux[toc]);
  if (N(toc_t)>0) insert_tree (remove_labels (toc_t));
}

/******************************************************************************
* Automatically generate an index
******************************************************************************/

static hashmap<string,tree> followup (TUPLE);

#ifndef OS_MINGW
struct locale_less_eq_operator {
  static std::locale le;
  static inline bool leq (string& a, string& b) {
    if (a == b) return true;
    string A= cork_to_utf8 (a), B= cork_to_utf8 (b);
    c_string a8 (A), b8 (B);    
    return le (std::string (a8), std::string (b8));
  }
};
std::locale locale_less_eq_operator::le;
#else
// use CompareStringEx function on Windows
struct locale_less_eq_operator {
  static string locale_name;
  static inline bool leq (string& a, string& b) {
    if (a == b) return true;
    string A= cork_to_utf8 (a), B= cork_to_utf8 (b);
    std::wstring wa= texmacs_utf8_to_wide (A);
    std::wstring wb= texmacs_utf8_to_wide (B);
    int result = CompareStringEx(
      texmacs_utf8_to_wide(locale_name).c_str(),
      SORT_STRINGSORT,
      wa.c_str(),
      static_cast<int>(wa.size()),
      wb.c_str(),
      static_cast<int>(wb.size()),
      NULL,
      NULL,
      0);
    return result == CSTR_LESS_THAN;
  }
};
string locale_less_eq_operator::locale_name = "en-US";
#endif

static string
index_name_sub (tree t, bool all) {
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
      r << index_name_sub (t[i], all);
    return r;
  }
  else if (is_tuple (t)) {
    string r;
    int i, j, n= N(t);
    for (i=0; i<n; i++) {
      if (i!=0) r << "\t";
      string s= index_name_sub (t[i], false);
      if (s == "") s= index_name_sub (t[i], true);
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
    return index_name_sub (t[N(t)-1], all);
  else return "";
}

static string
index_name (tree t) {
  if (is_func (t, TUPLE, 2)) t= t[0];
  else if (is_func (t, TUPLE, 3)) t= t[0];
  else if (is_func (t, TUPLE, 5)) {
    if (t[0] == "") t= t[3];
    else t= t[0];
  }
  if (!is_tuple (t)) t= tuple (t);
  string r= index_name_sub (t, false);
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

void
edit_process_rep::generate_index (string idx) {
  system_wait ("Generating index, ", "please wait");
  if (DEBUG_AUTO)
    debug_automatic << "Generating index [" << idx << "]\n";
  tree I= copy (buf->data->aux[idx]);
  hashmap<string,tree> R= buf->data->ref;
  if (buf->prj != NULL) {
    I= copy (buf->prj->data->aux[idx]);
    R= buf->prj->data->ref;
  }
  if (N(I)>0) {
    followup= hashmap<string,tree> (TUPLE);
    int i, n= N(I);
    array<string> entry (n);
    for (i=0; i<n; i++)
      entry[i]= index_name (I[i]);
#if __cplusplus >= 201103L
#ifndef OS_MINGW
    locale_less_eq_operator::le= get_std_locale (get_init_string ("language"));
#else
    locale_less_eq_operator::locale_name = language_to_locale (get_init_string ("language"));
    locale_less_eq_operator::locale_name[2] = '-';
#endif
    merge_sort_leq<string,locale_less_eq_operator> (entry);
#else
    merge_sort (entry);
#endif

    hashmap<string,tree> h (TUPLE);
    for (i=0; i<n; i++) {
      string name = index_name  (I[i]);
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

    string brst= get_init_string ("index-break-style");
    bool   rec = (brst == "recall");
    tree D (DOCUMENT);
    for (i=0; i<n; i++)
      make_entry (D, h (entry[i]), R, rec);
    insert_tree (remove_labels (D));
  }
  system_wait ("");
}

/******************************************************************************
* Automatically generate a glossary
******************************************************************************/

void
edit_process_rep::generate_glossary (string gly) {
  system_wait ("Generating glossary, ", "please wait");
  if (DEBUG_AUTO)
    debug_automatic << "Generating glossary [" << gly << "]\n";
  tree G= copy (buf->data->aux[gly]);
  if (buf->prj != NULL) G= copy (buf->prj->data->aux[gly]);
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
    insert_tree (remove_labels (D));
  }
  system_wait ("");
}

/******************************************************************************
* Automatically generate auxiliairy data and replace in text
******************************************************************************/

static bool
is_aux (tree t) {
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

void
edit_process_rep::generate_aux_recursively (string which, tree st, path p) {
  int i, n= N(st);
  for (i=0; i<n; i++)
    if (!is_aux (st[i])) {
      if (is_compound (st[i]))
        generate_aux_recursively (which, st[i], p * i);
    }
    else {
      tree t= st[i];
      path doc_p= p * path (i, N(t)-1);
      assign (doc_p, tree (DOCUMENT, ""));
      go_to (doc_p * path (0, 0));

      /*
        cout << "et= " << et << "\n";
        cout << "tp= " << tp << "\n";
        cout << "------------------------------------------------------\n";
      */
      if (arity (t) >= 1) {
        if ((is_compound (t, "table-of-contents") ||
             is_compound (t, "table-of-contents*")) &&
            ((which == "") || (which == "table-of-contents")))
          generate_table_of_contents (as_string (t[0]));
        if ((is_compound (t, "the-index") || is_compound (t, "the-index*")) &&
            ((which == "") || (which == "the-index")))
          generate_index (as_string (t[0]));
        if ((is_compound (t, "the-glossary") ||
             is_compound (t, "the-glossary*")) &&
            ((which == "") || (which == "the-glossary")))
          generate_glossary (as_string (t[0]));
        if (is_compound (t, "list-of-figures") &&
            ((which == "") || (which == "list-of-figures")))
          generate_glossary (as_string (t[0]));
        if (is_compound (t, "list-of-tables") &&
            ((which == "") || (which == "list-of-tables")))
          generate_glossary (as_string (t[0]));
      }
      /*
        cout << "et= " << et << "\n";
        cout << "tp= " << tp << "\n";
        cout << "------------------------------------------------------\n\n\n";
      */
    }
}

void
edit_process_rep::generate_aux (string which) {
  // path saved_path= tp;
  typeset_invalidate_all ();
  typeset_forced ();
  generate_aux_recursively (which, subtree (et, rp), rp);
  init_update ();
  // if (which == "") go_to (saved_path);
  // ... may be problematic if cursor was inside regenerated content
}

bool
edit_process_rep::get_save_aux () {
  return as_bool (get_init_string (SAVE_AUX));
}
