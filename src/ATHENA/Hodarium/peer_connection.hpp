/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#pragma once
#include "peer_tls.hpp"
#include "peer_transport.hpp"
#include <QObject>
#include <QByteArray>
#include <QTimer>
#include <deque>
class QTcpSocket;
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
} // namespace athena::hodarium
