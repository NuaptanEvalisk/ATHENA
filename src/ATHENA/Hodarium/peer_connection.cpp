/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#include "peer_connection.hpp"
#include <QTcpSocket>
#include <QTcpServer>
#include <QNetworkProxy>
#include <sodium.h>
#include <QPointer>
#include <QThread>
#include <QScopeGuard>
#include <cerrno>
#include <algorithm>
#include <stdexcept>

namespace athena::hodarium {
namespace {
constexpr std::size_t queue_budget= 1024*1024;
constexpr std::size_t record_budget= 16*1024;
const QByteArray direct_magic= "ATHENA-HODARIUM-DIRECT-v1\n";
constexpr qsizetype identity_size= 43;
qsizetype routing_size () { return direct_magic.size () + 5*identity_size; }
void route_identity (const std::string& id) {
  unsigned char bytes[32]; std::size_t size= 0;
  if (id.size () != identity_size || sodium_base642bin (bytes, sizeof bytes,
      id.data (), id.size (), nullptr, &size, nullptr,
      sodium_base64_VARIANT_URLSAFE_NO_PADDING) != 0 || size != sizeof bytes)
    throw std::invalid_argument ("Invalid Hodarium direct routing identity");
}
}
peer_connection::peer_connection (QTcpSocket* socket, device_identity local,
  std::string peer_key, bool server, peer_context context,
  std::function<bool ()> authorized, peer_events events, QObject* parent):
  peer_connection (tcp_peer_transport (socket), std::move (local), std::move (peer_key), server,
    std::move (context), std::move (authorized), std::move (events), parent) {}
peer_connection::peer_connection (std::unique_ptr<peer_transport> transport, device_identity local,
  std::string peer_key, bool server, peer_context context,
  std::function<bool ()> authorized, peer_events events, QObject* parent):
  QObject (parent), transport_ (std::move (transport)), authorized_ (std::move (authorized)), events_ (std::move (events)) {
  if (!transport_ || transport_->thread () != thread () || QThread::currentThread () != thread () ||
      !authorized_ || !events_.received || !events_.closed)
    throw std::invalid_argument ("Peer connection requires an owned transport, authorization and stream consumer");
  tls_= std::make_unique<peer_tls> (std::move (local), std::move (peer_key), server,
    std::move (context), authorized_, this, pull, push);
  handshake_timeout_.setSingleShot (true); idle_timeout_.setSingleShot (true);
  connect (&handshake_timeout_, &QTimer::timeout, this, [this] { fail ("Hodarium peer handshake timed out"); });
  connect (&idle_timeout_, &QTimer::timeout, this, [this] { fail ("Hodarium peer connection idle timeout"); });
  connect (&authorization_timer_, &QTimer::timeout, this, [this] {
    try { if (!authorized_ ()) fail ("Hodarium peer authorization expired"); }
    catch (const std::exception& e) { fail (e.what ()); }
  });
  // QWebSocket may emit several bounded messages in one socket read callback.
  // Consume each immediately; queuing all of them defeats transport backpressure.
  transport_->activity= [this] { pump (); };
  transport_->failed= [this] (std::string error) { fail (std::move (error)); };
  handshake_timeout_.start (30000); authorization_timer_.start (1000);
  schedule ();
}
peer_connection::~peer_connection () {
  closed_= true;
  transport_->activity= {}; transport_->failed= {};
  transport_->abort ();
}
void peer_connection::owner () const {
  if (QThread::currentThread () != thread ()) throw std::logic_error ("Peer connection accessed outside its worker");
}
bool peer_connection::established () const { owner (); return ready_ && !closed_; }
void peer_connection::close () { owner (); fail ("Hodarium peer connection closed"); }
void peer_connection::fail (std::string reason) {
  if (closed_) return;
  closed_= true; ready_= false;
  handshake_timeout_.stop (); idle_timeout_.stop (); authorization_timer_.stop ();
  outgoing_.clear (); queued_= offset_= 0;
  transport_->abort ();
  auto callback= std::move (events_.closed);
  QTimer::singleShot (0, this, [callback= std::move (callback), reason= std::move (reason)] {
    if (callback) callback (reason);
  });
}
bool peer_connection::enqueue (QByteArray bytes) {
  owner ();
  if (!ready_ || closed_) return false;
  if (bytes.isEmpty ()) return true;
  if (std::size_t (bytes.size ()) > queue_budget - queued_) { blocked_= true; return false; }
  queued_+= std::size_t (bytes.size ()); outgoing_.push_back (std::move (bytes));
  schedule (); return true;
}
void peer_connection::schedule () {
  if (closed_ || scheduled_) return;
  scheduled_= true;
  QTimer::singleShot (0, this, [this] { scheduled_= false; pump (); });
}
ssize_t peer_connection::pull (void* context, void* output, std::size_t size) noexcept {
  auto& self= *static_cast<peer_connection*> (context);
  if (self.closed_) { errno= ECONNRESET; return -1; }
  return self.transport_->read (output, size);
}
ssize_t peer_connection::push (void* context, const void* input, std::size_t size) noexcept {
  auto& self= *static_cast<peer_connection*> (context);
  if (self.closed_) { errno= ECONNRESET; return -1; }
  return self.transport_->write (input, size);
}
void peer_connection::pump () {
  if (closed_) return;
  if (pumping_) { wake_pending_= true; return; }
  pumping_= true;
  QPointer<peer_connection> alive= this;
  auto finish= qScopeGuard ([alive] {
    if (!alive) return;
    alive->pumping_= false;
    if (alive->wake_pending_) { alive->wake_pending_= false; alive->schedule (); }
  });
  try {
    if (!ready_) {
      if (transport_->state () != transport_state::connected) {
        if (transport_->state () == transport_state::closed) fail ("Hodarium peer disconnected before authentication");
        return;
      }
      if (!tls_->handshake ()) return;
      ready_= true; handshake_timeout_.stop (); idle_timeout_.start (90000);
      if (events_.established) events_.established ();
      if (!alive || closed_) return;
    }
    int writes= 0;
    while (!outgoing_.empty () && writes++ < 32) {
      const auto& bytes= outgoing_.front ();
      auto size= std::min (record_budget, std::size_t (bytes.size ()) - offset_);
      auto n= tls_->send (bytes.constData () + offset_, size);
      if (n == GNUTLS_E_AGAIN || n == GNUTLS_E_INTERRUPTED) break;
      if (n <= 0) { fail ("Hodarium peer send failed"); return; }
      offset_+= n; queued_-= n; idle_timeout_.start (90000);
      if (offset_ == std::size_t (bytes.size ())) { outgoing_.pop_front (); offset_= 0; }
    }
    if (blocked_ && queued_ < queue_budget / 2) {
      blocked_= false;
      if (events_.writable) events_.writable ();
      if (!alive || closed_) return;
    }
    for (int reads= 0; reads < 32; ++reads) {
      QByteArray data (int (record_budget), Qt::Uninitialized);
      auto n= tls_->receive (data.data (), data.size ());
      if (n == GNUTLS_E_AGAIN || n == GNUTLS_E_INTERRUPTED) {
        if (writes > 32) schedule ();
        return;
      }
      if (n == 0) { fail ("Hodarium peer closed its stream"); return; }
      data.resize (n); idle_timeout_.start (90000);
      events_.received (std::move (data));
      if (!alive || closed_) return;
    }
    schedule ();
  }
  catch (const std::exception& e) { if (alive) fail (e.what ()); }
}

direct_peer_listener::direct_peer_listener (resolver resolve, receiver accepted):
  server_ (new QTcpServer (this)), resolve_ (std::move (resolve)), accepted_ (std::move (accepted)) {
  if (!resolve_ || !accepted_) throw std::invalid_argument ("Direct listener requires routing callbacks");
  server_->setProxy (QNetworkProxy::NoProxy);
  server_->setMaxPendingConnections (16);
  connect (server_, &QTcpServer::newConnection, this, [this] { accept (); });
}
direct_peer_listener::~direct_peer_listener () { close (); }
bool direct_peer_listener::listen (const QHostAddress& address, quint16 port) {
  return server_->listen (address, port);
}
quint16 direct_peer_listener::port () const { return server_->serverPort (); }
std::string direct_peer_listener::error () const { return server_->errorString ().toStdString (); }
void direct_peer_listener::close () {
  server_->close ();
  const auto pending= pending_;
  for (auto* socket: pending) discard (socket);
  while (server_->hasPendingConnections ()) {
    auto* socket= server_->nextPendingConnection ();
    socket->abort (); socket->deleteLater ();
  }
}
void direct_peer_listener::discard (QTcpSocket* socket) {
  if (!pending_.remove (socket)) return;
  disconnect (socket, nullptr, this, nullptr);
  socket->abort (); socket->deleteLater ();
}
void direct_peer_listener::accept () {
  while (server_->hasPendingConnections ()) {
    auto* socket= server_->nextPendingConnection ();
    if (pending_.size () >= 16) { socket->abort (); socket->deleteLater (); continue; }
    pending_.insert (socket);
    socket->setReadBufferSize (routing_size ());
    connect (socket, &QTcpSocket::readyRead, this, [this, socket] { inspect (socket); });
    connect (socket, &QTcpSocket::disconnected, this, [this, socket] { discard (socket); });
    // Context-bound timer cannot touch an already-deleted accepted connection.
    QTimer::singleShot (5000, socket, [this, socket, alive= QPointer<direct_peer_listener> (this)] {
      if (alive) discard (socket);
    });
    inspect (socket);
  }
}
void direct_peer_listener::inspect (QTcpSocket* socket) {
  if (!pending_.contains (socket) || socket->bytesAvailable () < routing_size ()) return;
  auto bytes= socket->read (routing_size ());
  if (!bytes.startsWith (direct_magic)) { discard (socket); return; }
  peer_context context;
  std::optional<std::string> key;
  try {
    auto at= direct_magic.size ();
    auto field= [&] {
      auto value= bytes.mid (at, identity_size).toStdString (); at+= identity_size;
      route_identity (value); return value;
    };
    context.group= field (); context.generation= field (); context.epoch= field ();
    context.remote_member= field (); context.local_member= field ();
    key= resolve_ (context);
    if (key) route_identity (*key);
  }
  catch (const std::exception&) { discard (socket); return; }
  if (!key) { discard (socket); return; }
  pending_.remove (socket); disconnect (socket, nullptr, this, nullptr);
  socket->setParent (nullptr);
  // Transport ownership crosses here; all following authentication remains TLS.
  auto transport= tcp_peer_transport (socket);
  accepted_ (std::move (transport), std::move (context), std::move (*key));
}
std::unique_ptr<peer_transport> connect_direct_peer (const QHostAddress& address,
  quint16 port, const peer_context& context) {
  if (address.isNull () || port == 0) throw std::invalid_argument ("Invalid direct peer endpoint");
  QByteArray header= direct_magic;
  for (const auto* id: {&context.group, &context.generation, &context.epoch,
                       &context.local_member, &context.remote_member}) {
    route_identity (*id); header+= QByteArray::fromStdString (*id);
  }
  auto socket= std::make_unique<QTcpSocket> ();
  socket->setProxy (QNetworkProxy::NoProxy);
  // Register before the transport's connected callback: the routing bytes must
  // be queued before its TLS ClientHello on the same TCP stream.
  QObject::connect (socket.get (), &QTcpSocket::connected, socket.get (),
    [socket= socket.get (), header] {
      if (socket->write (header) != header.size ()) socket->abort ();
    });
  socket->connectToHost (address, port);
  return tcp_peer_transport (socket.release ());
}
} // namespace athena::hodarium
