/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#include "rendezvous.hpp"
#include "control_http.hpp"
#include <QCryptographicHash>
#include <QHostAddress>
#include <QThread>
#include <QTimer>
#include <QDateTime>
#include <nlohmann/json.hpp>
#include <sodium.h>
#include <set>
#include <algorithm>

namespace athena::hodarium {
namespace {
using json= nlohmann::json;
void identifier (const std::string& text) {
  unsigned char decoded[32]; std::size_t size= 0;
  if (text.size () != 43 || sodium_base642bin (decoded, sizeof decoded,
      text.data (), text.size (), nullptr, &size, nullptr,
      sodium_base64_VARIANT_URLSAFE_NO_PADDING) != 0 || size != sizeof decoded)
    throw std::invalid_argument ("Invalid Hodarium presence identity");
}
json parse (const QByteArray& bytes) {
  if (bytes.size () > 256*1024)
    throw std::invalid_argument ("Hodarium presence response exceeds budget");
  std::vector<std::set<std::string>> keys;
  return json::parse (bytes.constData (), bytes.constData () + bytes.size (),
    [&] (int depth, json::parse_event_t event, json& value) {
      if (depth > 8) throw std::invalid_argument ("Hodarium presence exceeds depth budget");
      if (event == json::parse_event_t::object_start) keys.emplace_back ();
      else if (event == json::parse_event_t::object_end) keys.pop_back ();
      else if (event == json::parse_event_t::key &&
          !keys.back ().insert (value.get<std::string> ()).second)
        throw std::invalid_argument ("Duplicate Hodarium presence field");
      return true;
    });
}
QByteArray encode (const json& value) { return QByteArray::fromStdString (value.dump ()); }
std::string base64 (const QByteArray& bytes) {
  return bytes.toBase64 (QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals).toStdString ();
}
void routes (const std::vector<std::string>& direct, const std::vector<std::string>& relays) {
  if (direct.size () > 8 || relays.size () > 8)
    throw std::invalid_argument ("Hodarium presence route count exceeds budget");
  for (const auto& text: direct) {
    auto address= QString::fromStdString (text);
    int colon= address.lastIndexOf (':');
    auto host= address.left (colon);
    if (host.startsWith ('[') && host.endsWith (']')) host= host.mid (1, host.size () - 2);
    else if (host.contains (':')) throw std::invalid_argument ("IPv6 presence address needs brackets");
    bool valid= false;
    auto port= address.mid (colon + 1).toUInt (&valid);
    QHostAddress ip (host);
    if (text.size () > 80 || colon < 1 || !valid || port == 0 || port > 65535 ||
        ip.isNull () || !ip.isGlobal () || ip.isLoopback () || !ip.scopeId ().isEmpty ())
      throw std::invalid_argument ("Invalid Hodarium direct address");
  }
  for (const auto& text: relays) {
    if (text.size () > 256) throw std::invalid_argument ("Hodarium relay origin exceeds budget");
    validate_authority_origin (QUrl (QString::fromStdString (text), QUrl::StrictMode));
  }
}
}
rendezvous_client::rendezvous_client (QUrl origin, device_identity device):
  device_ (std::move (device)), http_ (new control_http (std::move (origin), this)),
  deadline_ (new QTimer (this)) {
  deadline_->setSingleShot (true);
  connect (deadline_, &QTimer::timeout, this, [this] {
    http_->cancel ();
    finish ({control_failure::transport, "Hodarium rendezvous timed out"});
  });
}
rendezvous_client::~rendezvous_client () { http_->cancel (); }
void rendezvous_client::finish (control_result result, presence_page page) {
  deadline_->stop ();
  auto callback= std::move (completion_); completion_= {}; authorized_= {};
  if (callback) callback (std::move (result), std::move (page));
}
void rendezvous_client::cancel () {
  if (QThread::currentThread () != thread ())
    throw std::logic_error ("Hodarium rendezvous accessed outside its owner");
  http_->cancel ();
  finish ({control_failure::cancelled, "Hodarium rendezvous cancelled"});
}
void rendezvous_client::post (const QString& path, const QByteArray& bytes,
  std::function<void (QByteArray)> success) {
  if (!authorized_ (context_)) {
    finish ({control_failure::denied, "Hodarium rendezvous membership expired or changed"}); return;
  }
  http_->request (path, bytes, [this, success= std::move (success)] (control_response response) {
    if (!response.error.empty ()) {
      auto failure= response.status == 401 || response.status == 403 ? control_failure::denied :
        response.status == 409 || response.oversized ? control_failure::invalid_state : control_failure::transport;
      finish ({failure, std::move (response.error)}); return;
    }
    try {
      if (!authorized_ (context_)) {
        finish ({control_failure::denied, "Hodarium rendezvous membership expired or changed"}); return;
      }
      success (std::move (response.body));
    }
    catch (const key_store_error& e) { finish ({control_failure::key_store, e.what ()}); }
    catch (const std::exception& e) { finish ({control_failure::invalid_state, e.what ()}); }
  });
}
void rendezvous_client::request (presence_context context, presence_request request,
  std::function<bool (const presence_context&)> authorized, completion completed) {
  if (QThread::currentThread () != thread () || completion_)
    throw std::logic_error ("Hodarium rendezvous owner or operation state violation");
  if (!authorized || !completed) throw std::invalid_argument ("Rendezvous requires owner callbacks");
  identifier (context.group); identifier (context.member);
  identifier (context.generation); identifier (context.epoch);
  if (!request.after.empty ()) identifier (request.after);
  routes (request.direct, request.relays);
  if ((request.operation != presence_operation::publish && (!request.direct.empty () || !request.relays.empty ())) ||
      (request.operation != presence_operation::list && !request.after.empty ()))
    throw std::invalid_argument ("Unexpected Hodarium presence request fields");
  const char* operation= request.operation == presence_operation::publish ? "publish" :
    request.operation == presence_operation::list ? "list" : "withdraw";
  auto payload= encode ({{"operation", operation}, {"member", context.member},
    {"generation", context.generation}, {"epoch", context.epoch},
    {"direct", request.direct}, {"relays", request.relays}, {"after", request.after}});
  auto subject= base64 (QCryptographicHash::hash (payload, QCryptographicHash::Sha256));
  context_= std::move (context); request_= std::move (request);
  authorized_= std::move (authorized); completion_= std::move (completed);
  deadline_->start (30000);
  post ("/api/device/challenge", encode ({{"purpose", "rendezvous"}, {"subject", subject}}),
    [this, payload, subject] (QByteArray bytes) {
      std::string nonce= parse (bytes).at ("challenge");
      auto signature= sign_device_proof (device_, context_.group, "rendezvous", subject, nonce);
      post ("/api/device/rendezvous", encode ({{"payload", base64 (payload)},
        {"challenge", nonce}, {"signature", signature}}), [this] (QByteArray bytes) {
        auto value= parse (bytes);
        if (value.at ("generation") != context_.generation || value.at ("epoch") != context_.epoch)
          throw std::invalid_argument ("Hodarium presence context changed");
        const auto& entries= value.at ("entries");
        if (!entries.is_array () || entries.size () > 64)
          throw std::invalid_argument ("Invalid Hodarium presence page");
        presence_page page;
        auto previous= request_.after;
        for (const auto& entry: entries) {
          if (!entry.at ("expires").is_number_integer ())
            throw std::invalid_argument ("Invalid Hodarium presence expiration");
          peer_presence peer{entry.at ("member"), entry.at ("direct"), entry.at ("relays"), entry.at ("expires")};
          identifier (peer.member); routes (peer.direct, peer.relays);
          if (peer.expires <= 0 || peer.member <= previous || peer.member == context_.member ||
              entry.at ("generation") != context_.generation || entry.at ("epoch") != context_.epoch)
            throw std::invalid_argument ("Invalid Hodarium presence entry order or context");
          previous= peer.member; page.entries.push_back (std::move (peer));
        }
        page.next= value.value ("next", "");
        if ((!page.next.empty () && (page.entries.size () != 64 || page.next != previous)) ||
            (request_.operation != presence_operation::list && (!page.entries.empty () || !page.next.empty ())))
          throw std::invalid_argument ("Invalid Hodarium presence cursor");
        finish ({}, std::move (page));
      });
    });
}
presence_directory::presence_directory (QUrl origin, device_identity device,
  authorization authorized, observer changed): client_ (std::move (origin), std::move (device)),
  authorized_ (std::move (authorized)), changed_ (std::move (changed)), timer_ (new QTimer (this)) {
  if (!authorized_) throw std::invalid_argument ("Presence directory requires membership authorization");
  publication_.operation= presence_operation::publish;
  timer_->setInterval (1000);
  connect (timer_, &QTimer::timeout, this, [this] { tick (); });
}
presence_directory::~presence_directory () { changed_= {}; stop (); }
void presence_directory::notify () { if (changed_) changed_ (snapshot_); }
presence_snapshot presence_directory::snapshot () const { return snapshot_; }
void presence_directory::stop () {
  active_= false; ++serial_; timer_->stop (); client_.cancel (); busy_= false;
  collecting_.clear ();
  bool changed= !snapshot_.peers.empty () || !snapshot_.diagnostic.empty ();
  snapshot_= {};
  if (changed) notify ();
}
void presence_directory::start (presence_context context) {
  if (active_ && context.group == context_.group && context.member == context_.member &&
      context.generation == context_.generation && context.epoch == context_.epoch) return;
  stop (); context_= std::move (context);
  if (!authorized_ (context_)) return;
  active_= true; next_= {}; timer_->start ();
  QTimer::singleShot (0, this, [this] { tick (); });
}
void presence_directory::set_routes (std::vector<std::string> direct, std::vector<std::string> relays) {
  routes (direct, relays);
  if (publication_.direct == direct && publication_.relays == relays) return;
  publication_.direct= std::move (direct); publication_.relays= std::move (relays);
  ++serial_; client_.cancel (); busy_= false; collecting_.clear (); next_= {};
}
void presence_directory::failed (control_result result) {
  busy_= false; collecting_.clear (); next_= clock::now () + std::chrono::seconds (30);
  auto previous= snapshot_.peers.size ();
  if (result.failure == control_failure::denied || result.failure == control_failure::invalid_state)
    snapshot_.peers.clear ();
  if (snapshot_.diagnostic != result.diagnostic || previous != snapshot_.peers.size ()) {
    snapshot_.diagnostic= std::move (result.diagnostic); notify ();
  }
}
void presence_directory::tick () {
  if (!active_) return;
  if (!authorized_ (context_)) { stop (); return; }
  auto now= clock::now ();
  auto wall= QDateTime::currentSecsSinceEpoch ();
  auto previous= snapshot_.peers.size ();
  auto& peers= snapshot_.peers;
  peers.erase (std::remove_if (peers.begin (), peers.end (), [&] (const peer_presence& peer) {
    return peer.expires <= wall || now - received_ >= std::chrono::seconds (90);
  }), peers.end ());
  if (previous != peers.size ()) notify ();
  if (busy_ && now - cycle_started_ >= std::chrono::seconds (60)) {
    ++serial_; client_.cancel ();
    failed ({control_failure::transport, "Hodarium presence scan timed out"});
  }
  if (busy_ || now < next_) return;
  busy_= true; cycle_started_= now; collecting_.clear ();
  auto serial= ++serial_;
  client_.request (context_, publication_, authorized_,
    [this, serial] (control_result result, presence_page) {
      if (!active_ || serial != serial_) return;
      if (result.failure != control_failure::none) { failed (std::move (result)); return; }
      page ({});
    });
}
void presence_directory::page (std::string after) {
  presence_request query; query.after= std::move (after);
  auto serial= serial_;
  client_.request (context_, std::move (query), authorized_,
    [this, serial] (control_result result, presence_page page) {
      if (!active_ || serial != serial_) return;
      if (result.failure != control_failure::none) { failed (std::move (result)); return; }
      if (collecting_.size () + page.entries.size () > 4096 ||
          (collecting_.size () + page.entries.size () == 4096 && !page.next.empty ())) {
        failed ({control_failure::invalid_state, "Hodarium presence exceeds membership budget"}); return;
      }
      for (auto& peer: page.entries) collecting_.push_back (std::move (peer));
      if (!page.next.empty ()) { this->page (std::move (page.next)); return; }
      auto wall= QDateTime::currentSecsSinceEpoch ();
      collecting_.erase (std::remove_if (collecting_.begin (), collecting_.end (),
        [wall] (const peer_presence& peer) { return peer.expires <= wall; }), collecting_.end ());
      snapshot_.peers= std::move (collecting_); snapshot_.diagnostic.clear ();
      received_= cycle_started_; busy_= false;
      next_= clock::now () + std::chrono::seconds (30);
      notify ();
    });
}
} // namespace athena::hodarium
