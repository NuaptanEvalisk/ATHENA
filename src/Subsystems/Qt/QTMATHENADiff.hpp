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
#include "Data/Convert/Xml/document_file_codec.hpp"

void athena_diff_show ();
// GUI-owner only: exact immutable source snapshots, isolated from live files.
void athena_diff_show_snapshots (
  athena::document::native_xml_read_result left,
  athena::document::native_xml_read_result right, url left_source,
  url right_source, string left_title, string right_title);
// Strict native envelope decoding; no legacy migration or format inference.
void athena_diff_show_snapshots (const std::string& left, const std::string& right,
  url left_source, url right_source, string left_title, string right_title);
// Compatibility entry for the explicitly v2-only Hodarium protocol caller.
void athena_diff_show_snapshots (tree left, tree right, url left_source,
  url right_source, string left_title, string right_title);

#endif // QTMATHENADIFF_HPP
