/******************************************************************************
* MODULE     : server.cpp
* DESCRIPTION: Authenticated local IPC transport and connection lifecycle
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#include "audmap_server.hpp"
#include <zmq.hpp>
#include <zmq_addon.hpp>
#include <chrono>
#include <deque>
#include <fstream>
#include <future>
#include <thread>
#include <sys/stat.h>
#include <unistd.h>

namespace athena::interop {
namespace {
using clock = std::chrono::steady_clock;
value control (transport_opcode opcode, value argument = nullptr) {
  return value::array ({static_cast<unsigned> (opcode), std::move (argument)});
}
std::filesystem::path private_directory () {
  const char* runtime = std::getenv ("XDG_RUNTIME_DIR");
  std::string path = std::string (runtime && *runtime ? runtime : "/tmp") +
                    "/athena-audmap-" + std::to_string (getpid ()) + "-XXXXXX";
  std::vector<char> buffer (path.begin (), path.end ()); buffer.push_back (0);
  if (!mkdtemp (buffer.data ())) throw std::runtime_error ("Cannot create AUDMAP runtime directory");
  return buffer.data ();
}
} // namespace

struct local_server::impl {
  struct inbox {
    std::mutex mutex;
    std::deque<std::function<void (impl&)>> events;
    void post (std::function<void (impl&)> event) {
      std::lock_guard<std::mutex> lock (mutex); events.push_back (std::move (event));
    }
  };
  struct connection {
    std::string name;
    std::string ui_id;
    clock::time_point touched;
    std::unique_ptr<protocol_session> session;
  };
  const std::shared_ptr<const resolver_registry> registry;
  const authorization_ui ui;
  resolution_workers resolutions;
  resolution_workers operations;
  const std::filesystem::path directory;
  const std::filesystem::path discovery;
  std::shared_ptr<inbox> incoming = std::make_shared<inbox> ();
  std::atomic<bool> stopping {false};
  std::thread thread;
  std::map<std::string, connection> connections;
  std::uint64_t next_connection = 0;
  zmq::socket_t* router = nullptr; // owned and accessed only by the I/O thread

  impl (std::shared_ptr<const resolver_registry> registry, authorization_ui ui,
        std::size_t resolutions, std::size_t operations): registry (std::move (registry)),
    ui (std::move (ui)), resolutions (resolutions), operations (operations),
    directory (private_directory ()), discovery (directory / "connection.json") {
    std::promise<void> ready;
    auto started = ready.get_future ();
    thread = std::thread ([this, ready = std::move (ready)] () mutable {
      bool announced = false;
      try { run ([&] { ready.set_value (); announced = true; }); }
      catch (const std::exception& e) {
        if (!announced) ready.set_exception (std::current_exception ());
        else std::fprintf (stderr, "ATHENA AUDMAP transport stopped: %s\n", e.what ());
      }
      catch (...) {
        if (!announced) ready.set_exception (std::current_exception ());
        else std::fprintf (stderr, "ATHENA AUDMAP transport stopped: unknown exception\n");
      }
      for (auto& c: connections) if (this->ui.disconnect) this->ui.disconnect (c.second.ui_id);
      connections.clear ();
      router = nullptr;
    });
    try { started.get (); }
    catch (...) {
      stopping = true;
      thread.join ();
      std::error_code ignored; std::filesystem::remove_all (directory, ignored);
      throw;
    }
  }
  ~impl () {
    stopping.store (true);
    if (thread.joinable ()) thread.join ();
    incoming.reset ();
    std::error_code ignored; std::filesystem::remove_all (directory, ignored);
    // Pools drain admitted operations after connections and GUI callbacks detach.
  }

  void send (const std::string& peer, const value& message) {
    const auto body = encode_message (message);
    try {
      if (!router->send (zmq::buffer (peer), zmq::send_flags::sndmore | zmq::send_flags::dontwait)) return;
      router->send (zmq::buffer (body), zmq::send_flags::dontwait);
    }
    catch (const zmq::error_t& e) {
      if (e.num () != EHOSTUNREACH && e.num () != EAGAIN) throw;
    }
  }

  void forget (const std::string& peer) {
    auto it = connections.find (peer);
    if (it == connections.end ()) return;
    const auto ui_id = it->second.ui_id;
    connections.erase (it);
    if (ui.disconnect) ui.disconnect (ui_id);
  }

  void admit (const std::string& peer, const std::string& name) {
    if (connections.size () >= 32) { send (peer, control (transport_opcode::rejected, "Connection capacity exceeded")); return; }
    const auto ui_id = peer + ":" + std::to_string (++next_connection);
    connections.emplace (peer, connection {name, ui_id, clock::now (), {}});
    send (peer, control (transport_opcode::pending));
    std::weak_ptr<inbox> weak = incoming;
    ui.connect (ui_id, peer, name, [weak, peer, ui_id] (std::optional<trust_mode> trust) {
      if (auto queue = weak.lock ()) queue->post ([peer, ui_id, trust] (impl& s) {
        auto it = s.connections.find (peer);
        if (it == s.connections.end () || it->second.ui_id != ui_id || it->second.session) return;
        if (!trust) { s.send (peer, control (transport_opcode::rejected, "Connection denied")); s.forget (peer); return; }
        const auto confirmation = s.ui.confirm;
        it->second.session = std::make_unique<protocol_session> (
          s.resolutions, s.operations, s.registry, *trust,
          [confirmation, ui_id] (value request, std::function<void (bool)> reply) {
            confirmation (ui_id, std::move (request), std::move (reply));
          }, [&s, peer] (value reply) { s.send (peer, reply); });
        s.send (peer, control (transport_opcode::welcome, 1));
      });
    });
  }

  void receive (zmq::socket_t& socket) {
    std::vector<zmq::message_t> parts;
    try {
      if (!zmq::recv_multipart_n (socket, std::back_inserter (parts), 2, zmq::recv_flags::dontwait)) return;
    }
    catch (const std::runtime_error&) {
      if (parts.size () != 2 || !socket.get (zmq::sockopt::rcvmore)) throw;
      while (socket.get (zmq::sockopt::rcvmore)) {
        zmq::message_t discarded;
        if (!socket.recv (discarded)) break;
      }
      return;
    }
    if (parts.size () != 2) return;
    const std::string peer = parts[0].to_string ();
    // ZAP User-Id comes from the authenticated CURVE public key, never from the
    // peer's self-declared name/routing ID. A different key cannot steal a route.
    const char* authenticated = zmq_msg_gets (parts[1].handle (), "User-Id");
    if (!authenticated || peer != authenticated) return;
    try {
      const auto msg = decode_message (parts[1].to_string_view ());
      auto it = connections.find (peer);
      const auto op = msg[0].get<unsigned> ();
      if (op == static_cast<unsigned> (transport_opcode::hello)) {
        if (msg.size () != 3 || msg[1] != 1 || !msg[2].is_string () || msg[2].get_ref<const std::string&> ().size () > 256)
          throw std::invalid_argument ("HELLO requires version 1 and client name (<=256 bytes)");
        if (it == connections.end ()) admit (peer, msg[2]);
        else {
          it->second.touched = clock::now ();
          send (peer, control (it->second.session ? transport_opcode::welcome : transport_opcode::pending, 1));
        }
        return;
      }
      if (it == connections.end ()) { send (peer, control (transport_opcode::rejected, "Connection has not been authorized")); return; }
      it->second.touched = clock::now ();
      if (op == static_cast<unsigned> (transport_opcode::ping)) return;
      if (op == static_cast<unsigned> (transport_opcode::bye)) { forget (peer); return; }
      if (!it->second.session) { send (peer, control (transport_opcode::pending)); return; }
      it->second.session->receive (msg);
    }
    catch (const std::exception& e) { send (peer, control (transport_opcode::rejected, e.what ())); }
  }

  void authenticate (zmq::socket_t& zap) {
    std::vector<zmq::message_t> request;
    if (!zmq::recv_multipart (zap, std::back_inserter (request), zmq::recv_flags::dontwait)) return;
    const bool valid = request.size () == 7 && request[0].to_string () == "1.0" &&
      request[2].to_string () == "athena-audmap" && request[5].to_string () == "CURVE" && request[6].size () == 32;
    char public_key[41] = {};
    if (valid) zmq_z85_encode (public_key, static_cast<const std::uint8_t*> (request[6].data ()), 32);
    const std::vector<std::string> reply {"1.0", request.size () > 1 ? request[1].to_string () : "",
      valid ? "200" : "400", valid ? "OK" : "Unsupported authentication", valid ? public_key : "", ""};
    for (std::size_t i = 0; i < reply.size (); ++i)
      zap.send (zmq::buffer (reply[i]), i + 1 < reply.size () ? zmq::send_flags::sndmore : zmq::send_flags::none);
  }

  void run (const std::function<void ()>& ready) {
    if (!zmq_has ("curve")) throw std::runtime_error ("AUDMAP requires libzmq CURVE support");
    zmq::context_t context (1);
    zmq::socket_t zap (context, zmq::socket_type::rep);
    zap.bind ("inproc://zeromq.zap.01");
    zmq::socket_t socket (context, zmq::socket_type::router);
    router = &socket;
    char public_key[41], secret_key[41];
    if (zmq_curve_keypair (public_key, secret_key)) throw std::runtime_error ("CURVE key generation failed");
    socket.set (zmq::sockopt::linger, 0);
    socket.set (zmq::sockopt::router_mandatory, 1);
    socket.set (zmq::sockopt::router_handover, 0);
    socket.set (zmq::sockopt::maxmsgsize, static_cast<std::int64_t> (wire_size_limit));
    socket.set (zmq::sockopt::rcvhwm, 128);
    socket.set (zmq::sockopt::sndhwm, 128);
    socket.set (zmq::sockopt::curve_server, 1);
    socket.set (zmq::sockopt::curve_secretkey, secret_key);
    socket.set (zmq::sockopt::zap_domain, "athena-audmap");
    const uid_t uid = getuid ();
    if (zmq_setsockopt (socket.handle (), ZMQ_IPC_FILTER_UID, &uid, sizeof (uid)))
      throw zmq::error_t ();
    const auto endpoint = "ipc://" + (directory / "socket").string ();
    socket.bind (endpoint);
    chmod ((directory / "socket").c_str (), 0600);
    {
      std::ofstream output (discovery);
      if (!output || !(output << value ({{"version", 1}, {"pid", getpid ()},
          {"endpoint", endpoint}, {"server_key", public_key}}).dump (2)))
        throw std::runtime_error ("Cannot publish AUDMAP endpoint");
    }
    chmod (discovery.c_str (), 0600);
    ready ();
    while (!stopping.load ()) {
      zmq::pollitem_t items[] {{zap.handle (), 0, ZMQ_POLLIN, 0}, {socket.handle (), 0, ZMQ_POLLIN, 0}};
      try { zmq::poll (items, 2, std::chrono::milliseconds (10)); }
      catch (const zmq::error_t& e) { if (e.num () == EINTR) continue; throw; }
      if (items[0].revents & ZMQ_POLLIN) authenticate (zap);
      if (items[1].revents & ZMQ_POLLIN) receive (socket);
      std::deque<std::function<void (impl&)>> events;
      { std::lock_guard<std::mutex> lock (incoming->mutex); events.swap (incoming->events); }
      for (auto& event: events) event (*this);
      std::vector<std::string> expired;
      for (auto& c: connections) {
        if (clock::now () - c.second.touched > std::chrono::seconds (15)) expired.push_back (c.first);
        else if (c.second.session) c.second.session->poll ();
      }
      for (const auto& peer: expired) forget (peer);
    }
  }
};

local_server::local_server (std::shared_ptr<const resolver_registry> registry,
    authorization_ui ui, std::size_t resolutions, std::size_t operations) {
  if (!registry || !ui.connect || !ui.confirm) throw std::invalid_argument ("Missing AUDMAP authorization interface");
  implementation = std::make_unique<impl> (std::move (registry), std::move (ui), resolutions, operations);
}
local_server::~local_server () = default;
const std::filesystem::path& local_server::discovery_file () const { return implementation->discovery; }
} // namespace athena::interop
