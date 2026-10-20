/******************************************************************************
* MODULE     : source_document_creation.hpp
* DESCRIPTION: Explicit storage format selection for new source documents
* COPYRIGHT  : (C) 2026 ATHENA contributors
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
******************************************************************************/
#pragma once

#include "Data/Convert/Xml/document_file_codec.hpp"
#include "vaultfile_json.hpp"
#include <filesystem>
#include <optional>
#include <stdexcept>

namespace athena::document {

inline xml_format_version
new_source_version_for_model (int node_model_version) {
  switch (node_model_version) {
  case 0:
  case 1: return xml_format_version::v2;
  case 2: return xml_format_version::v3;
  default: throw std::invalid_argument ("Unsupported Vault node model for document creation");
  }
}

// Creation only: does not load/open a Vault, migrate a template, or change the
// version of an existing document. Resolve symlinks before finding its owner.
inline xml_format_version
new_source_version_for_target (
    const std::filesystem::path& target,
    std::optional<document_source_format> template_format= {}) {
  namespace fs= std::filesystem;
  if (target.empty ()) throw std::invalid_argument ("Missing document creation target");
  auto directory= fs::weakly_canonical (fs::absolute (target)).parent_path ();
  while (!directory.empty ()) {
    if (athena_vaultfile_present (directory)) {
      AthenaVaultfileInfo info;
      std::string error;
      if (!athena_vaultfile_read (directory, info, error))
        throw std::invalid_argument (error);
      const auto version= new_source_version_for_model (info.node_model_version);
      if (template_format == document_source_format::xml_v3 &&
          version != xml_format_version::v3)
        throw std::invalid_argument ("An XML v3 template requires an XML v3 target Vault");
      return version;
    }
    const auto parent= directory.parent_path ();
    if (parent == directory) break;
    directory= parent;
  }
  // No Vault owns this target: a new ATHENA 0.11 source is XML v3.
  // An existing legacy Vault above still retains authority over its format.
  return xml_format_version::v3;
}

} // namespace athena::document
