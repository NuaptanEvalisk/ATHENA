/******************************************************************************
* MODULE     : vault_map_sqlite.hpp
* DESCRIPTION: Non-temporal SQLite storage for Vault object locations
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
*******************************************************************************/

#ifndef ATHENA_VAULT_MAP_SQLITE_HPP
#define ATHENA_VAULT_MAP_SQLITE_HPP

#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

struct AthenaVaultMapNode {
  std::string uuid;
  std::string path;
  std::string anchor_begin;
  std::string anchor_end;
};

struct AthenaVaultMapRenameOperation {
  std::string operation_id;
  std::string old_path;
  std::string new_path;
  bool is_directory= false;
  std::string phase;
};

class AthenaVaultMapSqlite {
public:
  AthenaVaultMapSqlite ();
  ~AthenaVaultMapSqlite ();

  AthenaVaultMapSqlite (const AthenaVaultMapSqlite&) = delete;
  AthenaVaultMapSqlite& operator= (const AthenaVaultMapSqlite&) = delete;

  bool open (const std::filesystem::path& path, bool create,
             std::string& error);
  void close ();
  bool valid () const;

  bool set_node (const AthenaVaultMapNode& node, std::string& error);
  bool get_node (const std::string& uuid, AthenaVaultMapNode& node,
                 bool& found, std::string& error) const;
  bool remove_node (const std::string& uuid, std::string& error);
  bool has_node (const std::string& uuid, bool& found,
                 std::string& error) const;
  bool find_uuid (const std::string& path, const std::string& anchor_begin,
                  const std::string& anchor_end, std::string& uuid,
                  std::string& error) const;
  bool replace_all (const std::vector<AthenaVaultMapNode>& nodes,
                    std::string& error);
  bool read_all (std::vector<AthenaVaultMapNode>& nodes,
                 std::string& error) const;
  bool rewrite_anchors (
    const std::string& path,
    const std::vector<std::pair<std::string, std::string>>& renames,
    size_t& changed, std::string& error);
  bool quick_check (std::string& error) const;
  bool integrity_check (std::string& error) const;
  bool count_path_rename (const std::string& old_path, bool is_directory,
                          size_t& count, std::string& error) const;
  bool prepare_path_rename (const AthenaVaultMapRenameOperation& operation,
                            std::string& error);
  bool apply_path_rename (const std::string& operation_id, size_t& changed,
                          std::string& error);
  bool finish_path_rename (const std::string& operation_id,
                           std::string& error);
  bool pending_path_renames (
    std::vector<AthenaVaultMapRenameOperation>& operations,
    std::string& error) const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl;
};

bool athena_vault_map_prepare (const std::string& requested_relative_path,
                               std::string& resolved_relative_path,
                               std::string& error);

bool athena_vault_map_rewrite_at_root (
  const std::filesystem::path& root, const std::string& relative_document_path,
  const std::vector<std::pair<std::string, std::string>>& renames,
  size_t& changed, std::string& error);

#endif // ATHENA_VAULT_MAP_SQLITE_HPP
