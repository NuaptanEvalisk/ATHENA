/******************************************************************************
* MODULE     : shaping-cache-builder.cpp
* DESCRIPTION: Headless, parallel Vault-derived shaping-cache preparation
* COPYRIGHT  : (C) 2026 ATHENA contributors
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
******************************************************************************/
#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;
namespace fs= std::filesystem;

namespace {
struct temporary_directory {
  fs::path location;
  temporary_directory () {
    const auto base= (fs::temp_directory_path () /
      "athena-shaping-builder-XXXXXX").string ();
    std::vector<char> bytes (base.begin (), base.end ());
    bytes.push_back ('\0');
    const char* created= mkdtemp (bytes.data ());
    if (!created) throw std::runtime_error ("Could not create temporary export directory");
    location= created;
  }
  ~temporary_directory () {
    std::error_code ec;
    fs::remove_all (location, ec);
  }
};
std::uint64_t stable_rank (std::string_view name) {
  std::uint64_t value= 14695981039346656037ull;
  for (unsigned char byte: name) {
    value^= byte;
    value*= 1099511628211ull;
  }
  return value;
}
struct document {
  fs::path path;
  std::string relative;
  std::uint64_t rank= 0;
};
std::vector<document> list_documents (const fs::path& vault, unsigned percent) {
  std::vector<document> files;
  for (fs::recursive_directory_iterator i (vault,
         fs::directory_options::skip_permission_denied), end; i != end; ++i) {
    const auto& path= i->path ();
    if (i->is_directory ()) {
      if (path.filename () == ".athena" ||
          path.filename () == ".git") i.disable_recursion_pending ();
      continue;
    }
    if (!i->is_regular_file () || path.extension () != ".ath") continue;
    const auto relative= path.lexically_relative (vault).generic_string ();
    files.push_back ({path, relative, stable_rank (relative)});
  }
  std::sort (files.begin (), files.end (), [] (const auto& a, const auto& b) {
    return a.rank == b.rank ? a.relative < b.relative : a.rank < b.rank;
  });
  files.resize ((files.size () * percent + 99) / 100);
  return files;
}
struct process {
  std::string relative;
  fs::path output, log;
};
pid_t launch (const fs::path& binary, const document& doc,
              const fs::path& output, const fs::path& log) {
  auto executable= binary.string (), source= doc.path.string (),
       destination= output.string ();
  char* argv[]= {
    executable.data (), const_cast<char*> ("-H"),
    const_cast<char*> ("-C"), source.data (), destination.data (), nullptr};
  posix_spawn_file_actions_t actions;
  if (posix_spawn_file_actions_init (&actions) != 0)
    throw std::runtime_error ("Cannot initialize process file actions");
  const auto log_name= log.string ();
  posix_spawn_file_actions_addopen (&actions, STDOUT_FILENO, log_name.c_str (),
                                    O_WRONLY | O_CREAT | O_TRUNC, 0600);
  posix_spawn_file_actions_adddup2 (&actions, STDOUT_FILENO, STDERR_FILENO);
  pid_t pid= -1;
  int code= posix_spawn (&pid, executable.c_str (), &actions, nullptr, argv, environ);
  posix_spawn_file_actions_destroy (&actions);
  if (code != 0) throw std::runtime_error (
    "Cannot start native ATHENA: " + std::string (strerror (code)));
  return pid;
}
}

int main (int argc, char** argv) {
  try {
    fs::path vault, binary;
    unsigned percent= 100, jobs= 1;
    for (int i=1; i<argc; ++i) {
      const std::string arg= argv[i];
      if (arg == "--help") {
        std::cout << "Usage: athena-shaping-cache-builder --vault DIRECTORY "
          "[--percent 1..100] [--jobs N] [--athena ATHENA.bin]\n"
          "Prepares the global disposable LMDB cache by performing the native "
          "layout/export pipeline for selected .ath documents.\n";
        return 0;
      }
      if (i+1 >= argc) throw std::invalid_argument ("Missing value for " + arg);
      const auto value= std::string (argv[++i]);
      if (arg == "--vault") vault= value;
      else if (arg == "--athena") binary= value;
      else if (arg == "--percent") {
        const auto parsed= std::stoul (value);
        if (parsed < 1 || parsed > 100) throw std::invalid_argument ("Invalid percentage");
        percent= parsed;
      }
      else if (arg == "--jobs") {
        const auto parsed= std::stoul (value);
        if (parsed < 1 || parsed > 32) throw std::invalid_argument ("Invalid worker count");
        jobs= parsed;
      }
      else throw std::invalid_argument ("Unknown option: " + arg);
    }
    if (vault.empty ()) throw std::invalid_argument ("--vault is required");
    vault= fs::canonical (vault);
    if (!fs::is_directory (vault)) throw std::invalid_argument ("Vault must be a directory");
    if (binary.empty ()) {
      const auto self= fs::canonical (argv[0]);
      binary= self.parent_path () / "ATHENA.bin";
    }
    binary= fs::canonical (binary);
    auto documents= list_documents (vault, percent);
    std::cout << "ATHENA shaping cache: " << documents.size () <<
      " selected document(s), " << jobs << " native worker(s)\n" << std::flush;
    if (documents.empty ()) return 0;
    temporary_directory temporary;
    std::map<pid_t,process> active;
    std::size_t next= 0, finished= 0, failed= 0;
    setenv ("ATHENA_SHAPING_BUILDER", "1", 1);
    while (next < documents.size () || !active.empty ()) {
      while (next < documents.size () && active.size () < jobs) {
        const auto output= temporary.location /
          ("source-" + std::to_string (next) + ".pdf");
        const auto log= temporary.location /
          ("source-" + std::to_string (next) + ".log");
        const auto pid= launch (binary, documents[next], output, log);
        active.emplace (pid, process {documents[next].relative, output, log});
        ++next;
      }
      int status= 0;
      const auto done= waitpid (-1, &status, 0);
      if (done < 0) {
        if (errno == EINTR) continue;
        throw std::runtime_error ("Cannot await native shaping worker");
      }
      auto found= active.find (done);
      if (found == active.end ()) continue;
      const auto current= std::move (found->second);
      active.erase (found);
      std::error_code ec;
      const bool successful= WIFEXITED (status) && WEXITSTATUS (status) == 0 &&
        fs::is_regular_file (current.output, ec) &&
        fs::file_size (current.output, ec) > 0 && !ec;
      if (!successful) {
        ++failed;
        std::cerr << "Cache preparation failed: " << current.relative << '\n';
        std::ifstream input (current.log);
        std::string line;
        std::deque<std::string> tail;
        while (std::getline (input, line)) {
          if (tail.size () == 8) tail.pop_front ();
          tail.push_back (line);
        }
        for (const auto& message: tail) std::cerr << "  " << message << '\n';
      }
      fs::remove (current.output, ec);
      fs::remove (current.log, ec);
      ++finished;
      std::cout << '\r' << finished << '/' << documents.size ()
                << " processed; " << failed << " error(s)    " << std::flush;
    }
    std::cout << '\n';
    return failed ? 1 : 0;
  }
  catch (const std::exception& e) {
    std::cerr << "ATHENA shaping builder: " << e.what () << '\n';
    return 1;
  }
}
