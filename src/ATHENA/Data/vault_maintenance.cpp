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
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <zstd.h>

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
    std::cout << message << std::endl;
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
  std::cerr << "ATHENA] vault maintenance: " << message << std::endl;
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
  log_info ("vault root: " + root.string ());

  if (!fs::exists (root) || !fs::is_directory (root)) {
    log_error ("vault root is not a directory");
    return false;
  }
  if (!fs::exists (root / "Vaultfile")) {
    log_error ("missing Vaultfile in " + root.string ());
    return false;
  }

  fs::path archive;
  if (!create_backup (root, archive)) return false;
  log_info ("backup complete: " + archive.string ());

  std::vector<fs::path> images = scan_noncanonical_images (root);
  log_info ("found " + std::to_string (images.size ()) +
            " non-canonical image files");
  if (images.empty ()) {
    log_info ("no image normalization needed");
    return true;
  }

  std::vector<RenamePlan> plans = build_rename_plan (images);
  log_info ("planned " + std::to_string (plans.size ()) + " image renames");

  if (!rename_images (plans)) return false;

  size_t replacements = 0;
  if (!rewrite_documents (root, plans, replacements)) return false;
  log_info ("updated " + std::to_string (replacements) + " image references");
  log_info ("complete");
  return true;
}
