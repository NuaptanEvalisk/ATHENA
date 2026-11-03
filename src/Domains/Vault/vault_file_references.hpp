/******************************************************************************
* MODULE     : vault_file_references.hpp
* DESCRIPTION: Structural local-file references in ATHENA documents
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
*******************************************************************************/

#ifndef ATHENA_VAULT_FILE_REFERENCES_HPP
#define ATHENA_VAULT_FILE_REFERENCES_HPP

#include "tree.hpp"

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

struct AthenaVaultFileReference {
  std::string value;
  std::filesystem::path resolved_path;
};

bool athena_vault_file_reference_argument (tree t, int& index);
bool athena_vault_is_local_file_reference (const std::string& value);
std::filesystem::path athena_vault_normalized_path (
  const std::filesystem::path& path);
bool athena_vault_path_at_or_below (const std::filesystem::path& path,
                                    const std::filesystem::path& parent);
std::filesystem::path athena_vault_resolve_file_reference (
  const std::filesystem::path& source, const std::string& value);
void athena_vault_collect_file_references (
  tree document, const std::filesystem::path& source,
  std::vector<AthenaVaultFileReference>& references);
void athena_vault_collect_image_file_references (
  tree document, const std::filesystem::path& source,
  std::vector<AthenaVaultFileReference>& references);

tree athena_vault_rewrite_file_references (
  tree document, const std::filesystem::path& source_before,
  const std::filesystem::path& source_after,
  const std::filesystem::path& renamed_before,
  const std::filesystem::path& renamed_after, size_t& replacements);

tree athena_vault_rewrite_file_reference_map (
  tree document, const std::filesystem::path& source_before,
  const std::filesystem::path& source_after,
  const std::unordered_map<std::string, std::filesystem::path>& renames,
  size_t& replacements);

#endif // ATHENA_VAULT_FILE_REFERENCES_HPP
