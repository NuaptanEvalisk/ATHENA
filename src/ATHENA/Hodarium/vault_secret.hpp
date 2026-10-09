/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#pragma once
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include "device_identity.hpp"

namespace athena::hodarium {
// Public device-local descriptor; the 32-byte secret lives only in the system
// key store. Creating this is explicit and does not publish it as canonical.
struct vault_secret {
  std::string handle, group, vault, commitment;
};
struct conflict_tokens {
  std::string vault, conflict, branches, resolution;
};
vault_secret create_vault_secret (const std::string& group, const std::string& vault);
// Blocking check of the actual protected bytes against the public descriptor.
void verify_vault_secret (const vault_secret& secret);
// Blocking key-store access: identity worker only. No replacement on failure.
conflict_tokens derive_conflict_tokens (const vault_secret& secret,
  const std::string& object, std::vector<std::string> parents,
  const std::string& resolution_id);

struct vault_secret_context {
  std::string group, generation, epoch, vault, commitment;
  std::string sender_public_key, recipient_public_key;
};
// The owner must verify both device memberships, selected Vault and canonical
// commitment against this exact context, not merely return general connectivity.
using vault_secret_authorization= std::function<bool (const vault_secret_context&)>;
class vault_secret_receiver {
public:
  vault_secret_receiver (const device_identity& recipient, vault_secret_context context,
                         vault_secret_authorization authorized);
  ~vault_secret_receiver ();
  vault_secret_receiver (const vault_secret_receiver&)= delete;
  vault_secret_receiver& operator= (const vault_secret_receiver&)= delete;
  const std::string& request () const;
  vault_secret receive (const std::string& sealed_response);
private:
  struct implementation;
  std::unique_ptr<implementation> state_;
};
// Ciphertext-only return value. Use only on an authorized E2E peer channel;
// capsules are not a substitute for membership or canonical-secret selection.
std::string seal_vault_secret (const vault_secret& secret, const device_identity& sender,
  const vault_secret_context& context, const std::string& request,
  const vault_secret_authorization& authorized);
} // namespace athena::hodarium
