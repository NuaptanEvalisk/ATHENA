/******************************************************************************
* MODULE     : vault_maintenance_pass_backup.cpp
* DESCRIPTION: Vault maintenance full backup pass
* COPYRIGHT  : (C) 2026  Felix
******************************************************************************/

#include "ATHENA/Data/vault_maintenance_internal.hpp"

#include <array>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include <zstd.h>

namespace fs = std::filesystem;

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
      finish_progress ();
      log_error ("failed to archive " + entries[i].string ());
      return false;
    }
  }
  finish_progress ();

  static const char zeros[1024] = {0};
  if (!writer.write (zeros, sizeof (zeros)) || !writer.finish ()) {
    log_error ("zstd backup failed: " + writer.error ());
    return false;
  }
  return true;
}


VaultMaintenancePassResult
vault_maintenance_pass_create_backup (VaultMaintenanceContext& ctx) {
  fs::path archive;
  if (!create_backup (ctx.root, archive))
    return VaultMaintenancePassResult::failure ("full backup creation failed");
  ctx.summary.backup_archive = archive;
  log_info ("backup complete: " + archive.string ());
  return VaultMaintenancePassResult::success ();
}
