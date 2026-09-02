/******************************************************************************
* MODULE     : outline_snapshot.hpp
* DESCRIPTION: Shared blob format for BufferActor outline snapshots
* COPYRIGHT  : (C) 2026  Nuaptan F. Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef OUTLINE_SNAPSHOT_HPP
#define OUTLINE_SNAPSHOT_HPP

#include "actor_transport.hpp"
#include "heading_word_count.hpp"

#include <cstdint>
#include <type_traits>

constexpr std::uint32_t ATHENA_OUTLINE_SNAPSHOT_VERSION= 1;

struct actor_outline_snapshot_header {
  std::uint64_t signature;
  std::uint32_t version;
  std::uint32_t entry_count;
};

struct actor_outline_snapshot_entry {
  std::int32_t level;
  std::int32_t words;
  std::uint32_t title_offset;
  std::uint32_t title_size;
  std::uint32_t path_offset;
  std::uint32_t path_count;
};

static_assert (std::is_trivially_copyable<actor_outline_snapshot_header>::value,
               "outline snapshot header must remain POD");
static_assert (std::is_trivially_copyable<actor_outline_snapshot_entry>::value,
               "outline snapshot entries must remain POD");

std::uint64_t athena_outline_signature (const tree& document);
athena_blob_id athena_pack_outline_snapshot (
  const array<heading_word_count_entry>& entries, std::uint64_t signature);

#endif // defined OUTLINE_SNAPSHOT_HPP
