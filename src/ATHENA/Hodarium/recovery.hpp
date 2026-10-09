/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#pragma once
#include "client_settings.hpp"
#include "authority_client.hpp"
#include <QByteArray>
#include <chrono>

namespace athena::hodarium {
// Cryptographic verification only: no enrollment, settings mutation or lease.
authority_pin verify_current_recovery (const authority_pin& previous,
  const std::string& recovery_key, const QByteArray& envelope, const std::string& nonce);

class recovery_candidate {
public:
  const authority_pin& proposed_pin () const { return proposed_; }
  recovery_candidate (recovery_candidate&&)= default;
  recovery_candidate& operator= (recovery_candidate&&)= default;
  recovery_candidate (const recovery_candidate&)= delete;
private:
  recovery_candidate ()= default;
  client_profile previous_;
  authority_pin proposed_;
  QByteArray envelope_;
  std::string nonce_;
  std::chrono::steady_clock::time_point deadline_;
  bool consumed_= false;
  friend class client_settings;
  friend void query_authority_recovery (control_http&, const client_profile&,
    std::function<void (control_result, std::optional<recovery_candidate>)>);
};

// Call on explicit recovery inspection. The candidate expires two minutes after
// sending the fresh query. UI approval must call settings.accept_recovery().
void query_authority_recovery (control_http& http, const client_profile& previous,
  std::function<void (control_result, std::optional<recovery_candidate>)> completed);
} // namespace athena::hodarium
