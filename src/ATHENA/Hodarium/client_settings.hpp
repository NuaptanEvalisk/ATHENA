/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#pragma once
#include "device_identity.hpp"
#include "membership.hpp"
#include "vault_secret.hpp"
#include <QUrl>

namespace athena::hodarium {
class recovery_candidate;
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
  bool allow_code_resources= false;
};
struct relay_binding {
  std::string group;
  QUrl origin;
  std::string credential_handle;
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
  // Pass the enrollment operation's captured generation. The legacy empty
  // argument is accepted only for profiles that have never accepted recovery.
  void complete_admission (const std::string& group, const std::string& handle,
                           const std::string& public_key, const std::string& member,
                           const std::string& generation= "");
  void set_enabled (const std::string& group, bool enabled);
  // Explicit approval only, after stopping the old profile session. Revokes
  // local admission without deleting bindings, source files or old trust data.
  client_profile accept_recovery (recovery_candidate&& candidate);
  std::filesystem::path trust_database_path (const std::filesystem::path& directory,
    const std::string& group) const;
  // Recovery generations never replay old merge certificates with new secrets.
  // The caller must settle old apply intents before accepting recovery, retain
  // the old journal, and seed the new journal from reconciled saved state.
  std::filesystem::path revision_database_path (const std::filesystem::path& directory,
    const std::string& group) const;
  std::vector<relay_binding> relays (const std::string& group) const;
  void set_relay (const relay_binding& binding);
  void remove_relay (const std::string& group, const QUrl& origin);
  std::vector<vault_binding> vaults (const std::string& group) const;
  // Explicit local action only. Never derive this path from peer messages.
  void bind_vault (const vault_binding& binding);
  void set_vault_enabled (const std::string& group, const std::string& vault, bool enabled);
  void set_vault_code_resources (const std::string& group, const std::string& vault, bool allowed);
  void unbind_vault (const std::string& group, const std::string& vault);
  // Identity-worker only: verifies key-store possession before recording the
  // descriptor. This records availability, not canonical-secret authority.
  void remember_vault_secret (const std::string& generation, const vault_secret& secret);
  std::optional<vault_secret> find_vault_secret (const std::string& group,
    const std::string& generation, const std::string& vault, const std::string& commitment) const;
  std::optional<vault_secret> vault_secret_candidate (const std::string& group,
    const std::string& generation, const std::string& vault) const;
  std::optional<std::string> vault_secret_commitment (const std::string& group,
    const std::string& generation, const std::string& vault) const;
  std::optional<vault_secret_registration> accept_vault_registration (const std::string& group,
    const std::string& vault, const std::string& response, const std::string& epoch,
    const std::string& nonce, const std::string& subject);
private:
  std::filesystem::path generation_database_path (const std::filesystem::path& directory,
    const std::string& group, const char* kind) const;
  sqlite3* db_= nullptr;
};
} // namespace athena::hodarium
