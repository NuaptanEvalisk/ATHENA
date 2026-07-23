/******************************************************************************
* MODULE     : web_server_common.hpp
* DESCRIPTION: Shared validation helpers for Web-Accessible ATHENA
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace athena::web {

namespace fs = std::filesystem;

struct IceServer {
  std::string uri;
  std::string browser_url;
  std::string username;
  std::string credential;
};

struct TurnCredentials {
  std::string stun_server;
  std::vector<IceServer> ice_servers;
};

struct HttpRequest {
  std::string method;
  std::string target;
  std::string path;
  std::string query;
  std::string version;
  std::map<std::string,std::string> headers;
  std::string buffered_body;
  uint64_t content_length= 0;
};

uint64_t parse_byte_size (std::string_view text);
std::string format_byte_size (uint64_t bytes);
bool valid_upload_filename (std::string_view name);
bool valid_download_path (std::string_view path);
bool valid_session_token (std::string_view token);
std::string percent_decode (std::string_view text);
std::string percent_encode (std::string_view text);
std::vector<std::string> split_path (std::string_view path);
std::optional<IceServer> parse_ice_server (std::string_view uri);
std::optional<TurnCredentials>
parse_cloudflare_turn_credentials (std::string_view response);
bool read_http_request (int fd, HttpRequest& request, std::string& error);
fs::path choose_web_root (const std::optional<fs::path>& configured,
                          const fs::path& executable,
                          const fs::path& source_default,
                          const fs::path& install_default);

} // namespace athena::web
