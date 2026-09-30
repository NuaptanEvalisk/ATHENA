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
#include "tm_ostream.hpp"
#include <algorithm>
#include <functional>
#include <mutex>
#include <set>

namespace athena::background {
namespace {
struct registry {
  std::mutex lock;
  std::array<progress, 4> statuses;
  std::array<std::set<std::string>, 4> reported;
  std::array<std::string, 4> pending;
};
registry& state () {
  // Other process-lifetime workers may publish while static objects shut down.
  static auto* value= new registry;
  return *value;
}
}
const char* worker_name (worker id) {
  const char* names[]= {"UUID", "NPU RAG", "Maintenance", "Artifacts"};
  return names[std::size_t (id)];
}
bool failed (const progress& value) {
  return value.state != phase::inactive &&
    (value.state == phase::error || value.errors != 0);
}
void publish (worker id, progress value) {
  std::string notification;
  {
    std::lock_guard<std::mutex> lock (state ().lock);
    const auto i= std::size_t (id);
    if (failed (value)) {
      if (value.error_detail.empty ()) {
        if (value.state == phase::error && !value.detail.empty ())
          value.error_detail= value.detail;
        else value.error_detail= state ().statuses[i].error_detail;
        if (value.error_detail.empty ())
          value.error_detail= "Worker reported an error without a diagnostic";
      }
      auto& reported= state ().reported[i];
      if (!reported.count (value.error_detail)) {
        // Bound retained diagnostics even during a very long failed sweep.
        if (reported.size () >= 256) reported.erase (reported.begin ());
        reported.insert (value.error_detail);
        notification= std::string (worker_name (id)) + ": " + value.error_detail;
        state ().pending[i]= notification;
      }
    }
    else {
      value.error_detail.clear ();
      state ().reported[i].clear ();
    }
    state ().statuses[i]= std::move (value);
  }
  if (!notification.empty ())
    athena_spdlog_warning ("background worker: " + notification);
}
std::array<progress, 4> snapshot () {
  std::lock_guard<std::mutex> lock (state ().lock);
  return state ().statuses;
}
std::vector<std::string> take_error_notifications () {
  std::lock_guard<std::mutex> lock (state ().lock);
  std::vector<std::string> result;
  for (auto& message: state ().pending) {
    if (!message.empty ()) result.push_back (std::move (message));
    message.clear ();
  }
  return result;
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
