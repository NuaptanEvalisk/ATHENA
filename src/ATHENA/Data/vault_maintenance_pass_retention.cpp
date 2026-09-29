/******************************************************************************
* MODULE     : vault_maintenance_pass_retention.cpp
* DESCRIPTION: Vault maintenance retention cleanup pass
* COPYRIGHT  : (C) 2026  Felix
******************************************************************************/

#include "ATHENA/Data/vault_maintenance_internal.hpp"

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static bool
purge_old_backups (const fs::path& root, int max_full_backups, size_t& purged) {
  purged = 0;
  if (max_full_backups == VAULT_BACKUP_LIMIT_UNLIMITED) {
    log_info ("backup retention: Unlimited");
    return true;
  }

  fs::path backup_root = root / ".backup";
  if (!fs::exists (backup_root)) {
    log_info ("backup retention: no .backup directory found");
    return true;
  }

  std::vector<fs::path> backup_dirs;
  std::error_code ec;
  for (fs::directory_iterator it (backup_root, fs::directory_options::skip_permission_denied, ec), end;
       !ec && it != end; it.increment (ec)) {
    if (!it->is_directory (ec)) continue;
    fs::path archive = it->path () / "vault.tar.zst";
    if (fs::is_regular_file (archive, ec)) backup_dirs.push_back (it->path ());
  }
  if (ec) {
    log_error ("failed to scan backup directory for retention: " + ec.message ());
    return false;
  }

  std::sort (backup_dirs.begin (), backup_dirs.end (),
             [] (const fs::path& a, const fs::path& b) {
               return a.filename ().string () > b.filename ().string ();
             });

  log_info ("backup retention: keeping at most " +
            std::to_string (max_full_backups) + " full backup(s)");
  if (backup_dirs.size () <= (size_t) max_full_backups) {
    log_info ("backup retention: no old backups purged");
    return true;
  }

  size_t total = backup_dirs.size () - (size_t) max_full_backups;
  for (size_t i= max_full_backups; i<backup_dirs.size (); i++) {
    print_progress (i - (size_t) max_full_backups + 1, total,
                    "Purging backups", backup_dirs[i].filename ().string ());
    fs::remove_all (backup_dirs[i], ec);
    if (ec) {
      finish_progress ();
      log_error ("failed to purge old backup " + backup_dirs[i].string () +
                 ": " + ec.message ());
      return false;
    }
    purged++;
  }
  finish_progress ();
  log_info ("backup retention: purged " + std::to_string (purged) +
            " old full backup(s)");
  return true;
}

VaultMaintenancePassResult
vault_maintenance_pass_purge_retained_data (VaultMaintenanceContext& ctx) {
  if (!purge_old_backups (ctx.root, ctx.summary.backup_limit,
                          ctx.summary.backups_purged))
    return VaultMaintenancePassResult::failure ("backup retention cleanup failed");
  return VaultMaintenancePassResult::success ();
}
