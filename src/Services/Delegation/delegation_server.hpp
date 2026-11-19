/******************************************************************************
* MODULE     : delegation_server.hpp
* DESCRIPTION: Authenticated embedding and artifact delegation server
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef DELEGATION_SERVER_HPP
#define DELEGATION_SERVER_HPP

#include <filesystem>
#include <string>

namespace athena::delegation {

struct DelegationServerOptions {
  int port= 8765;
  std::filesystem::path embedding_model;
  std::string embedding_device= "auto";
  std::string listen_address= "127.0.0.1";
  std::filesystem::path delegation_key_dir;
  std::filesystem::path delegation_accepted_clients;
  std::filesystem::path artifact_range_model;
  int artifact_range_batch_size= 16;
  int artifact_queue_limit= 4096;
  int artifact_queue_bytes= 32 * 1024 * 1024;
};

bool start_delegation_server (const DelegationServerOptions& options);

} // namespace athena::delegation

#endif // DELEGATION_SERVER_HPP
