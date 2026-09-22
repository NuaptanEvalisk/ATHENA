/******************************************************************************
* MODULE     : confined_filesystem_test.cpp
* DESCRIPTION: Temporary-vault tests for symlink confinement and descriptor lifetime
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#include "../../src/System/Files/confined_filesystem.hpp"
#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <future>
#include <fcntl.h>
#include <iostream>
#include <stdexcept>
#include <sys/stat.h>
#include <sys/file.h>
#include <unistd.h>
#include <thread>

namespace fs = std::filesystem;
using athena::filesystem::confined_root;
void require (bool ok) { if (!ok) throw std::runtime_error ("Filesystem test assertion failed"); }
template<class F> void rejects (F f) {
  bool failed = false;
  try { f (); } catch (const std::exception&) { failed = true; }
  require (failed);
}
int main () {
  char pattern[] = "/tmp/athena-confined-filesystem-XXXXXX";
  const char* created = ::mkdtemp (pattern);
  if (!created) return 1;
  const fs::path directory (created);
  struct cleanup { fs::path p; ~cleanup () { std::error_code ec; fs::remove_all (p, ec); } } cleanup_ {directory};
  try {
    const auto root = directory / "vault";
    fs::create_directories (root / "sub");
    { std::ofstream f (root / "inside.ath"); f << "inside"; }
    { std::ofstream f (directory / "outside.ath"); f << "outside"; }
    confined_root filesystem (root);
    require (filesystem.open (".").stat ().directory);
    const auto file = filesystem.open ("inside.ath");
    require (!file.stat ().directory && file.stat ().size == 6);
    require (file.read (6) == "inside");
    rejects ([&] { file.read (5); });
    fs::create_symlink ("../inside.ath", root / "sub" / "relative");
    fs::create_symlink (root / "inside.ath", root / "absolute");
    require (filesystem.open ("sub/relative").same_object (file));
    require (filesystem.open ("absolute").read (6) == "inside");
    fs::create_symlink (directory / "outside.ath", root / "external");
    fs::create_symlink ("/proc/self/fd/0", root / "magic");
    fs::create_symlink ("missing", root / "broken");
    fs::create_symlink ("cycle", root / "cycle");
    require (::mkfifo ((root / "fifo").c_str (), 0600) == 0);
    for (const auto* p: {"external", "magic", "broken", "cycle", "fifo", "..", "sub/../../outside.ath", "/etc/passwd"})
      rejects ([&] { filesystem.open (p); });
    rejects ([&] { filesystem.open (fs::path (std::string ("inside.ath\0suffix", 17))); });
    require (filesystem.open (".").names () == filesystem.open (".").names ());
    std::vector<std::future<std::vector<std::string>>> listings;
    for (int i = 0; i != 8; ++i)
      listings.push_back (std::async (std::launch::async, [&] { return filesystem.open (".").names (); }));
    const auto expected = filesystem.open (".").names ();
    for (auto& listing: listings) require (listing.get () == expected);

    { std::ofstream f (root / "write.ath"); f << "old"; }
    require (::chmod ((root / "write.ath").c_str (), 0640) == 0);
    auto writable= filesystem.open ("write.ath");
    const auto revision= writable.stat ();
    const auto replacement= filesystem.replace ("write.ath", writable, revision, "new content");
    require (replacement.directory_synced);
    require (replacement.file.read (100) == "new content");
    require (filesystem.open ("write.ath").same_object (replacement.file));
    require (writable.read (100) == "old");
    struct stat permissions {};
    require (::stat ((root / "write.ath").c_str (), &permissions) == 0);
    require ((permissions.st_mode & 0777) == 0640);
    rejects ([&] { filesystem.replace ("write.ath", writable, revision, "stale"); });
    const auto changed_revision= replacement.file.stat ();
    { std::ofstream f (root / "write.ath"); f << "external edit"; }
    rejects ([&] { filesystem.replace ("write.ath", replacement.file, changed_revision, "lost update"); });
    require (filesystem.open ("write.ath").read (100) == "external edit");
    auto external= filesystem.open ("write.ath");
    fs::create_symlink ("write.ath", root / "write-alias");
    const auto via_alias= filesystem.replace ("write-alias", external, external.stat (), "through alias");
    require (fs::is_symlink (root / "write-alias"));
    require (filesystem.open ("write.ath").same_object (via_alias.file));
    require (via_alias.file.read (100) == "through alias");
    rejects ([&] { filesystem.replace ("external", via_alias.file, via_alias.file.stat (), "escape"); });
    rejects ([&] { filesystem.replace ("../outside.ath", via_alias.file, via_alias.file.stat (), "escape"); });
    require (std::ifstream (directory / "outside.ath").peek () == 'o');
    const int lock= ::open ((root / "write.ath").c_str (), O_RDONLY | O_CLOEXEC);
    require (lock >= 0 && ::flock (lock, LOCK_EX | LOCK_NB) == 0);
    rejects ([&] { filesystem.replace ("write.ath", via_alias.file, via_alias.file.stat (), "locked"); });
    ::close (lock);
    std::promise<void> start;
    const auto ready= start.get_future ().share ();
    const auto concurrent_revision= via_alias.file.stat ();
    const auto writer= [&] (const char* text) {
      ready.wait ();
      try {
        filesystem.replace ("write.ath", via_alias.file, concurrent_revision, text);
        return true;
      }
      catch (const std::system_error& error) {
        if (error.code ().value () != ESTALE && error.code ().value () != EAGAIN) throw;
        return false;
      }
    };
    auto first_writer= std::async (std::launch::async, writer, "first writer");
    auto second_writer= std::async (std::launch::async, writer, "second writer");
    start.set_value ();
    const bool first_won= first_writer.get (), second_won= second_writer.get ();
    require (first_won != second_won);
    require (filesystem.open ("write.ath").read (100) == (first_won ? "first writer" : "second writer"));
    for (const auto& name: filesystem.open (".").names ())
      require (name.rfind (".athena-audm-", 0) != 0);

    const std::string original ("old\0\xff", 5);
    auto backup= filesystem.preserve ("backups/format/original", original);
    require (backup.read (5) == original);
    require (filesystem.preserve ("backups/format/original", original).same_object (backup));
    require (::stat ((root / "backups/format/original").c_str (), &permissions) == 0);
    require ((permissions.st_mode & 0777) == 0400);
    rejects ([&] { filesystem.preserve ("backups/format/original", "different"); });
    require (backup.read (5) == original);
    fs::create_directory_symlink (directory, root / "backup-escape");
    rejects ([&] { filesystem.preserve ("backup-escape/new", "escape"); });
    require (!fs::exists (directory / "new"));
    fs::create_symlink ("original", root / "backups/format/alias");
    rejects ([&] { filesystem.preserve ("backups/format/alias", original); });
    rejects ([&] { filesystem.preserve ("../outside-backup", original); });
    rejects ([&] { filesystem.preserve ("backups/format", original); });
    auto backup_a= std::async (std::launch::async, [&] { return filesystem.preserve ("backups/concurrent", original); });
    auto backup_b= std::async (std::launch::async, [&] { return filesystem.preserve ("backups/concurrent", original); });
    require (backup_a.get ().same_object (backup_b.get ()));

    fs::rename (root / "inside.ath", root / "old.ath");
    { std::ofstream f (root / "inside.ath"); f << "replacement"; }
    require (file.read (6) == "inside");
    require (!filesystem.open ("inside.ath").same_object (file));

    std::atomic<bool> stop {false};
    std::thread changer ([&] {
      std::error_code ec;
      while (!stop.load ()) {
        for (const auto& target: {root / "inside.ath", directory / "outside.ath"}) {
          fs::remove (root / "next", ec);
          fs::create_symlink (target, root / "next", ec);
          fs::rename (root / "next", root / "racing", ec);
        }
      }
    });
    bool leaked = false;
    for (int i = 0; i != 300; ++i) {
      try { if (filesystem.open ("racing").read (100) != "replacement") leaked = true; }
      catch (const std::system_error&) {}
    }
    stop.store (true);
    changer.join ();
    require (!leaked);
    fs::rename (root, directory / "previous-vault");
    fs::create_directories (root);
    rejects ([&] { filesystem.open ("."); });
    std::cout << "Confined filesystem tests passed\n";
  }
  catch (const std::exception& e) { std::cerr << e.what () << '\n'; return 1; }
}
