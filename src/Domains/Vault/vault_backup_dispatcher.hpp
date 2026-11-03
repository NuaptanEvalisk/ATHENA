/******************************************************************************
* MODULE     : vault_backup_dispatcher.hpp
* DESCRIPTION: One-way vault backup dispatch through rsync
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef ATHENA_VAULT_BACKUP_DISPATCHER_HPP
#define ATHENA_VAULT_BACKUP_DISPATCHER_HPP

#include <filesystem>
#include <string>
#include <vector>

struct AthenaBackupDispatchCommand {
  std::string program;
  std::vector<std::string> arguments;
  std::string normalized_destination;
};

bool athena_backup_dispatch_validate_destination (
  const std::filesystem::path& vault_root, const std::string& destination,
  std::string& normalized_destination, std::string& error);
bool athena_backup_dispatch_prepare (
  const std::filesystem::path& vault_root, const std::string& destination,
  AthenaBackupDispatchCommand& command, std::string& error);
bool athena_backup_dispatch_run (
  const std::filesystem::path& vault_root, const std::string& destination,
  std::string& error);

#endif // ATHENA_VAULT_BACKUP_DISPATCHER_HPP
