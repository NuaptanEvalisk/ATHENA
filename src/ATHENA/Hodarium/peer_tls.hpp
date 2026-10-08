/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#pragma once
#include "device_identity.hpp"
#include <gnutls/gnutls.h>
#include <memory>
#include <functional>
#include <string>

namespace athena::hodarium {
struct peer_context {
  std::string group, generation, epoch, local_member, remote_member;
};
// Nonblocking inner TLS, independent of TCP versus relay transport. Construct,
// drive and destroy on one identity/network worker. Transport callbacks must
// return EAGAIN instead of waiting for network I/O and must not throw.
// The authorization callback must check the current local membership lease and
// both member/key identities against this exact group/generation/epoch. It is
// checked at handshake and every record operation, including retry after EAGAIN.
class peer_tls {
public:
  peer_tls (device_identity local, std::string expected_peer_key, bool server,
    peer_context context, std::function<bool ()> authorized,
    void* transport, gnutls_pull_func pull, gnutls_push_func push);
  ~peer_tls ();
  peer_tls (const peer_tls&)= delete;
  peer_tls& operator= (const peer_tls&)= delete;
  bool handshake (); // false means the transport needs more I/O
  bool wants_write () const;
  ssize_t send (const void* data, std::size_t size);
  ssize_t receive (void* data, std::size_t size);
  // RFC 9266 binding, available only after mutual raw-key TLS authentication.
  std::string channel_binding () const;
private:
  struct implementation;
  std::unique_ptr<implementation> state_;
};
} // namespace athena::hodarium
