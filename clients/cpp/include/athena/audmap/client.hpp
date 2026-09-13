/******************************************************************************
* MODULE     : client.hpp
* DESCRIPTION: Standalone thread-safe AUDMAP client and asynchronous request interface
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#pragma once
#include <nlohmann/json.hpp>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <future>
#include <memory>
#include <stdexcept>
#include <string>

namespace athena::audmap {
using value = nlohmann::json;
using id = std::uint64_t;
struct response {
  std::string status = "OK";
  value data;
};
struct pending_request {
  id ticket = 0, operation = 0;
  std::shared_future<response> result;
};
class protocol_error: public std::runtime_error {
public:
  const value frame;
  explicit protocol_error (value frame);
};
struct options {
  std::filesystem::path endpoint;
  std::filesystem::path identity;
  std::string name = "AUDMAP C++ client";
  std::chrono::milliseconds authorization_timeout {300000};
};

// The socket, authentication and heartbeat live on one private I/O thread.
// Returned futures do not require the caller to pump an event loop. Timeout on
// a future does not cancel or retry an admitted operation.
class client {
  struct impl;
  std::unique_ptr<impl> implementation;
public:
  explicit client (options options = {});
  ~client ();
  client (const client&) = delete;
  client& operator= (const client&) = delete;

  pending_request resolve (std::string selector, bool leaves = false, id limit = 0);
  pending_request operate (id ticket, id handle, std::string command,
                           value parameters = value::object ());
  pending_request lineage (id ticket, id handle);
  pending_request ask (id ticket, id operation = 0);
  pending_request release (id ticket, id operation);
  pending_request cancel (id ticket);
  pending_request finish (id ticket);
  void close ();
};
}
