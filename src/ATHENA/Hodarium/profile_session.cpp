/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#include "profile_session.hpp"
#include "relay_credentials.hpp"
#include <QThread>
#include <stdexcept>
#include <sodium.h>
#include <algorithm>

namespace athena::hodarium {
using clock= membership_lease::steady;
profile_session::profile_session (client_settings& settings, client_profile profile,
  std::filesystem::path database, observer changed): settings_ (settings),
  profile_ (std::move (profile)), trust_database_ (std::move (database)),
  changed_ (std::move (changed)) {
  status_.group= profile_.pin.group;
  connect (&timer_, &QTimer::timeout, this, [this] { tick (); });
  timer_.setInterval (1000);
}
profile_session::~profile_session () {
  paused_= true;
  timer_.stop ();
  changed_= {};
  feed_client_.reset ();
  decisions_.reset ();
  secret_registration_.reset ();
  replication_.reset ();
  peer_secrets_.reset ();
  if (directory_) directory_->stop ();
  if (network_) network_->stop ();
  if (client_) client_->suspend ();
  if (enrollment_) enrollment_->cancel ();
}
void profile_session::owner () const {
  if (QThread::currentThread () != thread ())
    throw std::logic_error ("Hodarium profile session accessed outside its worker");
}
void profile_session::publish (profile_phase phase, std::string diagnostic,
  std::string code, std::int64_t code_expires) {
  profile_status next{profile_.pin.group, phase, std::move (diagnostic), std::move (code), code_expires};
  next.discovery_diagnostic= status_.discovery_diagnostic;
  next.discovered_devices= status_.discovered_devices;
  next.decision_diagnostic= status_.decision_diagnostic;
  if (next.phase == status_.phase && next.diagnostic == status_.diagnostic &&
      next.code == status_.code && next.code_expires == status_.code_expires) return;
  status_= std::move (next);
  if (changed_) changed_ (status_);
}
void profile_session::start () {
  owner ();
  paused_= false;
  timer_.start ();
  if (profile_.member.empty ()) {
    if (!pending_) timer_.stop ();
    publish (profile_phase::pending); return;
  }
  if (!profile_.enabled) { timer_.stop (); publish (profile_phase::disabled); return; }
  try {
    if (!client_) client_= std::make_unique<authority_client> (profile_.origin,
      profile_.pin, profile_.device, profile_.member, trust_database_);
    refresh ();
  }
  catch (const std::exception& e) { publish (profile_phase::error, e.what ()); }
}
void profile_session::refresh () {
  if (busy_ || paused_ || !client_ || !profile_.enabled) return;
  busy_= true;
  if (!client_->authorized ()) publish (profile_phase::validating);
  client_->refresh ([this] (control_result result) {
    busy_= false; next_= clock::now () + std::chrono::seconds (60);
    if (paused_) return;
    if (result.failure == control_failure::none) {
      auto state= client_->current ();
      try {
        const bool new_network= !network_;
        if (!network_) network_= std::make_unique<peer_network> (profile_.device,
          [this] (const peer_context& c, const std::string& key) { return context_allowed (c, key); },
          [this] (const std::string& member, QByteArray message) {
            if (!replication_) throw std::invalid_argument ("Revision replication is unavailable");
            try { replication_->receive (member, message); }
            catch (const std::exception& e) { publish (profile_phase::error, e.what ()); throw; }
          });
        if (new_network) configure_relays ();
        network_->start (*state, profile_.member);
        if (!peer_secrets_) peer_secrets_= std::make_unique<peer_secret_exchange> (*network_, settings_, profile_,
          [this] { return membership (); }, [this] (const std::string& epoch) {
            return !paused_ && profile_.enabled && client_ &&
              client_->context_current (profile_.pin.group, profile_.pin.generation, epoch);
          });
        if (revisions_ && !replication_) replication_= std::make_unique<peer_replication> (
          *network_, *revisions_, vaults_, [this] (std::string error) { publish (profile_phase::error, std::move (error)); }, peer_secrets_.get ());
      }
      catch (const std::exception& e) { publish (profile_phase::error, e.what ()); return; }
      if (!directory_) directory_= std::make_unique<presence_directory> (profile_.origin, profile_.device,
        [this] (const presence_context& c) {
          return !paused_ && profile_.enabled && client_ && c.member == profile_.member &&
            client_->context_current (c.group, c.generation, c.epoch) &&
            client_->peer_allowed (c.member, profile_.device.public_key, c.epoch);
        }, [this] (const presence_snapshot& snapshot) {
          if (network_) network_->discover (snapshot);
          if (status_.discovery_diagnostic == snapshot.diagnostic &&
              status_.discovered_devices == snapshot.peers.size ()) return;
          status_.discovery_diagnostic= snapshot.diagnostic;
          status_.discovered_devices= snapshot.peers.size ();
          if (changed_) changed_ (status_);
        });
      directory_->set_routes (network_->addresses (), network_->relay_origins ());
      directory_->start ({state->group, profile_.member, state->generation, state->epoch});
      publish (profile_phase::authorized);
    }
    else if (result.failure == control_failure::denied) publish (profile_phase::denied, result.diagnostic);
    else if (result.failure == control_failure::transport && client_->authorized ())
      publish (profile_phase::offline_valid, result.diagnostic);
    else publish (profile_phase::error, result.diagnostic);
  });
}
void profile_session::tick () {
  if (paused_) return;
  try {
    if (registering_secret_ && client_ && !client_->authorized ()) secret_registration_->cancel ();
    register_next_vault ();
    if (fetching_decisions_ && client_ && !client_->authorized ()) feed_client_->cancel ();
    refresh_decisions ();
    if (deciding_ && client_ && !client_->authorized ()) decisions_->cancel ();
    decide_next ();
    inspect_waiting_result ();
    if (client_ && !client_->authorized () && network_) { replication_.reset (); peer_secrets_.reset (); network_->stop (); }
    if (client_ && (status_.phase == profile_phase::authorized ||
        status_.phase == profile_phase::offline_valid) && !client_->authorized ())
      publish (profile_phase::expired, "Hodarium membership validation expired");
    if (busy_ || clock::now () < next_) return;
    if (pending_ && enrollment_) {
      busy_= true;
      enrollment_->poll ([this] (control_result r, enrollment_status s) {
        enroll_result (std::move (r), std::move (s));
      });
    }
    else refresh ();
  }
  catch (const std::exception& e) {
    busy_= false; next_= clock::now () + std::chrono::seconds (60);
    publish (profile_phase::error, e.what ());
  }
}
void profile_session::join () {
  owner ();
  if (busy_ || !profile_.member.empty ())
    throw std::logic_error ("Hodarium profile cannot start another admission");
  paused_= false; pending_= false; timer_.start (); busy_= true;
  enrollment_= std::make_unique<enrollment> (profile_.origin, profile_.pin, profile_.device);
  publish (profile_phase::pending);
  enrollment_->resolve ([this] (control_result result, enrollment_status status) {
    if (paused_) { busy_= false; return; }
    if (result.failure == control_failure::denied) {
      enrollment_->join (profile_.name, [this] (control_result r, enrollment_status s) {
        enroll_result (std::move (r), std::move (s));
      });
    }
    else enroll_result (std::move (result), std::move (status));
  });
}
void profile_session::enroll_result (control_result result, enrollment_status status) {
  busy_= false; next_= clock::now () + std::chrono::seconds (10);
  if (paused_) return;
  if (result.failure != control_failure::none) {
    if (result.failure != control_failure::transport) pending_= false;
    publish (profile_phase::error, result.diagnostic); return;
  }
  try {
    if (status.member.empty ()) {
      pending_= true;
      publish (profile_phase::pending, {}, status.code, status.code_expires);
      return;
    }
    pending_= false;
    settings_.complete_admission (profile_.pin.group, profile_.device.handle,
                                  profile_.device.public_key, status.member, profile_.pin.generation);
    profile_.member= status.member;
    settings_.set_enabled (profile_.pin.group, true); profile_.enabled= true;
    // Run outside the enrollment completion stack before changing controller state.
    QTimer::singleShot (0, this, [this] { if (!paused_) start (); });
  }
  catch (const std::exception& e) { publish (profile_phase::error, e.what ()); }
}
void profile_session::suspend () {
  owner (); paused_= true; timer_.stop ();
  if (feed_client_) feed_client_->cancel ();
  if (decisions_) decisions_->cancel ();
  if (secret_registration_) secret_registration_->cancel ();
  replication_.reset ();
  peer_secrets_.reset ();
  if (directory_) directory_->stop ();
  if (network_) network_->stop ();
  if (client_) client_->suspend ();
  if (enrollment_) enrollment_->cancel ();
  busy_= false;
  publish (profile_phase::suspended);
}
void profile_session::resume () { owner (); next_= {}; start (); }
void profile_session::set_enabled (bool enabled) {
  owner ();
  settings_.set_enabled (profile_.pin.group, enabled); profile_.enabled= enabled;
  if (enabled) start ();
  else { suspend (); publish (profile_phase::disabled); }
}
bool profile_session::peer_allowed (const std::string& member,
  const std::string& key, const std::string& epoch) {
  owner ();
  return !paused_ && profile_.enabled && client_ && client_->peer_allowed (member, key, epoch);
}
profile_status profile_session::status () const {
  owner ();
  auto result= status_;
  if (network_) result.routes= network_->status ();
  return result;
}
std::optional<membership_state> profile_session::membership () const {
  owner (); return client_ ? client_->current () : std::nullopt;
}
std::optional<peer_context> profile_session::context_for_peer (
  const std::string& member, const std::string& public_key) {
  owner ();
  auto state= membership ();
  if (!state) return std::nullopt;
  peer_context context{state->group, state->generation, state->epoch, profile_.member, member};
  if (!context_allowed (context, public_key)) return std::nullopt;
  return context;
}
bool profile_session::context_allowed (const peer_context& context, const std::string& public_key) {
  owner ();
  return client_ && !paused_ && profile_.enabled &&
    context.group == profile_.pin.group && context.generation == profile_.pin.generation &&
    client_->context_current (context.group, context.generation, context.epoch) &&
    context.local_member == profile_.member &&
    context.remote_member != context.local_member && public_key != profile_.device.public_key &&
    peer_allowed (context.local_member, profile_.device.public_key, context.epoch) &&
    peer_allowed (context.remote_member, public_key, context.epoch);
}
presence_snapshot profile_session::discovered_peers () const {
  owner (); return directory_ ? directory_->snapshot () : presence_snapshot{};
}
void profile_session::set_presence_routes (std::vector<std::string> direct,
  std::vector<std::string> relays) {
  owner ();
  if (!directory_) throw std::logic_error ("Hodarium discovery requires validated membership");
  directory_->set_routes (std::move (direct), std::move (relays));
}
void profile_session::set_energy_constrained (bool constrained) {
  owner (); energy_constrained_= constrained;
  if (network_) network_->set_throughput_probes_enabled (!constrained);
}
void profile_session::configure_relays () {
  owner ();
  if (!network_) return;
  std::vector<relay_configuration> relays;
  for (const auto& entry: settings_.relays (profile_.pin.group))
    relays.push_back ({entry.origin, load_relay_token (entry.origin, entry.credential_handle)});
  network_->set_relays (std::move (relays));
  network_->set_throughput_probes_enabled (!energy_constrained_);
  if (directory_) directory_->set_routes (network_->addresses (), network_->relay_origins ());
}
void profile_session::configure_revisions (revision_store& store, std::vector<std::string> vaults) {
  owner ();
  if (feed_client_) feed_client_->cancel ();
  if (decisions_) decisions_->cancel ();
  replication_.reset ();
  peer_secrets_.reset ();
  if (network_) network_->stop ();
  if (directory_) directory_->stop ();
  if (secret_registration_) secret_registration_->cancel ();
  registered_vaults_.clear ();
  next_secret_registration_= {};
  revisions_= &store; vaults_= std::move (vaults);
  refresh ();
}

std::optional<vault_secret> profile_session::selected_secret (const std::string& vault) const {
  if (std::find (vaults_.begin (), vaults_.end (), vault) == vaults_.end ()) return {};
  auto bindings= settings_.vaults (profile_.pin.group);
  if (std::none_of (bindings.begin (), bindings.end (), [&] (const vault_binding& v) {
      return v.vault == vault && v.enabled;
    })) return {};
  auto commitment= settings_.vault_secret_commitment (profile_.pin.group, profile_.pin.generation, vault);
  return commitment ? settings_.find_vault_secret (profile_.pin.group, profile_.pin.generation, vault, *commitment) :
                      std::optional<vault_secret> ();
}

conflict_store& profile_session::conflict_journal () {
  owner ();
  if (!conflicts_) conflicts_= std::make_unique<conflict_store> (trust_database_.parent_path () /
    ("conflicts-" + profile_.pin.group + "-" + profile_.pin.generation + ".sqlite"), profile_.pin);
  return *conflicts_;
}
std::vector<conflict_proposal_summary> profile_session::proposals (const std::string& vault, const std::string& after) {
  return conflict_journal ().proposals (vault, after);
}
std::optional<conflict_proposal> profile_session::proposal (const std::string& operation) {
  return conflict_journal ().proposal (operation);
}
std::string profile_session::prepare_resolution (revision result) {
  owner ();
  if (!revisions_ || profile_.member.empty ()) throw std::runtime_error ("Hodarium journal unavailable");
  auto secret= selected_secret (result.vault);
  if (!secret) throw std::runtime_error ("Hodarium Vault secret is not available on this device");
  result.origin_member= profile_.member;
  result= seal_revision (std::move (result));
  auto tokens= derive_conflict_tokens (*secret, result.object, result.parents, result.id);
  conflict_journal ();
  auto previous= conflicts_->latest (tokens.vault, tokens.conflict);
  if (previous || (decision_feed_ && decision_feed_->latest (tokens.vault, tokens.conflict)))
    throw std::runtime_error ("This branch set already has a decision; wait for its accepted result");
  QByteArray random (32, Qt::Uninitialized); randombytes_buf (random.data (), random.size ());
  auto operation= random.toBase64 (QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals).toStdString ();
  decision_request request{tokens.vault, tokens.conflict, operation, tokens.branches, tokens.resolution,
                           previous ? previous->version : 0};
  conflicts_->prepare ({request, std::move (result)}, *revisions_);
  next_decision_= {};
  return operation;
}

void profile_session::submit_proposal (const std::string& operation, bool write) {
  auto proposal= conflicts_->proposal (operation);
  if (!proposal) throw std::logic_error ("Missing durable Hodarium proposal");
  const auto vault= proposal->resolution.vault;
  if (!selected_secret (vault)) return;
  auto state= client_->current ();
  if (!state) return;
  auto request= proposal->request;
  if (!write) request= {request.vault, request.conflict, {}, {}, {}, 0};
  deciding_= true;
  decisions_->request (state->epoch, request, [this, vault] (const std::string& epoch) {
    return !paused_ && profile_.enabled && client_ && selected_secret (vault) &&
      client_->context_current (profile_.pin.group, profile_.pin.generation, epoch);
  }, [this, operation, vault, write] (control_result result, std::optional<conflict_decision>) {
    deciding_= false;
    if (result.failure == control_failure::cancelled) return;
    if (result.failure != control_failure::none) {
      next_decision_= clock::now () + std::chrono::seconds (30);
      publish (profile_phase::error, result.diagnostic); return;
    }
    try {
      const auto evidence= decisions_->evidence ();
      auto decision= conflicts_->accept (operation, evidence.envelope, evidence.epoch, evidence.nonce, evidence.subject);
      auto proposal= conflicts_->proposal (operation);
      if (!decision || decision->version <= proposal->request.expected) {
        if (write) throw std::runtime_error ("Conflict write did not advance its expected version");
        submit_proposal (operation, true); return;
      }
      if (decision->request != operation) {
        publish (profile_phase::authorized, "Another device resolved this conflict; your merge draft is retained");
        return;
      }
      auto secret= selected_secret (vault);
      if (!secret) throw std::runtime_error ("Conflict Vault binding changed");
      revisions_->receive (proposal->resolution);
      if (!revisions_->accept_resolution (proposal->resolution.id, profile_.pin, *secret, evidence))
        throw std::runtime_error ("Conflict decision does not authorize the prepared revision");
      conflicts_->published (operation);
      publish (profile_phase::authorized, "Conflict resolution committed; protected document application is pending");
    }
    catch (const std::exception& e) {
      next_decision_= clock::now () + std::chrono::seconds (30);
      publish (profile_phase::error, e.what ());
    }
  });
}

void profile_session::refresh_decisions () {
  if (paused_ || !profile_.enabled || !client_ || !client_->authorized () ||
      fetching_decisions_ || clock::now () < next_feed_) return;
  if (!decision_feed_) decision_feed_= std::make_unique<decision_feed_store> (trust_database_.parent_path () /
    ("decisions-" + profile_.pin.group + "-" + profile_.pin.generation + ".sqlite"), profile_.pin);
  if (!feed_client_) feed_client_= std::make_unique<decision_feed_client> (
    profile_.origin, profile_.pin, profile_.device, profile_.member, *decision_feed_);
  auto state= client_->current ();
  if (!state) return;
  fetching_decisions_= true;
  feed_client_->refresh (state->epoch, [this] (const std::string& epoch) {
    return !paused_ && profile_.enabled && client_ &&
      client_->context_current (profile_.pin.group, profile_.pin.generation, epoch);
  }, [this] (control_result result, bool more) {
    fetching_decisions_= false;
    next_feed_= clock::now () + std::chrono::seconds (more ? 0 : 15);
    if (result.failure != control_failure::none && result.failure != control_failure::cancelled) {
      next_feed_= clock::now () + std::chrono::seconds (30);
      publish (profile_phase::error, result.diagnostic);
    }
  });
}
void profile_session::inspect_waiting_result () {
  if (paused_ || !profile_.enabled || !client_ || !client_->authorized () ||
      !decision_feed_ || !decision_feed_->caught_up () || !revisions_ || vaults_.empty ()) return;
  if (!waiting_object_.empty () && revisions_->heads (waiting_source_vault_, waiting_object_).size () < 2) {
    waiting_object_.clear (); waiting_source_vault_.clear (); status_.decision_diagnostic.clear ();
    if (changed_) changed_ (status_);
  }
  waiting_vault_%= vaults_.size ();
  const auto vault= vaults_[waiting_vault_];
  auto values= revisions_->conflicts (vault, waiting_cursor_, 1);
  if (values.empty ()) {
    waiting_cursor_.clear (); waiting_vault_= (waiting_vault_+1)%vaults_.size ();
    return;
  }
  const auto& value= values.front (); waiting_cursor_= value.object;
  auto heads= revisions_->heads (vault, value.object);
  auto secret= selected_secret (vault);
  if (!secret || heads.size () < 2 || heads.size () > 256) return;
  const auto tokens= derive_conflict_tokens (*secret, value.object, heads, heads.front ());
  if (decision_feed_->latest (tokens.vault, tokens.conflict)) {
    waiting_object_= value.object; waiting_source_vault_= vault;
    const auto diagnostic= "Accepted conflict result is awaiting content or application: " + value.representative_path;
    if (status_.decision_diagnostic != diagnostic) {
      status_.decision_diagnostic= diagnostic;
      if (changed_) changed_ (status_);
    }
  }
}
void profile_session::decide_next () {
  if (paused_ || !profile_.enabled || !client_ || !client_->authorized () || !revisions_ ||
      deciding_ || clock::now () < next_decision_) return;
  conflict_journal ();
  if (!decisions_) decisions_= std::make_unique<decision_client> (profile_.origin, profile_.pin,
                                                               profile_.device, profile_.member);
  auto pending= conflicts_->pending (proposal_cursor_, 1);
  if (!pending.empty ()) {
    proposal_cursor_= pending.front ();
    submit_proposal (proposal_cursor_, false); return;
  }
  proposal_cursor_.clear ();
  if (vaults_.empty ()) return;
  resolution_vault_%= vaults_.size ();
  auto vault= vaults_[resolution_vault_];
  auto secret= selected_secret (vault);
  auto incoming= secret ? revisions_->pending_resolutions (vault, resolution_cursor_, 1) : std::vector<revision> ();
  if (incoming.empty ()) {
    resolution_cursor_.clear ();
    resolution_vault_= (resolution_vault_ + 1) % vaults_.size ();
    if (resolution_vault_ == 0) next_decision_= clock::now () + std::chrono::seconds (15);
    return;
  }
  auto r= incoming.front ();
  resolution_cursor_= r.id;
  auto tokens= derive_conflict_tokens (*secret, r.object, r.parents, r.id);
  if (decision_feed_ && decision_feed_->caught_up ()) {
    auto accepted= decision_feed_->latest (tokens.vault, tokens.conflict);
    if (accepted) {
      if (revisions_->accept_resolution (r.id, profile_.pin, *secret, accepted->evidence)) {
        decision_feed_->acknowledge (tokens.vault, tokens.conflict, accepted->decision.version);
        status_.decision_diagnostic.clear ();
        if (changed_) changed_ (status_);
      }
      return;
    }
  }
  auto state= client_->current ();
  deciding_= true;
  decisions_->request (state->epoch, {tokens.vault, tokens.conflict, {}, {}, {}, 0},
    [this, vault] (const std::string& epoch) {
      return !paused_ && profile_.enabled && client_ && selected_secret (vault) &&
        client_->context_current (profile_.pin.group, profile_.pin.generation, epoch);
    }, [this, secret= *secret, id= r.id] (control_result result, std::optional<conflict_decision> decision) {
      deciding_= false;
      if (result.failure == control_failure::cancelled) return;
      if (result.failure != control_failure::none) {
        next_decision_= clock::now () + std::chrono::seconds (30);
        publish (profile_phase::error, result.diagnostic); return;
      }
      try {
        if (decision) revisions_->accept_resolution (id, profile_.pin, secret, decisions_->evidence ());
      }
      catch (const std::exception& e) { publish (profile_phase::error, e.what ()); }
    });
}
void profile_session::register_next_vault () {
  if (paused_ || !profile_.enabled || !client_ || !client_->authorized () ||
      registering_secret_ || clock::now () < next_secret_registration_) return;
  for (const auto& vault: vaults_) {
    if (registered_vaults_.count (vault)) continue;
    auto state= client_->current ();
    if (!state) return;
    if (!secret_registration_) secret_registration_= std::make_unique<vault_registration_client> (settings_, profile_);
    registering_secret_= true;
    secret_registration_->ensure (vault, state->epoch,
      [this] (const std::string& epoch) {
        return !paused_ && profile_.enabled && client_ &&
          client_->context_current (profile_.pin.group, profile_.pin.generation, epoch);
      }, [this, vault] (control_result result, std::optional<vault_secret_registration> registration) {
        registering_secret_= false;
        if (paused_ || result.failure == control_failure::cancelled) return;
        if (result.failure != control_failure::none) {
          next_secret_registration_= clock::now () + std::chrono::seconds (60);
          publish (profile_phase::error, result.diagnostic); return;
        }
        registered_vaults_.insert (vault);
        // Registration is not proof of possession. A competing device must
        // obtain the winner over the encrypted peer channel, never replace it.
        if (registration && !settings_.find_vault_secret (profile_.pin.group,
              profile_.pin.generation, vault, registration->commitment))
          publish (profile_phase::authorized, "Vault secret registered; encrypted peer delivery is required");
      });
    return;
  }
}
} // namespace athena::hodarium
