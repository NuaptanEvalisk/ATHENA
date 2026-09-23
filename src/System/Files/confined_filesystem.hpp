/******************************************************************************
* MODULE     : confined_filesystem.hpp
* DESCRIPTION: Descriptor-backed filesystem reads confined to a captured vault root
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace athena::filesystem {
struct timestamp { std::int64_t seconds; std::uint32_t nanoseconds; };
struct metadata {
  bool directory;
  std::uint64_t device, inode, size;
  timestamp modified, accessed, changed;
  std::optional<timestamp> created;
};
bool same_revision (const metadata&, const metadata&);

class confined_root;
class entry {
  struct impl;
  std::shared_ptr<const impl> implementation;
  explicit entry (std::shared_ptr<const impl>);
  friend class confined_root;
public:
  const std::filesystem::path& path () const;
  metadata stat () const;
  std::string read (std::size_t byte_limit) const;
  // Directory iteration uses an independent descriptor for each invocation.
  std::vector<std::string> names () const;
  bool same_object (const entry&) const;
};

struct replacement {
  entry file;
  // Data was fsynced before commit. A false value means the rename succeeded
  // but directory fsync failed; callers must not treat this as an aborted write.
  bool directory_synced;
};

class confined_root {
  struct impl;
  std::shared_ptr<const impl> implementation;
public:
  explicit confined_root (const std::filesystem::path& root);
  const std::filesystem::path& path () const;
  entry open (const std::filesystem::path& relative) const;
  // Atomically replace an existing regular file, never create through a stale
  // accessor. Cooperating writers are serialized; revision checks also detect
  // outside writes before commit, but are not a CAS against uncooperative code.
  replacement replace (const std::filesystem::path& relative, const entry& expected,
                       const metadata& revision, std::string_view bytes) const;
  // Atomically create a new regular document. The destination must not exist;
  // parent directories must already exist inside the confined root.
  replacement create (const std::filesystem::path& relative,
                      std::string_view bytes) const;
  // Durable, create-only backup. Parent directories are created privately;
  // symlinks are refused. An existing file must contain exactly these bytes.
  // Failure never replaces an existing file; retrying is safe after interruption.
  entry preserve (const std::filesystem::path& relative, std::string_view bytes) const;
  static void validate_component (const std::string& name);
};
} // namespace athena::filesystem
