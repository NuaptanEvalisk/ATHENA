#pragma once

#include "Domains/Vault/vault.hpp"
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
// Apply a proof association to its actual owner-local source tree. The caller
// owns the transaction (BufferActor edit or detached, revision-checked file).
bool relink_source (tree scope, const std::string& proof_id,
                    const std::string& target_id, const std::string& previous);
}
