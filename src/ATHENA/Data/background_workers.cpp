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
#include "boot.hpp"
#include <algorithm>
#include <functional>
#include <mutex>
#include <optional>
#include <set>
#include <system_error>
#include <map>
#ifdef __linux__
#include <sys/inotify.h>
#include <unistd.h>
#endif

namespace athena::background {
struct source_watch::impl {
  std::mutex lock;
  std::uint64_t revision= 1;
#ifdef __linux__
  int fd= -1;
  std::map<int, std::filesystem::path> directories;
#endif
};
source_watch::source_watch (): data (std::make_unique<impl> ()) {
#ifdef __linux__
  data->fd= inotify_init1 (IN_CLOEXEC | IN_NONBLOCK);
  if (data->fd < 0)
    throw std::system_error (errno, std::generic_category (), "Watch vault sources");
#endif
}
source_watch::~source_watch () {
#ifdef __linux__
  close (data->fd);
#endif
}
void source_watch::directory (const std::filesystem::path& path) {
#ifdef __linux__
  std::lock_guard<std::mutex> guard (data->lock);
  const int wd= inotify_add_watch (data->fd, path.c_str (),
    IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO | IN_CLOSE_WRITE |
    IN_MODIFY | IN_ATTRIB | IN_DELETE_SELF | IN_MOVE_SELF | IN_ONLYDIR);
  if (wd < 0)
    throw std::system_error (errno, std::generic_category (), "Watch " + path.string ());
  data->directories[wd]= path;
#endif
}
std::uint64_t source_watch::revision () {
  std::lock_guard<std::mutex> guard (data->lock);
#ifdef __linux__
  alignas(inotify_event) char buffer[65536];
  for (;;) {
    const auto count= read (data->fd, buffer, sizeof (buffer));
    if (count < 0) {
      if (errno == EINTR) continue;
      if (errno == EAGAIN) break;
      throw std::system_error (errno, std::generic_category (), "Read vault source events");
    }
    if (count == 0) break;
    for (std::size_t offset= 0; offset < (std::size_t) count; ) {
      const auto& event= *reinterpret_cast<const inotify_event*> (buffer + offset);
      offset+= sizeof (event) + event.len;
      const std::string name= event.len ? event.name : "";
      if (name == ".athena" || name == ".backup" || name == ".git") continue;
      if (event.mask & IN_IGNORED) data->directories.erase (event.wd);
      if ((event.mask & (IN_Q_OVERFLOW | IN_DELETE_SELF | IN_MOVE_SELF | IN_IGNORED)) ||
          (event.mask & IN_ISDIR) || std::filesystem::path (name).extension () == ".ath")
        ++data->revision;
      // Renamed directories will be registered at their new paths in inventory.
      if (event.mask & (IN_MOVED_FROM | IN_DELETE)) {
        auto parent= data->directories.find (event.wd);
        if ((event.mask & IN_ISDIR) && parent != data->directories.end ()) {
          const auto removed= parent->second / name;
          for (auto it= data->directories.begin (); it != data->directories.end (); ) {
            auto relative= it->second.lexically_relative (removed);
            if (!relative.empty () && *relative.begin () != "..") {
              inotify_rm_watch (data->fd, it->first);
              it= data->directories.erase (it);
            }
            else ++it;
          }
        }
      }
    }
  }
#else
  // Other platforms retain the periodic inventory until a native watcher exists.
  ++data->revision;
#endif
  return data->revision;
}
namespace {
struct registry {
  std::mutex lock;
  std::array<progress, 4> statuses;
  std::array<std::set<std::string>, 4> reported;
  std::array<std::string, 4> pending;
  std::vector<std::string> console_messages;
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
        if (!headless_mode) state ().console_messages.push_back (notification);
      }
    }
    else {
      value.error_detail.clear ();
      state ().reported[i].clear ();
    }
    state ().statuses[i]= std::move (value);
  }
  if (headless_mode && !notification.empty ())
    athena_spdlog_error ("background worker: " + notification);
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
std::vector<std::string> take_error_messages () {
  std::lock_guard<std::mutex> lock (state ().lock);
  std::vector<std::string> result;
  result.swap (state ().console_messages);
  return result;
}
std::vector<disk_file> inventory (const std::filesystem::path& root,
                                const std::atomic<bool>* cancelled,
                                source_watch* watch) {
  filesystem::confined_root directory (root);
  std::vector<disk_file> files;
  std::set<std::pair<std::uint64_t,std::uint64_t>> directories;
  std::function<void(const std::filesystem::path&,std::size_t)> visit;
  visit= [&] (const std::filesystem::path& relative, std::size_t depth) {
    if (cancelled && cancelled->load (std::memory_order_acquire)) return;
    if (depth > 256) throw std::length_error ("Vault inventory exceeds depth budget");
    std::optional<filesystem::entry> opened;
    try { opened.emplace (directory.open (relative)); }
    catch (const std::system_error& error) {
      // Directory listings are not snapshots. A child may disappear (SQLite
      // journals, atomic saves) or an ancestor may cease to be a directory.
      // Do not hide a lost/replaced vault root or other filesystem failures.
      if (depth == 0 ||
          (error.code () != std::errc::no_such_file_or_directory &&
           error.code () != std::errc::not_a_directory)) throw;
      (void) directory.open (".");
      return;
    }
    const auto& entry= *opened;
    const auto physical= entry.path ().lexically_relative (directory.path ());
    const auto revision= entry.stat ();
    if (revision.directory) {
      if (!directories.emplace (revision.device, revision.inode).second) return;
      if (watch) watch->directory (entry.path ());
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
