/******************************************************************************
* MODULE     : rag_embedding.hpp
* DESCRIPTION: Optional llama.cpp embeddings for Continuous RAG
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef RAG_EMBEDDING_HPP
#define RAG_EMBEDDING_HPP

#include <string>
#include <vector>
#include <functional>

namespace athena::rag {

std::string rag_embedding_model_fingerprint (const std::string& model_path);

class RagEmbedder {
public:
  RagEmbedder ();
  ~RagEmbedder ();

  bool open (const std::string& model_path,
             const std::string& device_mode= "auto",
             int threads= 0);
  bool available () const;
  int  dimension () const;
  std::string model_fingerprint () const;
  std::vector<float> embed (const std::string& text);
  std::vector<std::vector<float>> embed_many (
    const std::vector<std::string>& texts,
    const std::function<void(size_t,size_t)>& progress= {});

private:
  struct Impl;
  Impl* impl;
};

} // namespace athena::rag

#endif // RAG_EMBEDDING_HPP
