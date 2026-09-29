/******************************************************************************
* MODULE     : document_history_store.hpp
* DESCRIPTION: SQLite + Fossil-delta file-scoped document history store
* COPYRIGHT  : (C) 2026 ATHENA contributors
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
******************************************************************************/

#ifndef ATHENA_DOCUMENT_HISTORY_STORE_HPP
#define ATHENA_DOCUMENT_HISTORY_STORE_HPP

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

struct sqlite3;

namespace athena::history {

struct version_entry {
  std::int64_t id= 0;
  std::string relative_path;
  std::int64_t created_at_ms= 0;
  std::string trigger;
  std::int64_t base_id= 0;
  bool delta= false;
  std::int64_t content_size= 0;
  std::int64_t stored_size= 0;
  std::string content_hash;
  int chain_depth= 0;
};

class document_history_store {
public:
  document_history_store ()= default;
  ~document_history_store ();
  document_history_store (const document_history_store&)= delete;
  document_history_store& operator = (const document_history_store&)= delete;

  bool open (const std::filesystem::path& vault_root, std::string& error);

  bool capture (const std::string& relative_path, std::string_view content,
                const std::string& trigger,
                std::optional<std::int64_t> retention_seconds,
                bool& inserted, std::int64_t& version_id,
                std::string& error);

  bool list (const std::string& relative_path,
             std::vector<version_entry>& versions,
             std::string& error) const;

  bool reconstruct (std::int64_t version_id, std::string& content,
                    std::string& error) const;

  bool rename_path (const std::string& old_relative_path,
                    const std::string& new_relative_path,
                    bool directory, std::string& error);

  const std::filesystem::path& vault_root () const { return root_; }
  const std::filesystem::path& database_path () const { return database_; }

private:
  sqlite3* db_= nullptr;
  std::filesystem::path root_;
  std::filesystem::path database_;

  bool prune (const std::string& relative_path,
              std::int64_t cutoff_ms, std::string& error);
};

bool valid_relative_document_path (const std::string& path);

} // namespace athena::history

#endif // ATHENA_DOCUMENT_HISTORY_STORE_HPP
