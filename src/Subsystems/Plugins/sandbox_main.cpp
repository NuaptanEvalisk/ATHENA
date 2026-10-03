/******************************************************************************
* MODULE     : sandbox_main.cpp
* DESCRIPTION: Minijail supervisor for ATHENA subprocess plugins
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************/

#include "confined_filesystem.hpp"
#include "value.hpp"
#include "desktop_environment.hpp"
#include "libminijail.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <poll.h>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/resource.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace fs= std::filesystem;
using athena::interop::value;

namespace {

struct jail_deleter { void operator() (minijail* jail) const { if (jail) minijail_destroy (jail); } };
using jail_ptr= std::unique_ptr<minijail,jail_deleter>;

std::string read_file (const fs::path& path, std::size_t limit= 1024 * 1024) {
  std::ifstream input (path, std::ios::binary);
  if (!input) throw std::runtime_error ("Cannot read plugin sandbox policy");
  std::string result;
  std::array<char,4096> block;
  while (input) {
    input.read (block.data (), block.size ());
    result.append (block.data (), static_cast<std::size_t> (input.gcount ()));
    if (result.size () > limit) throw std::runtime_error ("Plugin sandbox policy exceeds size limit");
  }
  return result;
}

std::string string_field (const value& object, const char* key, std::size_t limit) {
  if (!object.contains (key) || !object.at (key).is_string ())
    throw std::invalid_argument (std::string (key) + " must be a string");
  auto result= object.at (key).get<std::string> ();
  if (result.empty () || result.size () > limit || result.find ('\0') != std::string::npos)
    throw std::invalid_argument (std::string (key) + " has invalid length");
  return result;
}

fs::path absolute_field (const value& object, const char* key) {
  fs::path result (string_field (object, key, 16384));
  if (!result.is_absolute ()) throw std::invalid_argument (std::string (key) + " must be absolute");
  return result.lexically_normal ();
}

void require_fs_rule (int success, const char* what) {
  if (!success) throw std::runtime_error (std::string ("Cannot configure filesystem access for ") + what);
}

void allow_ro (minijail* jail, const fs::path& path) {
  if (fs::exists (path))
    require_fs_rule (minijail_add_fs_restriction_ro (jail, path.c_str ()), path.c_str ());
}

void allow_rx (minijail* jail, const fs::path& path) {
  if (fs::exists (path))
    require_fs_rule (minijail_add_fs_restriction_rx (jail, path.c_str ()), path.c_str ());
}

void allow_rw (minijail* jail, const fs::path& path) {
  if (fs::exists (path))
    require_fs_rule (minijail_add_fs_restriction_advanced_rw (jail, path.c_str ()), path.c_str ());
}

void allow_fd (minijail* jail, int descriptor, bool writable) {
  const std::string path= "/proc/self/fd/" + std::to_string (descriptor);
  require_fs_rule (writable ? minijail_add_fs_restriction_advanced_rw (jail, path.c_str ())
                            : minijail_add_fs_restriction_ro (jail, path.c_str ()),
                   "current-vault target");
  if (minijail_preserve_fd (jail, descriptor, descriptor))
    throw std::runtime_error ("Cannot preserve current-vault filesystem descriptor");
}

int close_mount_descriptors (void* payload) {
  auto* descriptors= static_cast<std::vector<int>*> (payload);
  for (int descriptor: *descriptors)
    if (descriptor >= 0) ::close (descriptor);
  return 0;
}

struct descriptor_list {
  std::vector<int> values;
  ~descriptor_list () { for (int descriptor: values) if (descriptor >= 0) ::close (descriptor); }
};

void copy_fd (int input, int output) {
  std::array<char,16384> buffer;
  for (;;) {
    ssize_t n= ::read (input, buffer.data (), buffer.size ());
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) break;
    std::size_t offset= 0;
    while (offset < static_cast<std::size_t> (n)) {
      ssize_t written= ::write (output, buffer.data () + offset,
                                 static_cast<std::size_t> (n) - offset);
      if (written < 0 && errno == EINTR) continue;
      if (written <= 0) break;
      offset+= static_cast<std::size_t> (written);
    }
  }
  ::close (input);
}

