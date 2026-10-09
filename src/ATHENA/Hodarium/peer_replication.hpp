/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#pragma once
#include "peer_network.hpp"
#include "revision_transfer.hpp"
#include <set>

namespace athena::hodarium {
// Owner-thread replication of immutable journals only; never applies files.
class peer_replication: public QObject {
public:
  peer_replication (peer_network& network, revision_store& store,
    std::vector<std::string> vaults, std::function<void(std::string)> error);
  void receive (const std::string& member, const QByteArray& message);
private:
  using clock= std::chrono::steady_clock;
  struct peer {
    std::uint64_t session= 0;
    bool announced= false, received= false;
    std::vector<std::string> common;
    std::unique_ptr<revision_exchange> exchange;
    std::size_t next= 0;
    clock::time_point cycle{}, deadline= clock::now () + std::chrono::seconds (30);
  };
  peer_network& network_;
  revision_store& store_;
  std::vector<std::string> vaults_;
  std::function<void(std::string)> error_;
  std::map<std::string, peer> peers_;
  QByteArray announcement_;
  QTimer timer_;
  peer& current (const peer_route_status& route);
  void tick ();
  void pump (const std::string& member, peer& state);
};
}
