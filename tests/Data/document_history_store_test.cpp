#include "ATHENA/Data/document_history_store.hpp"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <iostream>

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
  assert (store.rename_path ("Notes", "Archive", true, error));
  assert (store.list ("Archive/Foo.ath", versions, error));
  assert (versions.size () == 2);

  fs::remove_all (root);
  std::cout << "document_history_store_test passed\n";
}
