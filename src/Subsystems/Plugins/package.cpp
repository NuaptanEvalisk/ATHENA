/******************************************************************************
* MODULE     : package.cpp
* DESCRIPTION: Strict plugin manifests and bounded libarchive package extraction
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#include "package.hpp"
#include <archive.h>
#include <archive_entry.h>
#include <array>
#include <fstream>
#include <memory>
#include <set>
#include <stdexcept>
#include <system_error>
#include <dirent.h>
#include <fcntl.h>
#include <linux/fs.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace athena::plugins {
namespace fs = std::filesystem;
using interop::value;
namespace {
constexpr std::size_t manifest_limit = 256 * 1024;
constexpr std::uint64_t file_limit = 256ULL * 1024 * 1024;
constexpr std::uint64_t package_limit = 1024ULL * 1024 * 1024;
constexpr std::size_t entry_limit = 10000;
constexpr std::uint64_t license_limit = 4ULL * 1024 * 1024;
struct fd {
  int value;
  explicit fd (int n): value (n) { if (n < 0) throw std::system_error (errno, std::generic_category ()); }
  ~fd () { ::close (value); }
  fd (const fd&) = delete;
};
void relative_path (const fs::path& path) {
  const auto text = path.string ();
  if (text.empty () || path.is_absolute () || text.find ('\0') != std::string::npos ||
      text.find ('\\') != std::string::npos || text.find (':') != std::string::npos)
    throw std::invalid_argument ("Plugin paths must be relative and confined to the package");
  std::size_t depth = 0;
  for (const auto& part: path) {
    if (part == "." || part == ".." || ++depth > 64)
      throw std::invalid_argument ("Invalid plugin path component");
  }
}
std::string string_field (const value& object, const char* key, std::size_t limit, bool optional = false) {
  if (optional && !object.contains (key)) return {};
  const auto& field = object.at (key);
  if (!field.is_string ()) throw std::invalid_argument (std::string (key) + " must be a string");
  auto text = field.get<std::string> ();
  if ((!optional && text.empty ()) || text.size () > limit || text.find ('\0') != std::string::npos)
    throw std::invalid_argument (std::string (key) + " has invalid length or contains NUL");
  return text;
}
void archive_ok (int status, archive* a) {
  if (status < ARCHIVE_OK) throw std::runtime_error (
    archive_error_string (a) ? archive_error_string (a) : "Plugin archive read failed");
}
struct stage {
  fs::path path;
  bool active = true;
  explicit stage (const fs::path& root) {
    auto pattern = (root / ".install-XXXXXX").string ();
    if (!mkdtemp (pattern.data ())) throw std::system_error (errno, std::generic_category ());
    path = pattern;
  }
  fs::path release () { active = false; return path; }
  ~stage () { if (active) { std::error_code ignored; fs::remove_all (path, ignored); } }
};

bool bool_field (const value& object, const char* key, bool fallback = false) {
  if (!object.contains (key)) return fallback;
  if (!object.at (key).is_boolean ())
    throw std::invalid_argument (std::string (key) + " must be a boolean");
  return object.at (key).get<bool> ();
}

void vault_relative_path (const std::string& text, bool allow_empty) {
  if (text.empty ()) {
    if (allow_empty) return;
    throw std::invalid_argument ("Vault permission path may not be empty");
  }
  if (text.front () == '/' || text.back () == '/' || text.find ("//") != std::string::npos ||
      text.find ('\\') != std::string::npos || text.find ('\0') != std::string::npos ||
      text.find (':') != std::string::npos || text.front () == '~' || text.front () == '$')
    throw std::invalid_argument ("Vault permission paths must be plain vault-relative paths");
  fs::path path (text);
  if (path.is_absolute ()) throw std::invalid_argument ("Vault permission path may not be absolute");
  for (const auto& part: path)
    if (part.empty () || part == "." || part == "..")
      throw std::invalid_argument ("Vault permission path contains a traversal component");
}

void package_relative_file (const fs::path& path) {
  relative_path (path);
  if (path.empty () || path.filename ().empty ())
    throw std::invalid_argument ("Plugin package file path may not be empty");
}

void validate_package_file (const fs::path& root, const fs::path& relative,
                            std::uint64_t byte_limit, const char* what) {
  auto current= root;
  for (const auto& part: relative) {
    current/= part;
    if (fs::is_symlink (fs::symlink_status (current)))
      throw std::runtime_error (std::string (what) + " path contains a symlink");
  }
  std::error_code ec;
  const auto status= fs::status (current, ec);
  if (ec || !fs::is_regular_file (status))
    throw std::runtime_error (std::string (what) + " is not a regular file");
  const auto size= fs::file_size (current, ec);
  if (ec || size > byte_limit)
    throw std::runtime_error (std::string (what) + " exceeds its size limit");
}
struct store_lock {
  fd file;
  explicit store_lock (const fs::path& root): file (::open ((root / ".lock").c_str (),
    O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600)) {
    struct stat st {};
    if (fstat (file.value, &st) || !S_ISREG (st.st_mode) || st.st_uid != getuid () || st.st_nlink != 1)
      throw std::runtime_error ("Unsafe plugin store lock");
    while (flock (file.value, LOCK_EX)) if (errno != EINTR)
      throw std::system_error (errno, std::generic_category ());
  }
};
void write_bytes (int output, const char* data, std::size_t size) {
  std::size_t written = 0;
  while (written < size) {
    const auto n = ::write (output, data + written, size - written);
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) throw std::system_error (errno, std::generic_category ());
    written += n;
  }
}
// Descriptor-relative traversal never changes the process working directory,
// unlike archive_read_disk's portable directory walker.
void copy_directory (int source, const fs::path& destination, std::size_t depth,
                     std::size_t& count, std::uint64_t& total) {
  if (depth > 64) throw std::runtime_error ("Plugin directory nesting exceeds limit");
  const int duplicate = dup (source);
  if (duplicate < 0) throw std::system_error (errno, std::generic_category ());
  std::unique_ptr<DIR, decltype (&closedir)> directory (fdopendir (duplicate), closedir);
  if (!directory) { ::close (duplicate); throw std::system_error (errno, std::generic_category ()); }
  for (;;) {
    errno = 0;
    auto* item = readdir (directory.get ());
    if (!item) {
      if (errno) throw std::system_error (errno, std::generic_category ());
      break;
    }
    const std::string name = item->d_name;
    if (name == "." || name == "..") continue;
    relative_path (name);
    if (++count > entry_limit) throw std::runtime_error ("Plugin package contains too many entries");
    struct stat expected {};
    if (fstatat (source, name.c_str (), &expected, AT_SYMLINK_NOFOLLOW))
      throw std::system_error (errno, std::generic_category ());
    if (!S_ISREG (expected.st_mode) && !S_ISDIR (expected.st_mode))
      throw std::runtime_error ("Plugin package contains a link or special file");
    fd input (openat (source, name.c_str (), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC));
    struct stat st {};
    if (fstat (input.value, &st)) throw std::system_error (errno, std::generic_category ());
    if (st.st_dev != expected.st_dev || st.st_ino != expected.st_ino)
      throw std::runtime_error ("Plugin entry changed while opening");
    const auto target = destination / name;
    if (S_ISDIR (st.st_mode)) {
      if (mkdir (target.c_str (), 0700)) throw std::system_error (errno, std::generic_category ());
      copy_directory (input.value, target, depth + 1, count, total);
      continue;
    }
    if (!S_ISREG (st.st_mode) || st.st_nlink != 1 || st.st_size < 0 ||
        static_cast<std::uint64_t> (st.st_size) > file_limit)
      throw std::runtime_error ("Plugin files must be bounded regular files, not hard links or devices");
    fd output (::open (target.c_str (), O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC | O_NOFOLLOW, 0600));
    std::array<char, 65536> buffer;
    std::uint64_t size = 0;
    for (;;) {
      auto n = ::read (input.value, buffer.data (), buffer.size ());
      if (n < 0 && errno == EINTR) continue;
      if (n < 0) throw std::system_error (errno, std::generic_category ());
      if (!n) break;
      size += n; total += n;
      if (size > file_limit || total > package_limit) throw std::runtime_error ("Plugin package exceeds size limit");
      write_bytes (output.value, buffer.data (), n);
    }
    struct stat after {};
    if (fstat (input.value, &after) || size != static_cast<std::uint64_t> (st.st_size) ||
        after.st_mtim.tv_sec != st.st_mtim.tv_sec || after.st_mtim.tv_nsec != st.st_mtim.tv_nsec ||
        after.st_ctim.tv_sec != st.st_ctim.tv_sec || after.st_ctim.tv_nsec != st.st_ctim.tv_nsec)
      throw std::runtime_error ("Plugin file changed while copying");
    if (fchmod (output.value, st.st_mode & 0111 ? 0700 : 0600))
      throw std::system_error (errno, std::generic_category ());
  }
}
void extract_zip (const fs::path& source, const fs::path& destination) {
  std::unique_ptr<archive, decltype (&archive_read_free)> input (
    archive_read_new (), archive_read_free);
  if (!input) throw std::bad_alloc ();
  archive_ok (archive_read_support_format_zip (input.get ()), input.get ());
  fd zip (::open (source.c_str (), O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK));
  struct stat zip_stat {};
  if (fstat (zip.value, &zip_stat) || !S_ISREG (zip_stat.st_mode)) throw std::runtime_error ("ZIP is not a regular file");
  archive_ok (archive_read_open_fd (input.get (), zip.value, 65536), input.get ());
  std::set<fs::path> paths;
  std::uint64_t total = 0;
  std::size_t count = 0;
  archive_entry* entry = nullptr;
  for (;;) {
    const int status = archive_read_next_header (input.get (), &entry);
    if (status == ARCHIVE_EOF) break;
    archive_ok (status, input.get ());
    if (++count > entry_limit) throw std::runtime_error ("Plugin package contains too many entries");
    const char* name = archive_entry_pathname_utf8 (entry);
    if (!name) throw std::runtime_error ("Plugin package filename is not valid UTF-8");
    fs::path path (name);
    if (archive_entry_symlink (entry) || archive_entry_hardlink (entry))
      throw std::runtime_error ("Plugin packages may not contain symbolic or hard links");
    relative_path (path);
    path = path.lexically_normal ();
    if (path.filename ().empty ()) path = path.parent_path ();
    if (!paths.insert (path).second) throw std::runtime_error ("Duplicate plugin package path");
    const auto type = archive_entry_filetype (entry);
    if (type != AE_IFREG && type != AE_IFDIR)
      throw std::runtime_error ("Plugin packages may contain only regular files and directories");
    const auto target = destination / path;
    if (type == AE_IFDIR) {
      fs::create_directories (target);
      fs::permissions (target, fs::perms::owner_all);
      continue;
    }
    const auto advertised = archive_entry_size (entry);
    if (advertised < 0 || static_cast<std::uint64_t> (advertised) > file_limit)
      throw std::runtime_error ("Plugin file exceeds size limit");
    fs::create_directories (target.parent_path ());
    fd output (::open (target.c_str (), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600));
    std::array<char, 65536> buffer;
    std::uint64_t size = 0;
    for (;;) {
      const auto n = archive_read_data (input.get (), buffer.data (), buffer.size ());
      if (n < 0) archive_ok (static_cast<int> (n), input.get ());
      if (!n) break;
      size += n; total += n;
      if (size > file_limit || total > package_limit)
        throw std::runtime_error ("Plugin package exceeds unpacked size limit");
      write_bytes (output.value, buffer.data (), n);
    }
    if (size != static_cast<std::uint64_t> (advertised)) throw std::runtime_error ("Plugin file size changed while copying");
    // Preserve executable intent, never ownership, ACLs, setuid or writable mode bits.
    if (fchmod (output.value, archive_entry_perm (entry) & 0111 ? 0700 : 0600))
      throw std::system_error (errno, std::generic_category ());
  }
  archive_ok (archive_read_close (input.get ()), input.get ());
}
} // namespace

bool valid_plugin_id (const std::string& id) {
  return !id.empty () && id.size () <= 128 && id.front () >= 'a' && id.front () <= 'z' &&
    id.find_first_not_of ("abcdefghijklmnopqrstuvwxyz0123456789._-") == std::string::npos &&
    id.find ("..") == std::string::npos;
}
std::string jail_permission_id (const jail_permission& p) {
  switch (p.permission) {
  case jail_permission_kind::network:
    return "network";
  case jail_permission_kind::filesystem_read:
  case jail_permission_kind::filesystem_write: {
    const char* kind= p.permission == jail_permission_kind::filesystem_read ?
      "filesystem.read" : "filesystem.write";
    const char* scope= p.scope == filesystem_scope::file ? "file" : "tree";
    return std::string (kind) + ":" + p.root + ":" + scope + ":" + p.path.generic_string ();
  }
  }
  throw std::logic_error ("Unknown jail permission");
}
bool ends_with_text (const std::string& text, const char* suffix) {
  const std::string_view s (suffix);
  return text.size () >= s.size () &&
    text.compare (text.size () - s.size (), s.size (), s) == 0;
}
manifest parse_manifest (const value& json) {
  if (!json.is_object () || !json.contains ("schema") || !json.at ("schema").is_number_integer ())
    throw std::invalid_argument ("Plugin manifest requires a schema version");
  const int schema= json.at ("schema").get<int> ();
  if (schema != 1 && schema != 2)
    throw std::invalid_argument ("Plugin manifest requires schema 1 or 2");
  manifest result;
  result.schema= static_cast<unsigned> (schema);
  result.id = string_field (json, "id", 128);
  if (!valid_plugin_id (result.id)) throw std::invalid_argument ("Invalid plugin ID");
  result.name = string_field (json, "name", 256);
  result.version = string_field (json, "version", 64);
  result.description = string_field (json, "description", 4096, true);
  if (json.contains ("executable")) result.executable = string_field (json, "executable", 4096);
  relative_path (result.executable);
  if (json.contains ("arguments")) {
    const auto& arguments = json.at ("arguments");
    if (!arguments.is_array () || arguments.size () > 128) throw std::invalid_argument ("Invalid plugin arguments");
    for (const auto& arg: arguments) result.arguments.push_back (string_field (value {{"arg", arg}}, "arg", 4096, true));
  }
  if (json.contains ("commands")) {
    const auto& commands = json.at ("commands");
    if (!commands.is_array () || commands.size () > 128) throw std::invalid_argument ("Invalid plugin commands");
    std::set<std::string> names;
    for (const auto& item: commands) {
      plugin_command c;
      c.id = string_field (item, "id", 128);
      if (!valid_plugin_id (c.id) || !names.insert (c.id).second) throw std::invalid_argument ("Invalid or duplicate command ID");
      c.title = string_field (item, "title", 256);
      c.parameters = item.value ("parameters", value::object ());
      if (!c.parameters.is_object () || c.parameters.dump ().size () > 32768)
        throw std::invalid_argument ("Invalid command parameters");
      result.commands.push_back (std::move (c));
    }
  }
  if (schema >= 2 && json.contains ("license")) {
    const auto& license= json.at ("license");
    if (!license.is_object ()) throw std::invalid_argument ("license must be an object");
    const auto format= string_field (license, "format", 32);
    const auto file= string_field (license, "file", 4096);
    package_relative_file (file);
    license_spec spec;
    if (format == "athena") {
      if (!ends_with_text (file, ".ath"))
        throw std::invalid_argument ("ATHENA license file must end in .ath");
      spec.format= license_format::athena;
    }
    else if (format == "text") {
      if (!ends_with_text (file, ".txt"))
        throw std::invalid_argument ("Text license file must end in .txt");
      spec.format= license_format::text;
    }
    else throw std::invalid_argument ("license format must be athena or text");
    spec.file= file;
    result.license= std::move (spec);
  }
  if (schema >= 2 && json.contains ("permissions")) {
    const auto& permissions= json.at ("permissions");
    if (!permissions.is_object ()) throw std::invalid_argument ("permissions must be an object");
    if (permissions.contains ("jail")) {
      const auto& entries= permissions.at ("jail");
      if (!entries.is_array () || entries.size () > 128)
        throw std::invalid_argument ("permissions.jail must be a bounded array");
      std::set<std::string> unique;
      for (const auto& item: entries) {
        if (!item.is_object ()) throw std::invalid_argument ("Jail permission must be an object");
        const auto name= string_field (item, "permission", 64);
        jail_permission p;
        p.required= bool_field (item, "required");
        if (name == "network") {
          if (item.contains ("root") || item.contains ("path") || item.contains ("scope"))
            throw std::invalid_argument ("network permission does not accept filesystem fields");
          p.permission= jail_permission_kind::network;
          if (!unique.insert (name).second) throw std::invalid_argument ("Duplicate network permission");
        }
        else if (name == "filesystem.read" || name == "filesystem.write") {
          p.permission= name == "filesystem.read" ? jail_permission_kind::filesystem_read :
                                                     jail_permission_kind::filesystem_write;
          p.root= string_field (item, "root", 32);
          if (p.root != "vault") throw std::invalid_argument ("Filesystem permissions currently require root=vault");
          const auto path= string_field (item, "path", 4096, true);
          const auto scope= item.contains ("scope") ? string_field (item, "scope", 16) : "tree";
          if (scope == "file") p.scope= filesystem_scope::file;
          else if (scope == "tree") p.scope= filesystem_scope::tree;
          else throw std::invalid_argument ("Filesystem permission scope must be file or tree");
          vault_relative_path (path, p.scope == filesystem_scope::tree);
          if (p.scope == filesystem_scope::file && path.empty ())
            throw std::invalid_argument ("File permission requires a path");
          p.path= path;
          const std::string key= name + "\n" + path + "\n" + scope;
          if (!unique.insert (key).second) throw std::invalid_argument ("Duplicate filesystem permission");
        }
        else throw std::invalid_argument ("Unknown jail permission: " + name);
        result.jail_permissions.push_back (std::move (p));
      }
    }
    if (permissions.contains ("audmap")) {
      const auto& entries= permissions.at ("audmap");
      if (!entries.is_array () || entries.size () > 128)
        throw std::invalid_argument ("permissions.audmap must be a bounded array");
      std::set<std::string> resources;
      for (const auto& item: entries) {
        if (!item.is_object ()) throw std::invalid_argument ("AUDMAP permission must be an object");
        audmap_permission p;
        p.resource= string_field (item, "resource", 128);
        if (!valid_plugin_id (p.resource) || p.resource == "root" || p.resource == "subscription")
          throw std::invalid_argument ("Invalid or reserved AUDMAP resource permission");
        if (!resources.insert (p.resource).second)
          throw std::invalid_argument ("Duplicate AUDMAP resource permission");
        p.required= bool_field (item, "required");
        if (!item.contains ("actions") || !item.at ("actions").is_array () ||
            item.at ("actions").empty () || item.at ("actions").size () > 128)
          throw std::invalid_argument ("AUDMAP permission actions must be a nonempty bounded array");
        for (const auto& action_value: item.at ("actions")) {
          value wrapper {{"action", action_value}};
          const auto action= string_field (wrapper, "action", 128);
          if (!valid_plugin_id (action) || !p.actions.insert (action).second)
            throw std::invalid_argument ("Invalid or duplicate AUDMAP action");
        }
        result.audmap_permissions.push_back (std::move (p));
      }
    }
  }
  return result;
}

prepared_plugin::prepared_plugin (manifest m, fs::path staging, fs::path package):
  staging_ (std::move (staging)), package_ (std::move (package)), metadata (std::move (m)) {}

prepared_plugin::prepared_plugin (prepared_plugin&& other) noexcept:
  staging_ (std::move (other.staging_)), package_ (std::move (other.package_)),
  metadata (std::move (other.metadata)) {
  other.staging_.clear (); other.package_.clear ();
}

prepared_plugin& prepared_plugin::operator= (prepared_plugin&& other) noexcept {
  if (this == &other) return *this;
  if (!staging_.empty ()) { std::error_code ignored; fs::remove_all (staging_, ignored); }
  staging_= std::move (other.staging_); package_= std::move (other.package_);
  metadata= std::move (other.metadata);
  other.staging_.clear (); other.package_.clear ();
  return *this;
}

prepared_plugin::~prepared_plugin () {
  if (!staging_.empty ()) { std::error_code ignored; fs::remove_all (staging_, ignored); }
}

std::optional<fs::path> prepared_plugin::license_file () const {
  if (!metadata.license) return {};
  return package_ / metadata.license->file;
}
manifest read_manifest (const fs::path& directory) {
  fd file (::open ((directory / "manifest.json").c_str (), O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK));
  struct stat st {};
  if (fstat (file.value, &st) || !S_ISREG (st.st_mode) || st.st_size < 0 || st.st_size > manifest_limit)
    throw std::runtime_error ("Invalid plugin manifest file");
  std::string text;
  std::array<char, 4096> buffer;
  for (;;) {
    auto n = ::read (file.value, buffer.data (), buffer.size ());
    if (n < 0 && errno == EINTR) continue;
    if (n < 0) throw std::system_error (errno, std::generic_category ());
    if (!n) break;
    text.append (buffer.data (), n);
    if (text.size () > manifest_limit) throw std::runtime_error ("Plugin manifest exceeds size limit");
  }
  std::vector<std::set<std::string>> keys;
  auto result = parse_manifest (value::parse (text, [&] (int, value::parse_event_t event, value& parsed) {
    if (event == value::parse_event_t::object_start) keys.emplace_back ();
    else if (event == value::parse_event_t::object_end) keys.pop_back ();
    else if (event == value::parse_event_t::key && !keys.back ().insert (parsed.get<std::string> ()).second)
      throw std::invalid_argument ("Duplicate key in plugin manifest");
    return true;
  }));
  auto current = directory;
  for (const auto& part: result.executable) {
    current /= part;
    if (fs::is_symlink (fs::symlink_status (current))) throw std::runtime_error ("Plugin executable path contains a symlink");
  }
  if (!fs::is_regular_file (current)) throw std::runtime_error ("Plugin executable is not a regular file");
  return result;
}

package_store::package_store (fs::path directory): directory_ (fs::absolute (std::move (directory)).lexically_normal ()) {
  fs::create_directories (directory_.parent_path ());
  if (mkdir (directory_.c_str (), 0700) && errno != EEXIST)
    throw std::system_error (errno, std::generic_category ());
  struct stat st {};
  fd directory_fd (::open (directory_.c_str (), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
  if (fstat (directory_fd.value, &st) || !S_ISDIR (st.st_mode) || st.st_uid != getuid ())
    throw std::runtime_error ("Plugin store must be a private directory owned by the current user");
  // Existing profiles may contain the former public plugin directory. Tighten
  // the verified directory itself, never a symlink or another user's directory.
  if ((st.st_mode & 0777) != 0700 && fchmod (directory_fd.value, 0700))
    throw std::system_error (errno, std::generic_category (), "Cannot make plugin store private");
}
installed_plugin package_store::install (const fs::path& source_path) const {
  auto prepared= prepare (source_path);
  if (prepared.metadata.license)
    throw std::runtime_error ("Plugin requires license review before installation");
  return publish (std::move (prepared));
}

prepared_plugin package_store::prepare (const fs::path& source_path) const {
  store_lock lock (directory_);
  const auto source = fs::absolute (source_path).lexically_normal ();
  const auto status = fs::symlink_status (source);
  if (!fs::is_directory (status) && !fs::is_regular_file (status))
    throw std::runtime_error ("Plugin source must be a directory or ZIP file, not a link");
  if (fs::is_directory (status)) {
    const auto relative = fs::canonical (directory_).lexically_relative (fs::canonical (source));
    if (!relative.empty () && *relative.begin () != "..")
      throw std::invalid_argument ("Cannot install a directory containing the plugin store");
  }
  stage staging (directory_);
  if (fs::is_directory (status)) {
    fd input (::open (source.c_str (), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
    std::size_t count = 0;
    std::uint64_t total = 0;
    copy_directory (input.value, staging.path, 0, count, total);
  }
  else extract_zip (source, staging.path);
  auto package = staging.path;
  if (!fs::exists (package / "manifest.json")) {
    std::vector<fs::path> top;
    for (const auto& child: fs::directory_iterator (package)) top.push_back (child.path ());
    if (top.size () != 1 || !fs::is_directory (top.front ()))
      throw std::runtime_error ("Package requires manifest.json at its root or in one enclosing directory");
    package = top.front ();
  }
  auto metadata = read_manifest (package);
  // ZIPs created on non-Unix systems may not carry an executable permission bit.
  fs::permissions (package / metadata.executable, fs::perms::owner_all);
  const auto destination = directory_ / metadata.id;
  if (fs::exists (destination))
    throw std::runtime_error ("Cannot install plugin (already installed?)");
  if (metadata.license)
    validate_package_file (package, metadata.license->file, license_limit, "Plugin license");
  return prepared_plugin (std::move (metadata), staging.release (), package);
}

installed_plugin package_store::publish (prepared_plugin&& prepared) const {
  store_lock lock (directory_);
  if (prepared.staging_.empty () || prepared.package_.empty () ||
      prepared.staging_.parent_path () != directory_)
    throw std::invalid_argument ("Prepared plugin does not belong to this package store");
  const auto destination= directory_ / prepared.metadata.id;
  if (syscall (SYS_renameat2, AT_FDCWD, prepared.package_.c_str (), AT_FDCWD,
               destination.c_str (), RENAME_NOREPLACE))
    throw std::system_error (errno, std::generic_category (), "Cannot install plugin (already installed?)");
  manifest metadata= std::move (prepared.metadata);
  prepared.package_.clear ();
  return {std::move (metadata), destination};
}
void package_store::uninstall (const std::string& id) const {
  if (!valid_plugin_id (id)) throw std::invalid_argument ("Invalid plugin ID");
  store_lock lock (directory_);
  const auto source = directory_ / id;
  if (!fs::is_directory (fs::symlink_status (source))) throw std::runtime_error ("Plugin is not an installed directory");
  stage staging (directory_);
  fs::rename (source, staging.path / "removed");
}
} // namespace athena::plugins
