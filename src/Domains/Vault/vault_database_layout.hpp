/******************************************************************************
* MODULE     : vault_database_layout.hpp
* DESCRIPTION: Restart-time, recoverable canonical vault database relocation
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#ifndef ATHENA_VAULT_DATABASE_LAYOUT_HPP
#define ATHENA_VAULT_DATABASE_LAYOUT_HPP
#include <filesystem>
#include <string>

bool athena_vault_canonical_database_preference (const std::filesystem::path& root,
  bool fallback, bool use_vault_preferences);
bool athena_vault_canonicalize_databases (const std::filesystem::path& root,
  bool enabled, std::string& error);
bool athena_vault_database_layout_pending (const std::filesystem::path& root);
#endif
