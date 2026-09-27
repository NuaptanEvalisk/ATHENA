/******************************************************************************
* MODULE     : rag_realtime_generation.hpp
* DESCRIPTION: Process-local saved-revision generation registry for RAG
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef RAG_REALTIME_GENERATION_HPP
#define RAG_REALTIME_GENERATION_HPP

#include <cstdint>
#include <filesystem>

namespace athena::rag {

void rag_note_saved_generation (const std::filesystem::path& absolute_path,
                                std::uint64_t generation);
std::uint64_t rag_saved_generation (const std::filesystem::path& absolute_path);
bool rag_saved_generation_is_current (
  const std::filesystem::path& absolute_path, std::uint64_t generation);

} // namespace athena::rag

#endif // RAG_REALTIME_GENERATION_HPP
