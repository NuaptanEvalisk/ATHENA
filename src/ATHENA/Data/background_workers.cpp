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
#include <tuple>
#include <cerrno>
#include <cstdint>
#include <stdexcept>
#ifdef __linux__
#include <sys/inotify.h>
#include <unistd.h>
#elif defined(__APPLE__)
#include <sys/types.h>
#include <sys/event.h>
#include <sys/time.h>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace athena::background {
struct source_watch::impl {
  std::mutex lock;
  std::uint64_t revision= 1;
  file_predicate accepts;
#ifdef __linux__
  int fd= -1;
  std::map<int, std::filesystem::path> directories;
#elif defined(__APPLE__)
  int fd= -1;
  std::uintptr_t ticket= 0;
  struct vnode {
    std::filesystem::path path;
    std::uint64_t device, inode;
    std::uintptr_t ticket;
    std::unique_ptr<filesystem::confined_root> directory;
    std::map<std::string, std::pair<std::uint64_t,std::uint64_t>> children;
  };
  std::map<int, vnode> nodes;
  std::map<std::filesystem::path,int> paths;
  void remove (int descriptor) {
    auto it= nodes.find (descriptor);
    if (it == nodes.end ()) return;
    paths.erase (it->second.path);
    // Closing unregisters EVFILT_VNODE. Ticket matching prevents already
    // dequeued events for this fd being mistaken for a later fd reuse.
    ::close (descriptor); nodes.erase (it);
  }
  int watch (const filesystem::entry& file,
             std::unique_ptr<filesystem::confined_root> directory= {}) {
    const auto metadata= file.stat ();
    auto path= file.path ().lexically_normal ();
    if (path.filename ().empty ()) path= path.parent_path ();
    auto old= paths.find (path);
    if (old != paths.end ()) {
      const auto& known= nodes.at (old->second);
      if (known.device == metadata.device && known.inode == metadata.inode) return old->second;
      remove (old->second); ++revision;
    }
    const int descriptor= file.duplicate_descriptor ();
    const auto serial= ++ticket;
    struct kevent change;
    EV_SET (&change, descriptor, EVFILT_VNODE, EV_ADD | EV_ENABLE | EV_CLEAR,
      NOTE_WRITE | NOTE_EXTEND | NOTE_ATTRIB | NOTE_LINK | NOTE_DELETE | NOTE_RENAME | NOTE_REVOKE,
      0, reinterpret_cast<void*> (serial));
    while (::kevent (fd, &change, 1, nullptr, 0, nullptr) < 0) {
      if (errno == EINTR) continue;
      const int error= errno; ::close (descriptor);
      throw std::system_error (error, std::generic_category (), "Watch Vault vnode");
    }
    nodes.emplace (descriptor, vnode{path, metadata.device, metadata.inode,
      serial, std::move (directory), {}});
    paths.emplace (path, descriptor);
    return descriptor;
  }
  void scan (int descriptor, bool initial= false) {
    auto& node= nodes.at (descriptor);
    auto& scope= *node.directory;
    std::map<std::string, std::pair<std::uint64_t,std::uint64_t>> children;
    for (const auto& name: scope.open (".").names ()) {
      if (name == ".athena" || name == ".backup" || name == ".git") continue;
      try {
        auto child= scope.open (name);
        const auto metadata= child.stat ();
        if (metadata.directory || accepts (name)) {
          children.emplace (name, std::make_pair (metadata.device, metadata.inode));
          // Directory vnode notifications do NOT report in-place child writes.
          // Selected files therefore receive their own kernel watch.
          if (!metadata.directory) watch (child);
        }
      }
      catch (const std::system_error& error) {
        if (error.code () != std::errc::no_such_file_or_directory) throw;
      }
    }
    if (!initial && node.children != children) ++revision;
    for (const auto& previous: node.children) {
      if (children.count (previous.first)) continue;
      auto removed= paths.find (node.path / previous.first);
      if (removed != paths.end () && !nodes.at (removed->second).directory) remove (removed->second);
    }
    node.children= std::move (children);
  }
#else
  using snapshot= std::map<std::filesystem::path,
    std::tuple<std::filesystem::file_type,std::uintmax_t,std::filesystem::file_time_type>>;
  std::map<std::filesystem::path,snapshot> directories;
  snapshot inspect (const std::filesystem::path& path) {
    snapshot result;
    std::error_code error;
    const auto own= std::filesystem::symlink_status (path, error);
    if (error == std::errc::no_such_file_or_directory) return result;
    if (error) throw std::system_error (error, "Inspect watched directory");
    if (!std::filesystem::is_directory (own))
      throw std::runtime_error ("Watched directory was replaced by another file type");
    result.emplace (path, std::make_tuple (own.type (), 0, std::filesystem::file_time_type {}));
    for (const auto& file: std::filesystem::directory_iterator (path)) {
      const auto name= file.path ().filename ();
      if (name == ".athena" || name == ".backup" || name == ".git") continue;
      const auto status= file.symlink_status ();
      if (std::filesystem::is_directory (status))
        result.emplace (file.path (), std::make_tuple (status.type (), 0, std::filesystem::file_time_type {}));
      else if (accepts (name)) {
        if (!std::filesystem::is_regular_file (status))
          throw std::runtime_error ("Watched source is not a regular file");
        result.emplace (file.path (), std::make_tuple (status.type (), file.file_size (), file.last_write_time ()));
      }
    }
    return result;
  }
#endif
};
source_watch::source_watch (file_predicate accepts): data (std::make_unique<impl> ()) {
  data->accepts= accepts ? std::move (accepts) : file_predicate ([] (const auto& path) {
    return path.extension () == ".ath";
  });
#ifdef __linux__
  data->fd= inotify_init1 (IN_CLOEXEC | IN_NONBLOCK);
  if (data->fd < 0)
    throw std::system_error (errno, std::generic_category (), "Watch vault sources");
#elif defined(__APPLE__)
  data->fd= ::kqueue ();
  if (data->fd < 0)
    throw std::system_error (errno, std::generic_category (), "Create Vault kqueue");
  if (::fcntl (data->fd, F_SETFD, FD_CLOEXEC) < 0) {
    const int error= errno; ::close (data->fd);
    throw std::system_error (error, std::generic_category (), "Protect Vault kqueue descriptor");
  }
#endif
}
source_watch::~source_watch () {
#ifdef __linux__
  close (data->fd);
#elif defined(__APPLE__)
  for (const auto& node: data->nodes) ::close (node.first);
  ::close (data->fd);
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
#elif defined(__APPLE__)
  std::lock_guard<std::mutex> guard (data->lock);
  auto scope= std::make_unique<filesystem::confined_root> (path);
  auto entry= scope->open (".");
  auto canonical= entry.path ().lexically_normal ();
  if (canonical.filename ().empty ()) canonical= canonical.parent_path ();
  const bool initial= data->paths.find (canonical) == data->paths.end ();
  const auto descriptor= data->watch (entry, std::move (scope));
  data->scan (descriptor, initial);
#else
  std::lock_guard<std::mutex> guard (data->lock);
  auto next= data->inspect (path);
  auto old= data->directories.find (path);
  if (old != data->directories.end () && old->second != next) ++data->revision;
  data->directories[path]= std::move (next);
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
          (event.mask & IN_ISDIR) || data->accepts (std::filesystem::path (name)))
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
#elif defined(__APPLE__)
  struct kevent events[128];
  const struct timespec poll {0, 0};
  for (;;) {
    const int count= ::kevent (data->fd, nullptr, 0, events, 128, &poll);
    if (count < 0) {
      if (errno == EINTR) continue;
      throw std::system_error (errno, std::generic_category (), "Read Vault vnode events");
    }
    if (!count) break;
    for (int i= 0; i < count; ++i) {
      const auto& event= events[i];
      const int descriptor= int (event.ident);
      auto known= data->nodes.find (descriptor);
      if (known == data->nodes.end () ||
          known->second.ticket != reinterpret_cast<std::uintptr_t> (event.udata)) continue;
      if (event.flags & EV_ERROR)
        throw std::system_error (int (event.data), std::generic_category (), "Vault vnode notification failed");
      if (event.fflags & (NOTE_DELETE | NOTE_RENAME | NOTE_REVOKE)) {
        const auto removed= known->second.path;
        std::vector<int> obsolete;
        for (const auto& node: data->nodes) {
          const auto relative= node.second.path.lexically_relative (removed);
          if (!relative.empty () && *relative.begin () != "..") obsolete.push_back (node.first);
        }
        for (const auto old: obsolete) data->remove (old);
        ++data->revision;
      }
      else if (known->second.directory) {
        // Ignore unrelated SQLite/sidecar churn: only the selected child set
        // invalidates. Source modifications are reported by their own watches.
        data->scan (descriptor);
      }
      else ++data->revision;
    }
  }
#else
  // Poll actual selected metadata, not the number of calls. A stable revision
  // remains a usable before/after comparison on platforms without kernel events.
  for (auto& directory: data->directories) {
    auto next= data->inspect (directory.first);
    if (next != directory.second) { directory.second= std::move (next); ++data->revision; }
  }
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
