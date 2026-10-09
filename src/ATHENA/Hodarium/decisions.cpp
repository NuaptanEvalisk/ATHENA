/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#include "decisions.hpp"
#include "control_http.hpp"
#include <QCryptographicHash>
#include <QThread>
#include <QTimer>
#include <nlohmann/json.hpp>
#include <limits>

namespace athena::hodarium {
namespace {
using json= nlohmann::json;
std::string base64 (const QByteArray& bytes) {
  return bytes.toBase64 (QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals).toStdString ();
}
void identifier (const std::string& value) {
  auto text= QByteArray::fromStdString (value);
  auto bytes= QByteArray::fromBase64 (text, QByteArray::Base64UrlEncoding | QByteArray::AbortOnBase64DecodingErrors);
  if (value.size () != 43 || bytes.size () != 32 || base64 (bytes) != value)
    throw std::invalid_argument ("Invalid Hodarium decision identity");
}
QByteArray encode (const json& value) { return QByteArray::fromStdString (value.dump ()); }
}
decision_client::decision_client (QUrl origin, authority_pin pin, device_identity device,
  std::string member): pin_ (std::move (pin)), device_ (std::move (device)),
  member_ (std::move (member)), http_ (new control_http (std::move (origin), this)),
  deadline_ (new QTimer (this)) {
  identifier (pin_.group); identifier (pin_.generation); identifier (pin_.public_key); identifier (member_);
  deadline_->setSingleShot (true);
  connect (deadline_, &QTimer::timeout, this, [this] {
    http_->cancel ();
    finish ({control_failure::transport, "Hodarium decision timed out; query or retry the same operation"});
  });
}
decision_client::~decision_client () { http_->cancel (); }
void decision_client::finish (control_result result, std::optional<conflict_decision> decision) {
  deadline_->stop ();
  auto callback= std::move (completed_); completed_= {}; authorized_= {};
  if (callback) callback (std::move (result), std::move (decision));
}
void decision_client::cancel () {
  if (QThread::currentThread () != thread ())
    throw std::logic_error ("Hodarium decision client accessed outside its owner");
  http_->cancel ();
  finish ({control_failure::cancelled, "Hodarium decision cancelled; submitted decisions may have committed"});
}
void decision_client::post (const QString& path, const QByteArray& bytes,
  std::function<void (QByteArray)> success) {
  if (!authorized_ (epoch_)) {
    finish ({control_failure::denied, "Hodarium decision membership expired or changed"}); return;
  }
  http_->request (path, bytes, [this, success= std::move (success)] (control_response response) {
    if (!response.error.empty ()) {
      auto failure= response.status == 401 || response.status == 403 ? control_failure::denied :
        response.status == 409 || response.oversized ? control_failure::invalid_state : control_failure::transport;
      finish ({failure, std::move (response.error)}); return;
    }
    try {
      if (!authorized_ (epoch_)) {
        finish ({control_failure::denied, "Hodarium decision membership expired or changed"}); return;
      }
      success (std::move (response.body));
    }
    catch (const key_store_error& e) { finish ({control_failure::key_store, e.what ()}); }
    catch (const std::exception& e) { finish ({control_failure::invalid_state, e.what ()}); }
  });
}
void decision_client::request (std::string epoch, decision_request request,
  authorization authorized, completion completed) {
  if (QThread::currentThread () != thread () || completed_)
    throw std::logic_error ("Hodarium decision owner or operation state violation");
  if (!authorized || !completed) throw std::invalid_argument ("Decision requires owner callbacks");
  identifier (epoch); identifier (request.vault); identifier (request.conflict);
  const bool write= !request.request_id.empty ();
  if (write) {
    identifier (request.request_id); identifier (request.branches); identifier (request.resolution);
    if (request.expected < 0 || request.expected == std::numeric_limits<std::int64_t>::max ())
      throw std::invalid_argument ("Invalid Hodarium expected decision version");
  }
  else if (request.expected != 0 || !request.branches.empty () || !request.resolution.empty ())
    throw std::invalid_argument ("Unexpected Hodarium decision read fields");
  json value{{"operation", write ? "decide" : "get"}, {"member", member_},
    {"generation", pin_.generation}, {"epoch", epoch}, {"vault", request.vault},
    {"conflict", request.conflict}, {"expected", request.expected}};
  if (write) {
    value["request_id"]= request.request_id; value["branches"]= request.branches;
    value["resolution"]= request.resolution;
  }
  auto payload= encode (value);
  auto subject= base64 (QCryptographicHash::hash (payload, QCryptographicHash::Sha256));
  epoch_= std::move (epoch); request_= std::move (request);
  authorized_= std::move (authorized); completed_= std::move (completed);
  deadline_->start (30000);
  post ("/api/device/challenge", encode ({{"purpose", "decision"}, {"subject", subject}}),
    [this, payload, subject] (QByteArray bytes) {
      if (bytes.size () > 1024) throw std::invalid_argument ("Decision challenge exceeds budget");
      auto challenge= json::parse (bytes.constData (), bytes.constData () + bytes.size ());
      std::string nonce= challenge.at ("challenge"); identifier (nonce);
      auto signature= sign_device_proof (device_, pin_.group, "decision", subject, nonce);
      post ("/api/device/decision", encode ({{"payload", base64 (payload)},
        {"challenge", nonce}, {"signature", signature}}), [this, nonce, subject] (QByteArray bytes) {
        auto result= verify_decision (pin_, bytes.toStdString (), epoch_, nonce, subject,
                                      request_.vault, request_.conflict);
        if (!request_.request_id.empty () && (!result || result->version <= request_.expected ||
            result->branches != request_.branches ||
            (result->version == request_.expected+1 && (result->request != request_.request_id ||
              result->resolution != request_.resolution || result->member != member_))))
          throw std::invalid_argument ("Hodarium decision receipt does not acknowledge the operation");
        finish ({}, std::move (result));
      });
    });
}
} // namespace athena::hodarium
