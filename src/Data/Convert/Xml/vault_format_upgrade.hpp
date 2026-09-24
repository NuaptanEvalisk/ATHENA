/******************************************************************************
* MODULE     : vault_format_upgrade.hpp
* DESCRIPTION: Offline whole-vault UTF-8/XML migration with atomic directory commit
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#pragma once
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace athena::document {
using vault_upgrade_progress= std::function<void (
  const char* phase, std::size_t done, std::size_t total, const std::string& path)>;
struct vault_upgrade_result {
  std::filesystem::path backup;
  std::size_t converted= 0, already_xml= 0;
  bool durable= true;
};
// Offline only. Progress may throw to cancel BEFORE commit. Once exchanged,
// the complete original vault remains at backup even if directory fsync fails.
vault_upgrade_result upgrade_vault_format (
  const std::filesystem::path&, const vault_upgrade_progress& = {});
int upgrade_vault_format_cli (const std::filesystem::path&);

struct vault_upgrade_revision {
  std::string path, semantic_hash, storage_hash;
  long long size, mtime;
};
// Called only on the private snapshot, before any document bytes are replaced.
void prepare_vault_upgrade_indexes (const std::filesystem::path&,
  const std::vector<vault_upgrade_revision>&, const vault_upgrade_progress& = {});
}
