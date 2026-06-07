/******************************************************************************
* MODULE     : mcp_rag_server.hpp
* DESCRIPTION: MCP Streamable HTTP server for Continuous RAG
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef MCP_RAG_SERVER_HPP
#define MCP_RAG_SERVER_HPP

#include <filesystem>
#include <string>

namespace athena::mcp {

struct RagServerOptions {
  std::filesystem::path vault_root;
  int port= 8765;
  std::filesystem::path embedding_model;
  std::string embedding_device= "auto";
  int index_jobs= 0;
  bool force_reindex= false;
  std::string bearer_token;
};

bool start_rag_server (const RagServerOptions& options);

} // namespace athena::mcp

#endif // MCP_RAG_SERVER_HPP
