/******************************************************************************
* MODULE     : repl_session.hpp
* DESCRIPTION: Terminal-independent AUDMAP REPL state machine interface
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

namespace athena::interop {
enum class repl_progress { resolving, running, done, failed, cancelled, detached };
// Terminal-independent state machine. The transport and Readline stay on the
// caller's thread; waiting for input must not suspend protocol heartbeats.
class repl_session {
  std::function<void (value)> send;
  std::function<void (std::string)> print;
  std::function<void (repl_progress)> progress;
  std::uint64_t next_ticket = 0, ticket = 0, next_operation = 0, operation = 0;
  handle selected = 0;
  std::map<handle, handle> handles;
  bool resolving = false, finished = false;
  void reset ();
  void show_handles () const;
public:
  repl_session (std::function<void (value)> send, std::function<void (std::string)> print,
                std::function<void (repl_progress)> progress = [] (repl_progress) {}):
    send (std::move (send)), print (std::move (print)), progress (std::move (progress)) {}
  void line (const std::string& input);
  void receive (const value& message);
  std::string prompt () const;
  bool done () const { return finished; }
  bool busy () const { return resolving || operation != 0; }
  void close ();
};
} // namespace athena::interop
