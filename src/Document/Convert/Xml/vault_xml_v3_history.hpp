/******************************************************************************
* MODULE     : vault_xml_v3_history.hpp
* DESCRIPTION: Offline, staged history rewriting with immutable v2 evidence
* COPYRIGHT  : (C) 2026 ATHENA contributors
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
******************************************************************************/
#pragma once

#include <cstddef>
#include <filesystem>
#include <functional>
#include <string>

namespace athena::document {

struct v3_history_migration_stats {
  std::size_t versions_converted= 0;
  std::size_t protected_evidence= 0;
  std::size_t warnings= 0;
};

// History source is read exclusively. A staged vault root, when provided,
// receives every mutation; the source tree, database and files are untouched.
// Without a staged vault, this performs a read-only validation/conversion dry
// run. Caller must hold the appropriate offline Vault lock or fixed snapshot.
v3_history_migration_stats migrate_vault_history_v3 (
  const std::filesystem::path& original_vault,
  const std::filesystem::path* staged_vault,
  bool strict,
  const std::function<void(std::size_t,std::size_t,const std::string&)>& progress = {});

} // namespace athena::document
