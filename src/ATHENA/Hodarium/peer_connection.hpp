/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#pragma once
#include "peer_tls.hpp"
#include "peer_transport.hpp"
#include <QObject>
#include <QByteArray>
#include <QTimer>
#include <deque>
#include <QHostAddress>
#include <QSet>
#include <optional>
class QTcpSocket;
class QTcpServer;
namespace athena::hodarium {
struct peer_events {
  std::function<void ()> established;
  std::function<void (QByteArray)> received;
  std::function<void (std::string)> closed;
  std::function<void ()> writable;
};
// Owns a connected/connecting TCP socket on the same Qt network worker. The
// events run on that worker, never the GUI or a BufferActor. Data is a byte
// stream; framing and revision transfer are owned by the protocol above it.
class peer_connection: public QObject {
public:
  peer_connection (QTcpSocket* socket, device_identity local, std::string peer_key,
    bool server, peer_context context, std::function<bool ()> authorized,
    peer_events events, QObject* parent= nullptr);
  peer_connection (std::unique_ptr<peer_transport> transport, device_identity local,
    std::string peer_key, bool server, peer_context context,
    std::function<bool ()> authorized, peer_events events, QObject* parent= nullptr);
  ~peer_connection () override;
  bool enqueue (QByteArray bytes); // false: closed, not authenticated or backpressure
  void close ();
  bool established () const;
private:
  std::unique_ptr<peer_transport> transport_;
  std::function<bool ()> authorized_;
  peer_events events_;
  std::unique_ptr<peer_tls> tls_;
  QTimer handshake_timeout_, idle_timeout_, authorization_timer_;
  std::deque<QByteArray> outgoing_;
  std::size_t queued_= 0, offset_= 0;
  bool ready_= false, closed_= false, scheduled_= false, blocked_= false;
  bool pumping_= false, wake_pending_= false;
  void owner () const;
  void schedule ();
  void pump ();
  void fail (std::string reason);
  static ssize_t pull (void*, void*, std::size_t) noexcept;
  static ssize_t push (void*, const void*, std::size_t) noexcept;
};

// Routing hints precede inner TLS and are never authentication. The resolver
// must check current membership and return the exact expected physical key.
class direct_peer_listener: public QObject {
public:
  using resolver= std::function<std::optional<std::string> (const peer_context&)>;
  using receiver= std::function<void (std::unique_ptr<peer_transport>, peer_context, std::string)>;
  direct_peer_listener (resolver resolve, receiver accepted);
  ~direct_peer_listener () override;
  bool listen (const QHostAddress& address= QHostAddress::Any, quint16 port= 0);
  quint16 port () const;
  std::string error () const;
  void close ();
private:
  QTcpServer* server_;
  QSet<QTcpSocket*> pending_;
  resolver resolve_;
  receiver accepted_;
  void accept ();
  void inspect (QTcpSocket* socket);
  void discard (QTcpSocket* socket);
};
std::unique_ptr<peer_transport> connect_direct_peer (const QHostAddress& address,
  quint16 port, const peer_context& context);
} // namespace athena::hodarium
