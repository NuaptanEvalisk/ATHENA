/******************************************************************************
* MODULE     : vault_safe_rename.hpp
* DESCRIPTION: Transactional, reference-aware Vault renaming
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
*******************************************************************************/

#ifndef ATHENA_VAULT_SAFE_RENAME_HPP
#define ATHENA_VAULT_SAFE_RENAME_HPP

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "tree.hpp"

class AthenaVaultMapSqlite;

struct VaultSafeRenamePlan {
  std::filesystem::path source;
  std::filesystem::path target;
  std::string old_relative_path;
  std::string new_relative_path;
  bool is_directory= false;
  size_t filesystem_entries= 0;
  size_t map_rows= 0;
  size_t candidate_documents= 0;
  size_t rewritten_documents= 0;
  size_t rewritten_references= 0;
  size_t affected_open_buffers= 0;
  std::vector<std::string> modified_buffers;

  struct Impl;
  std::shared_ptr<Impl> impl;
};

bool vault_safe_rename_plan (const std::filesystem::path& source,
                             const std::filesystem::path& target,
                             VaultSafeRenamePlan& plan,
                             std::string& error);
bool vault_safe_rename_execute (VaultSafeRenamePlan& plan,
                                std::string& error);
bool vault_safe_rename_recover (const std::filesystem::path& root,
                                const std::string& map_relative_path,
                                std::string& error);
bool vault_safe_rename_recover (const std::filesystem::path& root,
                                AthenaVaultMapSqlite& map,
                                std::string& error);
tree vault_safe_rename_rewrite_tree (
  tree document, const std::filesystem::path& source_before,
  const std::filesystem::path& source_after,
  const std::filesystem::path& renamed_before,
  const std::filesystem::path& renamed_after, size_t& replacements);

#endif // ATHENA_VAULT_SAFE_RENAME_HPP
