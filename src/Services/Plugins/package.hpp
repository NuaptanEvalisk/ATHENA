/******************************************************************************
* MODULE     : package.hpp
* DESCRIPTION: ATHENA plugin manifests and transactional directory or ZIP installation
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#pragma once
#include "value.hpp"
#include <filesystem>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace athena::plugins {
struct plugin_command {
  std::string id, title;
  interop::value parameters = interop::value::object ();
};
enum class jail_permission_kind { network, desktop, filesystem_read, filesystem_write };
enum class filesystem_scope { file, tree };
struct jail_permission {
  jail_permission_kind permission;
  bool required = false;
  std::string root;
  std::filesystem::path path;
  filesystem_scope scope = filesystem_scope::tree;
};
struct audmap_permission {
  std::string resource;
  std::set<std::string> actions;
  bool required = false;
};
enum class license_format { athena, text };
struct license_spec {
  license_format format;
  std::filesystem::path file;
};
struct manifest {
  unsigned schema = 1;
  std::string id, name, version, description;
  std::filesystem::path executable = "plugin_exec";
  std::vector<std::string> arguments;
  std::vector<plugin_command> commands;
  std::vector<jail_permission> jail_permissions;
  std::vector<audmap_permission> audmap_permissions;
  std::optional<license_spec> license;
};
bool valid_plugin_id (const std::string& id);
std::string jail_permission_id (const jail_permission& permission);
manifest parse_manifest (const interop::value& json);
manifest read_manifest (const std::filesystem::path& directory);
struct installed_plugin { manifest metadata; std::filesystem::path directory; };

class prepared_plugin {
  std::filesystem::path staging_, package_;
  friend class package_store;
  prepared_plugin (manifest, std::filesystem::path, std::filesystem::path);
public:
  manifest metadata;
  prepared_plugin (prepared_plugin&&) noexcept;
  prepared_plugin& operator= (prepared_plugin&&) noexcept;
  prepared_plugin (const prepared_plugin&) = delete;
  prepared_plugin& operator= (const prepared_plugin&) = delete;
  ~prepared_plugin ();
  const std::filesystem::path& directory () const { return package_; }
  std::optional<std::filesystem::path> license_file () const;
};

// No plugin code is executed while inspecting or installing a package.
// These operations may block and belong on a management worker, not the GUI.
class package_store {
  std::filesystem::path directory_;
public:
  explicit package_store (std::filesystem::path directory);
  const std::filesystem::path& directory () const { return directory_; }
  prepared_plugin prepare (const std::filesystem::path& source) const;
  installed_plugin publish (prepared_plugin&& prepared) const;
  installed_plugin install (const std::filesystem::path& source) const;
  void uninstall (const std::string& id) const;
};
} // namespace athena::plugins
