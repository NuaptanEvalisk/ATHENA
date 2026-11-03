/******************************************************************************
* MODULE     : connection.cpp
* DESCRIPTION: Client discovery, persistent CURVE authentication and heartbeats
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#include "connection.hpp"
#include <fstream>
#include <cerrno>
#include <fcntl.h>
#include <iostream>
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>

namespace athena::interop {
namespace {
value read_descriptor (std::filesystem::path path) {
  path = std::filesystem::absolute (path);
  struct stat directory {}, file {};
  if (lstat (path.parent_path ().c_str (), &directory) || !S_ISDIR (directory.st_mode) ||
      directory.st_uid != getuid () || (directory.st_mode & 077))
    throw std::runtime_error ("AUDMAP endpoint descriptor must be a private file owned by this user");
  const int fd = ::open (path.c_str (), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
  if (fd < 0) throw std::runtime_error ("Cannot open AUDMAP endpoint descriptor");
  struct close_descriptor { int fd; ~close_descriptor () { ::close (fd); } } guard {fd};
  if (fstat (fd, &file) || !S_ISREG (file.st_mode) || file.st_uid != getuid () ||
      (file.st_mode & 077) || file.st_size > 65536)
    throw std::runtime_error ("AUDMAP endpoint descriptor must be a private file owned by this user");
  std::string bytes;
  char block[4096];
  for (;;) {
    const auto size = ::read (fd, block, sizeof (block));
    if (size < 0 && errno == EINTR) continue;
    if (size < 0) throw std::runtime_error ("Cannot read AUDMAP endpoint descriptor");
    if (!size) break;
    bytes.append (block, size);
    if (bytes.size () > 65536) throw std::length_error ("Endpoint descriptor too large");
  }
  return value::parse (bytes);
}
std::filesystem::path discover () {
  const char* runtime = std::getenv ("XDG_RUNTIME_DIR");
  const std::filesystem::path root = runtime && *runtime ? runtime : "/tmp";
  std::vector<std::filesystem::path> candidates;
  for (const auto& entry: std::filesystem::directory_iterator (root)) {
    if (entry.path ().filename ().string ().find ("athena-audmap-") != 0) continue;
    try {
      const auto data = read_descriptor (entry.path () / "connection.json");
      const auto pid = data.at ("pid").get<int> ();
      if (pid > 0 && kill (pid, 0) == 0 && std::filesystem::exists (entry.path () / "socket"))
        candidates.push_back (entry.path () / "connection.json");
    }
    catch (const std::exception&) {}
  }
  if (candidates.size () == 1) return candidates.front ();
  std::string error = candidates.empty () ? "No running ATHENA AUDMAP endpoint found" : "Multiple ATHENA endpoints; choose --endpoint:";
  for (const auto& p: candidates) error += "\n  " + p.string ();
  throw std::runtime_error (error);
}
} // namespace

client_connection::client_connection (std::filesystem::path discovery,
    const std::filesystem::path& identity_path, const std::string& name) {
  if (discovery.empty ()) discovery = discover ();
  const auto descriptor = read_descriptor (discovery);
  if (descriptor.at ("version") != audmap_endpoint_descriptor_version ||
      descriptor.at ("protocol_version") != audmap_protocol_version ||
      descriptor.at ("document_model_version") != audmap_document_model_version)
    throw std::invalid_argument ("Incompatible AUDMAP endpoint/protocol/document model version");
  const std::string endpoint = descriptor.at ("endpoint");
  if (endpoint.compare (0, 6, "ipc://") != 0) throw std::invalid_argument ("Only local IPC endpoints are supported");
  const auto identity = load_client_identity (identity_path);
  socket.set (zmq::sockopt::routing_id, identity.public_key);
  socket.set (zmq::sockopt::curve_publickey, identity.public_key);
  socket.set (zmq::sockopt::curve_secretkey, identity.secret_key);
  socket.set (zmq::sockopt::curve_serverkey, descriptor.at ("server_key").get<std::string> ());
  socket.set (zmq::sockopt::linger, 100);
  socket.set (zmq::sockopt::sndtimeo, 1000);
  socket.set (zmq::sockopt::maxmsgsize, static_cast<std::int64_t> (wire_size_limit));
  socket.connect (endpoint);
  send (value::array ({static_cast<unsigned> (transport_opcode::hello),
                       audmap_protocol_version, audmap_document_model_version, name}));
}
client_connection::~client_connection () {
  try { send (value::array ({static_cast<unsigned> (transport_opcode::bye)})); }
  catch (...) {}
}
void client_connection::send (const value& message) {
  const auto bytes = encode_message (message);
  if (!socket.send (zmq::buffer (bytes))) throw std::runtime_error ("AUDMAP send timed out");
}
std::optional<value> client_connection::receive () {
  zmq::message_t bytes;
  if (!socket.recv (bytes, zmq::recv_flags::dontwait)) return {};
  return decode_message (bytes.to_string_view ());
}
void client_connection::tick () {
  if (std::chrono::steady_clock::now () - heartbeat >= std::chrono::seconds (1)) {
    send (value::array ({static_cast<unsigned> (transport_opcode::ping)}));
    heartbeat = std::chrono::steady_clock::now ();
  }
}
void client_connection::wait_for_authorization () {
  const auto deadline = std::chrono::steady_clock::now () + std::chrono::minutes (5);
  bool announced = false;
  for (;;) {
    zmq::pollitem_t item {handle (), 0, ZMQ_POLLIN, 0};
    zmq::poll (&item, 1, std::chrono::milliseconds (100));
    tick ();
    if (auto msg = receive ()) {
      const auto op = msg->at (0).get<unsigned> ();
      if (op == static_cast<unsigned> (transport_opcode::welcome)) {
        if (msg->size () != 2 || !msg->at (1).is_object () ||
            msg->at (1).value ("protocol_version", 0u) != audmap_protocol_version ||
            msg->at (1).value ("document_model_version", 0u) != audmap_document_model_version)
          throw std::runtime_error ("AUDMAP server accepted an incompatible model version");
        return;
      }
      if (op == static_cast<unsigned> (transport_opcode::rejected)) throw std::runtime_error (msg->dump ());
      if (!announced) { std::cerr << "Waiting for ATHENA authorization...\n"; announced = true; }
    }
    if (std::chrono::steady_clock::now () >= deadline) throw std::runtime_error ("Connection approval timed out");
  }
}
} // namespace athena::interop
