#include "ATHENA/Data/document_history_store.hpp"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <sqlite3.h>

namespace fs= std::filesystem;

int main () {
  fs::path root= fs::temp_directory_path () /
    ("athena-history-test-" + std::to_string (
      std::chrono::steady_clock::now ().time_since_epoch ().count ()));
  fs::create_directories (root);

  athena::history::document_history_store store;
  std::string error;
  assert (store.open (root, error));
  bool inserted= false;
  std::int64_t first= 0, second= 0, duplicate= 0;
  const std::string a= "<doc>alpha alpha alpha alpha alpha</doc>";
  const std::string b= "<doc>alpha beta alpha alpha alpha</doc>";
  assert (store.capture ("Notes/Foo.ath", a, "manual", std::nullopt,
                         inserted, first, error));
  assert (inserted && first > 0);
  assert (store.capture ("Notes/Foo.ath", b, "periodic", std::nullopt,
                         inserted, second, error));
  assert (inserted && second > first);
  assert (store.capture ("Notes/Foo.ath", b, "manual", std::nullopt,
                         inserted, duplicate, error));
  assert (!inserted && duplicate == second);

  std::string rebuilt;
  assert (store.reconstruct (first, rebuilt, error) && rebuilt == a);
  assert (store.reconstruct (second, rebuilt, error) && rebuilt == b);

  std::vector<athena::history::version_entry> versions;
  assert (store.list ("Notes/Foo.ath", versions, error));
  assert (versions.size () == 2 && versions[0].id == second);

  std::int64_t protected_id= 0, retried= 0;
  assert (store.protect ("Notes/Foo.ath", a, "isolated-remote-apply", protected_id, error));
  assert (protected_id > second);
  assert (store.protect ("Notes/Foo.ath", a, "isolated-remote-apply", retried, error));
  assert (retried == protected_id);
  assert (!store.protect ("Notes/Foo.ath", b, "isolated-remote-apply", retried, error));
  assert (retried == 0);
  assert (store.reconstruct (protected_id, rebuilt, error) && rebuilt == a);
  assert (store.list ("Notes/Foo.ath", versions, error));
  assert (versions[0].protected_snapshot && !versions[0].delta);

  // Expire the isolated history, then create an independent full snapshot so
  // no delta dependency can incidentally keep the protected version alive.
  sqlite3* database= nullptr;
  assert (sqlite3_open (store.database_path ().string ().c_str (), &database) == SQLITE_OK);
  assert (sqlite3_exec (database, "UPDATE versions SET created_at_ms=0;", nullptr, nullptr, nullptr) == SQLITE_OK);
  sqlite3_close (database);
  assert (store.capture ("Notes/Foo.ath", "", "periodic", 1, inserted, second, error));
  assert (store.list ("Notes/Foo.ath", versions, error));
  assert (versions.size () == 2 && versions[1].id == protected_id && versions[1].protected_snapshot);
  assert (store.reconstruct (protected_id, rebuilt, error) && rebuilt == a);
  assert (store.rename_path ("Notes", "Archive", true, error));
  assert (store.list ("Archive/Foo.ath", versions, error));
  assert (versions.size () == 2);
  {
    athena::history::document_history_store reopened;
    assert (reopened.open (root, error));
    assert (reopened.protect ("Notes/Foo.ath", a, "isolated-remote-apply", retried, error));
    assert (retried == protected_id);
    assert (reopened.reconstruct (protected_id, rebuilt, error) && rebuilt == a);
  }

  fs::remove_all (root);
  std::cout << "document_history_store_test passed\n";
}
