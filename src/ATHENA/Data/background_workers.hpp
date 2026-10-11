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
#include <memory>
#include <functional>
#include <string>
#include <vector>

namespace athena::background {
enum class worker { uuid, rag, maintenance, artifacts, hodarium, count };
constexpr std::size_t worker_count= std::size_t (worker::count);
enum class phase { inactive, idle, working, paused, error, waiting, disconnected };
struct progress {
  phase state= phase::inactive;
  std::size_t current= 0, total= 0, errors= 0;
  std::string detail;
  // Kept separate from the currently processed file/progress description.
  std::string error_detail;
  std::string caption;
  bool bytes= false;
  bool conflicts= false;
};
const char* worker_name (worker);
bool failed (const progress&);
void publish (worker, progress);
std::array<progress, worker_count> snapshot ();
// Coalesced once per worker, consumed by the GUI rather than each status bar.
std::vector<std::string> take_error_notifications ();
// Full diagnostics for the main-thread standard error console, not coalesced.
std::vector<std::string> take_error_messages ();
struct disk_file {
  std::string path;
  filesystem::metadata revision;
};
// Watches source files/directories, never database sidecars or read accesses.
// Register directories during inventory; compare revision before the next sweep.
// revision() is a change token, never a polling tick. Linux uses inotify; Apple
// uses nonblocking kqueue directory and selected-file vnode watches. Other
// platforms compare selected metadata snapshots; callers choose poll intervals.
class source_watch {
public:
  // Predicate receives an event's basename. Omission preserves the ordinary
  // .ath source watcher. Directory/lifetime/overflow events always invalidate.
  using file_predicate= std::function<bool(const std::filesystem::path&)>;
  explicit source_watch (file_predicate accepts= {});
  ~source_watch ();
  std::uint64_t revision ();
  void directory (const std::filesystem::path&);
private:
  struct impl;
  std::unique_ptr<impl> data;
};
std::vector<disk_file> inventory (
  const std::filesystem::path&, const std::atomic<bool>* cancelled= nullptr,
  source_watch* watch= nullptr);
} // namespace athena::background
