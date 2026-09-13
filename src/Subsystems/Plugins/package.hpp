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
#include <string>
#include <vector>

namespace athena::plugins {
struct plugin_command {
  std::string id, title;
  interop::value parameters = interop::value::object ();
};
struct manifest {
  std::string id, name, version, description;
  std::filesystem::path executable = "plugin_exec";
  std::vector<std::string> arguments;
  std::vector<plugin_command> commands;
};
bool valid_plugin_id (const std::string& id);
manifest parse_manifest (const interop::value& json);
manifest read_manifest (const std::filesystem::path& directory);
struct installed_plugin { manifest metadata; std::filesystem::path directory; };

// No plugin code is executed while inspecting or installing a package.
// These operations may block and belong on a management worker, not the GUI.
class package_store {
  std::filesystem::path directory_;
public:
  explicit package_store (std::filesystem::path directory);
  const std::filesystem::path& directory () const { return directory_; }
  installed_plugin install (const std::filesystem::path& source) const;
  void uninstall (const std::string& id) const;
};
} // namespace athena::plugins
