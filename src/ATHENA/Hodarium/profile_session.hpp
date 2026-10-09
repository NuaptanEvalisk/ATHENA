/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#pragma once
#include "client_settings.hpp"
#include "enrollment.hpp"
#include "peer_tls.hpp"
#include "rendezvous.hpp"
#include "peer_network.hpp"
#include "peer_replication.hpp"
#include "vault_registration.hpp"
#include "peer_secrets.hpp"
#include "conflict_store.hpp"
#include "decision_feed.hpp"
#include <set>
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
  std::string decision_diagnostic;
  std::vector<peer_route_status> routes;
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
  void configure_revisions (revision_store& store, std::vector<std::string> vaults);
  std::string prepare_resolution (revision result);
  std::vector<conflict_proposal_summary> proposals (const std::string& vault, const std::string& after= {});
  std::optional<conflict_proposal> proposal (const std::string& operation);
  void configure_relays ();
  void set_energy_constrained (bool constrained);
private:
  bool energy_constrained_= false;
  std::unique_ptr<conflict_store> conflicts_;
  conflict_store& conflict_journal ();
  std::unique_ptr<decision_client> decisions_;
  std::unique_ptr<decision_feed_store> decision_feed_;
  std::unique_ptr<decision_feed_client> feed_client_;
  bool fetching_decisions_= false;
  membership_lease::steady::time_point next_feed_{};
  std::string waiting_cursor_;
  std::string waiting_object_, waiting_source_vault_;
  std::size_t waiting_vault_= 0;
  void refresh_decisions ();
  void inspect_waiting_result ();
  bool deciding_= false;
  std::string proposal_cursor_, resolution_cursor_;
  std::size_t resolution_vault_= 0;
  membership_lease::steady::time_point next_decision_{};
  std::optional<vault_secret> selected_secret (const std::string& vault) const;
  void decide_next ();
  void submit_proposal (const std::string& operation, bool write);
  client_settings& settings_;
  client_profile profile_;
  std::filesystem::path trust_database_;
  observer changed_;
  profile_status status_;
  std::unique_ptr<authority_client> client_;
  std::unique_ptr<enrollment> enrollment_;
  std::unique_ptr<presence_directory> directory_;
  std::unique_ptr<peer_network> network_;
  std::unique_ptr<peer_replication> replication_;
  std::unique_ptr<peer_secret_exchange> peer_secrets_;
  std::unique_ptr<vault_registration_client> secret_registration_;
  std::set<std::string> registered_vaults_;
  bool registering_secret_= false;
  membership_lease::steady::time_point next_secret_registration_{};
  revision_store* revisions_= nullptr;
  std::vector<std::string> vaults_;
  QTimer timer_;
  bool paused_= false, busy_= false, pending_= false;
  membership_lease::steady::time_point next_{};
  void owner () const;
  void publish (profile_phase phase, std::string diagnostic= {},
                std::string code= {}, std::int64_t code_expires= 0);
  void tick ();
  void refresh ();
  void register_next_vault ();
  void enroll_result (control_result result, enrollment_status status);
};
} // namespace athena::hodarium
