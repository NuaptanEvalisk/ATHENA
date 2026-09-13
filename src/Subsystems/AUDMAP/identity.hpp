/******************************************************************************
* MODULE     : identity.hpp
* DESCRIPTION: Client key identity and remembered authorization interfaces
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#pragma once
#include "session.hpp"
#include <filesystem>
#include <optional>

namespace athena::interop {
struct client_identity {
  std::string public_key, secret_key;
};
client_identity load_client_identity (const std::filesystem::path& path);
std::filesystem::path default_client_identity (const std::string& profile);

struct remembered_authorization {
  bool allow;
  trust_mode trust;
};
class authorization_store {
  std::filesystem::path path;
public:
  explicit authorization_store (std::filesystem::path path): path (std::move (path)) {}
  std::optional<remembered_authorization> lookup (const std::string& public_key) const;
  void remember (const std::string& public_key, remembered_authorization decision) const;
};
} // namespace athena::interop
