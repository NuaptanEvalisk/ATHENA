
/******************************************************************************
* MODULE     : metadata-acm.cpp
* DESCRIPTION: conversion of (ACM) TeX metadata into TeXmacs metadata
* COPYRIGHT  : (C) 2012 Joris van der Hoeven, Poulain François
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "Tex/convert_tex.hpp"
#include "metadata.hpp"

/******************************************************************************
* ACM metadata
******************************************************************************/

tree
get_acm_affiliation (tree orig) {
  tree r (CONCAT);
  tree t= orig;
  if (!is_document (t) && !is_concat (t)) t= tree (CONCAT, t);
  for (int i=0; i<N(t); i++)
    if (is_tuple (t[i], "\\institution", 1) ||
        is_tuple (t[i], "\\streetaddress", 1) ||
        is_tuple (t[i], "\\city", 1) ||
        is_tuple (t[i], "\\state", 1) ||
        is_tuple (t[i], "\\postcode", 1) ||
        is_tuple (t[i], "\\country", 1)) {
      if (N(r) != 0) r << "\\newline";
      r << t[i][1];
    }
  if (N(r) == 0) return orig;
  return r;
}

tree
collect_metadata_acm (tree t) {
  int i, n=N(t);
  tree u, r (CONCAT);
  tree doc_data (APPLY, "\\doc-data");
  tree abstract_data (APPLY, "\\abstract-data");
  array<tree> doc_notes;
  for (i=0; i<n; i++) {
    u= t[i];
    if (is_tuple (u, "\\date", 1))
      doc_data << tree (APPLY, "\\doc-date", u[1]);
    else if (is_tuple (u, "\\title", 1))
      doc_data << tree (APPLY, "\\doc-title", u[1]);
    else if (is_tuple (u, "\\title*", 2)) {
      doc_data << tree (APPLY, "\\doc-running-title", u[1]);
      doc_data << tree (APPLY, "\\doc-title", u[2]);
    }
    else if (is_tuple (u, "\\subtitle", 1))
      doc_data << tree (APPLY, "\\doc-subtitle", u[1]);
    else if (is_tuple (u, "\\titlenote", 1))
      doc_data << tree (APPLY, "\\doc-note", u[1]);
    else if (is_tuple (u, "\\subtitlenote", 1))
      doc_data << tree (APPLY, "\\doc-note", u[1]);
    else if (is_tuple (u, "\\author", 1)) {
      array<tree> authors;
      while (i<n && is_tuple (t[i], "\\author", 1))
        authors << t[i++][1];
      array<tree> author_datas;
      while (i<n) {
        if (is_tuple (t[i], "\\affiliation", 1)) {
          tree aff= get_acm_affiliation (t[i++][1]);
          author_datas << tree (APPLY, "\\author-affiliation", aff);
        }
        else if (is_tuple (t[i], "\\orcid", 1))
          i++;
        else if (is_tuple (t[i], "\\authornote", 1))
          author_datas << tree (APPLY, "\\author-note", t[i++][1]);
        else if (is_tuple (t[i], "\\email", 1))
          author_datas << tree (APPLY, "\\author-email", t[i++][1]);
        else if (is_atomic (t[i]) && is_whitespace (t[i]->label))
          i++;
        else break;
      }
      for (int j=0; j<N(authors); j++) {
        tree author_data (APPLY, "\\author-data");
        author_data << tree (APPLY, "\\author-name", authors[j]);
        author_data << author_datas;
        doc_data << tree (APPLY, "\\doc-author", author_data);
      }
      i--;
    }
    else if (is_tuple (u, "\\begin-abstract")) {
      tree abstract_text (CONCAT);
      i++;
      while (i<n && !is_tuple (t[i], "\\end-abstract"))
        abstract_text << t[i++];
      abstract_data << tree (APPLY, "\\abstract", abstract_text);
    }
    else if (is_tuple (u, "\\begin-keywords")) {
      tree keywords (CONCAT);
      i++;
      while (i<n && !is_tuple (t[i], "\\end-keywords"))
        keywords << t[i++];
      array<tree> tmp= tokenize_concat (keywords, A(concat (",", ";",
              tree (TUPLE, "\\tmsep"), tree (TUPLE, "\\tmSep"))));
      if (N(tmp) > 0) {
        tree kw= tree (APPLY, "\\abstract-keywords");
        kw << tmp;
        abstract_data << kw;
      }
    }
    else if (is_tuple (u, "\\tmmsc")  || is_tuple (u, "\\tmarxiv") ||
             is_tuple (u, "\\tmpacs"))
      abstract_data << collect_abstract_data (u);
    else if (is_tuple (u, "\\keywords", 1) || is_tuple (u, "\\terms", 1)) {
      array<tree> tmp= tokenize_concat (u[N(u)-1], A(concat (",", ";",
              tree (TUPLE, "\\tmsep"), tree (TUPLE, "\\tmSep"))));
      if (N(tmp) > 0) {
        tree kw= tree (APPLY, "\\abstract-keywords");
        kw << tmp;
        abstract_data << kw;
      }
    }
    else if (is_tuple (u, "\\category") || is_tuple (u, "\\category*")) {
      tree tmp (APPLY, "\\abstract-acm");
      for (int j=1; j<N(u); j++) tmp << u[j];
      abstract_data << tmp;
    }
  }
  if (N(doc_notes) > 0) doc_data << doc_notes;
  if (N(doc_data) > 1) r << doc_data << "\n";
  if (N(abstract_data) > 1) r << abstract_data << "\n";
  return r;
}
