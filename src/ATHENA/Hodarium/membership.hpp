/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

struct sqlite3;

namespace athena::hodarium {

struct member_identity {
  std::string id, name, public_key;
};
struct membership_state {
  std::string group, generation, epoch;
  std::int64_t revision= 0;
  std::vector<member_identity> members;
};
struct authority_pin {
  std::string group, public_key, generation;
};

// Verifies the original payload bytes, never a reserialized document. Recovery
// requires an explicitly replaced pin; a signed generation change is not enough.
membership_state verify_membership (const authority_pin& pin,
                                    const std::string& envelope);
struct membership_validation {
  membership_state state;
  std::chrono::seconds lifetime;
};
membership_validation verify_validation (const authority_pin& pin,
  const std::string& response, const std::string& nonce,
  const std::string& member, const std::string& public_key);

struct conflict_decision {
  std::string vault, conflict, request, branches, resolution, member, generation, epoch;
  std::int64_t version= 0, created= 0;
};
struct vault_secret_registration {
  std::string commitment, member;
  std::int64_t created= 0;
};
std::optional<vault_secret_registration> verify_vault_registration (const authority_pin& pin,
  const std::string& response, const std::string& epoch, const std::string& nonce,
  const std::string& subject, const std::string& slot);
// Verifies a nonce-bound current authority statement, not permission to apply
// its resolution. The owner must still validate the causal branches and lease.
std::optional<conflict_decision> verify_decision (const authority_pin& pin,
  const std::string& response, const std::string& epoch,
  const std::string& nonce, const std::string& subject,
  const std::string& vault, const std::string& conflict);

// A separate device-local public trust database. Opening with a different pin
// fails; enrollment/recovery must never replace trust implicitly on startup.
class membership_store {
public:
  membership_store (const std::filesystem::path& database, authority_pin pin);
  ~membership_store ();
  membership_store (const membership_store&)= delete;
  membership_store& operator= (const membership_store&)= delete;
  membership_state accept (const std::string& envelope);
  std::optional<membership_state> current () const;
private:
  sqlite3* db_= nullptr;
  authority_pin pin_;
};

// Process-local lease: restart starts expired; platform suspension must revoke.
// Caller starts the clock before sending the challenge request, not on receipt.
class membership_lease {
public:
  using steady= std::chrono::steady_clock;
  using wall= std::chrono::system_clock;
  void renew (std::chrono::seconds lifetime, steady::time_point sent,
              wall::time_point sent_wall);
  bool valid (steady::time_point now, wall::time_point wall_now);
  void revoke ();
private:
  bool active_= false;
  steady::time_point start_, last_;
  wall::time_point wall_start_, wall_last_;
  std::chrono::seconds lifetime_{};
};

} // namespace athena::hodarium
