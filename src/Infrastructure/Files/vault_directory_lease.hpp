/******************************************************************************
* MODULE     : vault_directory_lease.hpp
* DESCRIPTION: Shared vault lifetime and exclusive offline-upgrade directory lease
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#pragma once
#include <filesystem>

namespace athena::filesystem {
class vault_directory_lease {
  int fd_= -1;
public:
  explicit vault_directory_lease (const std::filesystem::path&, bool exclusive= false);
  ~vault_directory_lease ();
  vault_directory_lease (const vault_directory_lease&)= delete;
  vault_directory_lease& operator= (const vault_directory_lease&)= delete;
  int descriptor () const { return fd_; }
};
}
