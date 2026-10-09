/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#pragma once
#include "peer_connection.hpp"
#include "rendezvous.hpp"
#include <map>
#include <QPointer>

namespace athena::hodarium {
struct peer_route_status {
  std::string member;
  bool established= false;
  double roundtrip_ms= 0;
  std::uint64_t session= 0;
};
// Owns authenticated direct sessions, not document application. Relay routes
// will share the same framed channel and authorization predicate.
class peer_network: public QObject {
public:
  using authorization= std::function<bool (const peer_context&, const std::string&)>;
  using consumer= std::function<void (const std::string&, QByteArray)>;
  peer_network (device_identity device, authorization authorized, consumer received= {});
  ~peer_network () override;
  void start (const membership_state& state, const std::string& member);
  void stop ();
  void discover (const presence_snapshot& presence);
  std::vector<std::string> addresses () const;
  std::vector<peer_route_status> status () const;
  bool send (const std::string& member, QByteArray bytes);
  void disconnect_peer (const std::string& member);
private:
  using clock= std::chrono::steady_clock;
  struct link {
    QPointer<peer_connection> connection;
    QByteArray incoming, ping;
    clock::time_point next_ping{}, sent{};
    bool outgoing= false;
    double rtt= 0;
    std::uint64_t session= 0;
  };
  device_identity device_;
  authorization authorized_;
  consumer received_;
  std::unique_ptr<direct_peer_listener> listener_;
  QTimer timer_;
  membership_state state_;
  std::string member_;
  std::map<std::string, std::string> keys_;
  std::map<std::string, link> links_;
  std::map<std::string, clock::time_point> retry_;
  std::map<std::string, std::size_t> address_index_;
  presence_snapshot candidates_;
  bool active_= false;
  std::uint64_t next_session_= 1;
  peer_context context (const std::string& peer) const;
  void attach (std::unique_ptr<peer_transport> transport, peer_context context,
               std::string key, bool outgoing);
  void receive (const std::string& member, peer_connection* connection, QByteArray bytes);
  void tick ();
  static QByteArray frame (int kind, const QByteArray& bytes);
};
} // namespace athena::hodarium
