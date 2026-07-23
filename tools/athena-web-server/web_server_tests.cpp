/******************************************************************************
* MODULE     : web_server_tests.cpp
* DESCRIPTION: Unit tests for Web-Accessible ATHENA validation
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "web_server_common.hpp"

#include <sys/socket.h>
#include <unistd.h>

#include <iostream>
#include <stdexcept>

using namespace athena::web;

namespace {

void
require (bool condition, const char* message) {
  if (!condition) throw std::runtime_error (message);
}

bool
parse_request (const std::string& wire, HttpRequest& request,
               std::string& error) {
  int sockets[2];
  if (::socketpair (AF_UNIX, SOCK_STREAM, 0, sockets) != 0)
    throw std::runtime_error ("socketpair failed");
  ssize_t written= ::write (sockets[0], wire.data (), wire.size ());
  if (written != ssize_t (wire.size ())) {
    ::close (sockets[0]);
    ::close (sockets[1]);
    throw std::runtime_error ("request fixture write failed");
  }
  ::shutdown (sockets[0], SHUT_WR);
  bool result= read_http_request (sockets[1], request, error);
  ::close (sockets[0]);
  ::close (sockets[1]);
  return result;
}

} // namespace

int
main () {
  try {
    require (parse_byte_size ("512MiB") == (uint64_t (512) << 20),
             "MiB size parsing failed");
    require (parse_byte_size ("2g") == (uint64_t (2) << 30),
             "GiB size parsing failed");
    require (valid_upload_filename ("Lecture notes.ath"),
             "valid filename rejected");
    require (!valid_upload_filename ("../escape.ath"),
             "path traversal filename accepted");
    require (!valid_upload_filename ("nested/file.ath"),
             "nested filename accepted");
    require (valid_download_path ("Documents/result.ath"),
             "valid download path rejected");
    require (!valid_download_path ("Documents/../secret"),
             "download traversal path accepted");
    require (valid_session_token (std::string (48, 'a')),
             "valid session token rejected");
    require (!valid_session_token (std::string (47, 'a')),
             "short session token accepted");
    require (percent_decode ("A%20B%2Bath") == "A B+ath",
             "percent decoding failed");

    auto turn= parse_ice_server (
      "turn://demo%40user:secret%2Fword@turn.example:3478");
    require (turn.has_value (), "TURN URI rejected");
    require (turn->browser_url == "turn:turn.example:3478",
             "TURN browser URL is wrong");
    require (turn->username == "demo@user" &&
             turn->credential == "secret/word",
             "TURN credentials are wrong");
    require (!parse_ice_server ("https://example.com").has_value (),
             "non-ICE URI accepted");
    auto stun= parse_ice_server ("stun://stun.example:3478");
    require (stun.has_value () &&
             stun->uri == "stun://stun.example:3478" &&
             stun->browser_url == "stun:stun.example:3478",
             "STUN URI was not normalized for the browser");
    auto canonical_stun= parse_ice_server ("stun:stun.example:3478");
    require (canonical_stun.has_value () &&
             canonical_stun->browser_url == "stun:stun.example:3478",
             "canonical browser STUN URI changed");

    const std::string cloudflare_response= R"JSON({
      "iceServers": [
        {
          "urls": [
            "stun:stun.cloudflare.com:3478",
            "stun:stun.cloudflare.com:53"
          ]
        },
        {
          "urls": [
            "turn:turn.cloudflare.com:3478?transport=udp",
            "turns:turn.cloudflare.com:443?transport=tcp"
          ],
          "username": "session/user",
          "credential": "secret+word"
        }
      ]
    })JSON";
    auto cloudflare= parse_cloudflare_turn_credentials (cloudflare_response);
    require (cloudflare.has_value (), "Cloudflare TURN response rejected");
    require (cloudflare->stun_server == "stun:stun.cloudflare.com:3478",
             "Cloudflare primary STUN server was not selected");
    require (cloudflare->ice_servers.size () == 4,
             "Cloudflare ICE server list was truncated");
    const IceServer& relay= cloudflare->ice_servers[2];
    require (
      relay.browser_url ==
        "turn:turn.cloudflare.com:3478?transport=udp" &&
      relay.username == "session/user" &&
      relay.credential == "secret+word" &&
      relay.uri ==
        "turn://session%2Fuser:secret%2Bword@"
        "turn.cloudflare.com:3478?transport=udp",
      "Cloudflare TURN relay credentials were not normalized");
    require (!parse_cloudflare_turn_credentials (
               R"JSON({"iceServers":[{"urls":"turn:host:3478"}]})JSON"),
             "credential-free Cloudflare TURN relay accepted");
    require (!parse_cloudflare_turn_credentials ("{\"iceServers\":[]}"),
             "empty Cloudflare ICE response accepted");

    HttpRequest request;
    std::string error;
    require (parse_request (
               "PUT /api/sessions/token/upload?part=1 HTTP/1.1\r\n"
               "Host: localhost\r\nContent-Length: 4\r\n\r\ndata",
               request, error),
             "valid HTTP request rejected");
    require (request.path == "/api/sessions/token/upload" &&
             request.query == "part=1" && request.content_length == 4 &&
             request.buffered_body == "data",
             "valid HTTP request parsed incorrectly");

    request= HttpRequest {};
    error.clear ();
    require (!parse_request (
               "GET / HTTP/1.1\r\nHost: first\r\nHost: second\r\n\r\n",
               request, error),
             "duplicate HTTP headers accepted");
    request= HttpRequest {};
    error.clear ();
    require (!parse_request (
               "POST / HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n",
               request, error),
             "Transfer-Encoding accepted");
    request= HttpRequest {};
    error.clear ();
    require (!parse_request (
               "GET / HTTP/1.1\r\nHost: localhost\r\n folded\r\n\r\n",
               request, error),
             "folded HTTP header accepted");
    request= HttpRequest {};
    error.clear ();
    require (!parse_request (
               "GET http://localhost/ HTTP/1.1\r\nHost: localhost\r\n\r\n",
               request, error),
             "absolute-form request target accepted");
    request= HttpRequest {};
    error.clear ();
    require (!parse_request (
               "GET / HTTP/2\r\nHost: localhost\r\n\r\n",
               request, error),
             "unsupported HTTP version accepted");
  }
  catch (const std::exception& error) {
    std::cerr << "athena-web-server-tests: " << error.what () << '\n';
    return 1;
  }
  return 0;
}
