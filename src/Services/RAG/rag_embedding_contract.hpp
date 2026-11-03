/******************************************************************************
* MODULE     : rag_embedding_contract.hpp
* DESCRIPTION: Stable embedding-space contracts shared by RAG backends
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef RAG_EMBEDDING_CONTRACT_HPP
#define RAG_EMBEDDING_CONTRACT_HPP

namespace athena::rag {

inline constexpr const char* bge_m3_embedding_space_id=
  "athena-embedding/baai-bge-m3/dense-cls-l2/bytes12000-tokens8192/v1";
inline constexpr int bge_m3_embedding_dimension= 1024;
inline constexpr int bge_m3_vocab_size= 250002;
inline constexpr int bge_m3_bos_token= 0;
inline constexpr int bge_m3_pad_token= 1;
inline constexpr int bge_m3_eos_token= 2;
inline constexpr int bge_m3_input_byte_limit= 12000;
inline constexpr int bge_m3_max_tokens= 8192;

} // namespace athena::rag

#endif // RAG_EMBEDDING_CONTRACT_HPP
