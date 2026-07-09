/******************************************************************************
* MODULE     : rag_delegation_patch.hpp
* DESCRIPTION: SQLite patch helpers for delegated RAG embedding jobs
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef RAG_DELEGATION_PATCH_HPP
#define RAG_DELEGATION_PATCH_HPP

#include "rag_index.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace athena::rag::delegation {

struct DelegatedFile {
  std::string rel_path;
  std::string content;
  int64_t size= 0;
  int64_t mtime_ns= 0;
  std::string content_hash;
};

struct DelegatedJob {
  std::vector<DelegatedFile> files;
  std::vector<std::string> deleted;
};

bool valid_delegated_rel_path (const std::string& rel);
std::vector<std::filesystem::path> scan_delegation_ath_files (
  const std::filesystem::path& vault_root);
std::string relative_vault_path (const std::filesystem::path& vault_root,
                                 const std::filesystem::path& file);
bool read_file_bytes (const std::filesystem::path& file, std::string& bytes);
int64_t file_mtime_ns (const std::filesystem::path& file);
std::string content_hash (const std::string& bytes);

bool collect_delegated_job (const std::filesystem::path& vault_root,
                            const std::filesystem::path& local_db,
                            DelegatedJob& job,
                            std::string& error);

bool build_patch_for_job (const DelegatedJob& job,
                          const std::filesystem::path& patch_db,
                          const std::filesystem::path& temp_parent,
                          const RagConfig& config,
                          std::string& error);

bool apply_patch_database (const std::filesystem::path& vault_root,
                           const std::filesystem::path& local_db,
                           const std::filesystem::path& patch_db,
                           const std::vector<std::string>& deleted,
                           std::string& error);

} // namespace athena::rag::delegation

#endif // RAG_DELEGATION_PATCH_HPP
