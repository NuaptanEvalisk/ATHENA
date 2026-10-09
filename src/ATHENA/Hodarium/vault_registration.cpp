/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#include "vault_registration.hpp"
#include "control_http.hpp"
#include <QCryptographicHash>
#include <QThread>
#include <QTimer>
#include <QUuid>
#include <nlohmann/json.hpp>

namespace athena::hodarium {
namespace {
using json= nlohmann::json;
QByteArray encode (const json& value) { return QByteArray::fromStdString (value.dump ()); }
std::string base64 (const QByteArray& bytes) {
  return bytes.toBase64 (QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals).toStdString ();
}
std::string subject (const QByteArray& bytes) {
  return base64 (QCryptographicHash::hash (bytes, QCryptographicHash::Sha256));
}
}
std::string vault_registration_slot (const std::string& group, const std::string& vault) {
  auto id= QString::fromStdString (vault);
  if (QUuid (id).isNull () || QUuid (id).toString (QUuid::WithoutBraces) != id)
    throw std::invalid_argument ("Hodarium Vault registration requires a canonical UUID");
  return subject (encode (json::array ({"ATHENA-HODARIUM-VAULT-SLOT-v1", group, vault})));
}
vault_registration_client::vault_registration_client (client_settings& settings, client_profile profile):
  settings_ (settings), profile_ (std::move (profile)),
  http_ (new control_http (profile_.origin, this)), deadline_ (new QTimer (this)) {
  deadline_->setSingleShot (true);
  connect (deadline_, &QTimer::timeout, this, [this] {
    http_->cancel ();
    finish ({control_failure::transport, "Hodarium Vault registration timed out; retry will query the same slot"});
  });
}
vault_registration_client::~vault_registration_client () { http_->cancel (); }
void vault_registration_client::finish (control_result result, std::optional<vault_secret_registration> value) {
  deadline_->stop ();
  auto callback= std::move (done_); done_= {}; allowed_= {};
  if (callback) callback (std::move (result), std::move (value));
}
void vault_registration_client::cancel () {
  if (QThread::currentThread () != thread ()) throw std::logic_error ("Vault registration owner violation");
  http_->cancel ();
  finish ({control_failure::cancelled, "Hodarium Vault registration cancelled"});
}
void vault_registration_client::post (const QString& path, const QByteArray& bytes,
  std::function<void (QByteArray)> completed) {
  if (!allowed_ (epoch_)) { finish ({control_failure::denied, "Vault registration membership changed"}); return; }
  http_->request (path, bytes, [this, completed= std::move (completed)] (control_response response) {
    if (!response.error.empty ()) {
      auto failure= response.status == 401 || response.status == 403 ? control_failure::denied :
        response.status == 409 || response.oversized ? control_failure::invalid_state : control_failure::transport;
      finish ({failure, std::move (response.error)}); return;
    }
    try {
      if (!allowed_ (epoch_)) { finish ({control_failure::denied, "Vault registration membership changed"}); return; }
      completed (std::move (response.body));
    }
    catch (const key_store_error& e) { finish ({control_failure::key_store, e.what ()}); }
    catch (const std::exception& e) { finish ({control_failure::invalid_state, e.what ()}); }
  });
}
void vault_registration_client::ensure (std::string vault, std::string epoch,
  authorization allowed, completion done) {
  if (QThread::currentThread () != thread () || done_) throw std::logic_error ("Vault registration owner or operation violation");
  if (!allowed || !done) throw std::invalid_argument ("Vault registration requires owner callbacks");
  slot_= vault_registration_slot (profile_.pin.group, vault);
  vault_= std::move (vault); epoch_= std::move (epoch);
  allowed_= std::move (allowed); done_= std::move (done);
  deadline_->start (60000);
  request ({});
}
void vault_registration_client::request (const std::string& commitment) {
  json value{{"operation", commitment.empty () ? "get" : "register"}, {"member", profile_.member},
    {"generation", profile_.pin.generation}, {"epoch", epoch_}, {"slot", slot_}};
  if (!commitment.empty ()) value["commitment"]= commitment;
  auto payload= encode (value); auto digest= subject (payload);
  post ("/api/device/challenge", encode ({{"purpose", "vault-secret"}, {"subject", digest}}),
    [this, payload, digest, writing= !commitment.empty ()] (QByteArray bytes) {
      if (bytes.size () > 1024) throw std::invalid_argument ("Vault registration challenge exceeds budget");
      auto challenge= json::parse (bytes.constData (), bytes.constData () + bytes.size ());
      std::string nonce= challenge.at ("challenge");
      auto signature= sign_device_proof (profile_.device, profile_.pin.group, "vault-secret", digest, nonce);
      post ("/api/device/vault-secret", encode ({{"payload", base64 (payload)}, {"challenge", nonce}, {"signature", signature}}),
        [this, nonce, digest, writing] (QByteArray bytes) {
          auto result= settings_.accept_vault_registration (profile_.pin.group, vault_, bytes.toStdString (), epoch_, nonce, digest);
          if (result) { finish ({}, std::move (result)); return; }
          if (writing) throw std::invalid_argument ("Authority did not retain Vault registration");
          auto candidate= settings_.vault_secret_candidate (profile_.pin.group, profile_.pin.generation, vault_);
          if (!candidate) {
            candidate= create_vault_secret (profile_.pin.group, vault_);
            settings_.remember_vault_secret (profile_.pin.generation, *candidate);
          }
          else verify_vault_secret (*candidate);
          request (candidate->commitment);
        });
    });
}
} // namespace athena::hodarium
