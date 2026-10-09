/******************************************************************************
* MODULE     : QTMATHENADiff.hpp
* DESCRIPTION: Side-by-side structured comparison of ATHENA documents
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
*******************************************************************************/

#ifndef QTMATHENADIFF_HPP
#define QTMATHENADIFF_HPP
#include "tree.hpp"
#include "url.hpp"

void athena_diff_show ();
// GUI-owner only: exact immutable source snapshots, isolated from live files.
void athena_diff_show_snapshots (tree left, tree right, url left_source,
  url right_source, string left_title, string right_title);

#endif // QTMATHENADIFF_HPP
