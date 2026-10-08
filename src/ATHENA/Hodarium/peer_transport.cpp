/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#include "peer_transport.hpp"
#include "control_http.hpp"
#include <QTcpSocket>
#include <QThread>
#include <QWebSocket>
#include <QWebSocketHandshakeOptions>
#include <QNetworkRequest>
#include <QSslConfiguration>
#include <QSslSocket>
#include <QDateTime>
#include <nlohmann/json.hpp>
#include <sodium.h>
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <stdexcept>

namespace athena::hodarium {
namespace {
constexpr qint64 budget= 256*1024;
class tcp_transport final: public peer_transport {
  QTcpSocket* socket_;
public:
  explicit tcp_transport (QTcpSocket* socket): socket_ (socket) {
    if (!socket || socket->thread () != thread () || QThread::currentThread () != thread ())
      throw std::invalid_argument ("TCP transport socket is not owned by this worker");
    socket_->setParent (this); socket_->setReadBufferSize (budget);
    auto notify= [this] { if (activity) activity (); };
    connect (socket_, &QTcpSocket::connected, this, notify);
    connect (socket_, &QTcpSocket::readyRead, this, notify);
    connect (socket_, &QTcpSocket::bytesWritten, this, notify);
    connect (socket_, &QTcpSocket::disconnected, this, notify);
    connect (socket_, &QTcpSocket::errorOccurred, this, [this] (QAbstractSocket::SocketError error) {
      if (error != QAbstractSocket::RemoteHostClosedError && failed) failed (socket_->errorString ().toStdString ());
    });
  }
  transport_state state () const override {
    if (socket_->state () == QAbstractSocket::ConnectedState) return transport_state::connected;
    if (socket_->state () == QAbstractSocket::UnconnectedState) return transport_state::closed;
    return transport_state::connecting;
  }
  ssize_t read (void* output, std::size_t size) noexcept override {
    if (!socket_->bytesAvailable ()) {
      if (state () == transport_state::closed) return 0;
      errno= EAGAIN; return -1;
    }
    auto n= socket_->read (static_cast<char*> (output), qint64 (size));
    if (n < 0) errno= EIO;
    return n;
  }
  ssize_t write (const void* input, std::size_t size) noexcept override {
    auto remaining= budget - socket_->bytesToWrite ();
    if (state () != transport_state::connected || remaining <= 0) { errno= EAGAIN; return -1; }
    auto n= socket_->write (static_cast<const char*> (input), qint64 (std::min (size, std::size_t (remaining))));
    if (n < 0) errno= EIO;
    return n;
  }
  void abort () override { socket_->abort (); }
};
void capability (const std::string& value) {
  unsigned char bytes[32]; std::size_t count= 0;
  if (value.size () != 43 || sodium_base642bin (bytes, sizeof bytes, value.data (), value.size (),
      nullptr, &count, nullptr, sodium_base64_VARIANT_URLSAFE_NO_PADDING) != 0 || count != 32)
    throw std::invalid_argument ("Invalid relay capability");
}
class relay_transport final: public peer_transport {
  QWebSocket socket_;
  QByteArray incoming_;
  bool stopped_= false, connected_= false;
  void reject (const std::string& reason) {
    if (stopped_) return;
    stopped_= true; connected_= false; socket_.abort ();
    if (failed) failed (reason);
  }
public:
  relay_transport (QUrl origin, const std::string& access, const std::string& ticket) {
    validate_authority_origin (origin); capability (access); capability (ticket);
    socket_.setReadBufferSize (budget);
    socket_.setMaxAllowedIncomingFrameSize (64*1024);
    socket_.setMaxAllowedIncomingMessageSize (64*1024);
    socket_.setOutgoingFrameSize (32*1024);
    auto tls= QSslConfiguration::defaultConfiguration ();
    tls.setProtocol (QSsl::TlsV1_3OrLater); tls.setPeerVerifyMode (QSslSocket::VerifyPeer);
    socket_.setSslConfiguration (tls);
    connect (&socket_, &QWebSocket::connected, this, [this] {
      if (socket_.subprotocol () != "athena-hodarium-stream-v1") { reject ("Relay subprotocol mismatch"); return; }
      connected_= true; if (activity) activity ();
    });
    connect (&socket_, &QWebSocket::binaryMessageReceived, this, [this] (const QByteArray& bytes) {
      if (stopped_) return;
      if (bytes.size () > budget - incoming_.size ()) { reject ("Relay receive queue exceeds budget"); return; }
      incoming_+= bytes;
      if (activity) activity ();
    });
    connect (&socket_, &QWebSocket::textMessageReceived, this, [this] { reject ("Relay sent a nonbinary frame"); });
    connect (&socket_, &QWebSocket::bytesWritten, this, [this] { if (activity) activity (); });
    connect (&socket_, &QWebSocket::disconnected, this, [this] {
      stopped_= true; connected_= false; if (activity) activity ();
    });
    connect (&socket_, &QWebSocket::errorOccurred, this, [this] { reject ("Relay WebSocket connection failed"); });
    origin.setScheme ("wss"); origin.setPath ("/v1/stream");
    QNetworkRequest request (origin);
    request.setAttribute (QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setRawHeader ("Authorization", QByteArray ("Bearer ") + QByteArray::fromStdString (access));
    request.setRawHeader ("X-Hodarium-Ticket", QByteArray::fromStdString (ticket));
    QWebSocketHandshakeOptions options;
    options.setSubprotocols ({"athena-hodarium-stream-v1"});
    socket_.open (request, options);
  }
  transport_state state () const override {
    return stopped_ ? transport_state::closed : connected_ ? transport_state::connected : transport_state::connecting;
  }
  ssize_t read (void* output, std::size_t size) noexcept override {
    if (incoming_.isEmpty ()) {
      if (stopped_) return 0;
      errno= EAGAIN; return -1;
    }
    auto n= std::min (size, std::size_t (incoming_.size ()));
    std::memcpy (output, incoming_.constData (), n); incoming_.remove (0, qsizetype (n)); return n;
  }
  ssize_t write (const void* input, std::size_t size) noexcept override {
    if (stopped_) { errno= ECONNRESET; return -1; }
    auto remaining= budget - socket_.bytesToWrite () - 32;
    if (!connected_ || remaining <= 0) { errno= EAGAIN; return -1; }
    auto n= std::min ({size, std::size_t (remaining), std::size_t (32*1024)});
    try { return socket_.sendBinaryMessage (QByteArray (static_cast<const char*> (input), qsizetype (n))); }
    catch (...) { errno= ENOMEM; return -1; }
  }
  void abort () override { stopped_= true; connected_= false; incoming_.clear (); socket_.abort (); }
};
}
std::unique_ptr<peer_transport> tcp_peer_transport (QTcpSocket* socket) {
  return std::make_unique<tcp_transport> (socket);
}
std::unique_ptr<peer_transport> relay_peer_transport (QUrl origin, std::string access, std::string ticket) {
  return std::make_unique<relay_transport> (std::move (origin), access, ticket);
}
void allocate_relay_ticket (control_http& http, const std::string& access,
  std::function<void (control_result, relay_ticket)> completed) {
  capability (access);
  if (!completed) throw std::invalid_argument ("Relay allocation requires completion");
  http.request ("/v1/tickets", "{}", [completed= std::move (completed)] (control_response response) {
    if (!response.error.empty ()) {
      completed ({control_failure::transport, std::move (response.error)}, {}); return;
    }
    relay_ticket ticket;
    try {
      if (response.body.size () > 4096) throw std::invalid_argument ("Relay ticket exceeds budget");
      auto value= nlohmann::json::parse (response.body.constData (), response.body.constData () + response.body.size (),
        [] (int depth, nlohmann::json::parse_event_t, nlohmann::json&) {
          if (depth > 4) throw std::invalid_argument ("Relay ticket exceeds depth budget");
          return true;
        });
      const auto& endpoints= value.at ("endpoints");
      if (!endpoints.is_array () || endpoints.size () != 2) throw std::invalid_argument ("Invalid relay endpoint pair");
      for (int i= 0; i < 2; ++i) { ticket.endpoints[i]= endpoints[i].get<std::string> (); capability (ticket.endpoints[i]); }
      ticket.expires= value.at ("expires").get<std::int64_t> ();
      auto now= QDateTime::currentSecsSinceEpoch ();
      if (ticket.endpoints[0] == ticket.endpoints[1] || ticket.expires <= now || ticket.expires > now + 300)
        throw std::invalid_argument ("Invalid or expired relay ticket");
    }
    catch (const std::exception& e) { completed ({control_failure::invalid_state, e.what ()}, {}); return; }
    completed ({}, std::move (ticket));
  }, false, QByteArray::fromStdString (access));
}
} // namespace athena::hodarium
