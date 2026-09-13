/******************************************************************************
* MODULE     : plugin-package-test.cpp
* DESCRIPTION: Plugin directory and ZIP installation validation and confinement tests
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#include "package.hpp"
#include <archive.h>
#include <archive_entry.h>
#include <fstream>
#include <iostream>
#include <unistd.h>

using namespace athena::plugins;
using athena::interop::value;
namespace fs = std::filesystem;
static void require (bool condition, const char* message) {
  if (!condition) throw std::runtime_error (message);
}
template<class F> static void rejected (F f, const char* message) {
  bool failed = false;
  try { f (); } catch (const std::exception&) { failed = true; }
  require (failed, message);
}
static value metadata () {
  return {{"schema", 1}, {"id", "test-plugin"}, {"name", "Test plugin"}, {"version", "1"},
    {"commands", value::array ({{{"id", "hello"}, {"title", "Hello"}, {"parameters", {{"x", 1}}}}})}};
}
static void write (const fs::path& file, const std::string& text) {
  std::ofstream out (file); out << text;
  require (bool (out), "Cannot write test fixture");
}
struct zip_entry { std::string name, content; bool symlink = false; };
static void zip (const fs::path& path, const std::vector<zip_entry>& entries) {
  auto* writer = archive_write_new ();
  require (writer, "No ZIP writer");
  struct cleanup { archive* p; ~cleanup () { archive_write_free (p); } } guard {writer};
  require (archive_write_set_format_zip (writer) == ARCHIVE_OK, "Cannot select ZIP format");
  require (archive_write_open_filename (writer, path.c_str ()) == ARCHIVE_OK, "Cannot write ZIP");
  for (const auto& input: entries) {
    auto* entry = archive_entry_new ();
    archive_entry_set_pathname (entry, input.name.c_str ());
    archive_entry_set_filetype (entry, input.symlink ? AE_IFLNK : AE_IFREG);
    archive_entry_set_perm (entry, 0644);
    archive_entry_set_size (entry, input.symlink ? 0 : input.content.size ());
    if (input.symlink) archive_entry_set_symlink (entry, input.content.c_str ());
    const auto status = archive_write_header (writer, entry);
    archive_entry_free (entry);
    require (status == ARCHIVE_OK, "Cannot write ZIP entry");
    if (!input.symlink) require (archive_write_data (writer, input.content.data (), input.content.size ()) ==
      static_cast<la_ssize_t> (input.content.size ()), "Cannot write ZIP data");
  }
  require (archive_write_close (writer) == ARCHIVE_OK, "Cannot close ZIP");
}
int main () {
  try {
    char temp[] = "/tmp/athena-plugin-package-XXXXXX";
    require (mkdtemp (temp), "Cannot create test directory");
    struct cleanup { fs::path p; ~cleanup () { fs::remove_all (p); } } dir {temp};
    const auto source = dir.p / "source";
    fs::create_directory (source);
    write (source / "manifest.json", metadata ().dump ());
    write (source / "plugin_exec", "#!/bin/sh\nexit 0\n");
    fs::create_directory (source / "data");
    write (source / "data" / "text", "payload");
    package_store store (dir.p / "plugins");
    const auto cwd = fs::current_path ();
    const auto installed = store.install (source);
    require (fs::current_path () == cwd, "Installer changed process working directory");
    require (installed.metadata.commands.size () == 1 && installed.metadata.commands[0].parameters["x"] == 1,
             "Manifest command data lost");
    require (access ((installed.directory / "plugin_exec").c_str (), X_OK) == 0, "Installed entry point is not executable");
    rejected ([&] { store.install (source); }, "Installation overwrote an existing plugin");
    rejected ([&] { store.uninstall ("../source"); }, "Uninstall allowed traversal");
    store.uninstall (installed.metadata.id);
    require (!fs::exists (installed.directory), "Uninstall left installed plugin behind");
    require (fs::exists (source / "data" / "text"), "Uninstall affected source directory");
    rejected ([&] { store.install (dir.p); }, "Installer recursed into its own staging directory");

    const auto archive = dir.p / "plugin.zip";
    const std::vector<zip_entry> valid {{"bundle/manifest.json", metadata ().dump ()}, {"bundle/plugin_exec", "#!/bin/sh\n"}};
    zip (archive, valid);
    auto from_zip = store.install (archive);
    require (from_zip.metadata.id == "test-plugin", "Wrapped ZIP was not installed");
    store.uninstall ("test-plugin");
    for (const auto& bad: std::vector<zip_entry> {
      {"../escape", "bad"}, {"/tmp/escape", "bad"}, {"a/../../escape", "bad"},
      {"C:\\escape", "bad"}, {"bundle/link", "/tmp", true}, {"bundle/plugin_exec", "duplicate"}}) {
      auto entries = valid; entries.push_back (bad); zip (archive, entries);
      rejected ([&] { store.install (archive); }, "Unsafe ZIP was installed");
      require (!fs::exists (store.directory () / "test-plugin"), "Failed install was partially published");
    }
    fs::create_symlink ("/tmp", source / "link");
    rejected ([&] { store.install (source); }, "Directory symlink was followed");
    fs::remove (source / "link");
    fs::create_hard_link (source / "plugin_exec", source / "linked-exec");
    rejected ([&] { store.install (source); }, "Hard-linked directory file was copied");
    fs::remove (source / "linked-exec");
    write (source / "large", "");
    fs::resize_file (source / "large", 257ULL * 1024 * 1024);
    rejected ([&] { store.install (source); }, "Oversized directory file was accepted");
    fs::remove (source / "large");
    auto invalid = metadata (); invalid["executable"] = "../outside";
    write (source / "manifest.json", invalid.dump ());
    rejected ([&] { store.install (source); }, "Executable escaped package");
    write (source / "manifest.json", "{\"schema\":1,\"schema\":1,\"id\":\"test-plugin\",\"name\":\"x\",\"version\":\"1\"}");
    rejected ([&] { store.install (source); }, "Duplicate manifest keys accepted");
    for (const auto& entry: fs::directory_iterator (store.directory ()))
      require (entry.path ().filename () == ".lock", "Installer left staging debris");
    std::cout << "Plugin package tests passed\n";
    return 0;
  }
  catch (const std::exception& e) { std::cerr << e.what () << '\n'; return 1; }
}
