/******************************************************************************
* MODULE     : background_workers.cpp
* DESCRIPTION: Thread-safe worker status and shared physical vault sweep
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#include "background_workers.hpp"
#include <algorithm>
#include <functional>
#include <mutex>
#include <set>

namespace athena::background {
namespace {
struct registry {
  std::mutex lock;
  std::array<progress, 3> statuses;
};
registry& state () {
  // Other process-lifetime workers may publish while static objects shut down.
  static auto* value= new registry;
  return *value;
}
}
void publish (worker id, progress value) {
  std::lock_guard<std::mutex> lock (state ().lock);
  state ().statuses[std::size_t (id)]= std::move (value);
}
std::array<progress, 3> snapshot () {
  std::lock_guard<std::mutex> lock (state ().lock);
  return state ().statuses;
}
std::vector<disk_file> inventory (const std::filesystem::path& root,
                                const std::atomic<bool>* cancelled) {
  filesystem::confined_root directory (root);
  std::vector<disk_file> files;
  std::set<std::pair<std::uint64_t,std::uint64_t>> directories;
  std::function<void(const std::filesystem::path&,std::size_t)> visit;
  visit= [&] (const std::filesystem::path& relative, std::size_t depth) {
    if (cancelled && cancelled->load (std::memory_order_acquire)) return;
    if (depth > 256) throw std::length_error ("Vault inventory exceeds depth budget");
    const auto entry= directory.open (relative);
    const auto physical= entry.path ().lexically_relative (directory.path ());
    const auto revision= entry.stat ();
    if (revision.directory) {
      if (!directories.emplace (revision.device, revision.inode).second) return;
      auto names= entry.names (); std::sort (names.begin (), names.end ());
      for (const auto& child: names)
        if (child != ".athena" && child != ".backup" && child != ".git")
          visit (relative / child, depth + 1);
    }
    else if (physical.extension () == ".ath")
      files.push_back ({physical.generic_string (), revision});
  };
  visit ({}, 0);
  std::sort (files.begin (), files.end (), [] (const disk_file& a, const disk_file& b) {
    return a.path < b.path;
  });
  return files;
}
} // namespace athena::background
