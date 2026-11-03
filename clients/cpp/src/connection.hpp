/******************************************************************************
* MODULE     : connection.hpp
* DESCRIPTION: Shared CLI and REPL transport connection interface
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#pragma once
#include "protocol.hpp"
#include "identity.hpp"
#include <zmq.hpp>
#include <chrono>

namespace athena::interop {
class client_connection {
  zmq::context_t context {1};
  zmq::socket_t socket {context, zmq::socket_type::dealer};
  std::chrono::steady_clock::time_point heartbeat = std::chrono::steady_clock::now ();
public:
  client_connection (std::filesystem::path discovery, const std::filesystem::path& identity,
                     const std::string& name);
  ~client_connection ();
  void* handle () { return socket.handle (); }
  void send (const value& message);
  std::optional<value> receive ();
  void tick ();
  void wait_for_authorization ();
};
} // namespace athena::interop
