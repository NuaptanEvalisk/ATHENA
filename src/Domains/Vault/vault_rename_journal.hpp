/******************************************************************************
* MODULE     : vault_rename_journal.hpp
* DESCRIPTION: Durable safe-rename operations independent of disposable locators
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#pragma once
#include <filesystem>
#include <string>
#include <vector>
struct sqlite3;

struct VaultRenameOperation {
  std::string operation_id, old_path, new_path;
  bool is_directory= false;
  std::string phase;
};

class VaultRenameJournal {
  sqlite3* db= nullptr;
public:
  ~VaultRenameJournal ();
  VaultRenameJournal ()= default;
  VaultRenameJournal (const VaultRenameJournal&)= delete;
  VaultRenameJournal& operator= (const VaultRenameJournal&)= delete;
  bool open (const std::filesystem::path& root, std::string& error);
  bool prepare (const VaultRenameOperation&, std::string& error);
  bool pending (std::vector<VaultRenameOperation>&, std::string& error);
  bool finish (const std::string& id, std::string& error);
};
