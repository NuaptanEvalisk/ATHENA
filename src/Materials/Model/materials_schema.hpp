/******************************************************************************
* MODULE     : materials_schema.hpp
* DESCRIPTION: Pinned Zotero schema access for ATHENA Materials
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef ATHENA_MATERIALS_SCHEMA_HPP
#define ATHENA_MATERIALS_SCHEMA_HPP

#include <filesystem>
#include <string>
#include <vector>

struct MaterialSchemaField {
  std::string name;
  std::string label;
  std::string base_field;
};

struct MaterialSchemaCreatorType {
  std::string name;
  std::string label;
  bool primary= false;
};

struct MaterialSchemaItemType {
  std::string name;
  std::string label;
  std::vector<MaterialSchemaField> fields;
  std::vector<MaterialSchemaCreatorType> creator_types;
};

class MaterialSchema {
public:
  bool load (const std::filesystem::path& path, std::string& error);
  bool load_bundled (std::string& error);
  bool is_loaded () const;
  int version () const;

  const std::vector<MaterialSchemaItemType>& item_types () const;
  const MaterialSchemaItemType* item_type (const std::string& name) const;
  std::string item_type_label (const std::string& name) const;
  std::string field_label (const std::string& name) const;
  std::string creator_type_label (const std::string& name) const;

  static std::filesystem::path bundled_path ();

private:
  int schema_version= 0;
  std::vector<MaterialSchemaItemType> types;
  std::vector<MaterialSchemaField> field_labels;
  std::vector<MaterialSchemaCreatorType> creator_labels;
};

#endif // ATHENA_MATERIALS_SCHEMA_HPP
