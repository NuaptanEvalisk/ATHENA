/******************************************************************************
* MODULE     : vault_maintenance.hpp
* DESCRIPTION: Headless maintenance operations for ATHENA vaults
* COPYRIGHT  : (C) 2026  Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef VAULT_MAINTENANCE_HPP
#define VAULT_MAINTENANCE_HPP

#include "string.hpp"

#include <string>
#include <vector>

struct VaultMaintenancePlanEntry {
  std::string id;
  std::string description;
};

bool vault_maintenance_run (string vault_dir, bool check_only = false);
bool vault_rag_delegation_run (string vault_dir);
bool vault_maintenance_plan (
  string vault_dir, std::vector<VaultMaintenancePlanEntry>& entries,
  std::string& error);

#endif // VAULT_MAINTENANCE_HPP
