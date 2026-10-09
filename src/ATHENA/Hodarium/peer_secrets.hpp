/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#pragma once
#include "client_settings.hpp"
#include "peer_network.hpp"

namespace athena::hodarium {
class peer_secret_exchange {
public:
  using membership= std::function<std::optional<membership_state> ()>;
  using authorization= std::function<bool (const std::string& epoch)>;
  peer_secret_exchange (peer_network& network, client_settings& settings,
    client_profile profile, membership members, authorization allowed);
  bool receive (const std::string& peer, std::uint64_t session,
    const std::vector<std::string>& common, const QByteArray& message);
  void pump (const std::string& peer, std::uint64_t session, const std::vector<std::string>& common);
  void forget (const std::string& peer);
private:
  using clock= std::chrono::steady_clock;
  struct pending {
    std::uint64_t session= 0;
    std::string vault, commitment, request;
    std::unique_ptr<vault_secret_receiver> receiver;
    QByteArray outgoing, reply;
    clock::time_point next{}, deadline{};
    std::size_t cursor= 0;
  };
  peer_network& network_;
  client_settings& settings_;
  client_profile profile_;
  membership members_;
  authorization allowed_;
  std::map<std::string, pending> peers_;
  pending& current (const std::string& peer, std::uint64_t session);
  vault_secret_context context (const std::string& peer, const std::string& vault, bool sending);
  bool authorized (const vault_secret_context& context);
};
}
