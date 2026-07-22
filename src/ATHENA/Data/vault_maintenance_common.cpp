/******************************************************************************
* MODULE     : vault_maintenance_common.cpp
* DESCRIPTION: Shared helpers for ATHENA vault maintenance passes
* COPYRIGHT  : (C) 2026  Felix
******************************************************************************/

#include "ATHENA/Data/vault_maintenance_internal.hpp"
#include "ATHENA/Data/vaultfile_json.hpp"

#include "tm_ostream.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

namespace fs = std::filesystem;

std::string
tm_to_std (string s) {
  return std::string (as_charp (s));
}

string
std_to_tm (const std::string& s) {
  return string (s.c_str ());
}

std::string
lower_copy (std::string s) {
  std::transform (s.begin (), s.end (), s.begin (),
                  [] (unsigned char c) { return (char) std::tolower (c); });
  return s;
}

bool
starts_with (const std::string& s, const std::string& prefix) {
  return s.size () >= prefix.size () &&
         s.compare (0, prefix.size (), prefix) == 0;
}

bool
ends_with (const std::string& s, const std::string& suffix) {
  return s.size () >= suffix.size () &&
         s.compare (s.size () - suffix.size (), suffix.size (), suffix) == 0;
}

std::string
trim_copy (const std::string& s) {
  size_t begin = 0;
  while (begin < s.size () && std::isspace ((unsigned char) s[begin])) begin++;
  size_t end = s.size ();
  while (end > begin && std::isspace ((unsigned char) s[end - 1])) end--;
  return s.substr (begin, end - begin);
}

bool
is_hex_digit (char c) {
  return (c >= '0' && c <= '9') ||
         (c >= 'a' && c <= 'f') ||
         (c >= 'A' && c <= 'F');
}

bool
is_uuid_like (const std::string& s) {
  if (s.size () != 36) return false;
  for (size_t i=0; i<s.size (); i++) {
    if (i == 8 || i == 13 || i == 18 || i == 23) {
      if (s[i] != '-') return false;
    }
    else if (!is_hex_digit (s[i])) return false;
  }
  return true;
}

bool
is_image_extension (const fs::path& path) {
  std::string ext = lower_copy (path.extension ().string ());
  if (!ext.empty () && ext[0] == '.') ext.erase (ext.begin ());
  static const std::unordered_set<std::string> exts = {
    "png", "jpg", "jpeg", "gif", "webp", "bmp", "tif", "tiff",
    "svg", "pdf", "eps", "ps"
  };
  return exts.find (ext) != exts.end ();
}

bool
has_canonical_asset_name (const fs::path& path) {
  std::string name= path.filename ().string ();
  auto matches= [&] (const std::string& prefix) {
    if (!starts_with (name, prefix) || name.size () < prefix.size () + 36)
      return false;
    if (!is_uuid_like (name.substr (prefix.size (), 36))) return false;
    size_t suffix= prefix.size () + 36;
    return suffix == name.size () || name[suffix] == '.';
  };
  if (matches ("asset-") || matches ("figure-")) return true;
  return false;
}

std::string
path_key (const fs::path& path) {
  std::error_code ec;
  fs::path absolute = fs::absolute (path, ec);
  if (ec) absolute = path;
  return absolute.lexically_normal ().generic_string ();
}

std::string
compact_log_path (const fs::path& path, size_t limit) {
  std::string text = path.generic_string ();
  if (limit <= 3 || text.size () <= limit) return text;
  return "..." + text.substr (text.size () - (limit - 3));
}

bool
is_backup_path (const fs::path& root, const fs::path& path) {
  fs::path rel = path.lexically_relative (root);
  if (rel.empty ()) return false;
  auto it = rel.begin ();
  return it != rel.end () && *it == ".backup";
}

bool
is_digits (const std::string& s) {
  if (s.empty ()) return false;
  for (char c : s)
    if (!std::isdigit ((unsigned char) c)) return false;
  return true;
}

bool
is_orphan_dir_name (const std::string& name) {
  if (name == "orphan") return true;
  if (!starts_with (name, "orphan (") || !ends_with (name, ")")) return false;
  return is_digits (name.substr (8, name.size () - 9));
}

bool
is_orphan_collection_path (const fs::path& root, const fs::path& path) {
  fs::path rel = path.lexically_relative (root);
  if (rel.empty ()) return false;
  auto it = rel.begin ();
  if (it == rel.end ()) return false;
  return is_orphan_dir_name ((*it).string ());
}

