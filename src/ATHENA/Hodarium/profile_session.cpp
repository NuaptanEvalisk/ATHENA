/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#include "profile_session.hpp"
#include <QThread>
#include <stdexcept>

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
    if (result.failure == control_failure::none) publish (profile_phase::authorized);
    else if (result.failure == control_failure::denied) publish (profile_phase::denied, result.diagnostic);
    else if (result.failure == control_failure::transport && client_->authorized ())
      publish (profile_phase::offline_valid, result.diagnostic);
    else publish (profile_phase::error, result.diagnostic);
  });
}
void profile_session::tick () {
  if (paused_) return;
  try {
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
                                  profile_.device.public_key, status.member);
    profile_.member= status.member;
    settings_.set_enabled (profile_.pin.group, true); profile_.enabled= true;
    // Run outside the enrollment completion stack before changing controller state.
    QTimer::singleShot (0, this, [this] { if (!paused_) start (); });
  }
  catch (const std::exception& e) { publish (profile_phase::error, e.what ()); }
}
void profile_session::suspend () {
  owner (); paused_= true; timer_.stop ();
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
profile_status profile_session::status () const { owner (); return status_; }
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
} // namespace athena::hodarium
