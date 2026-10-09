/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#pragma once
#include "client_settings.hpp"
#include "enrollment.hpp"
#include "peer_tls.hpp"
#include "rendezvous.hpp"
#include "peer_network.hpp"
#include <QTimer>

namespace athena::hodarium {
enum class profile_phase {
  disabled, pending, validating, authorized, offline_valid, expired,
  denied, error, suspended
};
struct profile_status {
  std::string group;
  profile_phase phase= profile_phase::disabled;
  std::string diagnostic, code;
  std::int64_t code_expires= 0;
  std::string discovery_diagnostic;
  std::size_t discovered_devices= 0;
};

// One session per group, all on the same owner as the device settings store.
// Status is control-plane status only: it does not claim documents are synced.
class profile_session: public QObject {
public:
  using observer= std::function<void (profile_status)>;
  profile_session (client_settings& settings, client_profile profile,
    std::filesystem::path trust_database, observer changed);
  ~profile_session () override;
  void start ();
  void join ();
  void suspend ();
  void resume ();
  void set_enabled (bool enabled);
  bool peer_allowed (const std::string& member, const std::string& public_key,
                     const std::string& epoch);
  profile_status status () const;
  std::optional<membership_state> membership () const;
  std::optional<peer_context> context_for_peer (const std::string& member,
                                               const std::string& public_key);
  bool context_allowed (const peer_context& context, const std::string& public_key);
  presence_snapshot discovered_peers () const;
  void set_presence_routes (std::vector<std::string> direct, std::vector<std::string> relays);
private:
  client_settings& settings_;
  client_profile profile_;
  std::filesystem::path trust_database_;
  observer changed_;
  profile_status status_;
  std::unique_ptr<authority_client> client_;
  std::unique_ptr<enrollment> enrollment_;
  std::unique_ptr<presence_directory> directory_;
  std::unique_ptr<peer_network> network_;
  QTimer timer_;
  bool paused_= false, busy_= false, pending_= false;
  membership_lease::steady::time_point next_{};
  void owner () const;
  void publish (profile_phase phase, std::string diagnostic= {},
                std::string code= {}, std::int64_t code_expires= 0);
  void tick ();
  void refresh ();
  void enroll_result (control_result result, enrollment_status status);
};
} // namespace athena::hodarium
