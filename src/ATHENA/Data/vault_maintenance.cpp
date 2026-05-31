/******************************************************************************
* MODULE     : vault_maintenance.cpp
* DESCRIPTION: Headless maintenance operations for ATHENA vaults
* COPYRIGHT  : (C) 2026  Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "ATHENA/Data/vault_maintenance.hpp"

#include "boot.hpp"
#include "scheme.hpp"
#include "sys_utils.hpp"
#include "tm_ostream.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <ctime>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <zstd.h>

namespace fs = std::filesystem;

namespace {

static constexpr int BACKUP_LIMIT_UNLIMITED = -1;
static constexpr long long MANUAL_SAVE_RETENTION_UNLIMITED = -1;

static void log_info (const std::string& message);
static void log_error (const std::string& message);
static bool read_file_bytes (const fs::path& path, std::string& text);

struct RenamePlan {
  fs::path old_path;
  fs::path new_path;
  std::string old_key;
  std::string old_stem;
  std::string old_name;
};

struct ImageRef {
  size_t begin = 0;
  size_t end = 0;
  std::string raw_path;
};

struct MaintenanceSummary {
  fs::path backup_archive;
  int backup_limit = BACKUP_LIMIT_UNLIMITED;
  size_t backups_purged = 0;
  long long manual_save_retention_seconds = MANUAL_SAVE_RETENTION_UNLIMITED;
  size_t manual_save_histories_purged = 0;
  size_t image_renames = 0;
  size_t image_reference_updates = 0;
  size_t anchor_files_scanned = 0;
  size_t anchor_files_changed = 0;
  size_t anchor_enunciations_wrapped = 0;
  size_t anchor_dead_pairs_removed = 0;
  size_t anchor_failures = 0;
  bool orphan_collection_enabled = false;
  size_t orphan_assets_collected = 0;
  fs::path orphan_dir;
};

static std::string
tm_to_std (string s) {
  return std::string (as_charp (s));
}

static string
std_to_tm (const std::string& s) {
  return string (s.c_str ());
}

static std::string
lower_copy (std::string s) {
  std::transform (s.begin (), s.end (), s.begin (),
                  [] (unsigned char c) { return (char) std::tolower (c); });
  return s;
}

static bool
starts_with (const std::string& s, const std::string& prefix) {
  return s.size () >= prefix.size () &&
         s.compare (0, prefix.size (), prefix) == 0;
}

static bool
ends_with (const std::string& s, const std::string& suffix) {
  return s.size () >= suffix.size () &&
         s.compare (s.size () - suffix.size (), suffix.size (), suffix) == 0;
}

static std::string
trim_copy (const std::string& s) {
  size_t begin = 0;
  while (begin < s.size () && std::isspace ((unsigned char) s[begin])) begin++;
  size_t end = s.size ();
  while (end > begin && std::isspace ((unsigned char) s[end - 1])) end--;
  return s.substr (begin, end - begin);
}

static bool
is_hex_digit (char c) {
  return (c >= '0' && c <= '9') ||
         (c >= 'a' && c <= 'f') ||
         (c >= 'A' && c <= 'F');
}

static bool
is_uuid_v4 (const std::string& s) {
  if (s.size () != 36) return false;
  for (size_t i=0; i<s.size (); i++) {
    if (i == 8 || i == 13 || i == 18 || i == 23) {
      if (s[i] != '-') return false;
    }
    else if (!is_hex_digit (s[i])) return false;
  }
  if (s[14] != '4') return false;
  char variant = (char) std::tolower ((unsigned char) s[19]);
  return variant == '8' || variant == '9' || variant == 'a' || variant == 'b';
}

static bool
is_image_extension (const fs::path& path) {
  std::string ext = lower_copy (path.extension ().string ());
  if (!ext.empty () && ext[0] == '.') ext.erase (ext.begin ());
  static const std::unordered_set<std::string> exts = {
    "png", "jpg", "jpeg", "gif", "webp", "bmp", "tif", "tiff",
    "svg", "pdf", "eps", "ps"
  };
  return exts.find (ext) != exts.end ();
}

static bool
has_canonical_image_name (const fs::path& path) {
  if (!is_image_extension (path)) return false;
  std::string stem = path.stem ().string ();
  if (!starts_with (stem, "figure-")) return false;
  return is_uuid_v4 (stem.substr (7));
}

static std::string
path_key (const fs::path& path) {
  std::error_code ec;
  fs::path absolute = fs::absolute (path, ec);
  if (ec) absolute = path;
  return absolute.lexically_normal ().generic_string ();
}

static bool
is_backup_path (const fs::path& root, const fs::path& path) {
  fs::path rel = path.lexically_relative (root);
  if (rel.empty ()) return false;
  auto it = rel.begin ();
  return it != rel.end () && *it == ".backup";
}

static bool
is_digits (const std::string& s) {
  if (s.empty ()) return false;
  for (char c : s)
    if (!std::isdigit ((unsigned char) c)) return false;
  return true;
}

static bool
is_orphan_dir_name (const std::string& name) {
  if (name == "orphan") return true;
  if (!starts_with (name, "orphan (") || !ends_with (name, ")")) return false;
  return is_digits (name.substr (8, name.size () - 9));
}

static bool
is_orphan_collection_path (const fs::path& root, const fs::path& path) {
  fs::path rel = path.lexically_relative (root);
  if (rel.empty ()) return false;
  auto it = rel.begin ();
  if (it == rel.end ()) return false;
  return is_orphan_dir_name ((*it).string ());
}

static int
backup_limit_preference () {
  std::string pref =
    trim_copy (tm_to_std (get_preference ("vault max full backups", "Unlimited")));
  std::string low = lower_copy (pref);
  if (pref.empty () || low == "unlimited") return BACKUP_LIMIT_UNLIMITED;
  try {
    size_t pos = 0;
    int value = std::stoi (pref, &pos);
    if (pos == pref.size () && value >= 1) return value;
  }
  catch (...) {}
  log_info ("invalid backup retention preference '" + pref +
            "'; using Unlimited");
  return BACKUP_LIMIT_UNLIMITED;
}

static bool
collect_orphan_assets_preference () {
  return get_preference ("vault collect orphan assets", "off") == "on";
}

static long long
manual_save_retention_preference () {
  std::string pref = trim_copy (tm_to_std (
    get_preference ("vault pre-save history preservation", "1 week")));
  std::string low = lower_copy (pref);
  if (pref.empty () || low == "unlimited")
    return MANUAL_SAVE_RETENTION_UNLIMITED;
  if (low == "1 hour") return 60LL * 60LL;
  if (low == "6 hours") return 6LL * 60LL * 60LL;
  if (low == "1 day") return 24LL * 60LL * 60LL;
  if (low == "3 days") return 3LL * 24LL * 60LL * 60LL;
  if (low == "1 week") return 7LL * 24LL * 60LL * 60LL;
  if (low == "1 month") return 30LL * 24LL * 60LL * 60LL;
  log_info ("invalid pre-save history preservation preference '" + pref +
            "'; using Unlimited");
  return MANUAL_SAVE_RETENTION_UNLIMITED;
}

static std::string
manual_save_retention_label (long long seconds) {
  if (seconds == MANUAL_SAVE_RETENTION_UNLIMITED) return "Unlimited";
  if (seconds == 60LL * 60LL) return "1 hour";
  if (seconds == 6LL * 60LL * 60LL) return "6 hours";
  if (seconds == 24LL * 60LL * 60LL) return "1 day";
  if (seconds == 3LL * 24LL * 60LL * 60LL) return "3 days";
  if (seconds == 7LL * 24LL * 60LL * 60LL) return "1 week";
  if (seconds == 30LL * 24LL * 60LL * 60LL) return "1 month";
  return std::to_string (seconds) + " seconds";
}

static std::vector<std::string>
split_tabs (const std::string& text) {
  std::vector<std::string> parts;
  size_t begin = 0;
  for (size_t i=0; i<=text.size (); i++) {
    if (i == text.size () || text[i] == '\t') {
      parts.push_back (text.substr (begin, i - begin));
      begin = i + 1;
    }
  }
  return parts;
}

static size_t
parse_count (const std::string& text) {
  try {
    size_t pos = 0;
    unsigned long long value = std::stoull (text, &pos);
    if (pos == text.size ()) return (size_t) value;
  }
  catch (...) {}
  return 0;
}

static std::vector<std::string>
parse_vaultfile_strings (const std::string& text) {
  std::vector<std::string> values;
  for (size_t i=0; i<text.size (); i++) {
    if (text[i] != '"') continue;
    i++;
    std::string value;
    while (i < text.size ()) {
      char c = text[i++];
      if (c == '\\' && i < text.size ()) {
        value.push_back (text[i++]);
        continue;
      }
      if (c == '"') break;
      value.push_back (c);
    }
    values.push_back (value);
  }
  return values;
}

static bool
load_vault_preferences_if_enabled (const fs::path& root) {
  std::string requested = lower_copy (tm_to_std (
    get_env ("ATHENA_VAULT_MAINTENANCE_TAKE_PREFS")));
  bool take_prefs = requested == "on" ||
                    (requested.empty () &&
                     get_preference ("vault take preferences with vault", "off") == "on");
  if (!take_prefs) {
    log_info ("preferences: using system preferences");
    return true;
  }

  std::string text;
  fs::path vault_file = root / "Vaultfile";
  if (!read_file_bytes (vault_file, text)) {
    log_error ("failed to read Vaultfile for vault preferences");
    return false;
  }

  std::vector<std::string> fields = parse_vaultfile_strings (text);
  std::string prefs_rel = fields.size () >= 3 ? fields[2] : "";
  fs::path prefs_path = root / (prefs_rel.empty () ? "vprefs.scm" : prefs_rel);
  if (!fs::exists (prefs_path)) {
    log_info ("preferences: vault preferences enabled, but " +
              prefs_path.string () + " does not exist; using system preferences");
    return true;
  }

  load_user_preferences (url (prefs_path.string ().c_str ()));
  log_info ("preferences: loaded vault preferences from " + prefs_path.string ());
  return true;
}

static std::string
timestamp_string () {
  auto now = std::chrono::system_clock::now ();
  std::time_t t = std::chrono::system_clock::to_time_t (now);
  std::tm tm_value;
#if defined(_WIN32)
  localtime_s (&tm_value, &t);
#else
  localtime_r (&t, &tm_value);
#endif
  char buf[32];
  std::strftime (buf, sizeof (buf), "%Y%m%d-%H%M%S", &tm_value);
  return std::string (buf);
}

class ProgressDisplay {
public:
  void update (size_t current, size_t total, const std::string& phase,
               const std::string& item) {
    if (total == 0) total = 1;
    current_ = current;
    total_ = total;
    phase_ = phase;
    item_ = item;
    draw ();
  }

  void log (const std::string& message) {
    bool was_active = active_;
    clear ();
    cout << message.c_str () << LF;
    if (was_active) draw ();
  }

  void finish () {
    if (!active_) return;
    std::cout << std::endl;
    active_ = false;
  }

private:
  void draw () {
    int bar_width = 30;
    double progress = std::min (1.0, (double) current_ / (double) total_);
    int pos = (int) (bar_width * progress);

    std::string shown = item_;
    if (shown.size () > 44) shown = "..." + shown.substr (shown.size () - 41);

    std::ostringstream line;
    line << "[";
    for (int i=0; i<bar_width; i++) {
      if (i < pos) line << "=";
      else if (i == pos) line << ">";
      else line << " ";
    }
    line << "] " << (int) (progress * 100.0) << "% "
         << "[" << current_ << "/" << total_ << "] "
         << phase_ << ": " << shown;
    last_width_ = line.str ().size ();
    std::cout << "\r" << line.str () << std::flush;
    active_ = true;
  }

  void clear () {
    if (!active_) return;
    std::cout << "\r" << std::string (last_width_ + 8, ' ') << "\r"
              << std::flush;
  }

  bool active_ = false;
  size_t current_ = 0;
  size_t total_ = 1;
  size_t last_width_ = 0;
  std::string phase_;
  std::string item_;
};

static ProgressDisplay progress_display;

static void
log_info (const std::string& message) {
  progress_display.log ("ATHENA] vault maintenance: " + message);
}

static void
log_error (const std::string& message) {
  progress_display.finish ();
  cerr << "ATHENA] vault maintenance: " << message.c_str () << LF;
}

static void
print_progress (size_t current, size_t total, const std::string& phase,
                const std::string& item) {
  progress_display.update (current, total, phase, item);
}

static std::string
generate_uuid_v4 () {
  static std::random_device rd;
  static std::mt19937_64 gen (rd ());
  static std::uniform_int_distribution<int> dist (0, 255);

  std::array<unsigned char, 16> bytes;
  for (unsigned char& b : bytes) b = (unsigned char) dist (gen);
  bytes[6] = (unsigned char) ((bytes[6] & 0x0f) | 0x40);
  bytes[8] = (unsigned char) ((bytes[8] & 0x3f) | 0x80);

  std::ostringstream out;
  for (size_t i=0; i<bytes.size (); i++) {
    if (i == 4 || i == 6 || i == 8 || i == 10) out << '-';
    out << std::hex << std::setw (2) << std::setfill ('0') << (int) bytes[i];
  }
  return out.str ();
}

static std::string
canonical_extension (const fs::path& path) {
  std::string ext = lower_copy (path.extension ().string ());
  return ext.empty () ? ext : ext;
}

class ZstdWriter {
public:
  explicit ZstdWriter (const fs::path& path): out_ (path, std::ios::binary) {
    cctx_ = ZSTD_createCCtx ();
    if (cctx_ != nullptr) {
      ZSTD_CCtx_setParameter (cctx_, ZSTD_c_compressionLevel, 3);
      ZSTD_CCtx_setParameter (cctx_, ZSTD_c_nbWorkers,
                              std::max (1u, std::thread::hardware_concurrency ()));
    }
    output_.resize (ZSTD_CStreamOutSize ());
  }

  ~ZstdWriter () {
    if (cctx_ != nullptr) ZSTD_freeCCtx (cctx_);
  }

  bool ok () const {
    return out_.good () && cctx_ != nullptr && error_.empty ();
  }

  const std::string& error () const { return error_; }

  bool write (const void* data, size_t size) {
    if (!ok ()) return false;
    ZSTD_inBuffer input = { data, size, 0 };
    while (input.pos < input.size) {
      ZSTD_outBuffer output = { output_.data (), output_.size (), 0 };
      size_t ret = ZSTD_compressStream2 (cctx_, &output, &input, ZSTD_e_continue);
      if (ZSTD_isError (ret)) {
        error_ = ZSTD_getErrorName (ret);
        return false;
      }
      out_.write ((const char*) output.dst, (std::streamsize) output.pos);
      if (!out_) {
        error_ = "failed to write compressed backup";
        return false;
      }
    }
    return true;
  }

  bool write_string (const std::string& s) {
    return write (s.data (), s.size ());
  }

  bool finish () {
    if (!ok ()) return false;
    size_t ret = 0;
    do {
      ZSTD_inBuffer input = { nullptr, 0, 0 };
      ZSTD_outBuffer output = { output_.data (), output_.size (), 0 };
      ret = ZSTD_compressStream2 (cctx_, &output, &input, ZSTD_e_end);
      if (ZSTD_isError (ret)) {
        error_ = ZSTD_getErrorName (ret);
        return false;
      }
      out_.write ((const char*) output.dst, (std::streamsize) output.pos);
      if (!out_) {
        error_ = "failed to finalize compressed backup";
        return false;
      }
    } while (ret != 0);
    out_.close ();
    return true;
  }

private:
  std::ofstream out_;
  ZSTD_CCtx* cctx_ = nullptr;
  std::vector<char> output_;
  std::string error_;
};

static void
tar_write_octal (char* field, size_t width, uint64_t value) {
  std::snprintf (field, width, "%0*lo", (int) width - 1, (unsigned long) value);
}

static void
tar_write_base256 (char* field, size_t width, uint64_t value) {
  std::memset (field, 0, width);
  for (size_t i=0; i<width; i++) {
    field[width - 1 - i] = (char) (value & 0xff);
    value >>= 8;
  }
  field[0] |= (char) 0x80;
}

static void
tar_write_number (char* field, size_t width, uint64_t value) {
  uint64_t limit = 1;
  for (size_t i=1; i<width; i++) limit *= 8;
  if (value < limit) tar_write_octal (field, width, value);
  else tar_write_base256 (field, width, value);
}

static std::string
pax_record (const std::string& key, const std::string& value) {
  std::string body = key + "=" + value + "\n";
  size_t len = body.size () + 2;
  while (true) {
    size_t digits = std::to_string (len).size ();
    size_t next = body.size () + digits + 1;
    if (next == len) break;
    len = next;
  }
  return std::to_string (len) + " " + body;
}

static bool
tar_write_padding (ZstdWriter& writer, uint64_t size) {
  static const char zeros[512] = {0};
  size_t rem = (size_t) (size % 512);
  if (rem == 0) return true;
  return writer.write (zeros, 512 - rem);
}

static bool
tar_write_header (ZstdWriter& writer, const std::string& path, char type,
                  uint64_t size, uint64_t mtime, const std::string& link = "") {
  char h[512];
  std::memset (h, 0, sizeof (h));
  std::string name = path;
  std::string prefix;

  if (name.size () > 100) {
    size_t split = name.rfind ('/', std::min<size_t> (name.size (), 155));
    if (split != std::string::npos && name.size () - split - 1 <= 100) {
      prefix = name.substr (0, split);
      name = name.substr (split + 1);
    }
  }
  if (name.size () > 100 || prefix.size () > 155) {
    name = "PaxHeader";
    prefix.clear ();
  }

  std::memcpy (h, name.data (), std::min<size_t> (name.size (), 100));
  tar_write_number (h + 100, 8, type == '5' ? 0755 : 0644);
  tar_write_number (h + 108, 8, 0);
  tar_write_number (h + 116, 8, 0);
  tar_write_number (h + 124, 12, size);
  tar_write_number (h + 136, 12, mtime);
  std::memset (h + 148, ' ', 8);
  h[156] = type;
  if (!link.empty ())
    std::memcpy (h + 157, link.data (), std::min<size_t> (link.size (), 100));
  std::memcpy (h + 257, "ustar", 5);
  std::memcpy (h + 263, "00", 2);
  if (!prefix.empty ())
    std::memcpy (h + 345, prefix.data (), std::min<size_t> (prefix.size (), 155));

  unsigned int sum = 0;
  for (unsigned char c : h) sum += c;
  std::snprintf (h + 148, 8, "%06o", sum);
  h[154] = '\0';
  h[155] = ' ';
  return writer.write (h, sizeof (h));
}

static bool
tar_write_pax_path (ZstdWriter& writer, const std::string& path, uint64_t mtime) {
  std::string record = pax_record ("path", path);
  if (!tar_write_header (writer, "PaxHeader", 'x', record.size (), mtime))
    return false;
  return writer.write_string (record) && tar_write_padding (writer, record.size ());
}

static bool
tar_write_entry (ZstdWriter& writer, const fs::path& root, const fs::path& path) {
  std::error_code ec;
  fs::path rel = path.lexically_relative (root);
  std::string rel_s = rel.generic_string ();
  auto ftime = fs::last_write_time (path, ec);
  uint64_t mtime = 0;
  if (!ec) {
    auto system_time =
      std::chrono::time_point_cast<std::chrono::system_clock::duration> (
        ftime - fs::file_time_type::clock::now () +
        std::chrono::system_clock::now ());
    auto seconds = std::chrono::duration_cast<std::chrono::seconds> (
      system_time.time_since_epoch ()).count ();
    if (seconds > 0) mtime = (uint64_t) seconds;
  }

  if (rel_s.size () > 100) {
    if (!tar_write_pax_path (writer, rel_s, mtime)) return false;
  }

  if (fs::is_directory (path, ec)) {
    if (!rel_s.empty () && rel_s.back () != '/') rel_s += "/";
    return tar_write_header (writer, rel_s, '5', 0, mtime);
  }
  if (fs::is_symlink (path, ec)) {
    fs::path target = fs::read_symlink (path, ec);
    if (ec) return false;
    return tar_write_header (writer, rel_s, '2', 0, mtime, target.generic_string ());
  }
  if (!fs::is_regular_file (path, ec)) return true;

  uint64_t size = fs::file_size (path, ec);
  if (ec) return false;
  if (!tar_write_header (writer, rel_s, '0', size, mtime)) return false;

  std::ifstream in (path, std::ios::binary);
  if (!in) return false;
  std::vector<char> buf (1 << 20);
  while (in) {
    in.read (buf.data (), (std::streamsize) buf.size ());
    std::streamsize n = in.gcount ();
    if (n > 0 && !writer.write (buf.data (), (size_t) n)) return false;
  }
  return tar_write_padding (writer, size);
}

static bool
create_backup (const fs::path& root, fs::path& archive_path) {
  fs::path backup_dir = root / ".backup" / timestamp_string ();
  std::error_code ec;
  fs::create_directories (backup_dir, ec);
  if (ec) {
    log_error ("failed to create backup directory: " + backup_dir.string () +
               ": " + ec.message ());
    return false;
  }

  archive_path = backup_dir / "vault.tar.zst";
  log_info ("backing up vault to " + archive_path.string ());

  ZstdWriter writer (archive_path);
  if (!writer.ok ()) {
    log_error ("failed to initialize zstd backup writer");
    return false;
  }

  std::vector<fs::path> entries;
  for (fs::recursive_directory_iterator it (
         root, fs::directory_options::skip_permission_denied, ec), end;
       !ec && it != end; it.increment (ec)) {
    fs::path path = it->path ();
    if (it->is_directory (ec) && path.filename () == ".backup") {
      it.disable_recursion_pending ();
      continue;
    }
    if (!is_backup_path (root, path)) entries.push_back (path);
  }
  if (ec) {
    log_error ("backup scan failed: " + ec.message ());
    return false;
  }

  for (size_t i=0; i<entries.size (); i++) {
    print_progress (i + 1, entries.size (), "Backing up",
                    entries[i].filename ().string ());
    if (!tar_write_entry (writer, root, entries[i])) {
      progress_display.finish ();
      log_error ("failed to archive " + entries[i].string ());
      return false;
    }
  }
  progress_display.finish ();

  static const char zeros[1024] = {0};
  if (!writer.write (zeros, sizeof (zeros)) || !writer.finish ()) {
    log_error ("zstd backup failed: " + writer.error ());
    return false;
  }
  return true;
}

static bool
purge_old_backups (const fs::path& root, int max_full_backups, size_t& purged) {
  purged = 0;
  if (max_full_backups == BACKUP_LIMIT_UNLIMITED) {
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
      progress_display.finish ();
      log_error ("failed to purge old backup " + backup_dirs[i].string () +
                 ": " + ec.message ());
      return false;
    }
    purged++;
  }
  progress_display.finish ();
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
  if (retention_seconds == MANUAL_SAVE_RETENTION_UNLIMITED) {
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
      progress_display.finish ();
      log_error ("failed to purge pre-save history " + expired[i].string () +
                 ": " + ec.message ());
      return false;
    }
    purged++;
  }
  progress_display.finish ();
  log_info ("pre-save history retention: purged " + std::to_string (purged) +
            " old history folder(s)");
  return true;
}

static std::vector<fs::path>
scan_noncanonical_images (const fs::path& root) {
  std::vector<fs::path> images;
  std::error_code ec;
  fs::recursive_directory_iterator it (
    root, fs::directory_options::skip_permission_denied, ec);
  fs::recursive_directory_iterator end;

  for (; !ec && it != end; it.increment (ec)) {
    const fs::path path = it->path ();
    if (it->is_directory (ec)) {
      if (path.filename () == ".backup" ||
          is_orphan_collection_path (root, path))
        it.disable_recursion_pending ();
      continue;
    }
    if (!it->is_regular_file (ec)) continue;
    if (is_backup_path (root, path)) continue;
    if (is_orphan_collection_path (root, path)) continue;
    if (!is_image_extension (path)) continue;
    if (!has_canonical_image_name (path)) images.push_back (path);
  }

  if (ec)
    log_info ("scan warning: " + ec.message ());
  std::sort (images.begin (), images.end ());
  return images;
}

static std::vector<fs::path>
scan_documents (const fs::path& root) {
  std::vector<fs::path> docs;
  std::error_code ec;
  fs::recursive_directory_iterator it (
    root, fs::directory_options::skip_permission_denied, ec);
  fs::recursive_directory_iterator end;

  static const std::unordered_set<std::string> document_exts = {
    ".ath", ".tm", ".ts", ".tp", ".stm"
  };

  for (; !ec && it != end; it.increment (ec)) {
    const fs::path path = it->path ();
    if (it->is_directory (ec)) {
      if (path.filename () == ".backup") it.disable_recursion_pending ();
      continue;
    }
    if (!it->is_regular_file (ec)) continue;
    if (is_backup_path (root, path)) continue;
    if (document_exts.find (lower_copy (path.extension ().string ())) !=
        document_exts.end ())
      docs.push_back (path);
  }

  if (ec)
    log_info ("document scan warning: " + ec.message ());
  std::sort (docs.begin (), docs.end ());
  return docs;
}

static std::vector<fs::path>
scan_ath_documents (const fs::path& root) {
  std::vector<fs::path> docs;
  std::error_code ec;
  fs::recursive_directory_iterator it (
    root, fs::directory_options::skip_permission_denied, ec);
  fs::recursive_directory_iterator end;

  for (; !ec && it != end; it.increment (ec)) {
    const fs::path path = it->path ();
    if (it->is_directory (ec)) {
      if (path.filename () == ".backup" ||
          is_orphan_collection_path (root, path))
        it.disable_recursion_pending ();
      continue;
    }
    if (!it->is_regular_file (ec)) continue;
    if (is_backup_path (root, path)) continue;
    if (is_orphan_collection_path (root, path)) continue;
    if (lower_copy (path.extension ().string ()) == ".ath")
      docs.push_back (path);
  }

  if (ec)
    log_info ("ATH document scan warning: " + ec.message ());
  std::sort (docs.begin (), docs.end ());
  return docs;
}

static std::vector<fs::path>
scan_asset_files (const fs::path& root) {
  std::vector<fs::path> assets;
  std::error_code ec;
  fs::recursive_directory_iterator it (
    root, fs::directory_options::skip_permission_denied, ec);
  fs::recursive_directory_iterator end;

  for (; !ec && it != end; it.increment (ec)) {
    const fs::path path = it->path ();
    if (it->is_directory (ec)) {
      if (path.filename () == ".backup" ||
          is_orphan_collection_path (root, path))
        it.disable_recursion_pending ();
      continue;
    }
    if (!it->is_regular_file (ec)) continue;
    if (is_backup_path (root, path)) continue;
    if (is_orphan_collection_path (root, path)) continue;
    if (is_image_extension (path)) assets.push_back (path);
  }

  if (ec)
    log_info ("asset scan warning: " + ec.message ());
  std::sort (assets.begin (), assets.end ());
  return assets;
}

static std::vector<RenamePlan>
build_rename_plan (const std::vector<fs::path>& images) {
  std::vector<RenamePlan> plans;
  std::unordered_set<std::string> reserved;
  plans.reserve (images.size ());

  for (const fs::path& old_path : images) {
    fs::path target;
    do {
      std::string name = "figure-" + generate_uuid_v4 () +
                         canonical_extension (old_path);
      target = old_path.parent_path () / name;
    } while (fs::exists (target) ||
             reserved.find (path_key (target)) != reserved.end ());

    RenamePlan plan;
    plan.old_path = old_path;
    plan.new_path = target;
    plan.old_key = path_key (old_path);
    plan.old_stem = old_path.stem ().string ();
    plan.old_name = old_path.filename ().string ();
    plans.push_back (plan);
    reserved.insert (path_key (target));
  }

  return plans;
}

static bool
rename_images (const std::vector<RenamePlan>& plans) {
  for (size_t i=0; i<plans.size (); i++) {
    const RenamePlan& plan = plans[i];
    print_progress (i + 1, plans.size (), "Renaming", plan.old_name);
    std::error_code ec;
    fs::rename (plan.old_path, plan.new_path, ec);
    if (ec) {
      std::cout << std::endl;
      log_error ("failed to rename " + plan.old_path.string () + " -> " +
                 plan.new_path.string () + ": " + ec.message ());
      return false;
    }
  }
  progress_display.finish ();
  return true;
}

static bool
read_file_bytes (const fs::path& path, std::string& text) {
  std::ifstream in (path, std::ios::binary);
  if (!in) return false;
  std::ostringstream buf;
  buf << in.rdbuf ();
  text = buf.str ();
  return true;
}

static bool
write_file_bytes (const fs::path& path, const std::string& text) {
  std::ofstream out (path, std::ios::binary | std::ios::trunc);
  if (!out) return false;
  out.write (text.data (), (std::streamsize) text.size ());
  return (bool) out;
}

static std::string
tm_unescape_path (const std::string& s) {
  std::string out;
  out.reserve (s.size ());
  for (size_t i=0; i<s.size (); i++) {
    if (s[i] == '\\' && i + 1 < s.size ()) {
      i++;
      out.push_back (s[i]);
    }
    else out.push_back (s[i]);
  }
  return out;
}

static std::string
tm_escape_path (const std::string& s) {
  std::string out;
  out.reserve (s.size ());
  for (char c : s) {
    if (c == '\\' || c == '|' || c == '<' || c == '>') out.push_back ('\\');
    out.push_back (c);
  }
  return out;
}

static bool
parse_image_ref_at (const std::string& text, size_t pos, ImageRef& ref) {
  static const std::string marker = "<image|";
  if (text.compare (pos, marker.size (), marker) != 0) return false;

  size_t begin = pos + marker.size ();
  size_t i = begin;
  while (i < text.size ()) {
    if (text[i] == '\\' && i + 1 < text.size ()) {
      i += 2;
      continue;
    }
    if (text[i] == '|' || text[i] == '>') break;
    i++;
  }
  if (i >= text.size () || text[i] != '|') return false;

  ref.begin = begin;
  ref.end = i;
  ref.raw_path = text.substr (begin, i - begin);
  return true;
}

static bool
is_probably_local_path (const std::string& path) {
  if (path.empty ()) return false;
  if (starts_with (path, "http://") || starts_with (path, "https://") ||
      starts_with (path, "tmfs://") || starts_with (path, "$"))
    return false;
  return true;
}

static fs::path
resolve_reference_path (const fs::path& doc_path, const std::string& ref) {
  if (starts_with (ref, "file://")) {
    std::string local = ref.substr (7);
    return fs::path (local);
  }
  fs::path ref_path (ref);
  if (ref_path.is_absolute ()) return ref_path;
  return doc_path.parent_path () / ref_path;
}

static std::string
reference_for_replacement (const fs::path& doc_path, const fs::path& target,
                           const std::string& old_ref) {
  bool absolute = starts_with (old_ref, "file://") || fs::path (old_ref).is_absolute ();
  if (starts_with (old_ref, "file://"))
    return "file://" + target.generic_string ();
  if (absolute) return target.generic_string ();

  std::error_code ec;
  fs::path rel = fs::relative (target, doc_path.parent_path (), ec);
  if (ec || rel.empty ()) return target.generic_string ();
  return rel.generic_string ();
}

static std::string
stem_from_reference (const std::string& ref) {
  return fs::path (ref).stem ().string ();
}

static bool
collect_used_asset_refs_from_document (const fs::path& doc_path,
                                       std::unordered_set<std::string>& used) {
  std::string text;
  if (!read_file_bytes (doc_path, text)) {
    log_error ("failed to read document " + doc_path.string ());
    return false;
  }

  size_t cursor = 0;
  while (true) {
    size_t pos = text.find ("<image|", cursor);
    if (pos == std::string::npos) break;

    ImageRef ref;
    if (!parse_image_ref_at (text, pos, ref)) {
      cursor = pos + 1;
      continue;
    }

    std::string unescaped = tm_unescape_path (ref.raw_path);
    if (is_probably_local_path (unescaped)) {
      fs::path resolved = resolve_reference_path (doc_path, unescaped);
      if (is_image_extension (resolved)) used.insert (path_key (resolved));
    }
    cursor = ref.end;
  }

  return true;
}

static bool
rewrite_document_image_refs (
  const fs::path& doc_path,
  const std::unordered_map<std::string, std::vector<size_t>>& plans_by_stem,
  const std::unordered_map<std::string, fs::path>& rename_path_by_old_path,
  size_t& replacements) {
  std::string text;
  if (!read_file_bytes (doc_path, text)) {
    log_error ("failed to read document " + doc_path.string ());
    return false;
  }

  std::string out;
  out.reserve (text.size ());
  size_t cursor = 0;
  bool changed = false;
  size_t document_replacements = 0;

  while (true) {
    size_t pos = text.find ("<image|", cursor);
    if (pos == std::string::npos) break;

    ImageRef ref;
    if (!parse_image_ref_at (text, pos, ref)) {
      cursor = pos + 1;
      continue;
    }

    std::string unescaped = tm_unescape_path (ref.raw_path);
    std::string stem = stem_from_reference (unescaped);
    auto hit = plans_by_stem.find (stem);
    if (hit == plans_by_stem.end () || !is_probably_local_path (unescaped)) {
      cursor = ref.end;
      continue;
    }

    std::string ref_key = path_key (resolve_reference_path (doc_path, unescaped));
    auto key_hit = rename_path_by_old_path.find (ref_key);
    if (key_hit == rename_path_by_old_path.end ()) {
      cursor = ref.end;
      continue;
    }

    std::string new_ref =
      reference_for_replacement (doc_path, key_hit->second, unescaped);
    out.append (text, cursor, ref.begin - cursor);
    out += tm_escape_path (new_ref);
    cursor = ref.end;
    changed = true;
    replacements++;
    document_replacements++;
  }

  if (!changed) return true;
  out.append (text, cursor, std::string::npos);
  if (!write_file_bytes (doc_path, out)) {
    log_error ("failed to write document " + doc_path.string ());
    return false;
  }
  log_info ("updated " + std::to_string (document_replacements) +
            " image references in " + doc_path.string ());
  return true;
}

static bool
rewrite_documents (const fs::path& root, const std::vector<RenamePlan>& plans,
                   size_t& replacements) {
  std::unordered_map<std::string, std::vector<size_t>> plans_by_stem;
  std::unordered_map<std::string, fs::path> rename_path_by_old_path;
  for (size_t i=0; i<plans.size (); i++)
    plans_by_stem[plans[i].old_stem].push_back (i);
  for (size_t i=0; i<plans.size (); i++)
    rename_path_by_old_path[plans[i].old_key] = plans[i].new_path;

  std::vector<fs::path> docs = scan_documents (root);
  log_info ("scanning " + std::to_string (docs.size ()) +
            " document files for image references");

  replacements = 0;
  for (size_t i=0; i<docs.size (); i++) {
    print_progress (i + 1, docs.size (), "Updating references",
                    docs[i].filename ().string ());
    if (!rewrite_document_image_refs (docs[i], plans_by_stem,
                                      rename_path_by_old_path,
                                      replacements)) {
      progress_display.finish ();
      return false;
    }
  }
  progress_display.finish ();
  return true;
}

static bool
anchor_enunciations_in_vault (const fs::path& root, MaintenanceSummary& summary) {
  std::vector<fs::path> docs = scan_ath_documents (root);
  summary.anchor_files_scanned = docs.size ();
  log_info ("anchor enunciations: scanning " + std::to_string (docs.size ()) +
            " .ath files");

  for (size_t i=0; i<docs.size (); i++) {
    print_progress (i + 1, docs.size (), "Anchoring enunciations",
                    docs[i].filename ().string ());
    std::string result;
    try {
      result = tm_to_std (as_string (
        call ("vault-anchor-maintenance-file",
              object (url_system (std_to_tm (docs[i].string ()))))));
    }
    catch (...) {
      progress_display.finish ();
      log_error ("anchor enunciations: Scheme failure for " + docs[i].string ());
      summary.anchor_failures++;
      continue;
    }

    std::vector<std::string> parts = split_tabs (result);
    if (parts.size () < 5) {
      log_error ("anchor enunciations: malformed result for " + docs[i].string ());
      summary.anchor_failures++;
      continue;
    }

    size_t wrapped = parse_count (parts[1]);
    size_t removed = parse_count (parts[2]);
    bool changed = parts[3] == "1";
    if (parts[0] == "ok") {
      summary.anchor_enunciations_wrapped += wrapped;
      summary.anchor_dead_pairs_removed += removed;
      if (changed) {
        summary.anchor_files_changed++;
        log_info ("anchor enunciations: updated " + docs[i].string () +
                  " (wrapped " + std::to_string (wrapped) +
                  ", removed " + std::to_string (removed) +
                  " dead pair(s))");
      }
    }
    else {
      summary.anchor_failures++;
      log_error ("anchor enunciations: failed for " + docs[i].string () +
                 (parts[4].empty () ? std::string () : (": " + parts[4])));
    }
  }
  progress_display.finish ();

  log_info ("anchor enunciations: changed " +
            std::to_string (summary.anchor_files_changed) + " file(s), wrapped " +
            std::to_string (summary.anchor_enunciations_wrapped) +
            " enunciation(s), removed " +
            std::to_string (summary.anchor_dead_pairs_removed) +
            " dead anchor pair(s)");
  if (summary.anchor_failures != 0)
    log_info ("anchor enunciations: " +
              std::to_string (summary.anchor_failures) + " file(s) failed");
  return true;
}

static fs::path
next_orphan_directory (const fs::path& root) {
  fs::path candidate = root / "orphan";
  if (!fs::exists (candidate)) return candidate;
  for (int i=1; ; i++) {
    candidate = root / ("orphan (" + std::to_string (i) + ")");
    if (!fs::exists (candidate)) return candidate;
  }
}

static bool
move_or_copy_file (const fs::path& from, const fs::path& to) {
  std::error_code ec;
  fs::rename (from, to, ec);
  if (!ec) return true;

  ec.clear ();
  fs::copy_file (from, to, fs::copy_options::none, ec);
  if (ec) return false;
  fs::remove (from, ec);
  return !ec;
}

static bool
collect_orphan_assets (const fs::path& root, size_t& moved, fs::path& orphan_dir) {
  moved = 0;
  orphan_dir.clear ();

  std::vector<fs::path> docs = scan_ath_documents (root);
  log_info ("orphan assets: scanning " + std::to_string (docs.size ()) +
            " .ath files for asset references");

  std::unordered_set<std::string> used_assets;
  for (size_t i=0; i<docs.size (); i++) {
    print_progress (i + 1, docs.size (), "Scanning asset references",
                    docs[i].filename ().string ());
    if (!collect_used_asset_refs_from_document (docs[i], used_assets)) {
      progress_display.finish ();
      return false;
    }
  }
  progress_display.finish ();

  std::vector<fs::path> assets = scan_asset_files (root);
  std::vector<fs::path> orphans;
  for (const fs::path& asset : assets)
    if (used_assets.find (path_key (asset)) == used_assets.end ())
      orphans.push_back (asset);

  log_info ("orphan assets: found " + std::to_string (orphans.size ()) +
            " orphan asset(s)");
  if (orphans.empty ()) return true;

  orphan_dir = next_orphan_directory (root);
  std::error_code ec;
  fs::create_directories (orphan_dir, ec);
  if (ec) {
    log_error ("failed to create orphan asset directory " +
               orphan_dir.string () + ": " + ec.message ());
    return false;
  }

  std::ofstream manifest (orphan_dir / "orphans.lst",
                          std::ios::binary | std::ios::trunc);
  if (!manifest) {
    log_error ("failed to create orphan manifest " +
               (orphan_dir / "orphans.lst").string ());
    return false;
  }
  manifest << "Renamed orphan\tOriginal full path\n";

  for (size_t i=0; i<orphans.size (); i++) {
    fs::path source = orphans[i];
    std::string name = "orphan-" + std::to_string (i + 1) +
                       canonical_extension (source);
    fs::path target = orphan_dir / name;
    print_progress (i + 1, orphans.size (), "Collecting orphans",
                    source.filename ().string ());
    manifest << name << "\t" << path_key (source) << "\n";
    if (!move_or_copy_file (source, target)) {
      progress_display.finish ();
      log_error ("failed to move orphan asset " + source.string () +
                 " -> " + target.string ());
      return false;
    }
    moved++;
  }
  progress_display.finish ();
  manifest.close ();
  if (!manifest) {
    log_error ("failed to finalize orphan manifest " +
               (orphan_dir / "orphans.lst").string ());
    return false;
  }

  log_info ("orphan assets: collected " + std::to_string (moved) +
            " asset(s) into " + orphan_dir.string ());
  return true;
}

static fs::path
normalize_root (const fs::path& input) {
  std::error_code ec;
  fs::path absolute = fs::absolute (input, ec);
  if (ec) absolute = input;
  fs::path canonical = fs::weakly_canonical (absolute, ec);
  if (ec) return absolute.lexically_normal ();
  return canonical;
}

} // namespace

bool
vault_maintenance_run (string vault_dir) {
  fs::path root = normalize_root (fs::path (tm_to_std (vault_dir)));
  MaintenanceSummary summary;
  log_info ("vault root: " + root.string ());

  if (!fs::exists (root) || !fs::is_directory (root)) {
    log_error ("vault root is not a directory");
    return false;
  }
  if (!fs::exists (root / "Vaultfile")) {
    log_error ("missing Vaultfile in " + root.string ());
    return false;
  }
  if (!load_vault_preferences_if_enabled (root)) return false;

  fs::path archive;
  if (!create_backup (root, archive)) return false;
  summary.backup_archive = archive;
  log_info ("backup complete: " + archive.string ());

  summary.backup_limit = backup_limit_preference ();
  summary.manual_save_retention_seconds = manual_save_retention_preference ();
  summary.orphan_collection_enabled = collect_orphan_assets_preference ();

  std::vector<fs::path> images = scan_noncanonical_images (root);
  log_info ("found " + std::to_string (images.size ()) +
            " non-canonical image files");

  if (images.empty ()) log_info ("no image normalization needed");
  else {
    std::vector<RenamePlan> plans = build_rename_plan (images);
    log_info ("planned " + std::to_string (plans.size ()) + " image renames");

    if (!rename_images (plans)) return false;

    size_t replacements = 0;
    if (!rewrite_documents (root, plans, replacements)) return false;
    summary.image_renames = plans.size ();
    summary.image_reference_updates = replacements;
    log_info ("updated " + std::to_string (replacements) + " image references");
  }

  if (!anchor_enunciations_in_vault (root, summary)) return false;

  if (summary.orphan_collection_enabled) {
    if (!collect_orphan_assets (root, summary.orphan_assets_collected,
                                summary.orphan_dir))
      return false;
  }
  else log_info ("orphan assets: collection disabled");

  if (!purge_old_backups (root, summary.backup_limit, summary.backups_purged))
    return false;
  if (!purge_old_manual_save_histories (
        root, summary.manual_save_retention_seconds,
        summary.manual_save_histories_purged))
    return false;

  log_info ("summary: backup archive " + summary.backup_archive.string ());
  if (summary.backup_limit == BACKUP_LIMIT_UNLIMITED)
    log_info ("summary: full backup retention Unlimited; purged " +
              std::to_string (summary.backups_purged) + " old backup(s)");
  else
    log_info ("summary: full backup retention " +
              std::to_string (summary.backup_limit) + "; purged " +
              std::to_string (summary.backups_purged) + " old backup(s)");
  log_info ("summary: pre-save history retention " +
            manual_save_retention_label (summary.manual_save_retention_seconds) +
            "; purged " +
            std::to_string (summary.manual_save_histories_purged) +
            " old history folder(s)");
  log_info ("summary: renamed " + std::to_string (summary.image_renames) +
            " image file(s), updated " +
            std::to_string (summary.image_reference_updates) +
            " image reference(s)");
  log_info ("summary: anchored " +
            std::to_string (summary.anchor_enunciations_wrapped) +
            " enunciation(s) in " +
            std::to_string (summary.anchor_files_changed) + " of " +
            std::to_string (summary.anchor_files_scanned) +
            " .ath file(s); removed " +
            std::to_string (summary.anchor_dead_pairs_removed) +
            " dead anchor pair(s); failures " +
            std::to_string (summary.anchor_failures));
  if (summary.orphan_collection_enabled) {
    std::string where = summary.orphan_dir.empty ()
                        ? std::string ("")
                        : (" into " + summary.orphan_dir.string ());
    log_info ("summary: collected " +
              std::to_string (summary.orphan_assets_collected) +
              " orphan asset(s)" + where);
  }
  else log_info ("summary: orphan asset collection disabled");
  log_info ("complete");
  return true;
}
