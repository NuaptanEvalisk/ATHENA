/******************************************************************************
* MODULE     : background_workers.hpp
* DESCRIPTION: Shared background progress and confined incremental vault inventory
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#pragma once
#include "confined_filesystem.hpp"
#include <array>
#include <atomic>
#include <string>
#include <vector>

namespace athena::background {
enum class worker { uuid, rag, maintenance };
enum class phase { inactive, idle, working, error };
struct progress {
  phase state= phase::inactive;
  std::size_t current= 0, total= 0, errors= 0;
  std::string detail;
};
void publish (worker, progress);
std::array<progress, 3> snapshot ();
struct disk_file {
  std::string path;
  filesystem::metadata revision;
};
std::vector<disk_file> inventory (
  const std::filesystem::path&, const std::atomic<bool>* cancelled= nullptr);
} // namespace athena::background
