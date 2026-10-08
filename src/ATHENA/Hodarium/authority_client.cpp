/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#include "authority_client.hpp"
#include "control_http.hpp"
#include <QThread>
#include <QTimer>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <stdexcept>

namespace athena::hodarium {
namespace {
using json= nlohmann::json;
QByteArray encode (const json& value) { return QByteArray::fromStdString (value.dump ()); }
}
authority_client::authority_client (QUrl origin, authority_pin pin,
  device_identity device, std::string member, const std::filesystem::path& database):
  origin_ (std::move (origin)), pin_ (std::move (pin)), device_ (std::move (device)),
  member_ (std::move (member)), store_ (database, pin_),
  http_ (new control_http (origin_, this)), refresh_timer_ (new QTimer (this)) {
  state_= store_.current ();
  index_members ();
  // Cached state is useful for status, never for authorizing a restarted client.
  refresh_timer_->setSingleShot (true);
  connect (refresh_timer_, &QTimer::timeout, this, [this] {
    ++operation_;
    http_->cancel ();
    finish ({control_failure::transport, "Hodarium authority validation timed out"});
  });
}
authority_client::~authority_client () {
  ++operation_;
  http_->cancel ();
}
void authority_client::owner () const {
  if (QThread::currentThread () != thread ())
    throw std::logic_error ("Hodarium authority client accessed outside its owning worker");
}
void authority_client::finish (control_result result) {
  refresh_timer_->stop ();
  auto callback= std::move (completion_);
  completion_= {};
  if (callback) callback (std::move (result));
}
void authority_client::post (const QString& path, const QByteArray& body,
  std::function<void (QByteArray)> success) {
  const auto operation= operation_;
  http_->request (path, body,
    [this, operation, success= std::move (success)] (control_response response) mutable {
      if (operation != operation_) return;
      int status= response.status;
      if (status == 401 || status == 403) {
        lease_.revoke ();
        finish ({control_failure::denied, "Hodarium authority denied device validation"});
        return;
      }
      if (response.oversized) {
        lease_.revoke ();
        finish ({control_failure::invalid_state, "Hodarium control response exceeds budget"});
        return;
      }
      if (!response.error.empty ()) {
        finish ({control_failure::transport, response.error});
        return;
      }
      try { success (std::move (response.body)); }
      catch (const key_store_error& e) {
        lease_.revoke ();
        finish ({control_failure::key_store, e.what ()});
      }
      catch (const std::exception& e) {
        lease_.revoke ();
        finish ({control_failure::invalid_state, e.what ()});
      }
    });
}
void authority_client::refresh (completion completed) {
  owner ();
  if (completion_) throw std::logic_error ("Hodarium validation is already pending");
  if (!completed) throw std::invalid_argument ("Hodarium validation requires completion");
  completion_= std::move (completed); ++operation_;
  sent_= membership_lease::steady::now (); sent_wall_= membership_lease::wall::now ();
  refresh_timer_->start (30000);
  post ("/api/device/challenge", encode ({{"purpose", "control"}, {"subject", member_}}),
    [this] (QByteArray bytes) {
      auto challenge= json::parse (bytes.constData (), bytes.constData () + bytes.size ());
      std::string nonce= challenge.at ("challenge");
      auto proof= sign_device_proof (device_, pin_.group, "control", member_, nonce);
      post ("/api/device/validate", encode ({{"member", member_},
        {"challenge", nonce}, {"signature", proof}}), [this, nonce] (QByteArray bytes) {
        auto response= bytes.toStdString ();
        auto validated= verify_validation (pin_, response, nonce, member_, device_.public_key);
        auto envelope= json::parse (response).at ("state").dump ();
        state_= store_.accept (envelope);
        index_members ();
        lease_.renew (validated.lifetime, sent_, sent_wall_);
        if (!authorized ()) {
          finish ({control_failure::invalid_state, "Hodarium validation lease is no longer fresh"});
          return;
        }
        finish ({});
      });
    });
}
void authority_client::suspend () {
  owner (); lease_.revoke (); ++operation_;
  http_->cancel ();
  finish ({control_failure::cancelled, "Hodarium validation suspended"});
}
bool authority_client::authorized () {
  owner ();
  return lease_.valid (membership_lease::steady::now (), membership_lease::wall::now ());
}
bool authority_client::peer_allowed (const std::string& member,
  const std::string& public_key, const std::string& epoch) {
  owner ();
  if (!authorized () || !state_ || state_->epoch != epoch) return false;
  auto found= members_.find (member);
  return found != members_.end () && found->second == public_key;
}
void authority_client::index_members () {
  std::unordered_map<std::string, std::string> next;
  if (state_) for (const auto& member: state_->members) next.emplace (member.id, member.public_key);
  members_.swap (next);
}
bool authority_client::context_current (const std::string& group,
  const std::string& generation, const std::string& epoch) {
  owner ();
  return authorized () && state_ && state_->group == group &&
    state_->generation == generation && state_->epoch == epoch;
}
std::optional<membership_state> authority_client::current () const {
  owner (); return state_;
}
} // namespace athena::hodarium
