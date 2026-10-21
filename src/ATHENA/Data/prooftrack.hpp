#pragma once

#include "vault.hpp"
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace athena::prooftrack {
struct proof {
  std::string id, target, file;
};
struct snapshot {
  std::map<std::string, std::vector<proof>> by_target;
  std::map<std::string, std::string> files;
};
// All filesystem/LMDB operations belong to the worker. Readers only acquire
// an immutable, already materialized relation snapshot.
std::shared_ptr<const snapshot> read ();
void start (vault_context_handle);
void stop ();
}
