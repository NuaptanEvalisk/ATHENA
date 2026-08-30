/******************************************************************************
* MODULE     : vaultfile_json.hpp
* DESCRIPTION: ATHENA vault metadata JSON reader/writer and legacy migration
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef ATHENA_VAULTFILE_JSON_HPP
#define ATHENA_VAULTFILE_JSON_HPP

#include <filesystem>
#include <string>
#include <vector>

struct AthenaBackupDispatcher {
  std::string destination;
  std::string trigger;
};

struct AthenaVaultfileInfo {
  std::string name= "Vault";
  std::string map_path= "map.sqlite";
  std::string preferences_path;
  std::string namespace_db_path= "ns.sqlite";
  std::string startup_page;
  std::string one_time_startup_page;
  std::string maintenance_summary_path;
  std::string rag_index_path= "rag.sqlite";
  std::string websites_path= "websites.json";
  std::string root_namespace;
  std::string artifacts_path= "artifacts.db";
  std::string enunciations_path= "enunciations.db";
  std::string bold_text_path= "bold-text.db";
  std::string materials_db_path= "materials.sqlite";
  std::string materials_directory= "materials";
  std::string artifact_title_filter_path= "artifact-title-filter.lst";
  std::vector<AthenaBackupDispatcher> backup_dispatchers;
};

std::filesystem::path athena_vaultfile_json_path (
  const std::filesystem::path& root);
std::filesystem::path athena_vaultfile_legacy_path (
  const std::filesystem::path& root);
bool athena_vaultfile_present (const std::filesystem::path& root);
std::vector<std::string> athena_vaultfile_legacy_strings (
  const std::string& text);
AthenaVaultfileInfo athena_vaultfile_normalize (
  const AthenaVaultfileInfo& info);
AthenaVaultfileInfo athena_vaultfile_from_fields (
  const std::vector<std::string>& fields);
std::vector<std::string> athena_vaultfile_to_fields (
  const AthenaVaultfileInfo& info);
bool athena_vaultfile_read (const std::filesystem::path& root,
                            AthenaVaultfileInfo& info,
                            std::string& error);
bool athena_vaultfile_write (const std::filesystem::path& root,
                             const AthenaVaultfileInfo& info,
                             std::string& error);
bool athena_vaultfile_ensure_json (const std::filesystem::path& root,
                                   std::string& error);

#endif // ATHENA_VAULTFILE_JSON_HPP
