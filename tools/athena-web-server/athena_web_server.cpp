/******************************************************************************
* MODULE     : athena_web_server.cpp
* DESCRIPTION: WebRTC access broker for isolated native-Wayland ATHENA sessions
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "web_server_common.hpp"

#include <boost/json.hpp>
#include <curl/curl.h>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <spawn.h>
#include <sys/file.h>
#include <sys/random.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fs = std::filesystem;
namespace json = boost::json;
using namespace athena::web;
using Clock = std::chrono::steady_clock;
using WallClock = std::chrono::system_clock;

extern char** environ;

namespace {

constexpr size_t max_process_output= 1024 * 1024;
constexpr int process_timeout_seconds= 90;

std::atomic<bool> stopping {false};
int listener_fd= -1;

class CurlGlobal {
public:
  CurlGlobal () {
    if (::curl_global_init (CURL_GLOBAL_DEFAULT) != CURLE_OK)
      throw std::runtime_error ("could not initialize TLS client");
  }
  ~CurlGlobal () { ::curl_global_cleanup (); }

  CurlGlobal (const CurlGlobal&)= delete;
  CurlGlobal& operator= (const CurlGlobal&)= delete;
};

struct Config {
  std::string listen_address= "127.0.0.1";
  int port= 8090;
  std::string container_runtime= "podman";
  std::string image= "localhost/athena-web:latest";
  fs::path state_dir;
  std::optional<fs::path> web_root;
  uint64_t max_memory= uint64_t (4) << 30;
  uint64_t storage_limit= uint64_t (2) << 30;
  size_t max_connections= 4;
  int session_seconds= 60 * 60;
  int warning_seconds= 5 * 60;
  int heartbeat_timeout_seconds= 45;
  int expired_retention_seconds= 15 * 60;
  int startup_timeout_seconds= 45;
  int width= 1920;
  int height= 1080;
  int framerate= 30;
  int video_min_bitrate= 4'000'000;
  int video_start_bitrate= 12'000'000;
  int video_max_bitrate= 24'000'000;
  std::string stun_server= "stun://stun.l.google.com:19302";
  std::vector<std::string> turn_servers;
  std::string cloudflare_turn_key_id;
  std::optional<fs::path> cloudflare_turn_token_file;
};

enum class SessionPhase {
  starting,
  running,
  expired,
  failed,
  closing
};

struct Session {
  mutable std::mutex mutex;
  std::mutex transfer_mutex;
  std::mutex lifecycle_mutex;
  std::string token;
  std::string sandbox_name;
  std::string streamer_name;
  fs::path state_dir;
  int vnc_bridge_port= 0;
  int signaling_port= 0;
  SessionPhase phase= SessionPhase::starting;
  Clock::time_point created_at= Clock::now ();
  Clock::time_point expires_at;
  Clock::time_point last_heartbeat= Clock::now ();
  Clock::time_point retained_until;
  std::string error;
  fs::path expired_archive;
  std::string stun_server;
  std::vector<IceServer> ice_servers;
  std::atomic<bool> cleanup_started {false};
};

struct ProcessResult {
  int status= -1;
  bool timed_out= false;
  std::string output;
};

struct DownloadMaterialization {
  fs::path path;
  fs::path cleanup_root;
  std::string filename;
};

std::string
lower_ascii (std::string text) {
  std::transform (text.begin (), text.end (), text.begin (), [] (char c) {
    return char (std::tolower (static_cast<unsigned char> (c)));
  });
  return text;
}

std::string
trim (std::string text) {
  while (!text.empty () &&
         std::isspace (static_cast<unsigned char> (text.back ())))
    text.pop_back ();
  size_t first= 0;
  while (first < text.size () &&
         std::isspace (static_cast<unsigned char> (text[first])))
    first++;
  return text.substr (first);
}

bool
send_all (int fd, const void* data, size_t size) {
  const char* bytes= static_cast<const char*> (data);
  while (size != 0) {
    ssize_t sent= ::send (fd, bytes, size, MSG_NOSIGNAL);
    if (sent < 0 && errno == EINTR) continue;
    if (sent <= 0) return false;
    bytes+= sent;
    size-= size_t (sent);
  }
  return true;
}

bool
send_all (int fd, const std::string& text) {
  return send_all (fd, text.data (), text.size ());
}

std::string
status_text (int status) {
  switch (status) {
  case 200: return "OK";
  case 201: return "Created";
  case 202: return "Accepted";
  case 204: return "No Content";
  case 400: return "Bad Request";
  case 404: return "Not Found";
  case 405: return "Method Not Allowed";
  case 409: return "Conflict";
  case 413: return "Payload Too Large";
  case 415: return "Unsupported Media Type";
  case 429: return "Too Many Requests";
  case 500: return "Internal Server Error";
  case 502: return "Bad Gateway";
  case 503: return "Service Unavailable";
  default: return "Error";
  }
}

void
send_response (int fd, int status, std::string_view content_type,
               const std::string& body,
               const std::vector<std::pair<std::string,std::string>>&
                 extra_headers= {}) {
  std::ostringstream head;
  head << "HTTP/1.1 " << status << ' ' << status_text (status) << "\r\n"
       << "Content-Type: " << content_type << "\r\n"
       << "Content-Length: " << body.size () << "\r\n"
       << "Cache-Control: no-store\r\n"
       << "X-Content-Type-Options: nosniff\r\n"
       << "Referrer-Policy: no-referrer\r\n";
  for (const auto& [name, value]: extra_headers)
    head << name << ": " << value << "\r\n";
  head << "Connection: close\r\n\r\n";
  send_all (fd, head.str ());
  send_all (fd, body);
}

void
send_json (int fd, int status, const json::value& value) {
  send_response (fd, status, "application/json; charset=utf-8",
                 json::serialize (value));
}

json::object
json_error (const std::string& message) {
  return {{"error", message}};
}

std::string
random_hex (size_t bytes) {
  std::vector<unsigned char> data (bytes);
  size_t done= 0;
  while (done < data.size ()) {
    ssize_t count= ::getrandom (data.data () + done, data.size () - done, 0);
    if (count < 0 && errno == EINTR) continue;
    if (count <= 0) throw std::runtime_error ("getrandom failed");
    done+= size_t (count);
  }
  static constexpr char hex[]= "0123456789abcdef";
  std::string result;
  result.reserve (bytes * 2);
  for (unsigned char byte: data) {
    result.push_back (hex[byte >> 4]);
    result.push_back (hex[byte & 15]);
  }
  return result;
}

std::string
phase_name (SessionPhase phase) {
  switch (phase) {
  case SessionPhase::starting: return "starting";
  case SessionPhase::running: return "running";
  case SessionPhase::expired: return "expired";
  case SessionPhase::failed: return "failed";
  case SessionPhase::closing: return "closing";
  }
  return "failed";
}

int64_t
unix_seconds_after (Clock::time_point when) {
  auto delta= when - Clock::now ();
  auto wall= WallClock::now () +
    std::chrono::duration_cast<WallClock::duration> (delta);
  return std::chrono::duration_cast<std::chrono::seconds> (
    wall.time_since_epoch ()).count ();
}

ProcessResult
run_process (const std::vector<std::string>& arguments,
             int timeout_seconds= process_timeout_seconds) {
  if (arguments.empty ()) throw std::invalid_argument ("empty command");
  int pipes[2];
  if (::pipe2 (pipes, O_CLOEXEC) != 0)
    throw std::runtime_error ("pipe2 failed");

  std::vector<char*> argv;
  argv.reserve (arguments.size () + 1);
  for (const std::string& argument: arguments)
    argv.push_back (const_cast<char*> (argument.c_str ()));
  argv.push_back (nullptr);

  posix_spawn_file_actions_t actions;
  int spawn_error= ::posix_spawn_file_actions_init (&actions);
  bool actions_initialized= spawn_error == 0;
  if (spawn_error == 0)
    spawn_error= ::posix_spawn_file_actions_adddup2 (
      &actions, pipes[1], STDOUT_FILENO);
  if (spawn_error == 0)
    spawn_error= ::posix_spawn_file_actions_adddup2 (
      &actions, pipes[1], STDERR_FILENO);
  if (spawn_error == 0)
    spawn_error= ::posix_spawn_file_actions_addclose (&actions, pipes[0]);
  if (spawn_error == 0)
    spawn_error= ::posix_spawn_file_actions_addclose (&actions, pipes[1]);

  pid_t pid= -1;
  if (spawn_error == 0)
    spawn_error= ::posix_spawnp (&pid, argv[0], &actions, nullptr,
                                 argv.data (), environ);
  if (actions_initialized) ::posix_spawn_file_actions_destroy (&actions);
  if (spawn_error != 0) {
    ::close (pipes[0]);
    ::close (pipes[1]);
    throw std::runtime_error (
      "posix_spawnp failed: " + std::string (std::strerror (spawn_error)));
  }

  ::close (pipes[1]);
  ProcessResult result;
  std::thread reader ([&] {
    std::array<char,8192> buffer {};
    while (true) {
      ssize_t count= ::read (pipes[0], buffer.data (), buffer.size ());
      if (count < 0 && errno == EINTR) continue;
      if (count <= 0) break;
      if (result.output.size () < max_process_output) {
        size_t room= max_process_output - result.output.size ();
        result.output.append (buffer.data (),
                              std::min (room, size_t (count)));
      }
    }
    ::close (pipes[0]);
  });

  int wait_status= 0;
  bool reaped= false;
  Clock::time_point deadline= Clock::now () +
    std::chrono::seconds (timeout_seconds);
  while (true) {
    pid_t waited= ::waitpid (pid, &wait_status, WNOHANG);
    if (waited == pid) {
      reaped= true;
      break;
    }
    if (waited < 0 && errno != EINTR) {
      result.status= -1;
      break;
    }
    if (Clock::now () >= deadline) {
      result.timed_out= true;
      ::kill (pid, SIGTERM);
      std::this_thread::sleep_for (std::chrono::milliseconds (250));
      if (::waitpid (pid, &wait_status, WNOHANG) == 0) {
        ::kill (pid, SIGKILL);
        ::waitpid (pid, &wait_status, 0);
      }
      reaped= true;
      break;
    }
    std::this_thread::sleep_for (std::chrono::milliseconds (25));
  }
  reader.join ();
  if (reaped && WIFEXITED (wait_status))
    result.status= WEXITSTATUS (wait_status);
  else if (reaped && WIFSIGNALED (wait_status))
    result.status= 128 + WTERMSIG (wait_status);
  return result;
}

std::string
read_secret_file (const fs::path& path) {
  int fd= ::open (path.c_str (), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
  if (fd < 0)
    throw std::runtime_error (
      "could not open Cloudflare TURN token file");
  struct stat metadata {};
  if (::fstat (fd, &metadata) != 0 || !S_ISREG (metadata.st_mode) ||
      metadata.st_uid != ::getuid () ||
      (metadata.st_mode & (S_IRWXG | S_IRWXO)) != 0 ||
      metadata.st_size <= 0 || metadata.st_size > 4096) {
    ::close (fd);
    throw std::runtime_error (
      "Cloudflare TURN token file must be a nonempty, owner-only regular file");
  }
  std::string result (size_t (metadata.st_size), '\0');
  size_t done= 0;
  while (done < result.size ()) {
    ssize_t count= ::read (fd, result.data () + done, result.size () - done);
    if (count < 0 && errno == EINTR) continue;
    if (count <= 0) {
      ::close (fd);
      throw std::runtime_error ("could not read Cloudflare TURN token file");
    }
    done+= size_t (count);
  }
  ::close (fd);
  result= trim (std::move (result));
  if (result.empty () || result.find_first_of ("\r\n") != std::string::npos)
    throw std::runtime_error ("Cloudflare TURN token file is malformed");
  return result;
}

size_t
append_curl_response (char* data, size_t size, size_t count, void* context) {
  size_t bytes= size * count;
  auto* output= static_cast<std::string*> (context);
  if (output->size () + bytes > max_process_output) return 0;
  output->append (data, bytes);
  return bytes;
}

std::optional<TurnCredentials>
generate_cloudflare_turn_credentials (const Config& config,
                                      std::string& error) {
  if (config.cloudflare_turn_key_id.empty () ||
      !config.cloudflare_turn_token_file) {
    error= "Cloudflare TURN provider is incompletely configured";
    return std::nullopt;
  }
  std::string token;
  try { token= read_secret_file (*config.cloudflare_turn_token_file); }
  catch (const std::exception& exception) {
    error= exception.what ();
    return std::nullopt;
  }

  CURL* curl= ::curl_easy_init ();
  if (!curl) {
    error= "could not initialize Cloudflare TURN request";
    return std::nullopt;
  }
  std::string response;
  std::string url= "https://rtc.live.cloudflare.com/v1/turn/keys/" +
    config.cloudflare_turn_key_id +
    "/credentials/generate-ice-servers";
  std::string authorization= "Authorization: Bearer " + token;
  curl_slist* headers= nullptr;
  headers= ::curl_slist_append (headers, authorization.c_str ());
  headers= ::curl_slist_append (headers, "Content-Type: application/json");
  constexpr char request[]= "{\"ttl\":172800}";
  ::curl_easy_setopt (curl, CURLOPT_URL, url.c_str ());
  ::curl_easy_setopt (curl, CURLOPT_HTTPHEADER, headers);
  ::curl_easy_setopt (curl, CURLOPT_POSTFIELDS, request);
  ::curl_easy_setopt (curl, CURLOPT_POSTFIELDSIZE, long (sizeof (request) - 1));
  ::curl_easy_setopt (curl, CURLOPT_CONNECTTIMEOUT, 10L);
  ::curl_easy_setopt (curl, CURLOPT_TIMEOUT, 20L);
  ::curl_easy_setopt (curl, CURLOPT_WRITEFUNCTION, append_curl_response);
  ::curl_easy_setopt (curl, CURLOPT_WRITEDATA, &response);
  ::curl_easy_setopt (curl, CURLOPT_USERAGENT, "ATHENA-Web/0.5");
  CURLcode status= ::curl_easy_perform (curl);
  long http_status= 0;
  ::curl_easy_getinfo (curl, CURLINFO_RESPONSE_CODE, &http_status);
  ::curl_slist_free_all (headers);
  ::curl_easy_cleanup (curl);
  std::fill (token.begin (), token.end (), '\0');
  std::fill (authorization.begin (), authorization.end (), '\0');

  if (status != CURLE_OK || http_status != 201) {
    error= "Cloudflare TURN credential request failed";
    if (status != CURLE_OK)
      error+= ": " + std::string (::curl_easy_strerror (status));
    else error+= " with HTTP " + std::to_string (http_status);
    return std::nullopt;
  }
  auto result= parse_cloudflare_turn_credentials (response);
  if (!result) {
    error= "Cloudflare TURN returned malformed ICE credentials";
    return std::nullopt;
  }
  return result;
}

bool
tcp_ready (int port) {
  int fd= ::socket (AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (fd < 0) return false;
  sockaddr_in address {};
  address.sin_family= AF_INET;
  address.sin_port= htons (uint16_t (port));
  address.sin_addr.s_addr= htonl (INADDR_LOOPBACK);
  bool result= ::connect (fd, reinterpret_cast<sockaddr*> (&address),
                          sizeof (address)) == 0;
  ::close (fd);
  return result;
}

int
reserve_available_port (const std::set<int>& excluded) {
  for (int attempt= 0; attempt < 64; attempt++) {
    int fd= ::socket (AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) throw std::runtime_error ("socket failed");
    int one= 1;
    ::setsockopt (fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof (one));
    sockaddr_in address {};
    address.sin_family= AF_INET;
    address.sin_addr.s_addr= htonl (INADDR_LOOPBACK);
    address.sin_port= 0;
    if (::bind (fd, reinterpret_cast<sockaddr*> (&address),
                sizeof (address)) != 0) {
      ::close (fd);
      continue;
    }
    socklen_t size= sizeof (address);
    ::getsockname (fd, reinterpret_cast<sockaddr*> (&address), &size);
    int port= ntohs (address.sin_port);
    ::close (fd);
    if (!excluded.count (port)) return port;
  }
  throw std::runtime_error ("could not allocate loopback port");
}

class SessionManager {
public:
  explicit SessionManager (Config config): config_ (std::move (config)) {
    std::error_code ec;
    fs::create_directories (config_.state_dir, ec);
    if (ec) throw std::runtime_error (
      "failed to create state directory: " + ec.message ());
    config_.state_dir= fs::weakly_canonical (config_.state_dir, ec);
    if (ec) throw std::runtime_error (
      "failed to canonicalize state directory: " + ec.message ());
    fs::permissions (config_.state_dir, fs::perms::owner_all,
                     fs::perm_options::replace, ec);
    if (ec) throw std::runtime_error (
      "failed to protect state directory: " + ec.message ());
    state_lock_fd_= ::open ((config_.state_dir / ".lock").c_str (),
                            O_CREAT | O_RDWR | O_CLOEXEC, 0600);
    if (state_lock_fd_ < 0 ||
        ::flock (state_lock_fd_, LOCK_EX | LOCK_NB) != 0) {
      if (state_lock_fd_ >= 0) ::close (state_lock_fd_);
      state_lock_fd_= -1;
      throw std::runtime_error (
        "another athena-web-server owns the configured state directory");
    }
    cleanup_stale_containers ();
    housekeeper_= std::thread ([this] { housekeeping_loop (); });
  }

  ~SessionManager () {
    stop_.store (true);
    if (housekeeper_.joinable ()) housekeeper_.join ();
    join_workers ();
    std::vector<std::shared_ptr<Session>> sessions= snapshot_sessions ();
    for (const auto& session: sessions) {
      stop_session (session, false);
      remove_containers (session);
      std::error_code ec;
      fs::remove_all (session->state_dir, ec);
    }
    if (state_lock_fd_ >= 0) {
      ::flock (state_lock_fd_, LOCK_UN);
      ::close (state_lock_fd_);
    }
  }

  std::shared_ptr<Session>
  create (std::string& error) {
    std::shared_ptr<Session> session= std::make_shared<Session> ();
    {
      std::lock_guard<std::mutex> lock (mutex_);
      size_t active= 0;
      std::set<int> ports;
      for (const auto& [token, existing]: sessions_) {
        std::lock_guard<std::mutex> session_lock (existing->mutex);
        if (existing->phase == SessionPhase::starting ||
            existing->phase == SessionPhase::running)
          active++;
        ports.insert (existing->vnc_bridge_port);
        ports.insert (existing->signaling_port);
      }
      if (active >= config_.max_connections) {
        error= "ATHENA is currently serving the maximum number of sessions";
        return {};
      }
      do session->token= random_hex (24);
      while (sessions_.count (session->token));
      std::string suffix= session->token.substr (0, 16);
      session->sandbox_name= "athena-web-sandbox-" + suffix;
      session->streamer_name= "athena-web-stream-" + suffix;
      session->state_dir= config_.state_dir / session->token;
      session->vnc_bridge_port= reserve_available_port (ports);
      ports.insert (session->vnc_bridge_port);
      session->signaling_port= reserve_available_port (ports);
      session->created_at= Clock::now ();
      session->last_heartbeat= session->created_at;
      session->expires_at= session->created_at +
        std::chrono::seconds (config_.session_seconds);
      sessions_[session->token]= session;
    }

    spawn_worker ([this, session] { start_session (session); });
    return session;
  }

  std::shared_ptr<Session>
  lookup (const std::string& token) const {
    std::lock_guard<std::mutex> lock (mutex_);
    auto found= sessions_.find (token);
    return found == sessions_.end () ? nullptr: found->second;
  }

  void
  heartbeat (const std::shared_ptr<Session>& session) {
    std::lock_guard<std::mutex> lock (session->mutex);
    if (session->phase == SessionPhase::starting ||
        session->phase == SessionPhase::running)
      session->last_heartbeat= Clock::now ();
  }

  bool
  extend (const std::shared_ptr<Session>& session, std::string& error) {
    std::lock_guard<std::mutex> lock (session->mutex);
    if (session->phase != SessionPhase::running) {
      error= "only a running session can be extended";
      return false;
    }
    Clock::time_point now= Clock::now ();
    if (now >= session->expires_at) {
      error= "the session has already expired";
      return false;
    }
    session->expires_at+= std::chrono::seconds (config_.session_seconds);
    session->last_heartbeat= now;
    return true;
  }

  void
  close (const std::shared_ptr<Session>& session) {
    spawn_worker ([this, session] { stop_session (session, false); });
  }

  json::object
  describe (const std::shared_ptr<Session>& session) const {
    std::lock_guard<std::mutex> lock (session->mutex);
    json::object result {
      {"token", session->token},
      {"status", phase_name (session->phase)},
      {"expires_at", unix_seconds_after (session->expires_at)},
      {"warning_seconds", config_.warning_seconds},
      {"session_seconds", config_.session_seconds}
    };
    if (!session->error.empty ()) result["error"]= session->error;
    result["can_download_expired"]=
      session->phase == SessionPhase::expired &&
      !session->expired_archive.empty ();
    json::array ice;
    for (const IceServer& server: session->ice_servers) {
      json::object entry {{"urls", server.browser_url}};
      if (!server.username.empty ()) entry["username"]= server.username;
      if (!server.credential.empty ()) entry["credential"]= server.credential;
      ice.push_back (std::move (entry));
    }
    result["ice_servers"]= std::move (ice);
    return result;
  }

  bool
  upload (const std::shared_ptr<Session>& session,
          const std::string& filename, const fs::path& staged,
          uint64_t bytes, std::string& error) {
    std::lock_guard<std::mutex> transfer_lock (session->transfer_mutex);
    {
      std::lock_guard<std::mutex> lock (session->mutex);
      if (session->phase != SessionPhase::running) {
        error= "the session is not running";
        return false;
      }
    }
    ProcessResult usage= podman ({
      "exec", session->sandbox_name,
      "/usr/local/bin/athena-web-session-helper", "usage"
    });
    if (usage.status != 0) {
      error= "could not inspect session storage";
      return false;
    }
    uint64_t used= 0;
    try { used= std::stoull (trim (usage.output)); }
    catch (...) {
      error= "session storage reported an invalid size";
      return false;
    }
    if (bytes > config_.storage_limit ||
        used > config_.storage_limit - bytes) {
      error= "the session storage limit would be exceeded";
      return false;
    }

    std::string incoming= ".upload-" + random_hex (8);
    std::string incoming_path=
      "/home/ATHENA-User/Desktop/Upload/" + incoming;
    ProcessResult copied= podman ({
      "cp", staged.string (),
      session->sandbox_name + ":" + incoming_path
    });
    if (copied.status != 0) {
      error= "failed to copy the upload into the session";
      return false;
    }
    ProcessResult installed= podman ({
      "exec", session->sandbox_name,
      "/usr/local/bin/athena-web-session-helper", "finish-upload",
      incoming, filename
    });
    if (installed.status != 0) {
      podman ({"exec", session->sandbox_name, "rm", "-f", "--", incoming_path});
      error= "failed to install the upload";
      return false;
    }
    return true;
  }

  json::array
  downloads (const std::shared_ptr<Session>& session, std::string& error) {
    {
      std::lock_guard<std::mutex> lock (session->mutex);
      if (session->phase == SessionPhase::expired) return {};
      if (session->phase != SessionPhase::running) {
        error= "the session is not running";
        return {};
      }
    }
    ProcessResult listed= podman ({
      "exec", session->sandbox_name,
      "/usr/local/bin/athena-web-session-helper", "list"
    });
    if (listed.status != 0) {
      error= "failed to list session downloads";
      return {};
    }
    boost::system::error_code parse_error;
    json::value value= json::parse (listed.output, parse_error);
    if (parse_error || !value.is_array ()) {
      error= "session returned an invalid download listing";
      return {};
    }
    json::array result;
    for (const json::value& item: value.as_array ()) {
      if (!item.is_object ()) continue;
      const json::object& object= item.as_object ();
      auto path= object.find ("path");
      auto size= object.find ("size");
      if (path == object.end () || !path->value ().is_string () ||
          size == object.end () ||
          !(size->value ().is_uint64 () || size->value ().is_int64 ()))
        continue;
      std::string name (path->value ().as_string ().data (),
                        path->value ().as_string ().size ());
      if (valid_download_path (name)) result.push_back (item);
    }
    return result;
  }

  std::optional<DownloadMaterialization>
  materialize (const std::shared_ptr<Session>& session,
               const std::string& relative, std::string& error) {
    std::lock_guard<std::mutex> transfer_lock (session->transfer_mutex);
    {
      std::lock_guard<std::mutex> lock (session->mutex);
      if (session->phase != SessionPhase::running) {
        error= "the session is not running";
        return std::nullopt;
      }
    }
    if (!valid_download_path (relative)) {
      error= "invalid download path";
      return std::nullopt;
    }
    ProcessResult checked= podman ({
      "exec", session->sandbox_name,
      "/usr/local/bin/athena-web-session-helper", "check", relative
    });
    if (checked.status != 0) {
      error= "download does not name a regular session file";
      return std::nullopt;
    }

    fs::path root= session->state_dir / "materialized" / random_hex (8);
    std::error_code ec;
    fs::create_directories (root, ec);
    if (ec) {
      error= "failed to create download staging directory";
      return std::nullopt;
    }
    std::string source= session->sandbox_name +
      ":/home/ATHENA-User/Desktop/Download/" + relative;
    ProcessResult copied= podman ({"cp", source, root.string ()});
    fs::path materialized= root / fs::path (relative).filename ();
    struct stat info {};
    if (copied.status != 0 ||
        ::lstat (materialized.c_str (), &info) != 0 ||
        !S_ISREG (info.st_mode) || S_ISLNK (info.st_mode)) {
      fs::remove_all (root, ec);
      error= "failed to materialize the session download";
      return std::nullopt;
    }
    return DownloadMaterialization {
      materialized, root, fs::path (relative).filename ().string ()
    };
  }

  std::optional<DownloadMaterialization>
  archive (const std::shared_ptr<Session>& session, std::string& error) {
    std::lock_guard<std::mutex> transfer_lock (session->transfer_mutex);
    {
      std::lock_guard<std::mutex> lock (session->mutex);
      if (session->phase == SessionPhase::expired &&
          !session->expired_archive.empty () &&
          fs::is_regular_file (session->expired_archive))
        return DownloadMaterialization {
          session->expired_archive, {}, "ATHENA-Download.tar.gz"
        };
      if (session->phase != SessionPhase::running) {
        error= "the session is not running";
        return std::nullopt;
      }
    }
    fs::path archive_path= session->state_dir / "ATHENA-Download.tar.gz";
    if (!create_archive (session, archive_path, error))
      return std::nullopt;
    return DownloadMaterialization {
      archive_path, {}, "ATHENA-Download.tar.gz"
    };
  }

  int
  signaling_port (const std::shared_ptr<Session>& session) const {
    std::lock_guard<std::mutex> lock (session->mutex);
    return session->phase == SessionPhase::running ?
      session->signaling_port: 0;
  }

private:
  Config config_;
  mutable std::mutex mutex_;
  std::unordered_map<std::string,std::shared_ptr<Session>> sessions_;
  std::atomic<bool> stop_ {false};
  std::thread housekeeper_;
  int state_lock_fd_= -1;
  struct Worker {
    std::thread thread;
    std::shared_ptr<std::atomic<bool>> done;
  };
  std::mutex workers_mutex_;
  std::vector<Worker> workers_;

  template<typename Function>
  void
  spawn_worker (Function&& function) {
    std::lock_guard<std::mutex> lock (workers_mutex_);
    for (auto worker= workers_.begin (); worker != workers_.end ();) {
      if (!worker->done->load ()) {
        ++worker;
        continue;
      }
      if (worker->thread.joinable ()) worker->thread.join ();
      worker= workers_.erase (worker);
    }
    auto done= std::make_shared<std::atomic<bool>> (false);
    workers_.push_back (Worker {
      std::thread ([function= std::forward<Function> (function), done] () mutable {
        try {
          function ();
        }
        catch (const std::exception& error) {
          std::cerr << "athena-web-server: session worker failed: "
                    << error.what () << '\n';
        }
        catch (...) {
          std::cerr << "athena-web-server: session worker failed\n";
        }
        done->store (true);
      }),
      done
    });
  }

  void
  join_workers () {
    std::vector<Worker> workers;
    {
      std::lock_guard<std::mutex> lock (workers_mutex_);
      workers.swap (workers_);
    }
    for (Worker& worker: workers)
      if (worker.thread.joinable ()) worker.thread.join ();
  }

  ProcessResult
  podman (std::vector<std::string> arguments, int timeout= process_timeout_seconds) const {
    arguments.insert (arguments.begin (), config_.container_runtime);
    return run_process (arguments, timeout);
  }

  std::string
  state_label () const {
    return "org.athena.web.state=" + config_.state_dir.string ();
  }

  void
  cleanup_stale_containers () const {
    ProcessResult listed= podman ({
      "ps", "-a", "--filter", "label=" + state_label (),
      "--format={{.Names}}"
    }, 30);
    if (listed.status != 0)
      throw std::runtime_error (
        "failed to inspect stale ATHENA web containers");
    std::istringstream names (listed.output);
    std::string name;
    while (std::getline (names, name)) {
      name= trim (name);
      if (name.rfind ("athena-web-sandbox-", 0) != 0 &&
          name.rfind ("athena-web-stream-", 0) != 0)
        continue;
      podman ({"rm", "--force", "--time=2", name}, 20);
    }
  }

  std::vector<std::shared_ptr<Session>>
  snapshot_sessions () const {
    std::lock_guard<std::mutex> lock (mutex_);
    std::vector<std::shared_ptr<Session>> result;
    result.reserve (sessions_.size ());
    for (const auto& [token, session]: sessions_) result.push_back (session);
    return result;
  }

  std::string
  turn_environment (const std::shared_ptr<Session>& session) const {
    std::string result;
    for (const IceServer& server: session->ice_servers) {
      if (server.browser_url.rfind ("turn:", 0) != 0 &&
          server.browser_url.rfind ("turns:", 0) != 0)
        continue;
      if (!result.empty ()) result.push_back ('\n');
      result+= server.uri;
    }
    return result;
  }

  bool
  prepare_ice_servers (const std::shared_ptr<Session>& session,
                       std::string& error) const {
    TurnCredentials credentials;
    if (config_.cloudflare_turn_token_file) {
      auto generated= generate_cloudflare_turn_credentials (config_, error);
      if (!generated) return false;
      credentials= std::move (*generated);
    }
    else {
      credentials.stun_server= config_.stun_server;
      auto add= [&] (const std::string& uri) {
        auto parsed= parse_ice_server (uri);
        if (parsed) credentials.ice_servers.push_back (std::move (*parsed));
      };
      if (!config_.stun_server.empty ()) add (config_.stun_server);
      for (const std::string& server: config_.turn_servers) add (server);
    }
    std::lock_guard<std::mutex> lock (session->mutex);
    session->stun_server= std::move (credentials.stun_server);
    session->ice_servers= std::move (credentials.ice_servers);
    return true;
  }

  void
  start_session (const std::shared_ptr<Session>& session) {
    std::string failure;
    if (!prepare_ice_servers (session, failure)) {
      std::lock_guard<std::mutex> lock (session->mutex);
      session->error= failure;
      session->phase= SessionPhase::failed;
      session->retained_until= Clock::now () +
        std::chrono::seconds (config_.expired_retention_seconds);
      return;
    }
    std::error_code ec;
    fs::create_directories (session->state_dir / "bridge", ec);
    fs::create_directories (session->state_dir / "staging", ec);
    fs::permissions (session->state_dir, fs::perms::owner_all,
                     fs::perm_options::replace, ec);
    fs::permissions (session->state_dir / "bridge", fs::perms::owner_all,
                     fs::perm_options::replace, ec);
    if (ec) failure= "failed to create session state: " + ec.message ();

    if (failure.empty () && session->cleanup_started.load ()) return;
    if (failure.empty ()) {
      std::vector<std::string> sandbox {
        "run", "--detach", "--name", session->sandbox_name,
        "--hostname", "ATHENA-Experience",
        "--network", "none",
        "--http-proxy=false",
        "--read-only",
        "--label=" + state_label (),
        "--cap-drop=all",
        "--security-opt=no-new-privileges",
        "--pids-limit=512",
        "--memory=" + std::to_string (config_.max_memory),
        "--memory-swap=" + std::to_string (config_.max_memory),
        "--ipc=private",
        "--userns=keep-id:uid=1000,gid=1000",
        "--user=1000:1000",
        "--tmpfs=/session-home:rw,nosuid,nodev,size=" +
          std::to_string (config_.storage_limit) +
          ",mode=1777",
        "--tmpfs=/tmp:rw,nosuid,nodev,noexec,size=256m,mode=1777",
        "--tmpfs=/run:rw,nosuid,nodev,size=64m,mode=0755",
        "--volume=" + (session->state_dir / "bridge").string () +
          ":/run/athena-bridge:rw,z",
        "--env=ATHENA_WEB_PRIVATE_VNC_BRIDGE=1",
        "--env=ATHENA_WEB_WIDTH=" + std::to_string (config_.width),
        "--env=ATHENA_WEB_HEIGHT=" + std::to_string (config_.height),
        config_.image, "sandbox"
      };
      ProcessResult result;
      {
        std::lock_guard<std::mutex> lifecycle_lock (
          session->lifecycle_mutex);
        if (session->cleanup_started.load ()) return;
        result= podman (std::move (sandbox));
      }
      if (result.status != 0)
        failure= "sandbox container failed to start: " + trim (result.output);
    }

    if (failure.empty () && session->cleanup_started.load ()) return;
    if (failure.empty ()) {
      std::vector<std::string> streamer {
        "run", "--detach", "--name", session->streamer_name,
        "--network=host",
        "--http-proxy=false",
        "--read-only",
        "--label=" + state_label (),
        "--cap-drop=all",
        "--security-opt=no-new-privileges",
        "--pids-limit=256",
        "--memory=768m",
        "--memory-swap=768m",
        "--ipc=private",
        "--userns=keep-id:uid=1000,gid=1000",
        "--user=1000:1000",
        "--tmpfs=/tmp:rw,nosuid,nodev,noexec,size=128m,mode=1777",
        "--tmpfs=/run:rw,nosuid,nodev,size=32m,mode=0755",
        "--volume=" + (session->state_dir / "bridge").string () +
          ":/run/athena-bridge:rw,z",
        "--env=ATHENA_WEB_VNC_PORT=" +
          std::to_string (session->vnc_bridge_port),
        "--env=ATHENA_WEB_SIGNAL_PORT=" +
          std::to_string (session->signaling_port),
        "--env=ATHENA_WEB_SESSION_TOKEN=" + session->token,
        "--env=ATHENA_WEB_FRAMERATE=" + std::to_string (config_.framerate),
        "--env=ATHENA_WEB_VIDEO_MIN_BITRATE=" +
          std::to_string (config_.video_min_bitrate),
        "--env=ATHENA_WEB_VIDEO_START_BITRATE=" +
          std::to_string (config_.video_start_bitrate),
        "--env=ATHENA_WEB_VIDEO_MAX_BITRATE=" +
          std::to_string (config_.video_max_bitrate),
        "--env=ATHENA_WEB_STUN_SERVER=" + session->stun_server,
        "--env=ATHENA_WEB_TURN_SERVERS=" + turn_environment (session),
        "--env=HOME=/tmp/streamer-home",
        config_.image, "stream"
      };
      ProcessResult result;
      {
        std::lock_guard<std::mutex> lifecycle_lock (
          session->lifecycle_mutex);
        if (session->cleanup_started.load ()) return;
        result= podman (std::move (streamer));
      }
      if (result.status != 0)
        failure= "streamer container failed to start: " + trim (result.output);
    }

    if (failure.empty ()) {
      Clock::time_point deadline= Clock::now () +
        std::chrono::seconds (config_.startup_timeout_seconds);
      while (Clock::now () < deadline) {
        if (session->cleanup_started.load ()) return;
        if (tcp_ready (session->signaling_port)) break;
        ProcessResult sandbox= podman ({
          "inspect", "--format={{.State.Running}}", session->sandbox_name
        }, 10);
        ProcessResult streamer= podman ({
          "inspect", "--format={{.State.Running}}", session->streamer_name
        }, 10);
        if (trim (sandbox.output) != "true" ||
            trim (streamer.output) != "true") {
          failure= "a session container exited during startup";
          break;
        }
        std::this_thread::sleep_for (std::chrono::milliseconds (250));
      }
      if (failure.empty () && !tcp_ready (session->signaling_port))
        failure= "WebRTC signaling did not become ready before the startup timeout";
    }

    if (!failure.empty ()) {
      ProcessResult sandbox_log= podman ({
        "logs", "--tail=60", session->sandbox_name
      }, 15);
      ProcessResult stream_log= podman ({
        "logs", "--tail=60", session->streamer_name
      }, 15);
      std::cerr << "athena-web-server: session " << session->token.substr (0, 12)
                << " failed: " << failure << '\n';
      if (!sandbox_log.output.empty ())
        std::cerr << "sandbox log:\n" << sandbox_log.output;
      if (!stream_log.output.empty ())
        std::cerr << "streamer log:\n" << stream_log.output;
      {
        std::lock_guard<std::mutex> lock (session->mutex);
        session->phase= SessionPhase::failed;
        session->error= failure;
        session->retained_until= Clock::now () + std::chrono::minutes (2);
      }
      remove_containers (session);
      clear_ice_credentials (session);
      return;
    }

    {
      std::lock_guard<std::mutex> lock (session->mutex);
      if (session->phase == SessionPhase::starting &&
          !session->cleanup_started.load ())
        session->phase= SessionPhase::running;
    }
    std::cout << "athena-web-server: session "
              << session->token.substr (0, 12) << " ready\n";
  }

  void
  remove_containers (const std::shared_ptr<Session>& session) const {
    podman ({"rm", "--force", "--time=2", session->streamer_name}, 20);
    podman ({"rm", "--force", "--time=2", session->sandbox_name}, 20);
  }

  static void
  clear_ice_credentials (const std::shared_ptr<Session>& session) {
    std::lock_guard<std::mutex> lock (session->mutex);
    for (IceServer& server: session->ice_servers) {
      std::fill (server.username.begin (), server.username.end (), '\0');
      std::fill (server.credential.begin (), server.credential.end (), '\0');
      std::fill (server.uri.begin (), server.uri.end (), '\0');
    }
    session->ice_servers.clear ();
    session->stun_server.clear ();
  }

  bool
  create_archive (const std::shared_ptr<Session>& session,
                  const fs::path& destination, std::string& error) {
    std::string inside= "/tmp/athena-download-" + random_hex (8) + ".tar.gz";
    ProcessResult made= podman ({
      "exec", session->sandbox_name,
      "/usr/local/bin/athena-web-session-helper", "archive", inside
    }, 120);
    if (made.status != 0) {
      error= "failed to archive the session Download directory";
      return false;
    }
    ProcessResult copied= podman ({
      "cp", session->sandbox_name + ":" + inside, destination.string ()
    }, 120);
    podman ({"exec", session->sandbox_name, "rm", "-f", "--", inside}, 15);
    struct stat info {};
    if (copied.status != 0 ||
        ::lstat (destination.c_str (), &info) != 0 ||
        !S_ISREG (info.st_mode) || S_ISLNK (info.st_mode)) {
      error= "failed to preserve the session Download archive";
      return false;
    }
    return true;
  }

  void
  stop_session (const std::shared_ptr<Session>& session, bool preserve) {
    bool expected= false;
    if (!session->cleanup_started.compare_exchange_strong (expected, true))
      return;
    std::lock_guard<std::mutex> lifecycle_lock (session->lifecycle_mutex);
    {
      std::lock_guard<std::mutex> lock (session->mutex);
      if (session->phase == SessionPhase::expired ||
          session->phase == SessionPhase::failed)
        preserve= false;
    }

    fs::path preserved;
    std::string archive_error;
    if (preserve) {
      std::lock_guard<std::mutex> transfer_lock (session->transfer_mutex);
      preserved= session->state_dir / "ATHENA-Download.tar.gz";
      if (!create_archive (session, preserved, archive_error))
        preserved.clear ();
    }
    {
      std::lock_guard<std::mutex> lock (session->mutex);
      session->phase= SessionPhase::closing;
    }
    remove_containers (session);
    clear_ice_credentials (session);

    std::error_code ec;
    if (preserve) {
      std::lock_guard<std::mutex> lock (session->mutex);
      session->phase= SessionPhase::expired;
      session->expired_archive= preserved;
      session->error= archive_error;
      session->retained_until= Clock::now () +
        std::chrono::seconds (config_.expired_retention_seconds);
      fs::remove_all (session->state_dir / "bridge", ec);
      fs::remove_all (session->state_dir / "staging", ec);
      fs::remove_all (session->state_dir / "materialized", ec);
      return;
    }

    fs::remove_all (session->state_dir, ec);
    std::lock_guard<std::mutex> map_lock (mutex_);
    auto found= sessions_.find (session->token);
    if (found != sessions_.end () && found->second == session)
      sessions_.erase (found);
  }

  void
  housekeeping_loop () {
    while (!stop_.load ()) {
      std::vector<std::shared_ptr<Session>> sessions= snapshot_sessions ();
      Clock::time_point now= Clock::now ();
      for (const auto& session: sessions) {
        SessionPhase phase;
        Clock::time_point expires;
        Clock::time_point heartbeat;
        Clock::time_point retained;
        {
          std::lock_guard<std::mutex> lock (session->mutex);
          phase= session->phase;
          expires= session->expires_at;
          heartbeat= session->last_heartbeat;
          retained= session->retained_until;
        }
        if ((phase == SessionPhase::starting ||
             phase == SessionPhase::running) &&
            now - heartbeat >
              std::chrono::seconds (config_.heartbeat_timeout_seconds)) {
          spawn_worker ([this, session] { stop_session (session, false); });
        }
        else if (phase == SessionPhase::running && now >= expires) {
          spawn_worker ([this, session] { stop_session (session, true); });
        }
        else if ((phase == SessionPhase::expired ||
                  phase == SessionPhase::failed) &&
                 retained != Clock::time_point {} && now >= retained) {
          std::error_code ec;
          fs::remove_all (session->state_dir, ec);
          std::lock_guard<std::mutex> lock (mutex_);
          sessions_.erase (session->token);
        }
      }
      for (int i= 0; i < 10 && !stop_.load (); i++)
        std::this_thread::sleep_for (std::chrono::milliseconds (100));
    }
  }
};

std::string
mime_type (const fs::path& path) {
  std::string extension= lower_ascii (path.extension ().string ());
  if (extension == ".html") return "text/html; charset=utf-8";
  if (extension == ".js") return "text/javascript; charset=utf-8";
  if (extension == ".css") return "text/css; charset=utf-8";
  if (extension == ".json") return "application/json; charset=utf-8";
  if (extension == ".svg") return "image/svg+xml";
  if (extension == ".png") return "image/png";
  if (extension == ".ico") return "image/x-icon";
  if (extension == ".txt") return "text/plain; charset=utf-8";
  return "application/octet-stream";
}

bool
serve_file (int fd, const fs::path& path, const std::string& content_type,
            const std::optional<std::string>& disposition= std::nullopt,
            const std::vector<std::pair<std::string,std::string>>&
              extra_headers= {}) {
  struct stat info {};
  if (::lstat (path.c_str (), &info) != 0 ||
      !S_ISREG (info.st_mode) || S_ISLNK (info.st_mode))
    return false;
  std::ifstream input (path, std::ios::binary);
  if (!input) return false;
  std::ostringstream head;
  head << "HTTP/1.1 200 OK\r\n"
       << "Content-Type: " << content_type << "\r\n"
       << "Content-Length: " << uint64_t (info.st_size) << "\r\n"
       << "Cache-Control: no-store\r\n"
       << "X-Content-Type-Options: nosniff\r\n";
  if (disposition) head << "Content-Disposition: " << *disposition << "\r\n";
  for (const auto& [name, value]: extra_headers)
    head << name << ": " << value << "\r\n";
  head << "Connection: close\r\n\r\n";
  if (!send_all (fd, head.str ())) return false;
  std::array<char,64 * 1024> buffer {};
  while (input) {
    input.read (buffer.data (), buffer.size ());
    std::streamsize count= input.gcount ();
    if (count > 0 && !send_all (fd, buffer.data (), size_t (count)))
      return false;
  }
  return true;
}

bool
proxy_websocket (int client, const HttpRequest& request, int port) {
  int upstream= ::socket (AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (upstream < 0) return false;
  sockaddr_in address {};
  address.sin_family= AF_INET;
  address.sin_port= htons (uint16_t (port));
  address.sin_addr.s_addr= htonl (INADDR_LOOPBACK);
  if (::connect (upstream, reinterpret_cast<sockaddr*> (&address),
                 sizeof (address)) != 0) {
    ::close (upstream);
    return false;
  }

  std::ostringstream forwarded;
  forwarded << "GET / HTTP/1.1\r\n";
  for (const auto& [name, value]: request.headers) {
    if (name == "host") continue;
    forwarded << name << ": " << value << "\r\n";
  }
  forwarded << "host: 127.0.0.1:" << port << "\r\n\r\n";
  if (!send_all (upstream, forwarded.str ())) {
    ::close (upstream);
    return false;
  }
  if (!request.buffered_body.empty ())
    send_all (upstream, request.buffered_body);

  auto pump= [] (int source, int destination) {
    std::array<char,32 * 1024> buffer {};
    while (true) {
      ssize_t count= ::recv (source, buffer.data (), buffer.size (), 0);
      if (count < 0 && errno == EINTR) continue;
      if (count <= 0) break;
      if (!send_all (destination, buffer.data (), size_t (count))) break;
    }
    ::shutdown (destination, SHUT_WR);
  };
  std::thread reverse (pump, upstream, client);
  pump (client, upstream);
  reverse.join ();
  ::close (upstream);
  return true;
}

void
handle_static (int fd, const HttpRequest& request, const fs::path& web_root) {
  if (request.method != "GET") {
    send_json (fd, 405, json_error ("only GET is allowed"));
    return;
  }
  std::string relative= request.path == "/" ? "index.html":
    percent_decode (request.path.substr (1));
  if (!valid_download_path (relative)) {
    send_json (fd, 404, json_error ("asset not found"));
    return;
  }
  fs::path candidate= web_root;
  for (const std::string& part: split_path (relative)) candidate/= part;
  const std::vector<std::pair<std::string,std::string>> security_headers {
    {"Content-Security-Policy",
     "default-src 'none'; script-src 'self'; style-src 'self'; "
     "img-src 'self' data:; media-src blob:; connect-src 'self' ws: wss:; "
     "font-src 'self'; frame-ancestors 'none'; base-uri 'none'; "
     "form-action 'none'"},
    {"Permissions-Policy",
     "camera=(), microphone=(), geolocation=(), payment=(), usb=(), "
     "serial=(), bluetooth=()"},
    {"Cross-Origin-Resource-Policy", "same-origin"},
    {"X-Frame-Options", "DENY"}
  };
  if (!serve_file (fd, candidate, mime_type (candidate), std::nullopt,
                   security_headers))
    send_json (fd, 404, json_error ("asset not found"));
}

void
handle_api (int fd, HttpRequest& request, SessionManager& manager,
            const Config& config) {
  std::vector<std::string> parts= split_path (request.path);
  if (parts.size () == 2 && parts[0] == "api" &&
      parts[1] == "sessions" && request.method == "POST") {
    std::string error;
    auto session= manager.create (error);
    if (!session) {
      send_json (fd, 503, json_error (error));
      return;
    }
    send_json (fd, 202, manager.describe (session));
    return;
  }
  if (parts.size () < 3 || parts[0] != "api" ||
      parts[1] != "sessions" || !valid_session_token (parts[2])) {
    send_json (fd, 404, json_error ("session endpoint not found"));
    return;
  }
  std::shared_ptr<Session> session= manager.lookup (parts[2]);
  if (!session) {
    send_json (fd, 404, json_error ("session not found"));
    return;
  }

  if (parts.size () == 3 && request.method == "GET") {
    send_json (fd, 200, manager.describe (session));
    return;
  }
  if (parts.size () == 4 && parts[3] == "heartbeat" &&
      request.method == "POST") {
    manager.heartbeat (session);
    send_json (fd, 200, manager.describe (session));
    return;
  }
  if (parts.size () == 4 && parts[3] == "extend" &&
      request.method == "POST") {
    std::string error;
    if (!manager.extend (session, error)) {
      send_json (fd, 409, json_error (error));
      return;
    }
    send_json (fd, 200, manager.describe (session));
    return;
  }
  if (parts.size () == 4 && parts[3] == "close" &&
      request.method == "POST") {
    manager.close (session);
    send_response (fd, 204, "text/plain", "");
    return;
  }
  if (parts.size () == 4 && parts[3] == "signal" &&
      request.method == "GET") {
    auto upgrade= request.headers.find ("upgrade");
    if (upgrade == request.headers.end () ||
        lower_ascii (upgrade->second) != "websocket") {
      send_json (fd, 400, json_error ("WebSocket upgrade required"));
      return;
    }
    int port= manager.signaling_port (session);
    if (port == 0 || !proxy_websocket (fd, request, port))
      send_json (fd, 502, json_error ("WebRTC signaling is unavailable"));
    return;
  }
  if (parts.size () == 4 && parts[3] == "upload" &&
      request.method == "PUT") {
    auto header= request.headers.find ("x-athena-filename");
    std::string filename= header == request.headers.end () ? "":
      percent_decode (header->second);
    if (!valid_upload_filename (filename)) {
      send_json (fd, 400, json_error ("invalid upload filename"));
      return;
    }
    if (!request.chunked_transfer && request.content_length == 0) {
      send_json (fd, 400, json_error ("empty uploads are not accepted"));
      return;
    }
    if (request.content_length > config.storage_limit) {
      send_json (fd, 413, json_error ("upload exceeds session storage limit"));
      return;
    }
    fs::path staged= session->state_dir / "staging" /
      ("upload-" + random_hex (8));
    std::string error;
    uint64_t uploaded_bytes= 0;
    if (!read_http_body_to_file (fd, request, staged, uploaded_bytes, error)) {
      std::error_code ec;
      fs::remove (staged, ec);
      send_json (fd, error.find ("body limit") != std::string::npos ? 413: 400,
                 json_error (error));
      return;
    }
    if (uploaded_bytes == 0) {
      std::error_code ec;
      fs::remove (staged, ec);
      send_json (fd, 400, json_error ("empty uploads are not accepted"));
      return;
    }
    bool installed= manager.upload (session, filename, staged,
                                    uploaded_bytes, error);
    std::error_code ec;
    fs::remove (staged, ec);
    if (!installed) {
      send_json (fd, error.find ("limit") != std::string::npos ? 413: 500,
                 json_error (error));
      return;
    }
    send_json (fd, 201, json::object {{"name", filename}});
    return;
  }
  if (parts.size () == 4 && parts[3] == "downloads" &&
      request.method == "GET") {
    std::string error;
    json::array files= manager.downloads (session, error);
    if (!error.empty ()) {
      send_json (fd, 409, json_error (error));
      return;
    }
    send_json (fd, 200, json::object {{"files", std::move (files)}});
    return;
  }
  if (parts.size () == 5 && parts[3] == "download" &&
      request.method == "GET") {
    std::string relative= percent_decode (parts[4]);
    std::string error;
    auto file= manager.materialize (session, relative, error);
    if (!file) {
      send_json (fd, 404, json_error (error));
      return;
    }
    std::string disposition= "attachment; filename*=UTF-8''" +
      percent_encode (file->filename);
    serve_file (fd, file->path, "application/octet-stream", disposition);
    std::error_code ec;
    if (!file->cleanup_root.empty ()) fs::remove_all (file->cleanup_root, ec);
    return;
  }
  if (parts.size () == 4 && parts[3] == "download-all" &&
      request.method == "GET") {
    std::string error;
    auto archive= manager.archive (session, error);
    if (!archive) {
      send_json (fd, 409, json_error (error));
      return;
    }
    serve_file (fd, archive->path, "application/gzip",
                "attachment; filename=ATHENA-Download.tar.gz");
    return;
  }
  send_json (fd, 404, json_error ("session endpoint not found"));
}

void
handle_client (int fd, SessionManager& manager, const Config& config,
               const fs::path& web_root) {
  timeval timeout {120, 0};
  ::setsockopt (fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof (timeout));
  ::setsockopt (fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof (timeout));
  HttpRequest request;
  std::string error;
  if (!read_http_request (fd, request, error, config.storage_limit)) {
    send_json (fd, 400, json_error (error));
    ::close (fd);
    return;
  }
  try {
    if (request.path.rfind ("/api/", 0) == 0)
      handle_api (fd, request, manager, config);
    else
      handle_static (fd, request, web_root);
  }
  catch (const std::exception& exception) {
    std::cerr << "athena-web-server: request failed: "
              << exception.what () << '\n';
    send_json (fd, 500, json_error ("internal server error"));
  }
  ::shutdown (fd, SHUT_RDWR);
  ::close (fd);
}

class ConnectionWorkers {
public:
  struct Worker {
    std::thread thread;
    std::shared_ptr<std::atomic<bool>> done;
  };

  ~ConnectionWorkers () {
    std::vector<Worker> workers;
    {
      std::lock_guard<std::mutex> lock (mutex_);
      stopping_= true;
      for (int fd: sockets_) ::shutdown (fd, SHUT_RDWR);
      workers.swap (workers_);
    }
    for (Worker& worker: workers)
      if (worker.thread.joinable ()) worker.thread.join ();
  }

  void
  start (int fd, SessionManager& manager, const Config& config,
         const fs::path& web_root) {
    std::lock_guard<std::mutex> lock (mutex_);
    if (stopping_) {
      ::close (fd);
      return;
    }
    sockets_.insert (fd);
    for (auto worker= workers_.begin (); worker != workers_.end ();) {
      if (!worker->done->load ()) {
        ++worker;
        continue;
      }
      if (worker->thread.joinable ()) worker->thread.join ();
      worker= workers_.erase (worker);
    }
    auto done= std::make_shared<std::atomic<bool>> (false);
    workers_.push_back (Worker {
      std::thread ([this, fd, &manager, &config, &web_root, done] {
        handle_client (fd, manager, config, web_root);
        {
          std::lock_guard<std::mutex> lock (mutex_);
          sockets_.erase (fd);
        }
        done->store (true);
      }),
      done
    });
  }

private:
  std::mutex mutex_;
  std::set<int> sockets_;
  std::vector<Worker> workers_;
  bool stopping_= false;
};

int
create_listener (const Config& config) {
  addrinfo hints {};
  hints.ai_family= AF_UNSPEC;
  hints.ai_socktype= SOCK_STREAM;
  hints.ai_flags= AI_PASSIVE;
  addrinfo* addresses= nullptr;
  std::string service= std::to_string (config.port);
  int resolved= ::getaddrinfo (config.listen_address.c_str (),
                               service.c_str (), &hints, &addresses);
  if (resolved != 0)
    throw std::runtime_error ("getaddrinfo: " +
                              std::string (gai_strerror (resolved)));
  int fd= -1;
  for (addrinfo* address= addresses; address; address= address->ai_next) {
    fd= ::socket (address->ai_family,
                  address->ai_socktype | SOCK_CLOEXEC,
                  address->ai_protocol);
    if (fd < 0) continue;
    int one= 1;
    ::setsockopt (fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof (one));
    if (::bind (fd, address->ai_addr, address->ai_addrlen) == 0 &&
        ::listen (fd, 128) == 0)
      break;
    ::close (fd);
    fd= -1;
  }
  ::freeaddrinfo (addresses);
  if (fd < 0) throw std::runtime_error ("failed to bind HTTP listener");
  return fd;
}

void
signal_handler (int) {
  stopping.store (true);
  if (listener_fd >= 0) ::close (listener_fd);
}

void
print_usage () {
  std::cout
    << "Usage: athena-web-server [options]\n"
    << "  --listen-address ADDRESS       HTTP bind address (default 127.0.0.1)\n"
    << "  --port PORT                    HTTP port (default 8090)\n"
    << "  --image IMAGE                  OCI image containing ATHENA web runtime\n"
    << "  --container-runtime COMMAND    OCI runtime (default podman)\n"
    << "  --state-dir PATH               Private ephemeral server state\n"
    << "  --web-root PATH                Browser assets directory\n"
    << "  --max-memory SIZE              Memory per ATHENA sandbox (default 4GiB)\n"
    << "  --storage-limit SIZE           Writable home limit (default 2GiB)\n"
    << "  --max-connections COUNT        Concurrent session limit (default 4)\n"
    << "  --session-seconds SECONDS      Session increment (default 3600)\n"
    << "  --warning-seconds SECONDS      Expiry warning (default 300)\n"
    << "  --expired-retention SECONDS    Download retention after expiry\n"
    << "  --heartbeat-timeout SECONDS    Abandoned-tab cleanup delay\n"
    << "  --width PIXELS                 Virtual display width (default 1920)\n"
    << "  --height PIXELS                Virtual display height (default 1080)\n"
    << "  --framerate FPS                WebRTC frame rate (default 30)\n"
    << "  --video-min-bitrate BPS        WebRTC minimum bitrate (default 4000000)\n"
    << "  --video-start-bitrate BPS      WebRTC initial bitrate (default 12000000)\n"
    << "  --video-max-bitrate BPS        WebRTC maximum bitrate (default 24000000)\n"
    << "  --stun-server URI              STUN URI, empty disables STUN\n"
    << "  --turn-server URI              Repeatable static TURN URI\n"
    << "  --cloudflare-turn-key-id ID    Cloudflare Realtime TURN key id\n"
    << "  --cloudflare-turn-token-file PATH\n"
    << "                                 Owner-only TURN key token file\n";
}

int
positive_int (const std::string& text, const char* option) {
  try {
    size_t consumed= 0;
    int result= std::stoi (text, &consumed);
    if (consumed != text.size () || result <= 0) throw std::invalid_argument ("");
    return result;
  }
  catch (...) {
    throw std::runtime_error (std::string (option) +
                              " requires a positive integer");
  }
}

Config
parse_arguments (int argc, char** argv) {
  Config config;
  const char* runtime= std::getenv ("XDG_RUNTIME_DIR");
  config.state_dir= runtime && runtime[0] ?
    fs::path (runtime) / "athena-web-server":
    fs::temp_directory_path () /
      ("athena-web-server-" + std::to_string (::getuid ()));
  auto require_value= [&] (int& index, const char* option) -> std::string {
    if (index + 1 >= argc)
      throw std::runtime_error (std::string (option) + " requires a value");
    return argv[++index];
  };
  for (int i= 1; i < argc; i++) {
    std::string option= argv[i];
    if (option == "--help" || option == "-h") {
      print_usage ();
      std::exit (0);
    }
    else if (option == "--listen-address")
      config.listen_address= require_value (i, option.c_str ());
    else if (option == "--port")
      config.port= positive_int (require_value (i, option.c_str ()),
                                 option.c_str ());
    else if (option == "--image")
      config.image= require_value (i, option.c_str ());
    else if (option == "--container-runtime")
      config.container_runtime= require_value (i, option.c_str ());
    else if (option == "--state-dir")
      config.state_dir= require_value (i, option.c_str ());
    else if (option == "--web-root")
      config.web_root= fs::path (require_value (i, option.c_str ()));
    else if (option == "--max-memory")
      config.max_memory= parse_byte_size (require_value (i, option.c_str ()));
    else if (option == "--storage-limit")
      config.storage_limit= parse_byte_size (require_value (i, option.c_str ()));
    else if (option == "--max-connections")
      config.max_connections= size_t (positive_int (
        require_value (i, option.c_str ()), option.c_str ()));
    else if (option == "--session-seconds")
      config.session_seconds= positive_int (
        require_value (i, option.c_str ()), option.c_str ());
    else if (option == "--warning-seconds")
      config.warning_seconds= positive_int (
        require_value (i, option.c_str ()), option.c_str ());
    else if (option == "--heartbeat-timeout")
      config.heartbeat_timeout_seconds= positive_int (
        require_value (i, option.c_str ()), option.c_str ());
    else if (option == "--expired-retention")
      config.expired_retention_seconds= positive_int (
        require_value (i, option.c_str ()), option.c_str ());
    else if (option == "--startup-timeout")
      config.startup_timeout_seconds= positive_int (
        require_value (i, option.c_str ()), option.c_str ());
    else if (option == "--width")
      config.width= positive_int (require_value (i, option.c_str ()),
                                  option.c_str ());
    else if (option == "--height")
      config.height= positive_int (require_value (i, option.c_str ()),
                                   option.c_str ());
    else if (option == "--framerate")
      config.framerate= positive_int (require_value (i, option.c_str ()),
                                      option.c_str ());
    else if (option == "--video-min-bitrate")
      config.video_min_bitrate= positive_int (
        require_value (i, option.c_str ()), option.c_str ());
    else if (option == "--video-start-bitrate")
      config.video_start_bitrate= positive_int (
        require_value (i, option.c_str ()), option.c_str ());
    else if (option == "--video-max-bitrate")
      config.video_max_bitrate= positive_int (
        require_value (i, option.c_str ()), option.c_str ());
    else if (option == "--stun-server")
      config.stun_server= require_value (i, option.c_str ());
    else if (option == "--turn-server")
      config.turn_servers.push_back (require_value (i, option.c_str ()));
    else if (option == "--cloudflare-turn-key-id")
      config.cloudflare_turn_key_id= require_value (i, option.c_str ());
    else if (option == "--cloudflare-turn-token-file")
      config.cloudflare_turn_token_file=
        fs::path (require_value (i, option.c_str ()));
    else throw std::runtime_error ("unknown option: " + option);
  }
  if (config.port > 65535) throw std::runtime_error ("port exceeds 65535");
  if (config.warning_seconds >= config.session_seconds)
    throw std::runtime_error ("warning interval must be shorter than a session");
  if (config.width < 640 || config.height < 480 ||
      config.width > 7680 || config.height > 4320)
    throw std::runtime_error ("virtual display size is outside supported bounds");
  if (config.framerate > 120)
    throw std::runtime_error ("framerate must not exceed 120");
  if (config.video_min_bitrate > config.video_start_bitrate ||
      config.video_start_bitrate > config.video_max_bitrate)
    throw std::runtime_error (
      "video bitrate must satisfy minimum <= initial <= maximum");
  if (!config.stun_server.empty () &&
      !parse_ice_server (config.stun_server))
    throw std::runtime_error ("invalid STUN server URI");
  for (const std::string& server: config.turn_servers) {
    auto parsed= parse_ice_server (server);
    if (!parsed || (server.rfind ("turn:", 0) != 0 &&
                    server.rfind ("turns:", 0) != 0 &&
                    server.rfind ("turn://", 0) != 0 &&
                    server.rfind ("turns://", 0) != 0))
      throw std::runtime_error ("invalid TURN server URI");
    if (server.find_first_of ("\"\r\n") != std::string::npos)
      throw std::runtime_error ("TURN server URI contains unsafe characters");
  }
  if (config.cloudflare_turn_key_id.empty () !=
      !config.cloudflare_turn_token_file)
    throw std::runtime_error (
      "Cloudflare TURN requires both a key id and token file");
  if (!config.cloudflare_turn_key_id.empty ()) {
    if (!std::all_of (
          config.cloudflare_turn_key_id.begin (),
          config.cloudflare_turn_key_id.end (), [] (unsigned char c) {
            return std::isalnum (c) || c == '-' || c == '_';
          }))
      throw std::runtime_error ("Cloudflare TURN key id is malformed");
    if (!config.turn_servers.empty ())
      throw std::runtime_error (
        "static TURN servers and Cloudflare TURN cannot be combined");
  }
  return config;
}

void
verify_container_isolation (const Config& config) {
  ProcessResult result= run_process ({
    config.container_runtime, "run", "--rm",
    "--network=none",
    "--http-proxy=false",
    "--read-only",
    "--cap-drop=all",
    "--security-opt=no-new-privileges",
    "--pids-limit=16",
    "--memory=64m",
    "--memory-swap=64m",
    "--userns=keep-id:uid=1000,gid=1000",
    "--user=1000:1000",
    "--tmpfs=/tmp:rw,nosuid,nodev,noexec,size=8m,mode=1777",
    "--entrypoint=/usr/bin/true",
    config.image
  }, 30);
  if (result.status == 0) return;

  throw std::runtime_error (
    "container runtime cannot enforce the required session isolation "
    "(memory, PID, user-namespace, read-only, and network controls). "
    "For rootless Podman on systemd, enable MemoryAccounting for the user "
    "slice and log in again. Runtime output: " + trim (result.output));
}

} // namespace

int
main (int argc, char** argv) {
  try {
    CurlGlobal curl_global;
    Config config= parse_arguments (argc, argv);
    fs::path web_root= choose_web_root (
      config.web_root, fs::path (argv[0]),
      fs::path (ATHENA_WEB_SOURCE_ASSET_DIR),
      fs::path (ATHENA_WEB_INSTALL_ASSET_DIR));

    ProcessResult runtime= run_process ({
      config.container_runtime, "--version"
    }, 15);
    if (runtime.status != 0)
      throw std::runtime_error ("container runtime is unavailable: " +
                                trim (runtime.output));
    ProcessResult initialized= run_process ({
      config.container_runtime, "info",
      "--format={{.Host.Security.Rootless}}"
    }, 30);
    if (initialized.status != 0)
      throw std::runtime_error (
        "container runtime could not initialize its rootless namespace: " +
        trim (initialized.output));
    ProcessResult image= run_process ({
      config.container_runtime, "image", "exists", config.image
    }, 30);
    if (image.status != 0)
      throw std::runtime_error (
        "container image does not exist; run tools/athena-web-server/"
        "build-image.sh first: " + config.image);
    verify_container_isolation (config);

    SessionManager manager (config);
    ConnectionWorkers connections;
    listener_fd= create_listener (config);
    struct sigaction action {};
    action.sa_handler= signal_handler;
    sigemptyset (&action.sa_mask);
    sigaction (SIGINT, &action, nullptr);
    sigaction (SIGTERM, &action, nullptr);
    signal (SIGPIPE, SIG_IGN);

    std::cout << "athena-web-server: listening on http://"
              << config.listen_address << ':' << config.port << '\n'
              << "athena-web-server: image=" << config.image
              << " max-connections=" << config.max_connections
              << " memory=" << format_byte_size (config.max_memory)
              << " storage=" << format_byte_size (config.storage_limit)
              << '\n';

    while (!stopping.load ()) {
      int client= ::accept4 (listener_fd, nullptr, nullptr, SOCK_CLOEXEC);
      if (client < 0) {
        if (errno == EINTR) continue;
        if (stopping.load () || errno == EBADF) break;
        std::cerr << "athena-web-server: accept failed: "
                  << std::strerror (errno) << '\n';
        continue;
      }
      connections.start (client, manager, config, web_root);
    }
    listener_fd= -1;
    return 0;
  }
  catch (const std::exception& error) {
    std::cerr << "athena-web-server: " << error.what () << '\n';
    return 1;
  }
}
