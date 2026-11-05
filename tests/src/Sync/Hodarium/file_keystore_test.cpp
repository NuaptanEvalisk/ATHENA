/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
// Standalone: compile with device_key_file.cpp, libsodium and -pthread.
#include "device_key_file.hpp"
#include "device_key_internal.hpp"
#include "device_identity.hpp"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

using namespace athena::hodarium;
using namespace athena::hodarium::detail;
namespace {
const std::string handle (43, 'A');
using bytes= std::array<unsigned char, 32>;
template<typename F> void fails (key_store_failure reason, F action) {
  bool caught= false;
  try { action (); }
  catch (const key_store_error& error) { assert (error.reason == reason); caught= true; }
  assert (caught);
}
template<typename F> void invalid (F action) {
  bool caught= false;
  try { action (); }
  catch (const std::invalid_argument&) { caught= true; }
  assert (caught);
}
struct temporary_directory {
  std::string path;
  temporary_directory () {
    char pattern[]= "/tmp/athena-file-keystore-XXXXXX";
    const auto* result= ::mkdtemp (pattern);
    assert (result); path= result;
  }
  ~temporary_directory () { std::filesystem::remove_all (path); }
};
int wait_child (pid_t child) {
  int status= 0;
  assert (::waitpid (child, &status, 0) == child);
  assert (WIFEXITED (status));
  return WEXITSTATUS (status);
}
} // namespace

