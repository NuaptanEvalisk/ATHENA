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
  std::string route;
  double transfer_bytes_per_second= 0;
};
struct relay_configuration {
  QUrl origin;
  std::string access_token; // supplied from protected local credential storage
  relay_configuration (QUrl origin, std::string token);
  relay_configuration (const relay_configuration&)= default;
  relay_configuration (relay_configuration&&) noexcept;
  relay_configuration& operator= (relay_configuration other);
  ~relay_configuration ();
};
// One selected application stream per peer, with authenticated warm alternatives.
class peer_network: public QObject {
public:
  using authorization= std::function<bool (const peer_context&, const std::string&)>;
  using consumer= std::function<void (const std::string&, QByteArray)>;
  peer_network (device_identity device, authorization authorized, consumer received= {});
  ~peer_network () override;
  void start (const membership_state& state, const std::string& member);
  void stop ();
  void discover (const presence_snapshot& presence);
  void set_relays (std::vector<relay_configuration> relays);
  std::vector<std::string> relay_origins () const;
  void set_throughput_probes_enabled (bool enabled) { throughput_probes_= enabled; }
  std::vector<std::string> addresses () const;
  std::vector<peer_route_status> status () const;
  bool send (const std::string& member, QByteArray bytes);
  void disconnect_peer (const std::string& member);
private:
  using clock= std::chrono::steady_clock;
  struct link {
    QPointer<peer_connection> connection;
    QByteArray incoming, ping, probe;
    clock::time_point next_ping{}, sent{}, probe_sent{}, next_probe{}, last_data{};
    bool outgoing= false;
    double rtt= 0;
    std::uint64_t session= 0;
    std::string member, route;
    unsigned samples= 0;
    unsigned probe_samples= 0;
    double transfer_rate= 0;
    clock::time_point since{};
  };
  struct relay_attempt {
    std::unique_ptr<control_http> http;
    clock::time_point deadline;
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
  std::map<std::string, std::string> selected_, selecting_;
  std::map<std::string, clock::time_point> selected_at_;
  std::map<std::string, std::pair<std::string, clock::time_point>> improving_;
  std::map<std::string, relay_configuration> relays_;
  std::map<std::string, relay_attempt> joining_;
  std::map<std::string, clock::time_point> retry_;
  std::map<std::string, std::size_t> address_index_;
  presence_snapshot candidates_;
  bool active_= false;
  bool throughput_probes_= true;
  std::uint64_t next_session_= 1;
  clock::time_point next_probe_{};
  peer_context context (const std::string& peer) const;
  void attach (std::unique_ptr<peer_transport> transport, peer_context context,
               std::string key, bool outgoing, std::string route= "p2p");
  void receive (const std::string& slot, peer_connection* connection, QByteArray bytes);
  void activate (const std::string& slot);
  void select_routes ();
  void dial_relay (const std::string& member, const std::string& route);
  void tick ();
  static QByteArray frame (int kind, const QByteArray& bytes);
};
} // namespace athena::hodarium