bool
collect_vault_infrastructure_paths (const fs::path& root,
                                    std::unordered_set<std::string>& paths,
                                    std::string& error) {
  paths.clear ();
  auto add= [&] (const std::string& value) {
    if (!value.empty ()) paths.insert (path_key (root / value));
  };
  add ("Vaultfile");
  add ("Vaultfile.json");
  add ("map.sqlite");
  add ("maps.sqlite");
  add ("ns.sqlite");
  add ("rag.sqlite");
  add ("artifacts.db");
  add ("enunciations.db");
  add ("bold-text.db");
  add ("websites.json");
  if (!fs::exists (root / "Vaultfile.json")) return true;

  AthenaVaultfileInfo info;
  if (!athena_vaultfile_read (root, info, error)) return false;
  add (info.map_path);
  add (info.preferences_path);
  add (info.namespace_db_path);
  add (info.rag_index_path);
  add (info.websites_path);
  add (info.artifacts_path);
  add (info.enunciations_path);
  add (info.bold_text_path);
  return true;
}


std::string
manual_save_retention_label (long long seconds) {
  if (seconds == VAULT_MANUAL_SAVE_RETENTION_UNLIMITED) return "Unlimited";
  if (seconds == 60LL * 60LL) return "1 hour";
  if (seconds == 6LL * 60LL * 60LL) return "6 hours";
  if (seconds == 24LL * 60LL * 60LL) return "1 day";
  if (seconds == 3LL * 24LL * 60LL * 60LL) return "3 days";
  if (seconds == 7LL * 24LL * 60LL * 60LL) return "1 week";
  if (seconds == 30LL * 24LL * 60LL * 60LL) return "1 month";
  return std::to_string (seconds) + " seconds";
}

std::string
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

void
log_info (const std::string& message) {
  progress_display.log ("vault maintenance: " + message);
}

void
log_error (const std::string& message) {
  progress_display.finish ();
  cerr << "vault maintenance: " << message.c_str () << LF;
}

void
print_progress (size_t current, size_t total, const std::string& phase,
                const std::string& item) {
  progress_display.update (current, total, phase, item);
}

std::string
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

std::string
canonical_extension (const fs::path& path) {
  std::string name= lower_copy (path.filename ().string ());
  static const std::vector<std::string> compound_extensions= {
    ".tar.gz", ".tar.bz2", ".tar.xz", ".tar.zst"};
  for (const std::string& extension: compound_extensions)
    if (ends_with (name, extension)) return extension;
  return lower_copy (path.extension ().string ());
}

void
finish_progress () {
  progress_display.finish ();
}

std::vector<fs::path>
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
      if (path.filename () == ".backup" || path.filename () == ".athena" ||
          path.filename () == ".git" ||
          is_orphan_collection_path (root, path))
        it.disable_recursion_pending ();
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

std::vector<fs::path>
scan_ath_documents (const fs::path& root) {
  std::vector<fs::path> docs;
  std::error_code ec;
  fs::recursive_directory_iterator it (
    root, fs::directory_options::skip_permission_denied, ec);
  fs::recursive_directory_iterator end;

  for (; !ec && it != end; it.increment (ec)) {
    const fs::path path = it->path ();
    if (it->is_directory (ec)) {
      if (path.filename () == ".backup" || path.filename () == ".athena" ||
          path.filename () == ".git" ||
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

std::vector<fs::path>
scan_asset_files (const fs::path& root) {
  std::vector<fs::path> assets;
  std::error_code ec;
  fs::recursive_directory_iterator it (
    root, fs::directory_options::skip_permission_denied, ec);
  fs::recursive_directory_iterator end;

  for (; !ec && it != end; it.increment (ec)) {
    const fs::path path = it->path ();
    if (it->is_directory (ec)) {
      if (path.filename () == ".backup" || path.filename () == ".athena" ||
          path.filename () == ".git" ||
          is_orphan_collection_path (root, path))
        it.disable_recursion_pending ();
      continue;
    }
    if (!it->is_regular_file (ec)) continue;
    if (is_backup_path (root, path)) continue;
    if (is_orphan_collection_path (root, path)) continue;
    if (is_image_extension (path) || has_canonical_asset_name (path))
      assets.push_back (path);
  }

  if (ec)
    log_info ("asset scan warning: " + ec.message ());
  std::sort (assets.begin (), assets.end ());
  return assets;
}

bool
read_file_bytes (const fs::path& path, std::string& text) {
  std::ifstream in (path, std::ios::binary);
  if (!in) return false;
  std::ostringstream buf;
  buf << in.rdbuf ();
  text = buf.str ();
  return true;
}

bool
write_file_bytes (const fs::path& path, const std::string& text) {
  std::ofstream out (path, std::ios::binary | std::ios::trunc);
  if (!out) return false;
  out.write (text.data (), (std::streamsize) text.size ());
  return (bool) out;
}

fs::path
normalize_root (const fs::path& input) {
  std::error_code ec;
  fs::path absolute = fs::absolute (input, ec);
  if (ec) absolute = input;
  fs::path canonical = fs::weakly_canonical (absolute, ec);
  if (ec) return absolute.lexically_normal ();
  return canonical;
}



void
vault_maintenance_log_info (const std::string& message) {
  log_info (message);
}

void
vault_maintenance_log_error (const std::string& message) {
  log_error (message);
}
