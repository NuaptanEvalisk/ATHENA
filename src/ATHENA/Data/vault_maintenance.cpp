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
#include <unordered_map>
#include <unordered_set>
#include <vector>

#if defined(__unix__) || defined(__APPLE__)
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace {

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

static std::string
tm_to_std (string s) {
  return std::string (as_charp (s));
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

static void
print_progress (size_t current, size_t total, const std::string& phase,
                const std::string& item) {
  if (total == 0) total = 1;
  int bar_width = 30;
  double progress = std::min (1.0, (double) current / (double) total);
  int pos = (int) (bar_width * progress);

  std::string shown = item;
  if (shown.size () > 44) shown = "..." + shown.substr (shown.size () - 41);

  std::cout << "\r[";
  for (int i=0; i<bar_width; i++) {
    if (i < pos) std::cout << "=";
    else if (i == pos) std::cout << ">";
    else std::cout << " ";
  }
  std::cout << "] " << (int) (progress * 100.0) << "% "
            << "[" << current << "/" << total << "] "
            << phase << ": " << shown << "                              "
            << std::flush;
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

static bool
run_process (const std::vector<std::string>& args) {
  if (args.empty ()) return false;
#if defined(__unix__) || defined(__APPLE__)
  pid_t pid = fork ();
  if (pid < 0) {
    std::cerr << "ATHENA] vault maintenance: fork failed: "
              << std::strerror (errno) << std::endl;
    return false;
  }
  if (pid == 0) {
    std::vector<char*> argv;
    argv.reserve (args.size () + 1);
    for (const std::string& arg : args)
      argv.push_back (const_cast<char*> (arg.c_str ()));
    argv.push_back (nullptr);
    execvp (argv[0], argv.data ());
    _exit (127);
  }

  int status = 0;
  if (waitpid (pid, &status, 0) < 0) {
    std::cerr << "ATHENA] vault maintenance: waitpid failed: "
              << std::strerror (errno) << std::endl;
    return false;
  }
  return WIFEXITED (status) && WEXITSTATUS (status) == 0;
#else
  std::ostringstream cmd;
  for (size_t i=0; i<args.size (); i++) {
    if (i != 0) cmd << ' ';
    cmd << '"' << args[i] << '"';
  }
  return std::system (cmd.str ().c_str ()) == 0;
#endif
}

static bool
create_backup (const fs::path& root, fs::path& archive_path) {
  fs::path backup_dir = root / ".backup" / timestamp_string ();
  std::error_code ec;
  fs::create_directories (backup_dir, ec);
  if (ec) {
    std::cerr << "ATHENA] vault maintenance: failed to create backup directory: "
              << backup_dir << ": " << ec.message () << std::endl;
    return false;
  }

  archive_path = backup_dir / "vault.tar.bz2";
  std::cout << "ATHENA] vault maintenance: backing up vault to "
            << archive_path << std::endl;

  std::vector<std::string> args = {
    "tar", "--exclude=./.backup", "-cjf",
    archive_path.string (), "-C", root.string (), "."
  };
  if (!run_process (args)) {
    std::cerr << "ATHENA] vault maintenance: backup failed; expected tar with "
              << "bzip2 support" << std::endl;
    return false;
  }
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
      if (path.filename () == ".backup") it.disable_recursion_pending ();
      continue;
    }
    if (!it->is_regular_file (ec)) continue;
    if (is_backup_path (root, path)) continue;
    if (!is_image_extension (path)) continue;
    if (!has_canonical_image_name (path)) images.push_back (path);
  }

  if (ec)
    std::cerr << "ATHENA] vault maintenance: scan warning: "
              << ec.message () << std::endl;
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
    std::cerr << "ATHENA] vault maintenance: document scan warning: "
              << ec.message () << std::endl;
  std::sort (docs.begin (), docs.end ());
  return docs;
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
      std::cerr << "ATHENA] vault maintenance: failed to rename "
                << plan.old_path << " -> " << plan.new_path << ": "
                << ec.message () << std::endl;
      return false;
    }
  }
  if (!plans.empty ()) std::cout << std::endl;
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
rewrite_document_image_refs (
  const fs::path& doc_path,
  const std::unordered_map<std::string, std::vector<size_t>>& plans_by_stem,
  const std::unordered_map<std::string, fs::path>& rename_path_by_old_path,
  size_t& replacements) {
  std::string text;
  if (!read_file_bytes (doc_path, text)) {
    std::cerr << "ATHENA] vault maintenance: failed to read document "
              << doc_path << std::endl;
    return false;
  }

  std::string out;
  out.reserve (text.size ());
  size_t cursor = 0;
  bool changed = false;

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
    std::cout << "ATHENA] vault maintenance: " << doc_path
              << ": " << unescaped << " -> " << new_ref << std::endl;
  }

  if (!changed) return true;
  out.append (text, cursor, std::string::npos);
  if (!write_file_bytes (doc_path, out)) {
    std::cerr << "ATHENA] vault maintenance: failed to write document "
              << doc_path << std::endl;
    return false;
  }
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
  std::cout << "ATHENA] vault maintenance: scanning " << docs.size ()
            << " document files for image references" << std::endl;

  replacements = 0;
  for (size_t i=0; i<docs.size (); i++) {
    print_progress (i + 1, docs.size (), "Updating references",
                    docs[i].filename ().string ());
    if (!rewrite_document_image_refs (docs[i], plans_by_stem,
                                      rename_path_by_old_path,
                                      replacements)) {
      std::cout << std::endl;
      return false;
    }
  }
  if (!docs.empty ()) std::cout << std::endl;
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
  std::cout << "ATHENA] vault maintenance: vault root: "
            << root << std::endl;

  if (!fs::exists (root) || !fs::is_directory (root)) {
    std::cerr << "ATHENA] vault maintenance: vault root is not a directory"
              << std::endl;
    return false;
  }
  if (!fs::exists (root / "Vaultfile")) {
    std::cerr << "ATHENA] vault maintenance: missing Vaultfile in "
              << root << std::endl;
    return false;
  }

  fs::path archive;
  if (!create_backup (root, archive)) return false;
  std::cout << "ATHENA] vault maintenance: backup complete: "
            << archive << std::endl;

  std::vector<fs::path> images = scan_noncanonical_images (root);
  std::cout << "ATHENA] vault maintenance: found " << images.size ()
            << " non-canonical image files" << std::endl;
  if (images.empty ()) {
    std::cout << "ATHENA] vault maintenance: no image normalization needed"
              << std::endl;
    return true;
  }

  std::vector<RenamePlan> plans = build_rename_plan (images);
  for (const RenamePlan& plan : plans)
    std::cout << "ATHENA] vault maintenance: plan "
              << plan.old_path << " -> " << plan.new_path << std::endl;

  if (!rename_images (plans)) return false;

  size_t replacements = 0;
  if (!rewrite_documents (root, plans, replacements)) return false;
  std::cout << "ATHENA] vault maintenance: updated " << replacements
            << " image references" << std::endl;
  std::cout << "ATHENA] vault maintenance: complete" << std::endl;
  return true;
}
