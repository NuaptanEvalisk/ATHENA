/******************************************************************************
* MODULE     : namespace_sorter_migration.hpp
* DESCRIPTION: Offline explicit-mapping migration for namespace sorters
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#pragma once

#include <filesystem>
#include <optional>
#include <string>

int athena_upgrade_vault_sorters_cli (
  const std::filesystem::path& vault_root,
  const std::optional<std::string>& inline_map,
  const std::optional<std::filesystem::path>& map_file,
  bool dry_run);
