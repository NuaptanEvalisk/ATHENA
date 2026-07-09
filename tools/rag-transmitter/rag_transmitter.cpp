/******************************************************************************
* MODULE     : rag_transmitter.cpp
* DESCRIPTION: Standalone ATHENA RAG delegation transmitter
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "rag_delegation_crypto.hpp"

#include <boost/json.hpp>
#include <boost/json/src.hpp>

#include <arpa/inet.h>
#include <netdb.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cctype>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
namespace json = boost::json;
namespace crypto = athena::rag::delegation;

namespace {

struct Config {
  std::string listen_address= "127.0.0.1";
  int port= 8766;
  fs::path key_dir;
  fs::path accepted_clients;
  fs::path pending_clients;
  std::string upstream_url;
  std::string upstream_public_key;
  std::string upstream_fingerprint;
  std::string pre_forward_script;
  std::string post_forward_script;
  int timeout_seconds= 300;
  int idle_shutdown_seconds= 0;
};

struct HttpRequest {
  std::string method;
  std::string path;
  std::map<std::string, std::string> headers;
  std::string body;
};

struct HttpResponse {
  int status= 0;
  std::string body;
};

struct ParsedUrl {
  std::string host;
  std::string port= "80";
  std::string path= "/";
};

static Config config;
static crypto::KeyPair transmitter_keypair;

std::string
read_file (const fs::path& path) {
  std::ifstream in (path, std::ios::binary);
  std::ostringstream ss;
  ss << in.rdbuf ();
  return ss.str ();
}

bool
write_file (const fs::path& path, const std::string& text,
            std::string& error) {
  std::error_code ec;
  fs::create_directories (path.parent_path (), ec);
  if (ec) {
    error= "failed to create " + path.parent_path ().generic_string () +
           ": " + ec.message ();
    return false;
  }
  std::ofstream out (path, std::ios::binary | std::ios::trunc);
  if (!out) {
    error= "failed to write " + path.generic_string ();
    return false;
  }
  out << text;
  return true;
}

std::string
trim (std::string s) {
  while (!s.empty () &&
         (s.back () == '\r' || s.back () == '\n' ||
          s.back () == ' ' || s.back () == '\t'))
    s.pop_back ();
  size_t i= 0;
  while (i < s.size () &&
         (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n'))
    i++;
  return s.substr (i);
}

std::string
lower (std::string s) {
  for (char& c: s)
    c= char (std::tolower (static_cast<unsigned char> (c)));
  return s;
}

std::string
json_string (const json::object& obj, const char* key,
             const std::string& def= "") {
  auto it= obj.find (key);
  if (it == obj.end () || !it->value ().is_string ()) return def;
  json::string_view sv= it->value ().as_string ();
  return std::string (sv.data (), sv.size ());
}

int
json_int (const json::object& obj, const char* key, int def) {
  auto it= obj.find (key);
  if (it == obj.end ()) return def;
  const json::value& v= it->value ();
  if (v.is_int64 ()) return int (v.as_int64 ());
  if (v.is_uint64 ()) return int (v.as_uint64 ());
  if (v.is_double ()) return int (v.as_double ());
  if (v.is_string ()) {
    try {
      return std::stoi (std::string (v.as_string ().data (),
                                     v.as_string ().size ()));
    }
    catch (...) { return def; }
  }
  return def;
}

json::object
parse_json_object (const std::string& text, std::string& error) {
  boost::system::error_code ec;
  json::value v= json::parse (text, ec);
  if (ec || !v.is_object ()) {
    error= ec ? ec.message (): "JSON root is not an object";
    return {};
  }
  return v.as_object ();
}

std::string
now_iso_utc () {
  std::time_t t= std::time (nullptr);
  std::tm tm {};
  gmtime_r (&t, &tm);
  char buf[32];
  std::strftime (buf, sizeof (buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
  return buf;
}

fs::path
default_key_dir () {
  const char* home= std::getenv ("HOME");
  fs::path base= home == nullptr || home[0] == '\0' ? fs::path ("."):
                                                      fs::path (home);
  return base / ".config" / "ATHENA" / "rag-transmitter";
}

bool
load_config (const fs::path& path, Config& out, std::string& error) {
  json::object root= parse_json_object (read_file (path), error);
  if (!error.empty ()) return false;
  out.listen_address= json_string (root, "listen_address",
                                   out.listen_address);
  out.port= json_int (root, "listen_port",
             json_int (root, "port", out.port));
  out.key_dir= fs::path (json_string (root, "key_dir",
                                      default_key_dir ().generic_string ()));
  out.accepted_clients= fs::path (
    json_string (root, "accepted_clients",
                 (out.key_dir / "accepted-clients.json").generic_string ()));
  out.pending_clients= fs::path (
    json_string (root, "pending_clients",
                 (out.key_dir / "pending-clients.json").generic_string ()));
  out.upstream_url= json_string (root, "upstream_url");
  out.upstream_public_key= json_string (root, "upstream_public_key");
  out.upstream_fingerprint= json_string (root, "upstream_fingerprint");
  out.pre_forward_script= json_string (root, "pre_forward_script");
  out.post_forward_script= json_string (root, "post_forward_script");
  out.timeout_seconds= json_int (root, "timeout_seconds",
                                 out.timeout_seconds);
  out.idle_shutdown_seconds= json_int (root, "idle_shutdown_seconds",
                                       out.idle_shutdown_seconds);

  auto upstream= root.find ("upstream");
  if (upstream != root.end () && upstream->value ().is_object ()) {
    const json::object& up= upstream->value ().as_object ();
    out.upstream_url= json_string (up, "url", out.upstream_url);
    out.upstream_public_key=
      json_string (up, "public_key", out.upstream_public_key);
    out.upstream_fingerprint=
      json_string (up, "fingerprint", out.upstream_fingerprint);
  }
  if (out.upstream_url.empty ()) {
    error= "upstream_url is required";
    return false;
  }
  return true;
}

json::object
error_object (const std::string& message) {
  json::object o;
  o["ok"]= false;
  o["error"]= message;
  return o;
}

json::object
identity_object () {
  json::object o;
  o["name"]= "ATHENA RAG Transmitter";
  o["kind"]= "transmitter";
  o["protocol"]= 1;
  o["public_key"]= crypto::base64_encode (transmitter_keypair.public_key);
  o["fingerprint"]=
    crypto::fingerprint_for_public_key (transmitter_keypair.public_key);
  json::array caps;
  caps.emplace_back ("rag-delegation-v1");
  caps.emplace_back ("embedding-patch");
  caps.emplace_back ("pending-enrollment");
  caps.emplace_back ("pre-post-forward-scripts");
  o["capabilities"]= caps;
  return o;
}

bool
load_public_key_list (const fs::path& path, std::vector<std::string>& keys,
                      std::string& error) {
  keys.clear ();
  if (path.empty () || !fs::exists (path)) return true;
  json::object root= parse_json_object (read_file (path), error);
  if (!error.empty ()) return false;
  auto it= root.find ("accepted");
  if (it == root.end () || !it->value ().is_array ()) return true;
  for (const json::value& v: it->value ().as_array ()) {
    if (!v.is_string ()) continue;
    json::string_view sv= v.as_string ();
    std::string decoded;
    std::string local_error;
    if (crypto::base64_decode (std::string (sv.data (), sv.size ()),
                               decoded, local_error))
      keys.push_back (decoded);
  }
  return true;
}

bool
public_key_accepted (const std::string& public_key) {
  std::vector<std::string> keys;
  std::string error;
  if (!load_public_key_list (config.accepted_clients, keys, error)) {
    std::cerr << "accepted clients error: " << error << "\n";
    return false;
  }
  for (const std::string& key: keys)
    if (key == public_key) return true;
  return false;
}

bool
append_pending_client (const std::string& public_key, std::string& error) {
  json::array pending;
  if (fs::exists (config.pending_clients)) {
    json::object root= parse_json_object (read_file (config.pending_clients),
                                          error);
    if (!error.empty ()) return false;
    auto it= root.find ("pending");
    if (it != root.end () && it->value ().is_array ())
      pending= it->value ().as_array ();
  }
  std::string key64= crypto::base64_encode (public_key);
  for (const json::value& value: pending) {
    if (value.is_string ()) {
      json::string_view sv= value.as_string ();
      if (std::string (sv.data (), sv.size ()) == key64) return true;
    }
    if (value.is_object () &&
        json_string (value.as_object (), "public_key") == key64)
      return true;
  }
  json::object item;
  item["public_key"]= key64;
  item["fingerprint"]= crypto::fingerprint_for_public_key (public_key);
  item["requested_at"]= now_iso_utc ();
  pending.emplace_back (item);
  json::object root;
  root["pending"]= pending;
  return write_file (config.pending_clients, json::serialize (root) + "\n",
                     error);
}

bool
send_all (int fd, const std::string& bytes) {
  size_t sent= 0;
  while (sent < bytes.size ()) {
    ssize_t n= ::send (fd, bytes.data () + sent, bytes.size () - sent, 0);
    if (n <= 0) return false;
    sent += size_t (n);
  }
  return true;
}

std::optional<HttpRequest>
read_request (int fd) {
  std::string data;
  char buf[8192];
  size_t header_end= std::string::npos;
  int content_length= 0;
  while (true) {
    ssize_t n= ::recv (fd, buf, sizeof (buf), 0);
    if (n <= 0) return std::nullopt;
    data.append (buf, size_t (n));
    header_end= data.find ("\r\n\r\n");
    if (header_end == std::string::npos) continue;
    std::istringstream hs (data.substr (0, header_end));
    std::string line;
    std::getline (hs, line);
    while (std::getline (hs, line)) {
      size_t colon= line.find (':');
      if (colon == std::string::npos) continue;
      std::string name= lower (trim (line.substr (0, colon)));
      std::string value= trim (line.substr (colon + 1));
      if (name == "content-length") {
        try { content_length= std::stoi (value); }
        catch (...) { content_length= 0; }
      }
    }
    if (data.size () >= header_end + 4 + size_t (content_length)) break;
  }

  HttpRequest req;
  std::istringstream head (data.substr (0, header_end));
  std::string first;
  std::getline (head, first);
  std::istringstream first_stream (first);
  first_stream >> req.method >> req.path;
  size_t query= req.path.find ('?');
  if (query != std::string::npos) req.path.resize (query);
  std::string line;
  while (std::getline (head, line)) {
    size_t colon= line.find (':');
    if (colon == std::string::npos) continue;
    req.headers[lower (trim (line.substr (0, colon)))]=
      trim (line.substr (colon + 1));
  }
  req.body= data.substr (header_end + 4, size_t (content_length));
  return req;
}

std::string
http_reason (int status) {
  if (status == 200) return "OK";
  if (status == 400) return "Bad Request";
  if (status == 404) return "Not Found";
  if (status == 405) return "Method Not Allowed";
  return "Error";
}

void
write_response (int fd, int status, const std::string& type,
                const std::string& body) {
  std::ostringstream out;
  out << "HTTP/1.1 " << status << " " << http_reason (status) << "\r\n"
      << "Content-Type: " << type << "\r\n"
      << "Content-Length: " << body.size () << "\r\n"
      << "Connection: close\r\n\r\n"
      << body;
  send_all (fd, out.str ());
}

ParsedUrl
parse_url (const std::string& url, std::string& error) {
  ParsedUrl out;
  std::string rest= url;
  const std::string prefix= "http://";
  if (rest.rfind (prefix, 0) != 0) {
    error= "only http:// upstream URLs are supported";
    return out;
  }
  rest= rest.substr (prefix.size ());
  size_t slash= rest.find ('/');
  std::string hostport= slash == std::string::npos ? rest:
                                                     rest.substr (0, slash);
  out.path= slash == std::string::npos ? "/": rest.substr (slash);
  if (out.path.empty ()) out.path= "/";
  if (!hostport.empty () && hostport.front () == '[') {
    size_t close= hostport.find (']');
    if (close == std::string::npos) {
      error= "invalid IPv6 URL";
      return out;
    }
    out.host= hostport.substr (1, close - 1);
    if (close + 1 < hostport.size () && hostport[close + 1] == ':')
      out.port= hostport.substr (close + 2);
  }
  else {
    size_t colon= hostport.rfind (':');
    if (colon != std::string::npos) {
      out.host= hostport.substr (0, colon);
      out.port= hostport.substr (colon + 1);
    }
    else out.host= hostport;
  }
  if (out.host.empty ()) error= "URL host is empty";
  return out;
}

std::string
join_url (const std::string& base, const std::string& suffix) {
  std::string out= base;
  while (!out.empty () && out.back () == '/') out.pop_back ();
  return out + suffix;
}

HttpResponse
http_request (const std::string& method, const std::string& url,
              const std::string& body, std::string& error) {
  ParsedUrl parsed= parse_url (url, error);
  if (!error.empty ()) return {};

  struct addrinfo hints {};
  hints.ai_family= AF_UNSPEC;
  hints.ai_socktype= SOCK_STREAM;
  struct addrinfo* result= nullptr;
  int rc= ::getaddrinfo (parsed.host.c_str (), parsed.port.c_str (),
                         &hints, &result);
  if (rc != 0) {
    error= std::string ("getaddrinfo failed: ") + gai_strerror (rc);
    return {};
  }

  int fd= -1;
  for (struct addrinfo* p= result; p != nullptr; p= p->ai_next) {
    fd= ::socket (p->ai_family, p->ai_socktype, p->ai_protocol);
    if (fd < 0) continue;
    struct timeval tv {};
    tv.tv_sec= config.timeout_seconds;
    ::setsockopt (fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof (tv));
    ::setsockopt (fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof (tv));
    if (::connect (fd, p->ai_addr, p->ai_addrlen) == 0) break;
    ::close (fd);
    fd= -1;
  }
  ::freeaddrinfo (result);
  if (fd < 0) {
    error= "failed to connect to upstream";
    return {};
  }

  std::ostringstream req;
  req << method << " " << parsed.path << " HTTP/1.1\r\n"
      << "Host: " << parsed.host << "\r\n"
      << "Connection: close\r\n";
  if (method == "POST")
    req << "Content-Type: application/json; charset=utf-8\r\n"
        << "Content-Length: " << body.size () << "\r\n";
  req << "\r\n";
  if (method == "POST") req << body;
  if (!send_all (fd, req.str ())) {
    error= "failed to write upstream request";
    ::close (fd);
    return {};
  }

  std::string response;
  char buf[8192];
  while (true) {
    ssize_t n= ::recv (fd, buf, sizeof (buf), 0);
    if (n == 0) break;
    if (n < 0) {
      error= "failed to read upstream response";
      ::close (fd);
      return {};
    }
    response.append (buf, size_t (n));
  }
  ::close (fd);
  size_t split= response.find ("\r\n\r\n");
  if (split == std::string::npos) {
    error= "invalid upstream HTTP response";
    return {};
  }
  std::istringstream first (response.substr (0, split));
  std::string http;
  HttpResponse out;
  first >> http >> out.status;
  out.body= response.substr (split + 4);
  return out;
}

bool
get_upstream_public_key (std::string& public_key, std::string& error) {
  if (!config.upstream_public_key.empty ()) {
    if (!crypto::base64_decode (config.upstream_public_key, public_key, error))
      return false;
    if (!config.upstream_fingerprint.empty () &&
        crypto::fingerprint_for_public_key (public_key) !=
          config.upstream_fingerprint) {
      error= "configured upstream public key does not match fingerprint";
      return false;
    }
    return true;
  }

  HttpResponse res= http_request (
    "GET", join_url (config.upstream_url, "/athena-rag/v1/identity"),
    "", error);
  if (!error.empty ()) return false;
  if (res.status != 200) {
    error= "upstream identity returned HTTP " + std::to_string (res.status);
    return false;
  }
  json::object id= parse_json_object (res.body, error);
  if (!error.empty ()) return false;
  if (json_int (id, "protocol", 0) != 1) {
    error= "upstream is not an ATHENA RAG delegation v1 server";
    return false;
  }
  std::string public64= json_string (id, "public_key");
  if (!crypto::base64_decode (public64, public_key, error)) return false;
  std::string fp= crypto::fingerprint_for_public_key (public_key);
  if (!config.upstream_fingerprint.empty () &&
      fp != config.upstream_fingerprint) {
    error= "upstream public key does not match pinned fingerprint";
    return false;
  }
  return true;
}

int
run_script (const std::string& command, const std::string& phase,
            const std::string& client_public_key) {
  if (command.empty ()) return 0;
  std::string fp= crypto::fingerprint_for_public_key (client_public_key);
  ::setenv ("ATHENA_RAG_TRANSMITTER_PHASE", phase.c_str (), 1);
  ::setenv ("ATHENA_RAG_CLIENT_FINGERPRINT", fp.c_str (), 1);
  ::setenv ("ATHENA_RAG_UPSTREAM", config.upstream_url.c_str (), 1);
  return std::system (command.c_str ());
}

json::object
wrap_encrypted_json (const std::string& recipient_public_key,
                     const std::string& plain_json) {
  std::string nonce64, cipher64, error;
  if (!crypto::encrypt_payload (transmitter_keypair, recipient_public_key,
                                plain_json, nonce64, cipher64, error))
    return error_object (error);
  json::object out;
  out["ok"]= true;
  out["sender"]= crypto::base64_encode (transmitter_keypair.public_key);
  out["nonce"]= nonce64;
  out["ciphertext"]= cipher64;
  return out;
}

std::string
forward_plain_rpc (const std::string& plain_json,
                   const std::string& client_public_key,
                   std::string& error) {
  int pre_rc= run_script (config.pre_forward_script, "pre",
                          client_public_key);
  if (pre_rc != 0) {
    error= "pre-forward script failed with status " +
           std::to_string (pre_rc);
    return "";
  }

  std::string upstream_public_key;
  if (!get_upstream_public_key (upstream_public_key, error)) return "";

  std::string nonce64, cipher64;
  if (!crypto::encrypt_payload (transmitter_keypair, upstream_public_key,
                                plain_json, nonce64, cipher64, error))
    return "";
  json::object env;
  env["sender"]= crypto::base64_encode (transmitter_keypair.public_key);
  env["nonce"]= nonce64;
  env["ciphertext"]= cipher64;

  HttpResponse res= http_request (
    "POST", join_url (config.upstream_url, "/athena-rag/v1/rpc"),
    json::serialize (env), error);
  if (!error.empty ()) return "";
  if (res.status != 200) {
    error= "upstream RPC returned HTTP " + std::to_string (res.status);
    return "";
  }
  json::object reply= parse_json_object (res.body, error);
  if (!error.empty ()) return "";
  auto ok_it= reply.find ("ok");
  if (ok_it == reply.end () || !ok_it->value ().is_bool () ||
      !ok_it->value ().as_bool ()) {
    error= json_string (reply, "error", "upstream RPC failed");
    return "";
  }

  std::string sender;
  if (!crypto::base64_decode (json_string (reply, "sender"),
                              sender, error))
    return "";
  if (sender != upstream_public_key) {
    error= "upstream response key does not match pinned server key";
    return "";
  }

  std::string plain_reply;
  if (!crypto::decrypt_payload (
        transmitter_keypair, sender, json_string (reply, "nonce"),
        json_string (reply, "ciphertext"), plain_reply, error))
    return "";

  if (config.idle_shutdown_seconds > 0)
    std::this_thread::sleep_for (
      std::chrono::seconds (config.idle_shutdown_seconds));
  int post_rc= run_script (config.post_forward_script, "post",
                           client_public_key);
  if (post_rc != 0)
    std::cerr << "post-forward script failed with status "
              << post_rc << "\n";
  return plain_reply;
}

json::object
handle_plain_rpc (const json::object& plain,
                  const std::string& sender_public_key) {
  std::string method= json_string (plain, "method");
  if (method == "rag.enroll") {
    std::string error;
    if (!append_pending_client (sender_public_key, error))
      return error_object (error);
    json::object result;
    result["ok"]= true;
    result["status"]= public_key_accepted (sender_public_key)?
      "accepted": "pending";
    result["fingerprint"]= crypto::fingerprint_for_public_key (
      sender_public_key);
    return result;
  }
  if (method == "rag.auth.check") {
    json::object result;
    result["ok"]= true;
    result["status"]= public_key_accepted (sender_public_key)?
      "accepted": "pending";
    return result;
  }
  if (!public_key_accepted (sender_public_key))
    return error_object ("client public key is not accepted");
  if (method == "rag.embedding.build_patch") {
    std::string error;
    std::string reply= forward_plain_rpc (json::serialize (plain),
                                          sender_public_key, error);
    if (!error.empty ()) return error_object (error);
    std::string parse_error;
    json::object forwarded= parse_json_object (reply, parse_error);
    if (!parse_error.empty ()) return error_object (parse_error);
    return forwarded;
  }
  return error_object ("unknown delegation method");
}

json::object
handle_envelope (const std::string& body) {
  std::string error;
  json::object envelope= parse_json_object (body, error);
  if (!error.empty ()) return error_object (error);
  std::string sender_public_key;
  if (!crypto::base64_decode (json_string (envelope, "sender"),
                              sender_public_key, error))
    return error_object (error);
  std::string plain_json;
  if (!crypto::decrypt_payload (
        transmitter_keypair, sender_public_key,
        json_string (envelope, "nonce"),
        json_string (envelope, "ciphertext"),
        plain_json, error))
    return error_object (error);
  json::object plain= parse_json_object (plain_json, error);
  if (!error.empty ()) {
    json::object plain_error= error_object (error);
    return wrap_encrypted_json (sender_public_key,
                                json::serialize (plain_error));
  }
  json::object result= handle_plain_rpc (plain, sender_public_key);
  return wrap_encrypted_json (sender_public_key, json::serialize (result));
}

void
handle_connection (int fd) {
  std::optional<HttpRequest> maybe_req= read_request (fd);
  if (!maybe_req) {
    write_response (fd, 400, "text/plain", "Bad request");
    return;
  }
  const HttpRequest& req= *maybe_req;
  if (req.path == "/athena-rag/v1/identity") {
    if (req.method != "GET")
      write_response (fd, 405, "text/plain", "Method not allowed");
    else
      write_response (fd, 200, "application/json",
                      json::serialize (identity_object ()));
    return;
  }
  if (req.path == "/athena-rag/v1/rpc") {
    if (req.method != "POST")
      write_response (fd, 405, "text/plain", "Method not allowed");
    else
      write_response (fd, 200, "application/json",
                      json::serialize (handle_envelope (req.body)));
    return;
  }
  write_response (fd, 404, "text/plain", "Not found");
}

int
listen_socket (std::string& error) {
  struct addrinfo hints {};
  hints.ai_family= AF_UNSPEC;
  hints.ai_socktype= SOCK_STREAM;
  hints.ai_flags= AI_PASSIVE;
  struct addrinfo* result= nullptr;
  std::string service= std::to_string (config.port);
  const char* node= config.listen_address.empty () ? nullptr:
                                                    config.listen_address.c_str ();
  int rc= ::getaddrinfo (node, service.c_str (), &hints, &result);
  if (rc != 0) {
    error= std::string ("getaddrinfo failed: ") + gai_strerror (rc);
    return -1;
  }
  int fd= -1;
  for (struct addrinfo* p= result; p != nullptr; p= p->ai_next) {
    fd= ::socket (p->ai_family, p->ai_socktype, p->ai_protocol);
    if (fd < 0) continue;
    int yes= 1;
    ::setsockopt (fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof (yes));
    if (::bind (fd, p->ai_addr, p->ai_addrlen) == 0 &&
        ::listen (fd, 32) == 0)
      break;
    ::close (fd);
    fd= -1;
  }
  ::freeaddrinfo (result);
  if (fd < 0) error= "failed to bind listen socket";
  return fd;
}

void
usage () {
  std::cerr
    << "Usage: rag-transmitter --config config.json "
    << "[--generate-keypair]\n";
}

} // namespace

int
main (int argc, char** argv) {
  fs::path config_path;
  bool generate_keypair= false;
  for (int i= 1; i < argc; i++) {
    std::string arg= argv[i];
    if (arg == "--config" && i + 1 < argc)
      config_path= argv[++i];
    else if (arg == "--generate-keypair")
      generate_keypair= true;
    else {
      usage ();
      return 2;
    }
  }
  if (config_path.empty ()) {
    usage ();
    return 2;
  }

  std::string error;
  if (!load_config (config_path, config, error)) {
    std::cerr << "rag-transmitter: " << error << "\n";
    return 1;
  }
  bool generated= false;
  if (!crypto::ensure_keypair (config.key_dir, "server",
                               transmitter_keypair, &generated, error)) {
    std::cerr << "rag-transmitter: " << error << "\n";
    return 1;
  }
  if (generated || generate_keypair) {
    std::cout << "ATHENA RAG transmitter keypair "
              << (generated ? "generated": "already exists")
              << " in " << config.key_dir.generic_string () << "\n"
              << "Public key: "
              << crypto::base64_encode (transmitter_keypair.public_key)
              << "\n"
              << "Fingerprint: "
              << crypto::fingerprint_for_public_key (
                   transmitter_keypair.public_key)
              << "\n"
              << "Accepted clients file: "
              << config.accepted_clients.generic_string () << "\n";
  }
  if (generate_keypair) return 0;

  ::signal (SIGPIPE, SIG_IGN);
  int fd= listen_socket (error);
  if (fd < 0) {
    std::cerr << "rag-transmitter: " << error << "\n";
    return 1;
  }
  std::cout << "rag-transmitter: listening on http://"
            << config.listen_address << ":" << config.port
            << "/athena-rag/v1/identity\n";
  while (true) {
    int client= ::accept (fd, nullptr, nullptr);
    if (client < 0) {
      if (errno == EINTR) continue;
      std::cerr << "rag-transmitter: accept failed: "
                << std::strerror (errno) << "\n";
      continue;
    }
    handle_connection (client);
    ::close (client);
  }
  return 0;
}
