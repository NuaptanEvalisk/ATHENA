/******************************************************************************
* MODULE     : QTMMaterialCitationDialog.hpp
* DESCRIPTION: Materials citation inserter
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
******************************************************************************/

#ifndef QTMMATERIALCITATIONDIALOG_HPP
#define QTMMATERIALCITATIONDIALOG_HPP

#include "tree.hpp"

#include <string>

tree qtm_material_choose_citation (const std::string& csl_style);
tree qtm_material_choose_references ();

#endif // QTMMATERIALCITATIONDIALOG_HPP
