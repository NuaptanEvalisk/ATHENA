/******************************************************************************
* MODULE     : evaluation_bars.hpp
* DESCRIPTION: Promote ordinary mathematical bars using the current document DRD
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
******************************************************************************/

#ifndef ATHENA_EVALUATION_BARS_HPP
#define ATHENA_EVALUATION_BARS_HPP

#include "tree.hpp"
#include "Data/Convert/Xml/athena_document_xml.hpp"

tree athena_promote_evaluation_bars (tree body, int& promoted,
  string mode= "text",
  athena::document::xml_format_version version= athena::document::xml_format_version::v2);

#endif
