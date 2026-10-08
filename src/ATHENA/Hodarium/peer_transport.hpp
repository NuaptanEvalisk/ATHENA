/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#pragma once
#include <QObject>
#include <QUrl>
#include <functional>
#include <memory>
#include <string>
#include <sys/types.h>
#include "authority_client.hpp"
#include <array>
class QTcpSocket;
namespace athena::hodarium {
enum class transport_state { connecting, connected, closed };
// Bounded nonblocking byte transport. Neither implementation authenticates a
// Hodarium member; the same inner peer_tls runs over both.
class peer_transport: public QObject {
public:
  std::function<void ()> activity;
  std::function<void (std::string)> failed;
  virtual transport_state state () const= 0;
  virtual ssize_t read (void*, std::size_t) noexcept= 0;
  virtual ssize_t write (const void*, std::size_t) noexcept= 0;
  virtual void abort ()= 0;
};
std::unique_ptr<peer_transport> tcp_peer_transport (QTcpSocket* socket);
std::unique_ptr<peer_transport> relay_peer_transport (QUrl https_origin,
  std::string access_token, std::string endpoint_ticket);
struct relay_ticket {
  std::array<std::string, 2> endpoints;
  std::int64_t expires= 0;
};
void allocate_relay_ticket (control_http& http, const std::string& access_token,
  std::function<void (control_result, relay_ticket)> completed);
} // namespace athena::hodarium
