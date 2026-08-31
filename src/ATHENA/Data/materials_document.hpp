/******************************************************************************
* MODULE     : materials_document.hpp
* DESCRIPTION: Materials document AST rendering and tmfs pages
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
******************************************************************************/

#ifndef ATHENA_MATERIALS_DOCUMENT_HPP
#define ATHENA_MATERIALS_DOCUMENT_HPP

#include "tree.hpp"

#include <string>

tree athena_materials_update_document (tree document,
                                       const std::string& default_style,
                                       std::string& error);
std::string athena_materials_document_citation_style (
  const tree& document, const std::string& fallback_style);
tree athena_material_info_page (const std::string& name);

#endif // ATHENA_MATERIALS_DOCUMENT_HPP
