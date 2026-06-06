/******************************************************************************
* MODULE     : vault_maintenance_internal.hpp
* DESCRIPTION: Internal helpers for ATHENA vault maintenance passes
* COPYRIGHT  : (C) 2026  Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef VAULT_MAINTENANCE_INTERNAL_HPP
#define VAULT_MAINTENANCE_INTERNAL_HPP

#include "ATHENA/Data/vault_maintenance_passes.hpp"

#include "string.hpp"

#include <filesystem>
#include <string>
#include <unordered_set>
#include <vector>

inline constexpr int VAULT_BACKUP_LIMIT_UNLIMITED = -1;
inline constexpr long long VAULT_MANUAL_SAVE_RETENTION_UNLIMITED = -1;

struct RenamePlan {
  std::filesystem::path old_path;
  std::filesystem::path new_path;
  std::string old_key;
  std::string old_stem;
  std::string old_name;
};

struct ImageRef {
  size_t begin = 0;
  size_t end = 0;
  std::string raw_path;
};

std::string tm_to_std (string s);
string std_to_tm (const std::string& s);
std::string lower_copy (std::string s);
bool starts_with (const std::string& s, const std::string& prefix);
bool ends_with (const std::string& s, const std::string& suffix);
std::string trim_copy (const std::string& s);
bool is_image_extension (const std::filesystem::path& path);
bool has_canonical_image_name (const std::filesystem::path& path);
std::string path_key (const std::filesystem::path& path);
bool is_backup_path (const std::filesystem::path& root,
                     const std::filesystem::path& path);
bool is_orphan_collection_path (const std::filesystem::path& root,
                                const std::filesystem::path& path);
std::string manual_save_retention_label (long long seconds);
std::string timestamp_string ();
void log_info (const std::string& message);
void log_error (const std::string& message);
void print_progress (size_t current, size_t total, const std::string& action,
                     const std::string& item);
void finish_progress ();
std::string generate_uuid_v4 ();
std::string canonical_extension (const std::filesystem::path& path);
std::vector<std::filesystem::path> scan_noncanonical_images (
  const std::filesystem::path& root);
std::vector<std::filesystem::path> scan_documents (
  const std::filesystem::path& root);
std::vector<std::filesystem::path> scan_ath_documents (
  const std::filesystem::path& root);
std::vector<std::filesystem::path> scan_asset_files (
  const std::filesystem::path& root);
bool read_file_bytes (const std::filesystem::path& path, std::string& text);
bool write_file_bytes (const std::filesystem::path& path,
                       const std::string& text);
std::string tm_unescape_path (const std::string& text);
std::string tm_escape_path (const std::string& text);
bool parse_image_ref_at (const std::string& text, size_t pos, ImageRef& ref);
bool is_probably_local_path (const std::string& path);
std::filesystem::path resolve_reference_path (const std::filesystem::path& doc_path,
                                             const std::string& ref);
std::string reference_for_replacement (const std::filesystem::path& doc_path,
                                       const std::filesystem::path& target,
                                       const std::string& old_ref);
std::string stem_from_reference (const std::string& ref);
std::filesystem::path normalize_root (const std::filesystem::path& input);

#endif // VAULT_MAINTENANCE_INTERNAL_HPP
