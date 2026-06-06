/******************************************************************************
* MODULE     : vault_maintenance_pass_retention.cpp
* DESCRIPTION: Vault maintenance retention cleanup pass
* COPYRIGHT  : (C) 2026  Felix
******************************************************************************/

#include "ATHENA/Data/vault_maintenance_internal.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
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

static bool
manual_save_time_from_name (
  const std::string& name,
  std::chrono::system_clock::time_point& out) {
  if (name.size () < 15) return false;
  std::string stamp = name.substr (0, 15);
  if (stamp[8] != 'T') return false;
  for (size_t i=0; i<stamp.size (); i++)
    if (i != 8 && !std::isdigit ((unsigned char) stamp[i])) return false;

  int year = std::stoi (stamp.substr (0, 4));
  int month = std::stoi (stamp.substr (4, 2));
  int day = std::stoi (stamp.substr (6, 2));
  int hour = std::stoi (stamp.substr (9, 2));
  int minute = std::stoi (stamp.substr (11, 2));
  int second = std::stoi (stamp.substr (13, 2));
  if (month < 1 || month > 12 || day < 1 || day > 31 ||
      hour < 0 || hour > 23 || minute < 0 || minute > 59 ||
      second < 0 || second > 60)
    return false;

  std::tm tm_value;
  std::memset (&tm_value, 0, sizeof (tm_value));
  tm_value.tm_year = year - 1900;
  tm_value.tm_mon = month - 1;
  tm_value.tm_mday = day;
  tm_value.tm_hour = hour;
  tm_value.tm_min = minute;
  tm_value.tm_sec = second;
  tm_value.tm_isdst = -1;
  std::time_t t = std::mktime (&tm_value);
  if (t == (std::time_t) -1) return false;
  std::tm check;
#if defined(_WIN32)
  if (localtime_s (&check, &t) != 0) return false;
#else
  if (localtime_r (&t, &check) == nullptr) return false;
#endif
  if (check.tm_year != tm_value.tm_year || check.tm_mon != tm_value.tm_mon ||
      check.tm_mday != tm_value.tm_mday || check.tm_hour != tm_value.tm_hour ||
      check.tm_min != tm_value.tm_min || check.tm_sec != tm_value.tm_sec)
    return false;
  out = std::chrono::system_clock::from_time_t (t);
  return true;
}

static std::chrono::system_clock::time_point
file_time_to_system_time (fs::file_time_type t) {
  return std::chrono::time_point_cast<std::chrono::system_clock::duration> (
    t - fs::file_time_type::clock::now () + std::chrono::system_clock::now ());
}

static bool
manual_save_history_time (
  const fs::path& path,
  std::chrono::system_clock::time_point& out) {
  if (manual_save_time_from_name (path.filename ().string (), out)) return true;
  std::error_code ec;
  fs::file_time_type t = fs::last_write_time (path, ec);
  if (ec) return false;
  out = file_time_to_system_time (t);
  return true;
}

static bool
purge_old_manual_save_histories (const fs::path& root, long long retention_seconds,
                                 size_t& purged) {
  purged = 0;
  if (retention_seconds == VAULT_MANUAL_SAVE_RETENTION_UNLIMITED) {
    log_info ("pre-save history retention: Unlimited");
    return true;
  }

  fs::path manual_root = root / ".backup" / "manual-save";
  if (!fs::exists (manual_root)) {
    log_info ("pre-save history retention: no manual-save directory found");
    return true;
  }

  auto cutoff = std::chrono::system_clock::now () -
                std::chrono::seconds (retention_seconds);
  std::vector<fs::path> expired;
  std::error_code ec;
  for (fs::directory_iterator it (manual_root, fs::directory_options::skip_permission_denied, ec), end;
       !ec && it != end; it.increment (ec)) {
    if (!it->is_directory (ec)) continue;
    std::chrono::system_clock::time_point history_time;
    if (!manual_save_history_time (it->path (), history_time)) {
      log_info ("pre-save history retention: could not date " +
                it->path ().string () + "; keeping it");
      continue;
    }
    if (history_time < cutoff) expired.push_back (it->path ());
  }
  if (ec) {
    log_error ("failed to scan manual-save backup directory: " + ec.message ());
    return false;
  }

  std::sort (expired.begin (), expired.end ());
  log_info ("pre-save history retention: keeping " +
            manual_save_retention_label (retention_seconds));
  if (expired.empty ()) {
    log_info ("pre-save history retention: no old histories purged");
    return true;
  }

  for (size_t i=0; i<expired.size (); i++) {
    print_progress (i + 1, expired.size (), "Purging pre-save histories",
                    expired[i].filename ().string ());
    fs::remove_all (expired[i], ec);
    if (ec) {
      finish_progress ();
      log_error ("failed to purge pre-save history " + expired[i].string () +
                 ": " + ec.message ());
      return false;
    }
    purged++;
  }
  finish_progress ();
  log_info ("pre-save history retention: purged " + std::to_string (purged) +
            " old history folder(s)");
  return true;
}


VaultMaintenancePassResult
vault_maintenance_pass_purge_retained_data (VaultMaintenanceContext& ctx) {
  if (!purge_old_backups (ctx.root, ctx.summary.backup_limit,
                          ctx.summary.backups_purged))
    return VaultMaintenancePassResult::failure ("backup retention cleanup failed");
  if (!purge_old_manual_save_histories (
        ctx.root, ctx.summary.manual_save_retention_seconds,
        ctx.summary.manual_save_histories_purged))
    return VaultMaintenancePassResult::failure (
      "pre-save history retention cleanup failed");
  return VaultMaintenancePassResult::success ();
}
