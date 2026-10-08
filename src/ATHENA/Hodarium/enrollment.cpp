/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#include "enrollment.hpp"
#include "control_http.hpp"
#include <QThread>
#include <nlohmann/json.hpp>
#include <sodium.h>
#include <stdexcept>

namespace athena::hodarium {
namespace {
using json= nlohmann::json;
QByteArray encode (const json& value) { return QByteArray::fromStdString (value.dump ()); }
json parse (const QByteArray& bytes) {
  return json::parse (bytes.constData (), bytes.constData () + bytes.size (),
    [] (int depth, json::parse_event_t, json&) {
      if (depth > 16) throw std::invalid_argument ("Hodarium response exceeds depth budget");
      return true;
    });
}
}
enrollment::enrollment (QUrl origin, authority_pin pin, device_identity device):
  pin_ (std::move (pin)), device_ (std::move (device)),
  http_ (new control_http (std::move (origin), this)) {}
enrollment::~enrollment () { http_->cancel (); clear_credential (); }
void enrollment::clear_credential () {
  if (!credential_.empty ()) sodium_memzero (credential_.data (), credential_.size ());
  credential_.clear ();
}
void enrollment::begin (completion completed) {
  if (QThread::currentThread () != thread () || completion_)
    throw std::logic_error ("Hodarium enrollment owner or operation state violation");
  if (!completed) throw std::invalid_argument ("Enrollment requires completion");
  completion_= std::move (completed);
}
void enrollment::finish (control_result result) {
  auto callback= std::move (completion_); completion_= {};
  if (callback) callback (std::move (result), status_);
}
void enrollment::cancel () {
  if (QThread::currentThread () != thread ())
    throw std::logic_error ("Hodarium enrollment accessed outside its owner");
  http_->cancel ();
  finish ({control_failure::cancelled, "Hodarium enrollment suspended"});
}
void enrollment::request (const QString& path, const QByteArray& body,
  std::function<void (QByteArray)> success) {
  http_->request (path, body, [this, success= std::move (success)] (control_response r) {
    if (!r.error.empty ()) {
      auto failure= (r.status == 401 || r.status == 403) ? control_failure::denied :
        r.oversized ? control_failure::invalid_state : control_failure::transport;
      finish ({failure, std::move (r.error)}); return;
    }
    try { success (std::move (r.body)); }
    catch (const key_store_error& e) { finish ({control_failure::key_store, e.what ()}); }
    catch (const std::exception& e) { finish ({control_failure::invalid_state, e.what ()}); }
  });
}
void enrollment::proof (const std::string& purpose, const std::string& subject,
  std::function<void (std::string, std::string)> success) {
  request ("/api/device/challenge", encode ({{"purpose", purpose}, {"subject", subject}}),
    [this, purpose, subject, success= std::move (success)] (QByteArray bytes) {
      std::string nonce= parse (bytes).at ("challenge");
      auto signature= sign_device_proof (device_, pin_.group, purpose, subject, nonce);
      success (nonce, signature);
    });
}
void enrollment::join (const std::string& name, completion completed) {
  if (name.empty () || name.size () > 128)
    throw std::invalid_argument ("Hodarium device name must be 1-128 UTF-8 bytes");
  begin (std::move (completed));
  proof ("join", device_.public_key, [this, name] (std::string nonce, std::string signature) {
    request ("/api/device/join", encode ({{"name", name}, {"public_key", device_.public_key},
      {"challenge", nonce}, {"signature", signature}}),
      [this] (QByteArray bytes) { admission (bytes, true); });
  });
}
void enrollment::poll (completion completed) {
  if (status_.request.empty () || credential_.empty ())
    throw std::logic_error ("No pending Hodarium admission to poll");
  begin (std::move (completed));
  proof ("poll", status_.request, [this] (std::string nonce, std::string signature) {
    request ("/api/device/poll", encode ({{"id", status_.request},
      {"retrieval_credential", credential_}, {"challenge", nonce}, {"signature", signature}}),
      [this] (QByteArray bytes) { admission (bytes, false); });
  });
}
void enrollment::admission (const QByteArray& bytes, bool initial) {
  auto response= parse (bytes);
  const auto& pending= response.at ("request");
  std::string id= pending.at ("id");
  if (pending.at ("public_key") != device_.public_key || id.size () != 43 ||
      (!initial && id != status_.request))
    throw std::invalid_argument ("Hodarium admission does not match this device request");
  enrollment_status next;
  next.request= id; next.member= pending.value ("member_id", "");
  next.request_expires= pending.at ("expires");
  next.code= response.value ("code", "");
  next.code_expires= response.value ("code_expires", std::int64_t{0});
  if (initial) {
    std::string credential= response.at ("retrieval_credential");
    if (credential.size () != 43) throw std::invalid_argument ("Invalid Hodarium retrieval credential");
    clear_credential (); credential_= std::move (credential);
  }
  if (!next.member.empty ()) {
    if (next.member.size () != 43) throw std::invalid_argument ("Invalid Hodarium member identity");
    clear_credential ();
  }
  status_= std::move (next);
  finish ();
}
void enrollment::resolve (completion completed) {
  begin (std::move (completed));
  proof ("resolve", device_.public_key, [this] (std::string nonce, std::string signature) {
    request ("/api/device/resolve", encode ({{"public_key", device_.public_key},
      {"challenge", nonce}, {"signature", signature}}), [this] (QByteArray bytes) {
      auto member= parse (bytes);
      std::string id= member.at ("id");
      if (id.size () != 43 || member.at ("public_key") != device_.public_key)
        throw std::invalid_argument ("Recovered Hodarium identity does not match this device");
      status_= {}; status_.member= std::move (id); clear_credential ();
      finish ();
    });
  });
}
} // namespace athena::hodarium
