/******************************************************************************
* MODULE     : vault_backup.cpp
* DESCRIPTION: Vault backup helpers
* COPYRIGHT  : (C) 2026  Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "ATHENA/Data/vault_backup.hpp"

#include "ATHENA/Data/vault.hpp"
#include "file.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>
#include <zstd.h>

namespace fs = std::filesystem;

namespace {

static std::string
to_std (string s) {
  return std::string (as_charp (s), N(s));
}

static fs::path
canonical_path (const fs::path& p) {
  std::error_code ec;
  fs::path r= fs::weakly_canonical (p, ec);
  if (!ec) return r;
  r= fs::absolute (p, ec);
  return ec ? p : r.lexically_normal ();
}

static fs::path
url_path (url u) {
  return canonical_path (fs::path (to_std (concretize (u))));
}

static bool
path_descends (const fs::path& child, const fs::path& parent) {
  fs::path rel= child.lexically_relative (parent);
  if (rel.empty ()) return child == parent;
  for (const fs::path& part : rel)
    if (part == "..") return false;
  return true;
}

static std::string
timestamp_string () {
  using clock = std::chrono::system_clock;
  std::time_t now= clock::to_time_t (clock::now ());
  std::tm tm;
  localtime_r (&now, &tm);
  std::ostringstream out;
  out << std::put_time (&tm, "%Y%m%dT%H%M%S");
  return out.str ();
}

static bool
compress_file_zstd (const fs::path& source, const fs::path& target) {
  std::ifstream in (source, std::ios::binary);
  if (!in) return false;

  std::error_code ec;
  fs::create_directories (target.parent_path (), ec);
  if (ec) return false;

  std::ofstream out (target, std::ios::binary);
  if (!out) return false;

  ZSTD_CCtx* cctx= ZSTD_createCCtx ();
  if (cctx == nullptr) return false;
  ZSTD_CCtx_setParameter (cctx, ZSTD_c_compressionLevel, 3);

  std::vector<char> input (ZSTD_CStreamInSize ());
  std::vector<char> output (ZSTD_CStreamOutSize ());
  bool ok= true;
  while (ok && in) {
    in.read (input.data (), (std::streamsize) input.size ());
    std::streamsize got= in.gcount ();
    ZSTD_EndDirective mode= in.eof () ? ZSTD_e_end : ZSTD_e_continue;
    ZSTD_inBuffer ib= { input.data (), (size_t) std::max<std::streamsize> (got, 0), 0 };
    bool finished= false;
    while (!finished) {
      ZSTD_outBuffer ob= { output.data (), output.size (), 0 };
      size_t ret= ZSTD_compressStream2 (cctx, &ob, &ib, mode);
      if (ZSTD_isError (ret)) {
        ok= false;
        break;
      }
      out.write (output.data (), (std::streamsize) ob.pos);
      if (!out) {
        ok= false;
        break;
      }
      finished= (mode == ZSTD_e_continue) ? (ib.pos == ib.size) : (ret == 0);
    }
  }

  ZSTD_freeCCtx (cctx);
  return ok && out.good ();
}

static void
backup_child (std::string source, std::string target) {
  bool ok= compress_file_zstd (fs::path (source), fs::path (target));
  if (ok)
    std::cerr << "ATHENA] vault backup: saved pre-save copy to "
              << target << "\n";
  else
    std::cerr << "ATHENA] vault backup: failed to save pre-save copy of "
              << source << "\n";
  _exit (ok ? 0 : 1);
}

} // namespace

bool
vault_backup_pre_save (url document) {
  if (!vault_active () || is_none (document)) return false;
  if (is_rooted_web (document) || is_rooted_tmfs (document)) return false;

  fs::path source= url_path (document);
  fs::path root= url_path (vault_get_root ());
  if (!path_descends (source, root) || !fs::is_regular_file (source))
    return false;

  std::error_code ec;
  fs::path rel= fs::relative (source, root, ec);
  if (ec || rel.empty ()) rel= source.filename ();
  fs::path target= root / ".backup" / "manual-save" /
                   (timestamp_string () + "-" + std::to_string ((long) getpid ())) /
                   rel;
  target += ".zst";

  std::string source_s= source.string ();
  std::string target_s= target.string ();

  pid_t pid= fork ();
  if (pid < 0) {
    std::cerr << "ATHENA] vault backup: fork failed: "
              << std::strerror (errno) << "\n";
    return false;
  }
  if (pid == 0) {
    pid_t grandchild= fork ();
    if (grandchild < 0) _exit (1);
    if (grandchild == 0) backup_child (source_s, target_s);
    _exit (0);
  }

  int status= 0;
  while (waitpid (pid, &status, 0) < 0 && errno == EINTR) {}
  return true;
}
