/******************************************************************************
* MODULE     : vault_directory_lease.cpp
* DESCRIPTION: Nonblocking filesystem leases excluding offline vault replacement
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#include "vault_directory_lease.hpp"
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#include <system_error>

namespace athena::filesystem {
vault_directory_lease::vault_directory_lease (
  const std::filesystem::path& path, bool exclusive) {
  fd_= ::open (path.c_str (), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
  if (fd_ < 0) throw std::system_error (errno, std::generic_category (), "Open vault " + path.string ());
  int result;
  do { result= ::flock (fd_, (exclusive ? LOCK_EX : LOCK_SH) | LOCK_NB); }
  while (result < 0 && errno == EINTR);
  const int error= errno;
  struct stat held {}, current {};
  bool same= result == 0 && ::fstat (fd_, &held) == 0 &&
    ::lstat (path.c_str (), &current) == 0 && held.st_dev == current.st_dev &&
    held.st_ino == current.st_ino;
  if (!same) {
    ::close (fd_); fd_= -1;
    throw std::system_error (result < 0 ? error : ESTALE, std::generic_category (),
      "Vault busy or replaced; close other ATHENA instances before upgrading: " + path.string ());
  }
}
vault_directory_lease::~vault_directory_lease () { if (fd_ >= 0) ::close (fd_); }
}
