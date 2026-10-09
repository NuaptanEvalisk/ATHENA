/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#pragma once
#include "device_identity.hpp"
#include "membership.hpp"
#include "vault_secret.hpp"
#include <QUrl>

namespace athena::hodarium {
struct client_profile {
  QUrl origin;
  authority_pin pin;
  std::string recovery_public_key;
  device_identity device;
  std::string name;
  std::string member;
  bool enabled= false;
};
struct vault_binding {
  std::string group, vault;
  std::filesystem::path root;
  bool enabled= true;
};

// Device-local settings, outside any synchronized Vault. No private key,
// retrieval credential, lease deadline or local path from a peer is stored here.
class client_settings {
public:
  explicit client_settings (const std::filesystem::path& database);
  ~client_settings ();
  client_settings (const client_settings&)= delete;
  client_settings& operator= (const client_settings&)= delete;
  std::vector<client_profile> profiles () const;
  std::optional<client_profile> find (const std::string& group) const;
  // Commit the protected key's handle before the first admission request.
  // Existing trust is never overwritten implicitly by a repeated enrollment.
  void add_pending (const client_profile& profile);
  void complete_admission (const std::string& group, const std::string& handle,
                           const std::string& public_key, const std::string& member);
  void set_enabled (const std::string& group, bool enabled);
  std::vector<vault_binding> vaults (const std::string& group) const;
  // Explicit local action only. Never derive this path from peer messages.
  void bind_vault (const vault_binding& binding);
  void set_vault_enabled (const std::string& group, const std::string& vault, bool enabled);
  void unbind_vault (const std::string& group, const std::string& vault);
  // Identity-worker only: verifies key-store possession before recording the
  // descriptor. This records availability, not canonical-secret authority.
  void remember_vault_secret (const std::string& generation, const vault_secret& secret);
  std::optional<vault_secret> find_vault_secret (const std::string& group,
    const std::string& generation, const std::string& vault, const std::string& commitment) const;
private:
  sqlite3* db_= nullptr;
};
} // namespace athena::hodarium
