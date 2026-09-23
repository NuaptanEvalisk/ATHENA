/******************************************************************************
* MODULE     : confined_filesystem.cpp
* DESCRIPTION: Kernel-enforced vault path confinement with pinned descriptors
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#include "confined_filesystem.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <fcntl.h>
#include <stdexcept>
#include <system_error>
#include <utility>
#ifdef __linux__
#include <dirent.h>
#include <linux/openat2.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/random.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace athena::filesystem {
namespace {
[[noreturn]] void fail (const char* operation) {
  throw std::system_error (errno, std::generic_category (), operation);
}
#ifdef __linux__
struct descriptor {
  int fd;
  explicit descriptor (int fd): fd (fd) { if (fd < 0) fail ("Open confined filesystem entry"); }
  ~descriptor () { ::close (fd); }
  descriptor (const descriptor&) = delete;
};
int beneath (int root, const std::filesystem::path& path, int flags) {
  struct open_how how {};
  how.flags = flags | O_CLOEXEC;
  // canonical() resolves user symlinks first; this second, kernel-enforced
  // lookup refuses replacement symlinks and escapes during the actual open.
  how.resolve = RESOLVE_BENEATH | RESOLVE_NO_SYMLINKS | RESOLVE_NO_MAGICLINKS;
  int fd;
  do { fd = static_cast<int> (::syscall (SYS_openat2, root, path.c_str (), &how, sizeof how)); }
  while (fd < 0 && errno == EINTR);
  return fd;
}
metadata information (int fd) {
  struct statx s {};
  if (::statx (fd, "", AT_EMPTY_PATH, STATX_BASIC_STATS | STATX_BTIME, &s) < 0)
    fail ("Inspect confined filesystem entry");
  if (!S_ISREG (s.stx_mode) && !S_ISDIR (s.stx_mode))
    throw std::system_error (ENOTSUP, std::generic_category (), "Only regular files and directories are accessible");
  metadata result {S_ISDIR (s.stx_mode),
    (std::uint64_t (s.stx_dev_major) << 32) | s.stx_dev_minor, s.stx_ino, s.stx_size,
    {s.stx_mtime.tv_sec, s.stx_mtime.tv_nsec}, {s.stx_atime.tv_sec, s.stx_atime.tv_nsec},
    {s.stx_ctime.tv_sec, s.stx_ctime.tv_nsec}, {}};
  if (s.stx_mask & STATX_BTIME) result.created = timestamp {s.stx_btime.tv_sec, s.stx_btime.tv_nsec};
  return result;
}
descriptor readable (int fd, bool directory) {
  // fd is our own pinned O_PATH descriptor, already checked as a regular file
  // or directory. Reopening it never follows an untrusted pathname.
  const auto path = "/proc/self/fd/" + std::to_string (fd);
  return descriptor (::open (path.c_str (), O_RDONLY | O_CLOEXEC | (directory ? O_DIRECTORY : O_NONBLOCK)));
}
void sync_file (int fd) {
  int result;
  do { result= ::fsync (fd); } while (result < 0 && errno == EINTR);
  if (result < 0) fail ("Sync confined document data");
}
std::string temporary_name () {
  std::array<unsigned char, 16> bytes;
  std::size_t offset= 0;
  while (offset < bytes.size ()) {
    const auto n= ::getrandom (bytes.data () + offset, bytes.size () - offset, 0);
    if (n < 0) { if (errno == EINTR) continue; fail ("Name confined temporary document"); }
    if (!n) throw std::runtime_error ("No random bytes for confined temporary document");
    offset+= n;
  }
  std::string name= ".athena-audm-";
  constexpr char hex[]= "0123456789abcdef";
  for (auto c: bytes) { name+= hex[c >> 4]; name+= hex[c & 15]; }
  return name;
}
#endif
}

bool same_revision (const metadata& a, const metadata& b) {
  return a.directory == b.directory && a.device == b.device && a.inode == b.inode &&
    a.size == b.size && a.modified.seconds == b.modified.seconds &&
    a.modified.nanoseconds == b.modified.nanoseconds && a.changed.seconds == b.changed.seconds &&
    a.changed.nanoseconds == b.changed.nanoseconds;
}

struct entry::impl {
#ifdef __linux__
  descriptor descriptor_;
  const std::filesystem::path canonical_path;
  impl (int fd, std::filesystem::path path): descriptor_ (fd), canonical_path (std::move (path)) {}
#endif
};
struct confined_root::impl {
#ifdef __linux__
  const std::filesystem::path canonical_path;
  descriptor descriptor_;
  const metadata identity;
  explicit impl (const std::filesystem::path& path):
    canonical_path (std::filesystem::canonical (path)),
    descriptor_ (::open (canonical_path.c_str (), O_PATH | O_DIRECTORY | O_CLOEXEC)),
    identity (information (descriptor_.fd)) {}
#endif
};

entry::entry (std::shared_ptr<const impl> p): implementation (std::move (p)) {}
confined_root::confined_root (const std::filesystem::path& root) {
#ifdef __linux__
  implementation = std::make_shared<impl> (root);
#else
  throw std::runtime_error ("Confined filesystem access requires Linux openat2");
#endif
}
const std::filesystem::path& confined_root::path () const {
#ifdef __linux__
  return implementation->canonical_path;
#else
  throw std::runtime_error ("Confined filesystem access is unavailable");
#endif
}
const std::filesystem::path& entry::path () const {
#ifdef __linux__
  return implementation->canonical_path;
#else
  throw std::runtime_error ("Confined filesystem access is unavailable");
#endif
}
void confined_root::validate_component (const std::string& name) {
  if (name.empty () || name == "." || name == ".." || name.find ('/') != std::string::npos ||
      name.find ('\0') != std::string::npos)
    throw std::invalid_argument ("A filesystem selector must be one non-traversing path component");
}

entry confined_root::open (const std::filesystem::path& relative) const {
#ifdef __linux__
  if (relative.is_absolute ()) throw std::invalid_argument ("Expected a vault-relative path");
  if (relative != ".") for (const auto& part: relative) validate_component (part.string ());
  const auto& root = *implementation;
  descriptor current (::open (root.canonical_path.c_str (), O_PATH | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
  const auto current_identity = information (current.fd);
  if (current_identity.device != root.identity.device || current_identity.inode != root.identity.inode)
    throw std::system_error (ESTALE, std::generic_category (), "Vault root was replaced");
  const auto target = std::filesystem::canonical (root.canonical_path / relative);
  const auto normalized = target.lexically_relative (root.canonical_path);
  if (normalized.empty () || normalized.is_absolute () || *normalized.begin () == "..")
    throw std::system_error (EACCES, std::generic_category (), "Filesystem target escapes vault root");
  auto p = std::make_shared<entry::impl> (beneath (root.descriptor_.fd, normalized, O_PATH), target);
  information (p->descriptor_.fd);
  return entry (std::move (p));
#else
  throw std::runtime_error ("Confined filesystem access is unavailable");
#endif
}

replacement confined_root::replace (const std::filesystem::path& relative,
    const entry& expected, const metadata& revision, std::string_view bytes) const {
#ifdef __linux__
  auto original= open (relative);
  if (!original.same_object (expected))
    throw std::system_error (ESTALE, std::generic_category (), "Document file was replaced");
  if (revision.directory || original.stat ().directory)
    throw std::invalid_argument ("Cannot replace a directory with document data");
  // Replacing a directory entry must not bypass the original file's write
  // permissions. Open our pinned regular file for writing, without truncation.
  const auto lock_path= "/proc/self/fd/" + std::to_string (original.implementation->descriptor_.fd);
  descriptor locked (::open (lock_path.c_str (), O_RDWR | O_CLOEXEC | O_NONBLOCK));
  int locked_result;
  do { locked_result= ::flock (locked.fd, LOCK_EX | LOCK_NB); } while (locked_result < 0 && errno == EINTR);
  if (locked_result < 0) fail ("Lock confined document for replacement");
  const auto check_revision= [&] {
    auto current= open (relative);
    if (!current.same_object (expected))
      throw std::system_error (ESTALE, std::generic_category (), "Document file was replaced");
    if (!same_revision (current.stat (), revision))
      throw std::system_error (EAGAIN, std::generic_category (), "Document changed before replacement");
  };
  check_revision ();
  const auto destination= original.path ().lexically_relative (path ());
  auto parent_path= destination.parent_path ();
  if (parent_path.empty ()) parent_path= ".";
  descriptor parent (beneath (implementation->descriptor_.fd, parent_path, O_RDONLY | O_DIRECTORY));
  struct stat previous {};
  if (::fstat (locked.fd, &previous) < 0) fail ("Read document permissions");
  // QSaveFile works with pathnames and can follow a changed destination link.
  // O_TMPFILE keeps this write tied to the already-confined parent directory.
  descriptor temporary (::openat (parent.fd, ".", O_TMPFILE | O_RDWR | O_CLOEXEC, 0600));
  std::size_t offset= 0;
  while (offset < bytes.size ()) {
    const auto n= ::write (temporary.fd, bytes.data () + offset,
      std::min<std::size_t> (bytes.size () - offset, 1024 * 1024));
    if (n < 0) { if (errno == EINTR) continue; fail ("Write confined document replacement"); }
    if (!n) throw std::runtime_error ("Short write to confined document replacement");
    offset+= n;
  }
  if (::fchmod (temporary.fd, previous.st_mode & 0777) < 0) fail ("Set replacement document permissions");
  sync_file (temporary.fd);
  std::string name;
  const auto temporary_fd= "/proc/self/fd/" + std::to_string (temporary.fd);
  for (unsigned attempt= 0; attempt != 16; ++attempt) {
    name= temporary_name ();
    if (::linkat (AT_FDCWD, temporary_fd.c_str (), parent.fd, name.c_str (), AT_SYMLINK_FOLLOW) == 0) break;
    if (errno != EEXIST || attempt == 15) fail ("Link confined document replacement");
  }
  struct cleanup {
    int parent;
    const std::string& name;
    bool linked= true;
    ~cleanup () { if (linked) ::unlinkat (parent, name.c_str (), 0); }
  } cleanup_ {parent.fd, name};
  auto new_entry= std::make_shared<entry::impl> (
    beneath (parent.fd, name, O_PATH), original.path ());
  check_revision ();
  descriptor current_parent (beneath (implementation->descriptor_.fd, parent_path, O_PATH | O_DIRECTORY));
  const auto before= information (parent.fd), now= information (current_parent.fd);
  if (before.device != now.device || before.inode != now.inode)
    throw std::system_error (ESTALE, std::generic_category (), "Document parent directory moved");
  // Rename replaces the directory entry itself; a racing final symlink is
  // never followed for writing, even after the optimistic revision check.
  if (::renameat (parent.fd, name.c_str (), parent.fd, destination.filename ().c_str ()) < 0)
    fail ("Commit confined document replacement");
  cleanup_.linked= false;
  int synced;
  do { synced= ::fsync (parent.fd); } while (synced < 0 && errno == EINTR);
  return {entry (std::move (new_entry)), synced == 0};
#else
  throw std::runtime_error ("Confined filesystem access is unavailable");
#endif
}

replacement confined_root::create (const std::filesystem::path& relative,
                                    std::string_view bytes) const {
#ifdef __linux__
  if (relative.empty () || relative.is_absolute ())
    throw std::invalid_argument ("Expected a relative document path");
  for (const auto& part: relative) validate_component (part.string ());
  (void) open (".");
  auto parent_path= relative.parent_path ();
  if (parent_path.empty ()) parent_path= ".";
  descriptor parent (beneath (
    implementation->descriptor_.fd, parent_path, O_RDONLY | O_DIRECTORY));
  descriptor temporary (::openat (
    parent.fd, ".", O_TMPFILE | O_RDWR | O_CLOEXEC, 0600));
  std::size_t offset= 0;
  while (offset < bytes.size ()) {
    const auto n= ::write (temporary.fd, bytes.data () + offset,
      std::min<std::size_t> (bytes.size () - offset, 1024 * 1024));
    if (n < 0) {
      if (errno == EINTR) continue;
      fail ("Write confined document creation");
    }
    if (!n) throw std::runtime_error ("Short write to confined document creation");
    offset+= n;
  }
  if (::fchmod (temporary.fd, 0600) < 0)
    fail ("Set created document permissions");
  sync_file (temporary.fd);
  descriptor current_parent (beneath (
    implementation->descriptor_.fd, parent_path, O_PATH | O_DIRECTORY));
  const auto before= information (parent.fd), now= information (current_parent.fd);
  if (before.device != now.device || before.inode != now.inode)
    throw std::system_error (ESTALE, std::generic_category (),
                             "Document parent directory moved");
  const auto temporary_fd= "/proc/self/fd/" + std::to_string (temporary.fd);
  if (::linkat (AT_FDCWD, temporary_fd.c_str (), parent.fd,
                relative.filename ().c_str (), AT_SYMLINK_FOLLOW) < 0)
    fail ("Commit confined document creation");
  auto created= std::make_shared<entry::impl> (
    beneath (parent.fd, relative.filename (), O_PATH), path () / relative);
  int synced;
  do { synced= ::fsync (parent.fd); } while (synced < 0 && errno == EINTR);
  return {entry (std::move (created)), synced == 0};
#else
  throw std::runtime_error ("Confined filesystem access is unavailable");
#endif
}

entry confined_root::preserve (const std::filesystem::path& relative,
                              std::string_view bytes) const {
#ifdef __linux__
  if (relative.empty () || relative.is_absolute ())
    throw std::invalid_argument ("Expected a relative backup file path");
  for (const auto& part: relative) validate_component (part.string ());
  // Verify the captured root before using its descriptor, including retries.
  (void) open (".");
  std::vector<std::unique_ptr<descriptor>> parents;
  parents.push_back (std::make_unique<descriptor> (
    beneath (implementation->descriptor_.fd, ".", O_RDONLY | O_DIRECTORY)));
  for (const auto& part: relative.parent_path ()) {
    const int parent= parents.back ()->fd;
    if (::mkdirat (parent, part.c_str (), 0700) < 0 && errno != EEXIST)
      fail ("Create backup directory");
    parents.push_back (std::make_unique<descriptor> (
      beneath (parent, part, O_RDONLY | O_DIRECTORY)));
  }
  const int parent= parents.back ()->fd;
  descriptor temporary (::openat (parent, ".", O_TMPFILE | O_RDWR | O_CLOEXEC, 0600));
  std::size_t offset= 0;
  while (offset < bytes.size ()) {
    const auto n= ::write (temporary.fd, bytes.data () + offset,
      std::min<std::size_t> (bytes.size () - offset, 1024 * 1024));
    if (n < 0) { if (errno == EINTR) continue; fail ("Write original document backup"); }
    if (!n) throw std::runtime_error ("Short write to original document backup");
    offset+= n;
  }
  if (::fchmod (temporary.fd, 0400) < 0) fail ("Protect original document backup");
  sync_file (temporary.fd);
  (void) open (".");
  const auto parent_path= relative.has_parent_path () ? relative.parent_path () : std::filesystem::path (".");
  descriptor current_parent (beneath (implementation->descriptor_.fd, parent_path, O_PATH | O_DIRECTORY));
  const auto previous= information (parent), current= information (current_parent.fd);
  if (previous.device != current.device || previous.inode != current.inode)
    throw std::system_error (ESTALE, std::generic_category (), "Backup parent directory moved");
  const auto temporary_fd= "/proc/self/fd/" + std::to_string (temporary.fd);
  if (::linkat (AT_FDCWD, temporary_fd.c_str (), parent, relative.filename ().c_str (), AT_SYMLINK_FOLLOW) < 0 &&
      errno != EEXIST)
    fail ("Commit original document backup");
  entry result (std::make_shared<entry::impl> (
    beneath (implementation->descriptor_.fd, relative, O_PATH), path () / relative));
  if (result.read (bytes.size ()) != bytes)
    throw std::runtime_error ("Existing backup does not match the original document");
  // A previous attempt may have linked the file but failed during fsync.
  auto file= readable (result.implementation->descriptor_.fd, false);
  sync_file (file.fd);
  for (auto i= parents.rbegin (); i != parents.rend (); ++i) sync_file ((*i)->fd);
  return result;
#else
  throw std::runtime_error ("Confined filesystem access is unavailable");
#endif
}
metadata entry::stat () const {
#ifdef __linux__
  return information (implementation->descriptor_.fd);
#else
  throw std::runtime_error ("Confined filesystem access is unavailable");
#endif
}
bool entry::same_object (const entry& other) const {
  const auto a = stat (), b = other.stat ();
  return a.device == b.device && a.inode == b.inode;
}
std::string entry::read (std::size_t limit) const {
#ifdef __linux__
  const auto before = stat ();
  if (before.directory) throw std::invalid_argument ("Cannot read a directory as a document");
  if (before.size > limit) throw std::length_error ("File exceeds the document read limit");
  auto fd = readable (implementation->descriptor_.fd, false);
  std::string result;
  result.reserve (static_cast<std::size_t> (before.size));
  std::array<char, 65536> bytes;
  for (;;) {
    const auto n = ::read (fd.fd, bytes.data (), bytes.size ());
    if (n < 0) { if (errno == EINTR) continue; fail ("Read confined filesystem entry"); }
    if (!n) break;
    if (std::size_t (n) > limit - result.size ()) throw std::length_error ("File exceeds the document read limit");
    result.append (bytes.data (), n);
  }
  const auto after = stat ();
  if (!same_revision (before, after))
    throw std::system_error (EAGAIN, std::generic_category (), "Document changed while being read");
  return result;
#else
  throw std::runtime_error ("Confined filesystem access is unavailable");
#endif
}
std::vector<std::string> entry::names () const {
#ifdef __linux__
  if (!stat ().directory) throw std::invalid_argument ("Only directories have filesystem children");
  auto fd = readable (implementation->descriptor_.fd, true);
  const int duplicate = ::fcntl (fd.fd, F_DUPFD_CLOEXEC, 0);
  if (duplicate < 0) fail ("Duplicate confined directory descriptor");
  DIR* raw = ::fdopendir (duplicate);
  if (!raw) {
    const auto error = errno;
    ::close (duplicate);
    errno = error;
    fail ("List confined directory");
  }
  const auto close = [] (DIR* p) { ::closedir (p); };
  std::unique_ptr<DIR, decltype (close)> dir (raw, close);
  std::vector<std::string> result;
  for (;;) {
    errno = 0;
    auto* item = ::readdir (dir.get ());
    if (!item) { if (errno) fail ("Read confined directory"); break; }
    std::string name (item->d_name);
    if (name != "." && name != "..") result.push_back (std::move (name));
  }
  std::sort (result.begin (), result.end ());
  return result;
#else
  throw std::runtime_error ("Confined filesystem access is unavailable");
#endif
}
} // namespace athena::filesystem
