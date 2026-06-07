/******************************************************************************
* MODULE     : rag_index.hpp
* DESCRIPTION: Continuous RAG SQLite index for ATHENA vaults
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef RAG_INDEX_HPP
#define RAG_INDEX_HPP

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace athena::rag {

struct RagConfig {
  std::filesystem::path vault_root;
  std::filesystem::path db_path;
  std::filesystem::path embedding_model;
  std::string embedding_device= "auto";
  int embedding_threads= 0;
  bool force_reindex= false;
  bool load_embedding_model= true;
  int shard_index= 0;
  int shard_count= 1;
  bool progress= true;
  int progress_fd= -1;
};

struct RagChunk {
  std::string chunk_id;
  std::string rel_path;
  std::string kind;
  std::string tree_path;
  std::string anchor;
  std::string title;
  std::string heading_path;
  std::string text;
  std::string source;
  double score= 0.0;
};

struct RagStatus {
  bool open= false;
  std::string vault_root;
  std::string db_path;
  std::string embedding_model;
  bool embeddings_enabled= false;
  std::string embedding_warning;
  int document_count= 0;
  int chunk_count= 0;
  int malformed_count= 0;
  std::string last_error;
};

class RagIndex {
public:
  RagIndex ();
  ~RagIndex ();

  bool open (const RagConfig& config);
  bool scan_once ();
  bool parallel_reindex (int jobs);
  void set_progress_enabled (bool enabled);
  RagStatus status () const;

  std::vector<RagChunk> search (const std::string& query, int limit);
  std::optional<RagChunk> read_chunk (const std::string& chunk_id) const;
  std::string read_document (const std::string& rel_path) const;
  std::vector<RagChunk> related (const std::string& chunk_id, int limit) const;
  std::vector<RagChunk> backlinks (const std::string& target, int limit) const;
  std::vector<RagChunk> list_chunks (int limit) const;

  const std::filesystem::path& vault_root () const;

private:
  struct Impl;
  Impl* impl;
};

std::string rag_default_db_path (const std::filesystem::path& vault_root);
std::string rag_read_vault_db_path (const std::filesystem::path& vault_root);

} // namespace athena::rag

#endif // RAG_INDEX_HPP
