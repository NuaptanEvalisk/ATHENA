/******************************************************************************
* MODULE     : client.cpp
* DESCRIPTION: Scriptable AUDMAP client for selectors, operations and JSON frames
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#include "codec.hpp"
#include "audmap_server.hpp"
#include "connection.hpp"
#include <zmq.hpp>
#include <chrono>
#include <fstream>
#include <iostream>
#include <unistd.h>

using namespace athena::interop;
using clock_type = std::chrono::steady_clock;

int main (int argc, char** argv) {
  try {
    std::string discovery, selection, command;
    auto identity = default_client_identity ("cli");
    value parameters = value::object ();
    bool stdio = false;
    for (int i = 1; i < argc; ++i) {
      std::string option = argv[i];
      if (option == "--stdio") stdio = true;
      else if (i + 1 < argc && option == "--endpoint") discovery = argv[++i];
      else if (i + 1 < argc && option == "--identity") identity = argv[++i];
      else if (i + 1 < argc && option == "--select") selection = argv[++i];
      else if (i + 1 < argc && option == "--command") command = argv[++i];
      else if (i + 1 < argc && option == "--parameters") parameters = value::parse (argv[++i]);
      else throw std::invalid_argument ("Usage: athena-audmap [--endpoint connection.json] [--identity private/key.json] (--stdio | --select AUDM [--command NAME --parameters JSON])");
    }
    if (stdio == !selection.empty ()) throw std::invalid_argument ("Choose --stdio or --select");
    client_connection connection (discovery, identity, "athena-audmap CLI");
    auto send = [&] (value msg) { connection.send (msg); };
    bool ready = false, finished = false;
    int exit_code = 0;
    std::size_t outstanding = 0;
    std::string input;
    auto heartbeat = clock_type::now ();
    auto started = heartbeat;
    while (!finished) {
      zmq::pollitem_t items[] {{connection.handle (), 0, ZMQ_POLLIN, 0},
        {nullptr, STDIN_FILENO, static_cast<short> (stdio && ready ? ZMQ_POLLIN : 0), 0}};
      zmq::poll (items, 2, std::chrono::milliseconds (100));
      if (clock_type::now () - heartbeat >= std::chrono::seconds (1)) {
        send (value::array ({static_cast<unsigned> (transport_opcode::ping)}));
        heartbeat = clock_type::now ();
      }
      if (!ready && clock_type::now () - started > std::chrono::minutes (5))
        throw std::runtime_error ("Connection approval timed out");
      if (items[0].revents & ZMQ_POLLIN) {
        const auto received = connection.receive ();
        if (!received) continue;
        const auto& msg = *received;
        const auto op = msg[0].get<unsigned> ();
        if (op == static_cast<unsigned> (transport_opcode::welcome)) {
          if (ready) continue;
          ready = true;
          if (stdio) std::cout << msg.dump () << std::endl;
          else send (value::array ({static_cast<unsigned> (opcode::req), 1, selection, value::array ({1})}));
        }
        else if (op == static_cast<unsigned> (transport_opcode::pending)) {
          std::cerr << "Waiting for ATHENA authorization\n";
        }
        else if (op == static_cast<unsigned> (transport_opcode::rejected)) {
          std::cerr << msg.dump () << '\n'; exit_code = 1; finished = true;
        }
        else if (stdio) std::cout << msg.dump () << std::endl;
        else if (op == static_cast<unsigned> (opcode::acx)) {
          std::cout << msg.dump () << std::endl;
          const auto& handles = msg[2][0];
          if (command.empty () || handles.empty ()) finished = true;
          else {
            outstanding = handles.size ();
            std::uint64_t oid = 1;
            for (const auto& h: handles) send (value::array ({static_cast<unsigned> (opcode::opr), 1, oid++, h, command, parameters}));
          }
        }
        else if (op == static_cast<unsigned> (opcode::rsp)) {
          std::cout << msg.dump () << std::endl;
          if (msg[3] != "OK") exit_code = 1;
          if (outstanding && --outstanding == 0) finished = true;
        }
        else if (op == static_cast<unsigned> (opcode::err)) {
          std::cerr << msg.dump () << '\n'; exit_code = 1;
          if (msg.size () == 3 || !outstanding || --outstanding == 0) finished = true;
        }
      }
      if (items[1].revents & ZMQ_POLLIN) {
        char block[8192];
        const auto count = ::read (STDIN_FILENO, block, sizeof (block));
        if (count <= 0) finished = true;
        else {
          input.append (block, count);
          if (input.size () > wire_size_limit) throw std::length_error ("Input line too large");
          std::size_t end;
          while ((end = input.find ('\n')) != std::string::npos) {
            const auto line = input.substr (0, end); input.erase (0, end + 1);
            if (!line.empty ()) send (value::parse (line));
          }
        }
      }
    }
    if (!stdio && ready) send (value::array ({static_cast<unsigned> (opcode::fin), 1}));
    return exit_code;
  }
  catch (const std::exception& e) { std::cerr << "athena-audmap: " << e.what () << '\n'; return 1; }
}
