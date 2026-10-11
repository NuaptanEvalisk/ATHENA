/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#include "device_key_file.hpp"
#include "device_identity.hpp"
#include "device_key_internal.hpp"
#include <sodium.h>
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <linux/fs.h>
#include <mutex>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace athena::hodarium {
namespace {
struct descriptor {
  int value;
  explicit descriptor (int fd= -1): value (fd) {}
  ~descriptor () { if (value >= 0) ::close (value); }
  descriptor (const descriptor&)= delete;
  descriptor& operator= (const descriptor&)= delete;
};
std::mutex operations;
descriptor directory;

[[noreturn]] void unavailable () {
  throw key_store_error (key_store_failure::unavailable,
    "Cannot access the configured private Hodarium key directory");
}
[[noreturn]] void corrupt () {
  throw key_store_error (key_store_failure::corrupt,
    "Invalid private Hodarium secret file");
}
void initialize () {
  static const int result= sodium_init ();
  if (result < 0) unavailable ();
}
struct stat metadata (int fd) {
  struct stat st;
  if (::fstat (fd, &st) != 0) unavailable ();
  return st;
}
void private_directory (int fd) {
  const auto st= metadata (fd);
  if (!S_ISDIR (st.st_mode) || st.st_uid != ::geteuid () ||
      (st.st_mode & 07777) != 0700 || st.st_nlink == 0) unavailable ();
}
int key_directory () {
  if (directory.value < 0) unavailable ();
  private_directory (directory.value);
  return directory.value;
}
void sync (int fd) {
  int result;
  do { result= ::fsync (fd); } while (result < 0 && errno == EINTR);
  if (result != 0) unavailable ();
}
std::string filename (const std::string& handle, detail::secret_kind kind) {
  if (handle.size () != 43 || !std::all_of (handle.begin (), handle.end (),
      [] (unsigned char c) { return (c >= 'A' && c <= 'Z') ||
        (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_'; }))
    throw std::invalid_argument ("Invalid Hodarium key handle");
  switch (kind) {
    case detail::secret_kind::device: return "device-" + handle;
    case detail::secret_kind::vault: return "vault-" + handle;
    case detail::secret_kind::relay: return "relay-" + handle;
  }
  throw std::invalid_argument ("Unknown protected secret kind");
}
void private_file (int fd, bool require_seed) {
  const auto st= metadata (fd);
  if (!S_ISREG (st.st_mode) || st.st_uid != ::geteuid () ||
      (st.st_mode & 07777) != 0600 || st.st_nlink != 1 ||
      (require_seed && st.st_size != 32)) corrupt ();
}
int open_secret (int dir, const std::string& name) {
  const int fd= ::openat (dir, name.c_str (),
    O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
  if (fd < 0 && errno != ENOENT) {
    if (errno == ELOOP) corrupt ();
    unavailable ();
  }
  return fd;
}
struct secret_buffer {
  unsigned char bytes[32];
  ~secret_buffer () { sodium_memzero (bytes, sizeof bytes); }
};
struct temporary_file {
  int dir;
  std::string name;
  descriptor fd;
  explicit temporary_file (int parent): dir (parent) {
    unsigned char random[16];
    char hex[33];
    for (int attempt= 0; attempt < 16; ++attempt) {
      randombytes_buf (random, sizeof random);
      sodium_bin2hex (hex, sizeof hex, random, sizeof random);
      name= std::string (".pending-") + hex;
      fd.value= ::openat (dir, name.c_str (),
        O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
      if (fd.value >= 0) return;
      if (errno != EEXIST) unavailable ();
    }
    unavailable ();
  }
  ~temporary_file () {
    if (!name.empty ()) {
      ::unlinkat (dir, name.c_str (), 0);
      // Best-effort cleanup on failure; a crash may leave a private pending
      // file. Pending files are never considered identities or auto-recovered.
      ::fsync (dir);
    }
  }
};
} // namespace

void configure_private_key_directory (const std::string& path) {
  if (path.size () < 2 || path.front () != '/' || path.back () == '/' ||
      path.find ('\0') != std::string::npos)
    throw std::invalid_argument ("Private key directory requires an absolute path");
  std::lock_guard lock (operations);
  initialize ();
  descriptor current (::open ("/", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
  if (current.value < 0) unavailable ();
  std::size_t begin= 1;
  while (begin < path.size ()) {
    auto end= path.find ('/', begin);
    if (end == std::string::npos) end= path.size ();
    const auto component= path.substr (begin, end - begin);
    if (component.empty () || component == "." || component == "..")
      throw std::invalid_argument ("Invalid private key directory component");
    const auto parent= metadata (current.value);
    // Permit root-owned sticky temporary directories for isolated tests, but
    // reject ancestors that another uid can freely modify.
    if ((parent.st_uid != 0 && parent.st_uid != ::geteuid ()) ||
        ((parent.st_mode & 0022) && !(parent.st_uid == 0 && (parent.st_mode & S_ISVTX))))
      unavailable ();
    descriptor next (::openat (current.value, component.c_str (),
      O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
    if (next.value < 0) unavailable ();
    std::swap (current.value, next.value);
    begin= end + 1;
  }
  private_directory (current.value);
  // Fail configuration on filesystems that cannot provide directory durability.
  sync (current.value);
  if (directory.value >= 0) {
    private_directory (directory.value);
    const auto old= metadata (directory.value), next= metadata (current.value);
    if (old.st_dev != next.st_dev || old.st_ino != next.st_ino)
      throw key_store_error (key_store_failure::unavailable,
        "Private Hodarium key directory is already configured");
  }
  else std::swap (directory.value, current.value);
}

namespace detail {
void store_protected_seed (const std::string& handle, const unsigned char* seed, secret_kind kind) {
  const auto name= filename (handle, kind);
  if (!seed) throw std::invalid_argument ("Null Hodarium seed");
  std::lock_guard lock (operations);
  const int dir= key_directory ();
  temporary_file pending (dir);
  if (::fchmod (pending.fd.value, 0600) != 0) unavailable ();
  private_file (pending.fd.value, false);
  std::size_t offset= 0;
  while (offset < 32) {
    const auto count= ::write (pending.fd.value, seed + offset, 32 - offset);
    if (count < 0 && errno == EINTR) continue;
    if (count <= 0) unavailable ();
    offset+= static_cast<std::size_t> (count);
  }
  sync (pending.fd.value);
  // Linux's atomic no-replace rename is the interprocess arbitration point.
  // Never fall back to an existence check followed by an overwriting rename.
  if (::syscall (SYS_renameat2, dir, pending.name.c_str (), dir, name.c_str (),
                 RENAME_NOREPLACE) != 0) {
    if (errno == EEXIST) throw key_store_error (key_store_failure::corrupt,
      "Private Hodarium secret already exists; refusing replacement");
    unavailable ();
  }
  pending.name.clear ();
  // A failure here has an indeterminate durable outcome: do not remove the
  // published identity or retry by replacing it.
  sync (dir);
}

void load_protected_seed (const std::string& handle, unsigned char* seed, secret_kind kind) {
  const auto name= filename (handle, kind);
  if (!seed) throw std::invalid_argument ("Null Hodarium seed");
  std::lock_guard lock (operations);
  descriptor fd (open_secret (key_directory (), name));
  if (fd.value < 0) throw key_store_error (key_store_failure::missing,
    "Protected Hodarium secret is missing; explicit recovery is required");
  private_file (fd.value, true);
  secret_buffer buffer;
  std::size_t offset= 0;
  while (offset < sizeof buffer.bytes) {
    const auto count= ::read (fd.value, buffer.bytes + offset, sizeof buffer.bytes - offset);
    if (count < 0 && errno == EINTR) continue;
    if (count < 0) unavailable ();
    if (count == 0) corrupt ();
    offset+= static_cast<std::size_t> (count);
  }
  private_file (fd.value, true);
  std::memcpy (seed, buffer.bytes, sizeof buffer.bytes);
}

void delete_protected_seed (const std::string& handle, secret_kind kind) {
  const auto name= filename (handle, kind);
  std::lock_guard lock (operations);
  const int dir= key_directory ();
  descriptor fd (open_secret (dir, name));
  if (fd.value >= 0) {
    private_file (fd.value, false);
    if (::unlinkat (dir, name.c_str (), 0) != 0 && errno != ENOENT) unavailable ();
  }
  sync (dir);
}
} // namespace detail

void cancel_device_key_operations () {
  // Local filesystem calls have no prompt/cancellable session. As with the
  // Keychain backend, the owner must discard stale lifecycle completions.
}
} // namespace athena::hodarium
