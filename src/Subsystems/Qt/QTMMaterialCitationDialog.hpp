/******************************************************************************
* MODULE     : QTMMaterialCitationDialog.hpp
* DESCRIPTION: Materials citation inserter
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
******************************************************************************/

#ifndef QTMMATERIALCITATIONDIALOG_HPP
#define QTMMATERIALCITATIONDIALOG_HPP

#include "tree.hpp"

#include <string>
#include <functional>

tree qtm_material_choose_citation (const std::string& csl_style);
tree qtm_material_choose_references (bool document_context = true);
void qtm_material_choose_citation_async (string style, std::function<void(tree)> completion);
void qtm_material_choose_references_async (std::function<void(tree)> completion);

#endif // QTMMATERIALCITATIONDIALOG_HPP
