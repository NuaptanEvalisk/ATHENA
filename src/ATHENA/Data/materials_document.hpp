/******************************************************************************
* MODULE     : materials_document.hpp
* DESCRIPTION: Materials document AST rendering and tmfs pages
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
******************************************************************************/

#ifndef ATHENA_MATERIALS_DOCUMENT_HPP
#define ATHENA_MATERIALS_DOCUMENT_HPP

#include "tree.hpp"
#include "vault.hpp"

#include <string>
#include <vector>

struct inherited_material {
  std::string uuid;
  std::string namespace_uuid;
  std::string namespace_name;
};
std::vector<inherited_material> athena_materials_inherited (
  const vault_context_handle& context, url document, std::string& error);

tree athena_materials_update_document (tree document,
                                       const std::string& default_style,
                                       std::string& error,
                                       const std::vector<inherited_material>& inherited = {},
                                       vault_context_handle context = {});
tree athena_materials_update_for_file (tree document, url filename,
                                      const std::string& default_style,
                                      std::string& error);
std::string athena_materials_document_citation_style (
  const tree& document, const std::string& fallback_style);
tree athena_material_info_page (const std::string& name);

#endif // ATHENA_MATERIALS_DOCUMENT_HPP
