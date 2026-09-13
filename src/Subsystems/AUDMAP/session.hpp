/******************************************************************************
* MODULE     : session.hpp
* DESCRIPTION: Connection-owned AUDMAP session and trust policy interfaces
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#pragma once
#include "codec.hpp"
#include "resolution.hpp"
#include <map>
#include <set>

namespace athena::interop {
enum class trust_mode { full_access, confirm_operations, confirm_requests };
struct capability_mask {
  bool enforced = false;
  std::set<std::string> commands;
};

// Called with a frozen semantic request; completion may run on the GUI thread.
// Neither the request nor callback gives the GUI access to ticket-owned state.
using authorization = std::function<void (value, std::function<void (bool)>)>;

class protocol_session {
  struct impl;
  std::shared_ptr<impl> implementation;
public:
  protocol_session (resolution_workers& resolutions, resolution_workers& operations,
    std::shared_ptr<const resolver_registry> registry, trust_mode trust,
    authorization authorize, std::function<void (value)> send,
    std::map<std::string, capability_mask> capabilities = {});
  ~protocol_session ();
  protocol_session (const protocol_session&) = delete;
  protocol_session& operator= (const protocol_session&) = delete;
  // All three methods run on the connection owner's thread, not worker threads.
  void receive (const value& message);
  void poll ();
  void disconnect ();
};
} // namespace athena::interop
