/******************************************************************************
* MODULE     : web_server_common.cpp
* DESCRIPTION: Shared validation helpers for Web-Accessible ATHENA
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "web_server_common.hpp"

#include <boost/json.hpp>
#include <boost/json/src.hpp>

#include <sys/socket.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cctype>
#include <charconv>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace athena::web {

namespace json = boost::json;

namespace {

std::string
lower_ascii (std::string text) {
  std::transform (text.begin (), text.end (), text.begin (), [] (char c) {
    return char (std::tolower (static_cast<unsigned char> (c)));
  });
  return text;
}

int
hex_value (char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
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
valid_http_token (const std::string& text) {
  if (text.empty ()) return false;
  for (unsigned char c: text) {
    if (std::isalnum (c)) continue;
    switch (c) {
    case '!': case '#': case '$': case '%': case '&': case '\'':
    case '*': case '+': case '-': case '.': case '^': case '_':
    case '`': case '|': case '~':
      continue;
    default:
      return false;
    }
  }
  return true;
}

} // namespace

uint64_t
parse_byte_size (std::string_view input) {
  if (input.empty ()) throw std::invalid_argument ("empty byte size");
  size_t number_end= 0;
  while (number_end < input.size () &&
         std::isdigit (static_cast<unsigned char> (input[number_end])))
    number_end++;
  if (number_end == 0) throw std::invalid_argument ("invalid byte size");

  uint64_t value= 0;
  auto result= std::from_chars (input.data (), input.data () + number_end,
                                value);
  if (result.ec != std::errc ())
    throw std::invalid_argument ("invalid byte size");

  std::string suffix= lower_ascii (
    std::string (input.substr (number_end)));
  uint64_t multiplier= 1;
  if (suffix.empty () || suffix == "b") multiplier= 1;
  else if (suffix == "k" || suffix == "kb" || suffix == "kib")
    multiplier= uint64_t (1) << 10;
  else if (suffix == "m" || suffix == "mb" || suffix == "mib")
    multiplier= uint64_t (1) << 20;
  else if (suffix == "g" || suffix == "gb" || suffix == "gib")
    multiplier= uint64_t (1) << 30;
  else if (suffix == "t" || suffix == "tb" || suffix == "tib")
    multiplier= uint64_t (1) << 40;
  else throw std::invalid_argument ("unsupported byte-size suffix");

  if (value > std::numeric_limits<uint64_t>::max () / multiplier)
    throw std::overflow_error ("byte size is too large");
  return value * multiplier;
}

std::string
format_byte_size (uint64_t bytes) {
  static constexpr std::array<const char*,5> suffixes {
    "B", "KiB", "MiB", "GiB", "TiB"
  };
  double value= double (bytes);
  size_t suffix= 0;
  while (value >= 1024.0 && suffix + 1 < suffixes.size ()) {
    value/= 1024.0;
    suffix++;
  }
  std::ostringstream out;
  if (suffix == 0) out << bytes;
  else out << std::fixed << std::setprecision (value < 10 ? 1 : 0) << value;
  out << ' ' << suffixes[suffix];
  return out.str ();
}

bool
valid_upload_filename (std::string_view name) {
  if (name.empty () || name.size () > 240 || name == "." || name == "..")
    return false;
  for (unsigned char c: name) {
    if (c == '/' || c == '\\' || c == '\0' || c < 0x20 || c == 0x7f)
      return false;
  }
  return true;
}

bool
valid_download_path (std::string_view path) {
  if (path.empty () || path.size () > 1024 || path.front () == '/')
    return false;
  std::vector<std::string> parts= split_path (path);
  if (parts.empty ()) return false;
  size_t represented= parts.size () - 1;
  for (const std::string& part: parts) {
    if (!valid_upload_filename (part)) return false;
    represented+= part.size ();
  }
  return represented == path.size ();
}

bool
valid_session_token (std::string_view token) {
  if (token.size () != 48) return false;
  return std::all_of (token.begin (), token.end (), [] (char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
  });
}

std::string
percent_decode (std::string_view text) {
  std::string result;
  result.reserve (text.size ());
  for (size_t i= 0; i < text.size (); i++) {
    if (text[i] == '%' && i + 2 < text.size ()) {
      int high= hex_value (text[i + 1]);
      int low= hex_value (text[i + 2]);
      if (high >= 0 && low >= 0) {
        result.push_back (char ((high << 4) | low));
        i+= 2;
        continue;
      }
    }
    result.push_back (text[i] == '+' ? ' ': text[i]);
  }
  return result;
}

std::string
percent_encode (std::string_view text) {
  static constexpr char hex[]= "0123456789ABCDEF";
  std::string result;
  for (unsigned char c: text) {
    if (std::isalnum (c) || c == '-' || c == '_' || c == '.' || c == '~')
      result.push_back (char (c));
    else {
      result.push_back ('%');
      result.push_back (hex[c >> 4]);
      result.push_back (hex[c & 15]);
    }
  }
  return result;
}

std::vector<std::string>
split_path (std::string_view path) {
  std::vector<std::string> result;
  size_t start= 0;
  while (start < path.size ()) {
    while (start < path.size () && path[start] == '/') start++;
    if (start == path.size ()) break;
    size_t end= path.find ('/', start);
    if (end == std::string_view::npos) end= path.size ();
    result.emplace_back (path.substr (start, end - start));
    start= end;
  }
  return result;
}

bool
read_http_request (int fd, HttpRequest& request, std::string& error) {
  constexpr size_t max_header_bytes= 64 * 1024;
  std::string buffer;
  std::array<char,8192> chunk {};
  size_t header_end= std::string::npos;
  while ((header_end= buffer.find ("\r\n\r\n")) == std::string::npos) {
    ssize_t count= ::recv (fd, chunk.data (), chunk.size (), 0);
    if (count < 0 && errno == EINTR) continue;
    if (count <= 0) {
      error= "connection closed before HTTP headers";
      return false;
    }
    buffer.append (chunk.data (), size_t (count));
    if (buffer.size () > max_header_bytes) {
      error= "HTTP headers are too large";
      return false;
    }
  }

  std::string headers= buffer.substr (0, header_end);
  request.buffered_body= buffer.substr (header_end + 4);
  std::istringstream stream (headers);
  std::string line;
  if (!std::getline (stream, line)) {
    error= "missing HTTP request line";
    return false;
  }
  if (!line.empty () && line.back () == '\r') line.pop_back ();
  std::istringstream request_line (line);
  if (!(request_line >> request.method >> request.target >> request.version)) {
    error= "invalid HTTP request line";
    return false;
  }
  std::string trailing;
  if (request_line >> trailing ||
      (request.version != "HTTP/1.0" && request.version != "HTTP/1.1")) {
    error= "unsupported HTTP request line";
    return false;
  }
  if (request.target.empty () || request.target.front () != '/' ||
      request.target.find ('#') != std::string::npos) {
    error= "invalid HTTP request target";
    return false;
  }
  size_t query= request.target.find ('?');
  request.path= request.target.substr (0, query);
  if (query != std::string::npos) request.query= request.target.substr (query + 1);

  while (std::getline (stream, line)) {
    if (!line.empty () && line.back () == '\r') line.pop_back ();
    if (line.empty () ||
        std::isspace (static_cast<unsigned char> (line.front ()))) {
      error= "obsolete folded HTTP headers are not accepted";
      return false;
    }
    size_t colon= line.find (':');
    if (colon == std::string::npos) {
      error= "invalid HTTP header";
      return false;
    }
    std::string name= lower_ascii (trim (line.substr (0, colon)));
    if (!valid_http_token (name)) {
      error= "invalid HTTP header name";
      return false;
    }
    if (!request.headers.emplace (
          std::move (name), trim (line.substr (colon + 1))).second) {
      error= "duplicate HTTP headers are not accepted";
      return false;
    }
  }
  if (request.headers.count ("transfer-encoding") != 0) {
    error= "Transfer-Encoding is not supported";
    return false;
  }
  auto length= request.headers.find ("content-length");
  if (length != request.headers.end ()) {
    try {
      size_t consumed= 0;
      request.content_length= std::stoull (length->second, &consumed);
      if (consumed != length->second.size ())
        throw std::invalid_argument ("trailing content-length data");
    }
    catch (...) {
      error= "invalid Content-Length";
      return false;
    }
  }
  return true;
}

std::optional<IceServer>
parse_ice_server (std::string_view input) {
  std::string uri (input);
  size_t scheme_end= uri.find (':');
  if (scheme_end == std::string::npos) return std::nullopt;
  std::string scheme= lower_ascii (uri.substr (0, scheme_end));
  if (scheme != "stun" && scheme != "stuns" &&
      scheme != "turn" && scheme != "turns")
    return std::nullopt;

  IceServer result;
  result.uri= uri;
  size_t authority= scheme_end + 1;
  if (uri.compare (authority, 2, "//") == 0) authority+= 2;
  result.browser_url= scheme + ":" + uri.substr (authority);
  if (scheme == "turn" || scheme == "turns") {
    size_t at= uri.find ('@', authority);
    if (at != std::string::npos) {
      std::string userinfo= uri.substr (authority, at - authority);
      size_t colon= userinfo.find (':');
      result.username= percent_decode (
        colon == std::string::npos ? userinfo: userinfo.substr (0, colon));
      if (colon != std::string::npos)
        result.credential= percent_decode (userinfo.substr (colon + 1));
      result.browser_url= scheme + ":" + uri.substr (at + 1);
    }
  }
  return result;
}

std::optional<TurnCredentials>
parse_cloudflare_turn_credentials (std::string_view response) {
  boost::system::error_code error;
  json::value parsed= json::parse (response, error);
  if (error || !parsed.is_object ()) return std::nullopt;
  const json::object& root= parsed.as_object ();
  auto ice_it= root.find ("iceServers");
  if (ice_it == root.end () || !ice_it->value ().is_array ())
    return std::nullopt;

  TurnCredentials result;
  for (const json::value& entry_value: ice_it->value ().as_array ()) {
    if (!entry_value.is_object ()) return std::nullopt;
    const json::object& entry= entry_value.as_object ();
    auto urls_it= entry.find ("urls");
    if (urls_it == entry.end ()) return std::nullopt;

    std::string username;
    std::string credential;
    auto username_it= entry.find ("username");
    auto credential_it= entry.find ("credential");
    if (username_it != entry.end ()) {
      if (!username_it->value ().is_string ()) return std::nullopt;
      username= std::string (username_it->value ().as_string ());
    }
    if (credential_it != entry.end ()) {
      if (!credential_it->value ().is_string ()) return std::nullopt;
      credential= std::string (credential_it->value ().as_string ());
    }
    if (username.empty () != credential.empty ()) return std::nullopt;

    std::vector<std::string> urls;
    if (urls_it->value ().is_string ())
      urls.emplace_back (urls_it->value ().as_string ());
    else if (urls_it->value ().is_array ()) {
      for (const json::value& url: urls_it->value ().as_array ()) {
        if (!url.is_string ()) return std::nullopt;
        urls.emplace_back (url.as_string ());
      }
    }
    else return std::nullopt;

    for (const std::string& url: urls) {
      auto ice= parse_ice_server (url);
      if (!ice) return std::nullopt;
      bool is_turn= ice->browser_url.rfind ("turn:", 0) == 0 ||
                    ice->browser_url.rfind ("turns:", 0) == 0;
      bool is_stun= ice->browser_url.rfind ("stun:", 0) == 0 ||
                    ice->browser_url.rfind ("stuns:", 0) == 0;
      if (!is_turn && !is_stun) return std::nullopt;
      if (is_turn && (username.empty () || credential.empty ()))
        return std::nullopt;
      if (is_stun && (!username.empty () || !credential.empty ()))
        return std::nullopt;

      if (is_stun && result.stun_server.empty ())
        result.stun_server= ice->browser_url;
      if (is_turn) {
        size_t colon= ice->browser_url.find (':');
        std::string scheme= ice->browser_url.substr (0, colon);
        std::string authority= ice->browser_url.substr (colon + 1);
        ice->username= username;
        ice->credential= credential;
        ice->uri= scheme + "://" + percent_encode (username) + ":" +
          percent_encode (credential) + "@" + authority;
      }
      result.ice_servers.push_back (std::move (*ice));
    }
  }

  bool has_turn= std::any_of (
    result.ice_servers.begin (), result.ice_servers.end (),
    [] (const IceServer& server) {
      return server.browser_url.rfind ("turn:", 0) == 0 ||
             server.browser_url.rfind ("turns:", 0) == 0;
    });
  if (result.stun_server.empty () || !has_turn) return std::nullopt;
  return result;
}

fs::path
choose_web_root (const std::optional<fs::path>& configured,
                 const fs::path& executable,
                 const fs::path& source_default,
                 const fs::path& install_default) {
  std::vector<fs::path> candidates;
  if (configured) candidates.push_back (*configured);
  std::error_code ec;
  fs::path canonical_executable= fs::weakly_canonical (executable, ec);
  if (!ec && !canonical_executable.empty ())
    candidates.push_back (
      canonical_executable.parent_path ().parent_path () /
      "share" / "ATHENA" / "web");
  candidates.push_back (install_default);
  candidates.push_back (source_default);
  for (const fs::path& candidate: candidates) {
    if (fs::is_regular_file (candidate / "index.html") &&
        fs::is_regular_file (candidate / "app.js"))
      return fs::weakly_canonical (candidate);
  }
  throw std::runtime_error ("could not locate ATHENA web assets");
}

} // namespace athena::web
