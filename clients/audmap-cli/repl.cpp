/******************************************************************************
* MODULE     : repl.cpp
* DESCRIPTION: Readline terminal frontend with nonblocking AUDMAP event processing
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#include "connection.hpp"
#include "repl_session.hpp"
#include <readline/readline.h>
#include <readline/history.h>
#include <clocale>
#include <csignal>
#include <deque>
#include <iostream>
#include <unistd.h>

using namespace athena::interop;
namespace {
std::deque<std::string> lines;
bool eof = false;
bool input_active = false;
volatile std::sig_atomic_t interrupted = 0;
void on_interrupt (int) { interrupted = 1; }
void stop_input () {
  if (input_active) { rl_callback_handler_remove (); input_active = false; }
}
void on_line (char* line) {
  // Otherwise Readline immediately emits another prompt before we can process
  // this command's output or switch into a non-input waiting state.
  stop_input ();
  if (!line) { eof = true; return; }
  if (*line) add_history (line);
  lines.emplace_back (line); std::free (line);
}
struct terminal {
  bool interactive = isatty (STDIN_FILENO);
  bool waiting = false;
  struct sigaction previous {};
  terminal () {
    struct sigaction action {};
    action.sa_handler = on_interrupt;
    sigemptyset (&action.sa_mask);
    if (sigaction (SIGINT, &action, &previous)) throw std::runtime_error ("Cannot install interrupt handler");
    if (interactive) { using_history (); stifle_history (500); rl_catch_signals = 0; }
  }
  ~terminal () {
    stop_input ();
    if (waiting) std::cout << " disconnected.\n";
    sigaction (SIGINT, &previous, nullptr);
  }
  void print (const std::string& text) {
    std::cout << text << (text.empty () || text.back () != '\n' ? "\n" : "") << std::flush;
  }
  void prompt (const std::string& text) {
    if (!interactive || eof) return;
    if (text.empty ()) { stop_input (); return; }
    if (!input_active) { rl_callback_handler_install (text.c_str (), on_line); input_active = true; }
  }
  void interrupt () {
    if (input_active) { rl_replace_line ("", 0); rl_crlf (); stop_input (); }
  }
  void progress (repl_progress state) {
    if (state == repl_progress::resolving || state == repl_progress::running) {
      stop_input ();
      std::cout << (state == repl_progress::resolving ? "Resolving ..." : "Running operation ...") << std::flush;
      waiting = true; return;
    }
    if (!waiting) return;
    const char* result = state == repl_progress::done ? " done.\n" :
      state == repl_progress::failed ? " failed.\n" :
      state == repl_progress::cancelled ? " cancelled.\n" : " detached; the operation may still finish.\n";
    std::cout << result << std::flush;
    waiting = false;
  }
};
}
int main (int argc, char** argv) {
  std::setlocale (LC_ALL, "");
  try {
    std::filesystem::path endpoint, identity = default_client_identity ("repl");
    for (int i = 1; i < argc; ++i) {
      const std::string arg = argv[i];
      if (arg == "--help") {
        std::cout << "Usage: athena-audmap-repl [--endpoint connection.json] [--identity private/key.json]\n"
                     "Selector mode: AUDM expression, help, exit.\n"
                     "Operation mode: use HANDLE selects a target; inspect lists its operations.\n"
                     "Run COMMAND [JSON object] on that target. help explains each command.\n"
                     "exit returns to selectors; exit again quits. Ctrl+C interrupts waiting.\n";
        return 0;
      }
      if (i + 1 < argc && arg == "--endpoint") endpoint = argv[++i];
      else if (i + 1 < argc && arg == "--identity") identity = argv[++i];
      else throw std::invalid_argument ("Unknown argument; use --help");
    }
    client_connection connection (endpoint, identity, "ATHENA AUDMAP REPL");
    connection.wait_for_authorization ();
    std::cout << "Connected. Enter a selector; help lists commands.\n";
    terminal console;
    repl_session session ([&] (value msg) { connection.send (msg); },
                          [&] (std::string text) { console.print (text); },
                          [&] (repl_progress state) { console.progress (state); });
    std::string input;
    while (!session.done ()) {
      if (interrupted) {
        interrupted = 0; console.interrupt ();
        if (session.busy ()) session.line ("exit");
      }
      console.prompt (session.prompt ());
      zmq::pollitem_t items[] {{connection.handle (), 0, ZMQ_POLLIN, 0},
        {nullptr, STDIN_FILENO, static_cast<short> (!eof && !session.busy () &&
          (console.interactive ? input_active : lines.empty ()) ? ZMQ_POLLIN : 0), 0}};
      try { zmq::poll (items, 2, std::chrono::milliseconds (100)); }
      catch (const zmq::error_t& e) { if (e.num () == EINTR) continue; throw; }
      connection.tick ();
      if (auto msg = connection.receive ()) {
        if (msg->at (0) == static_cast<unsigned> (transport_opcode::rejected))
          throw std::runtime_error (msg->dump ());
        session.receive (*msg);
      }
      if (items[1].revents & ZMQ_POLLIN) {
        if (console.interactive) rl_callback_read_char ();
        else {
          char block[4096];
          const auto count = ::read (STDIN_FILENO, block, sizeof (block));
          if (count < 0 && errno != EINTR) throw std::runtime_error ("Cannot read stdin");
          if (!count) { eof = true; if (!input.empty ()) { lines.push_back (input); input.clear (); } }
          if (count > 0) {
            input.append (block, count);
            if (input.size () > wire_size_limit) throw std::length_error ("Input line too large");
            std::size_t end;
            while ((end = input.find ('\n')) != std::string::npos) {
              lines.push_back (input.substr (0, end)); input.erase (0, end + 1);
            }
          }
        }
      }
      if (!lines.empty () && (console.interactive || !session.busy ())) {
        auto line = std::move (lines.front ()); lines.pop_front (); session.line (line);
      }
      if (eof && lines.empty () && (console.interactive || !session.busy ())) session.close ();
    }
    return 0;
  }
  catch (const std::exception& e) { std::cerr << "athena-audmap-repl: " << e.what () << '\n'; return 1; }
}
