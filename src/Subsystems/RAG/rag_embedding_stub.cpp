/******************************************************************************
* MODULE     : rag_embedding_stub.cpp
* DESCRIPTION: Unavailable local embedding backend for inference-free targets
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "rag_embedding.hpp"
#include "rag_embedding_contract.hpp"

#include <filesystem>

namespace athena::rag {

struct RagEmbedder::Impl {};

RagEmbedder::RagEmbedder () : impl (new Impl) {}
RagEmbedder::~RagEmbedder () { delete impl; }

bool
RagEmbedder::open (const std::string&, const std::string&, int) {
  return false;
}

bool RagEmbedder::available () const { return false; }
int RagEmbedder::dimension () const { return 0; }
std::string RagEmbedder::model_fingerprint () const { return {}; }
std::string RagEmbedder::space_id () const { return {}; }

std::vector<float>
RagEmbedder::embed (const std::string&) {
  return {};
}

std::vector<std::vector<float>>
RagEmbedder::embed_many (
  const std::vector<std::string>& texts,
  const std::function<void(size_t,size_t)>& progress) {
  std::vector<std::vector<float>> result (texts.size ());
  if (progress) progress (texts.size (), texts.size ());
  return result;
}

const char*
rag_bge_m3_embedding_space_id () {
  return bge_m3_embedding_space_id;
}

std::string
rag_llama_embedding_space_id (const std::string& model_fingerprint) {
  if (model_fingerprint.empty ()) return {};
  return "athena-embedding/llama-gguf/v1/" + model_fingerprint;
}

std::string
rag_embedding_space_id_for_model (const std::string&) {
  return {};
}

std::string
rag_embedding_model_fingerprint (const std::string&) {
  return {};
}

} // namespace athena::rag