std::vector<char*> pointers (std::vector<std::string>& values) {
  std::vector<char*> result;
  result.reserve (values.size () + 1);
  for (auto& value: values) result.push_back (value.data ());
  result.push_back (nullptr);
  return result;
}

int run (const fs::path& policy_path) {
  const value policy= value::parse (read_file (policy_path));
  if (!policy.is_object () || policy.value ("version", 0) != 1)
    throw std::invalid_argument ("Unsupported plugin sandbox policy");

  const fs::path plugin= absolute_field (policy, "plugin_dir");
  const fs::path data= absolute_field (policy, "data_dir");
  const fs::path identity= absolute_field (policy, "identity_file");
  const fs::path connection= absolute_field (policy, "connection_file");
  const fs::path audmap_socket= absolute_field (policy, "audmap_socket");
  const std::string executable= string_field (policy, "executable", 4096);
  const std::string plugin_id= string_field (policy, "plugin_id", 128);
  const std::string subscription= string_field (policy, "subscription_guid", 128);
  const bool network= policy.value ("network", false);
  if (fs::path (executable).is_absolute () || executable.find ("..") != std::string::npos ||
      executable.find ('\\') != std::string::npos)
    throw std::invalid_argument ("Invalid sandbox executable path");
  for (const auto& path: {plugin, data, identity, connection, audmap_socket})
    if (!fs::exists (path)) throw std::runtime_error ("Plugin sandbox input disappeared before launch");

  jail_ptr jail (minijail_new ());
  if (!jail) throw std::runtime_error ("Cannot allocate Minijail");
  if (!minijail_is_fs_restriction_available ())
    throw std::runtime_error ("Kernel Landlock support is required for plugin filesystem isolation");
  minijail_namespace_user (jail.get ());
  minijail_namespace_user_disable_setgroups (jail.get ());
  const auto uid= getuid (), gid= getgid ();
  const std::string uidmap= "0 " + std::to_string (uid) + " 1";
  const std::string gidmap= "0 " + std::to_string (gid) + " 1";
  if (minijail_uidmap (jail.get (), uidmap.c_str ()) ||
      minijail_gidmap (jail.get (), gidmap.c_str ()))
    throw std::runtime_error ("Cannot configure plugin user namespace mapping");
  minijail_namespace_pids (jail.get ());
  minijail_namespace_vfs (jail.get ());
  minijail_namespace_ipc (jail.get ());
  minijail_namespace_uts (jail.get ());
  if (!network) minijail_namespace_net (jail.get ());
  minijail_run_as_init (jail.get ());
  minijail_remount_proc_readonly (jail.get ());
  minijail_no_new_privs (jail.get ());
  minijail_use_caps (jail.get (), 0);
  minijail_reset_signal_mask (jail.get ());
  minijail_reset_signal_handlers (jail.get ());
  minijail_close_open_fds (jail.get ());
  minijail_rlimit (jail.get (), RLIMIT_CORE, 0, 0);
  minijail_rlimit (jail.get (), RLIMIT_NOFILE, 1024, 1024);
  minijail_rlimit (jail.get (), RLIMIT_NPROC, 256, 256);
  minijail_rlimit (jail.get (), RLIMIT_FSIZE, 1024ULL * 1024 * 1024,
                   1024ULL * 1024 * 1024);

  minijail_enable_default_fs_restrictions (jail.get ());
  allow_ro (jail.get (), "/proc");
  allow_ro (jail.get (), "/usr/share");
  for (const char* path: {"/etc/ld.so.cache", "/etc/localtime", "/etc/nsswitch.conf"})
    allow_ro (jail.get (), path);
  if (network) {
    for (const char* path: {"/etc/resolv.conf", "/etc/hosts"})
      allow_ro (jail.get (), path);
    for (const char* path: {"/etc/ssl/certs", "/etc/pki"})
      allow_ro (jail.get (), path);
  }
  for (const char* path: {"/dev/null", "/dev/zero", "/dev/random", "/dev/urandom"})
    allow_rw (jail.get (), path);
  allow_rx (jail.get (), plugin);
  allow_rw (jail.get (), data);
  allow_ro (jail.get (), identity);
  allow_rw (jail.get (), fs::path (identity.string () + ".lock"));
  allow_ro (jail.get (), connection);
  allow_ro (jail.get (), audmap_socket.parent_path ());
  allow_rw (jail.get (), audmap_socket);

  // Desktop access is opt-in and independent of Internet or vault grants.
  // X11 has no per-window security boundary; the permission UI says so.
  const value desktop= policy.value ("desktop", value::object ());
  if (!desktop.empty ()) {
    if (desktop.contains ("DISPLAY")) allow_rw (jail.get (), "/tmp/.X11-unix");
    if (desktop.contains ("XAUTHORITY")) allow_ro (jail.get (), absolute_field (desktop, "XAUTHORITY"));
    if (desktop.contains ("WAYLAND_DISPLAY")) {
      const fs::path socket= desktop.at ("WAYLAND_DISPLAY").get<std::string> ();
      allow_rw (jail.get (), socket.is_absolute () ? socket :
        absolute_field (desktop, "XDG_RUNTIME_DIR") / socket);
    }
    allow_ro (jail.get (), "/etc/fonts");
    // Keep HOME private. Qt may read only desktop appearance configuration,
    // not arbitrary files below the real XDG configuration directories.
    std::vector<fs::path> config_dirs;
    if (desktop.contains ("XDG_CONFIG_HOME"))
      config_dirs.push_back (absolute_field (desktop, "XDG_CONFIG_HOME"));
    std::istringstream dirs (desktop.value ("XDG_CONFIG_DIRS", "/etc/xdg"));
    std::string directory;
    while (std::getline (dirs, directory, ':'))
      if (fs::path (directory).is_absolute ()) config_dirs.emplace_back (directory);
    for (const auto& directory: config_dirs)
      for (const char* name: athena::plugins::desktop_config_files)
        allow_ro (jail.get (), directory / name);
  }

  descriptor_list vault_descriptors;
  if (policy.contains ("vault_access")) {
    if (!policy.at ("vault_access").is_array ()) throw std::invalid_argument ("vault_access must be an array");
    const auto& access= policy.at ("vault_access");
    if (!access.empty () &&
        (!policy.contains ("vault_root") || !policy.at ("vault_root").is_string ()))
      throw std::invalid_argument ("vault_root is required for vault filesystem access");
    std::unique_ptr<athena::filesystem::confined_root> confined;
    if (!access.empty ()) confined= std::make_unique<athena::filesystem::confined_root> (
      absolute_field (policy, "vault_root"));
    for (const auto& rule: access) {
      if (!rule.is_object ()) throw std::invalid_argument ("Invalid vault filesystem rule");
      const std::string relative= rule.value ("path", std::string ());
      const std::string scope= rule.value ("scope", std::string ("tree"));
      const bool writable= rule.value ("writable", false);
      fs::path rel= relative.empty () ? fs::path (".") : fs::path (relative);
      auto entry= confined->open (rel);
      const auto info= entry.stat ();
      if ((scope == "tree") != info.directory)
        throw std::runtime_error ("Vault permission target type changed before launch");
      const int descriptor= entry.duplicate_descriptor ();
      vault_descriptors.values.push_back (descriptor);
      allow_fd (jail.get (), descriptor, writable);
    }
  }

  if (!vault_descriptors.values.empty () &&
      minijail_add_hook (jail.get (), close_mount_descriptors,
                         &vault_descriptors.values, MINIJAIL_HOOK_EVENT_PRE_EXECVE))
    throw std::runtime_error ("Cannot configure vault descriptor cleanup hook");

  if (minijail_forward_signals (jail.get ()))
    throw std::runtime_error ("Cannot configure plugin signal forwarding");

  std::vector<std::string> argv_storage;
  const fs::path canonical_plugin= fs::canonical (plugin);
  const fs::path program= fs::canonical (canonical_plugin / fs::path (executable));
  const fs::path relative_program= program.lexically_relative (canonical_plugin);
  if (relative_program.empty () || relative_program.is_absolute () ||
      *relative_program.begin () == "..")
    throw std::runtime_error ("Plugin executable escaped its package directory");
  argv_storage.push_back (program.string ());
  if (policy.contains ("arguments")) {
    if (!policy.at ("arguments").is_array () || policy.at ("arguments").size () > 128)
      throw std::invalid_argument ("Invalid plugin arguments");
    for (const auto& argument: policy.at ("arguments")) {
      if (!argument.is_string ()) throw std::invalid_argument ("Plugin argument must be a string");
      argv_storage.push_back (argument.get<std::string> ());
    }
  }
  auto argv= pointers (argv_storage);
  const fs::path private_tmp= data / ".tmp";
  fs::create_directories (private_tmp);
  std::vector<std::string> env_storage {
    "PATH=/usr/bin:/bin",
    "HOME=" + data.string (),
    "TMPDIR=" + private_tmp.string (),
    "LANG=C.UTF-8",
    "LC_ALL=C.UTF-8",
    "PYTHONUNBUFFERED=1",
    "ATHENA_AUDMAP_ENDPOINT=" + connection.string (),
    "ATHENA_AUDMAP_IDENTITY=" + identity.string (),
    "ATHENA_SUBSCRIPTION_GUID=" + subscription,
    "ATHENA_PLUGIN_ID=" + plugin_id,
    "ATHENA_PLUGIN_DATA_DIR=" + data.string ()
  };
  if (policy.contains ("vault_root"))
    env_storage.push_back ("ATHENA_VAULT_ROOT=" + absolute_field (policy, "vault_root").string ());
  for (const char* key: athena::plugins::desktop_environment_keys)
    if (desktop.contains (key))
      env_storage.push_back (std::string (key) + "=" + desktop.at (key).get<std::string> ());
  if (!desktop.empty ()) {
    env_storage.push_back ("QT_X11_NO_MITSHM=1");
    env_storage.push_back ("QT_ACCESSIBILITY=0");
  }
  auto envp= pointers (env_storage);
  fs::current_path (plugin);
  pid_t child= -1;
  int stdout_fd= -1, stderr_fd= -1;
  const int result= minijail_run_env_pid_pipes_no_preload (
    jail.get (), argv_storage.front ().c_str (), argv.data (), envp.data (),
    &child, nullptr, &stdout_fd, &stderr_fd);
  if (result) throw std::runtime_error ("Minijail failed to start plugin");
  for (int& descriptor: vault_descriptors.values) {
    if (descriptor >= 0) ::close (descriptor);
    descriptor= -1;
  }
  std::thread stdout_thread ([stdout_fd] { copy_fd (stdout_fd, STDOUT_FILENO); });
  std::thread stderr_thread ([stderr_fd] { copy_fd (stderr_fd, STDERR_FILENO); });
  const int status= minijail_wait (jail.get ());
  stdout_thread.join (); stderr_thread.join ();
  if (status < 0) return 127;
  return std::min (status, 255);
}

} // namespace

int main (int argc, char** argv) {
  try {
    if (argc != 3 || std::string_view (argv[1]) != "--policy") {
      std::cerr << "Usage: athena-plugin-sandbox --policy FILE\n";
      return 2;
    }
    return run (fs::path (argv[2]));
  }
  catch (const std::exception& e) {
    std::cerr << "ATHENA plugin sandbox: " << e.what () << '\n';
    return 126;
  }
}