int main (int argc, char** argv) {
  bytes original{}, other{}, loaded{};
  original.fill (0x19); other.fill (0x87);
  if (argc == 3 && std::string (argv[1]) == "--read") {
    configure_private_key_directory (argv[2]);
    load_protected_seed (handle, loaded.data (), secret_kind::device);
    return loaded == original ? 0 : 1;
  }
  assert (argc == 1);
  fails (key_store_failure::unavailable, [&] {
    store_protected_seed (handle, original.data (), secret_kind::device);
  });
  fails (key_store_failure::unavailable, [&] {
    load_protected_seed (handle, loaded.data (), secret_kind::device);
  });
  temporary_directory temp;
  const auto& path= temp.path;
  const auto file= path + "/device-" + handle;
  invalid ([&] { configure_private_key_directory ("relative"); });
  invalid ([&] { configure_private_key_directory (path + "/../anything"); });
  invalid ([&] { configure_private_key_directory (path + std::string ("\0suffix", 7)); });
  assert (::chmod (path.c_str (), 0750) == 0);
  fails (key_store_failure::unavailable, [&] { configure_private_key_directory (path); });
  assert (::chmod (path.c_str (), 0700) == 0);
  const auto alias= path + "/alias";
  assert (::symlink (path.c_str (), alias.c_str ()) == 0);
  fails (key_store_failure::unavailable, [&] { configure_private_key_directory (alias); });
  fails (key_store_failure::unavailable, [&] { configure_private_key_directory (alias + "/keys"); });
  assert (::unlink (alias.c_str ()) == 0);
  configure_private_key_directory (path);
  configure_private_key_directory (path);
  temporary_directory different;
  fails (key_store_failure::unavailable, [&] { configure_private_key_directory (different.path); });
  loaded.fill (0x55);
  fails (key_store_failure::missing, [&] {
    load_protected_seed (handle, loaded.data (), secret_kind::device);
  });
  assert (std::all_of (loaded.begin (), loaded.end (), [] (auto v) { return v == 0x55; }));
  invalid ([&] { store_protected_seed ("../escape", original.data (), secret_kind::device); });
  invalid ([&] { store_protected_seed (handle, nullptr, secret_kind::device); });
  invalid ([&] { store_protected_seed (handle, original.data (), static_cast<secret_kind> (99)); });
  const auto mask= ::umask (0777);
  store_protected_seed (handle, original.data (), secret_kind::device);
  ::umask (mask);
  struct stat st;
  assert (::stat (file.c_str (), &st) == 0);
  assert ((st.st_mode & 07777) == 0600 && st.st_uid == ::geteuid () && st.st_size == 32);
  fails (key_store_failure::corrupt, [&] {
    store_protected_seed (handle, other.data (), secret_kind::device);
  });
  load_protected_seed (handle, loaded.data (), secret_kind::device);
  assert (loaded == original);
  const auto restart= ::fork ();
  assert (restart >= 0);
  if (restart == 0) {
    ::execl ("/proc/self/exe", argv[0], "--read", path.c_str (), nullptr);
    ::_exit (99);
  }
  assert (wait_child (restart) == 0);
  store_protected_seed (handle, other.data (), secret_kind::vault);
  store_protected_seed (handle, other.data (), secret_kind::relay);
  for (auto kind: {secret_kind::vault, secret_kind::relay}) {
    load_protected_seed (handle, loaded.data (), kind); assert (loaded == other);
    delete_protected_seed (handle, kind); delete_protected_seed (handle, kind);
  }
  assert (::chmod (file.c_str (), 0640) == 0);
  fails (key_store_failure::corrupt, [&] { load_protected_seed (handle, loaded.data (), secret_kind::device); });
  fails (key_store_failure::corrupt, [&] { delete_protected_seed (handle, secret_kind::device); });
  assert (::chmod (file.c_str (), 0600) == 0);
  assert (::link (file.c_str (), alias.c_str ()) == 0);
  fails (key_store_failure::corrupt, [&] { load_protected_seed (handle, loaded.data (), secret_kind::device); });
  assert (::unlink (alias.c_str ()) == 0);
  assert (::chmod (path.c_str (), 0755) == 0);
  fails (key_store_failure::unavailable, [&] { load_protected_seed (handle, loaded.data (), secret_kind::device); });
  assert (::chmod (path.c_str (), 0700) == 0);
  for (auto size: {31, 33}) {
    assert (::truncate (file.c_str (), size) == 0);
    loaded.fill (0x55);
    fails (key_store_failure::corrupt, [&] { load_protected_seed (handle, loaded.data (), secret_kind::device); });
    assert (std::all_of (loaded.begin (), loaded.end (), [] (auto v) { return v == 0x55; }));
  }
  delete_protected_seed (handle, secret_kind::device);
  assert (::symlink ("missing", file.c_str ()) == 0);
  fails (key_store_failure::corrupt, [&] { load_protected_seed (handle, loaded.data (), secret_kind::device); });
  fails (key_store_failure::corrupt, [&] { delete_protected_seed (handle, secret_kind::device); });
  fails (key_store_failure::corrupt, [&] { store_protected_seed (handle, original.data (), secret_kind::device); });
  assert (::unlink (file.c_str ()) == 0);
  assert (::mkfifo (file.c_str (), 0600) == 0);
  fails (key_store_failure::corrupt, [&] { load_protected_seed (handle, loaded.data (), secret_kind::device); });
  assert (::unlink (file.c_str ()) == 0);
  assert (::mkdir (file.c_str (), 0700) == 0);
  fails (key_store_failure::corrupt, [&] { load_protected_seed (handle, loaded.data (), secret_kind::device); });
  assert (::rmdir (file.c_str ()) == 0);
  // Concurrent processes must agree on exactly one complete, immutable winner.
  int gate[2]; assert (::pipe (gate) == 0);
  std::array<pid_t, 8> children;
  for (std::size_t i= 0; i < children.size (); ++i) {
    children[i]= ::fork (); assert (children[i] >= 0);
    if (children[i] == 0) {
      ::close (gate[1]); char start;
      assert (::read (gate[0], &start, 1) == 0);
      bytes candidate; candidate.fill (static_cast<unsigned char> (i));
      try { store_protected_seed (handle, candidate.data (), secret_kind::device); ::_exit (0); }
      catch (const key_store_error& error) { ::_exit (error.reason == key_store_failure::corrupt ? 1 : 2); }
    }
  }
  ::close (gate[0]); ::close (gate[1]);
  int successes= 0;
  for (auto child: children) {
    const int result= wait_child (child); assert (result <= 1);
    if (result == 0) ++successes;
  }
  assert (successes == 1);
  load_protected_seed (handle, loaded.data (), secret_kind::device);
  assert (loaded[0] < children.size ());
  assert (std::all_of (loaded.begin (), loaded.end (), [&] (auto v) { return v == loaded[0]; }));
  delete_protected_seed (handle, secret_kind::device);
  store_protected_seed (handle, other.data (), secret_kind::device);
  load_protected_seed (handle, loaded.data (), secret_kind::device); assert (loaded == other);
  delete_protected_seed (handle, secret_kind::device);
  assert (std::filesystem::is_empty (path));
  cancel_device_key_operations ();
  std::cout << "file keystore tests passed\n";
}
