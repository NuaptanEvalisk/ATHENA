/******************************************************************************
* MODULE     : audmap_server.hpp
* DESCRIPTION: Local AUDMAP transport and connection authorization interfaces
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#pragma once
#include "session.hpp"
#include <filesystem>
#include <optional>

namespace athena::interop {
// Transport control frames are distinct from AUDMAP opcodes 1..11.
enum class transport_opcode: unsigned { hello = 100, welcome, pending, ping, bye, rejected };
struct authorization_ui {
  // Connection instance ID, authenticated CURVE public key, self-declared name.
  std::function<void (std::string, std::string, std::string,
    std::function<void (std::optional<trust_mode>)>)> connect;
  std::function<void (std::string, value, std::function<void (bool)>)> confirm;
  std::function<void (std::string)> disconnect;
};

class local_server {
  struct impl;
  std::unique_ptr<impl> implementation;
public:
  local_server (std::shared_ptr<const resolver_registry> registry,
                authorization_ui ui, std::size_t resolution_count,
                std::size_t operation_count = 2);
  ~local_server ();
  const std::filesystem::path& discovery_file () const;
};
} // namespace athena::interop
