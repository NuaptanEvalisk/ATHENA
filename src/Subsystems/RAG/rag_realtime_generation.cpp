/******************************************************************************
* MODULE     : rag_realtime_generation.cpp
* DESCRIPTION: Process-local saved-revision generation registry for RAG
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "rag_realtime_generation.hpp"

#include <mutex>
#include <string>
#include <unordered_map>

namespace athena::rag {
namespace {

std::mutex generation_mutex;
std::unordered_map<std::string,std::uint64_t> latest_generation;

std::string
generation_key (const std::filesystem::path& path) {
  return path.lexically_normal ().generic_string ();
}

} // namespace

void
rag_note_saved_generation (const std::filesystem::path& absolute_path,
                           std::uint64_t generation) {
  if (absolute_path.empty () || generation == 0) return;
  std::lock_guard<std::mutex> guard (generation_mutex);
  std::uint64_t& current= latest_generation[generation_key (absolute_path)];
  if (generation > current) current= generation;
}

std::uint64_t
rag_saved_generation (const std::filesystem::path& absolute_path) {
  std::lock_guard<std::mutex> guard (generation_mutex);
  auto found= latest_generation.find (generation_key (absolute_path));
  return found == latest_generation.end () ? 0 : found->second;
}

bool
rag_saved_generation_is_current (const std::filesystem::path& absolute_path,
                                 std::uint64_t generation) {
  return !absolute_path.empty () &&
    rag_saved_generation (absolute_path) == generation;
}

} // namespace athena::rag
