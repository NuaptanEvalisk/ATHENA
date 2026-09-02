/******************************************************************************
* MODULE     : outline_snapshot.cpp
* DESCRIPTION: Shared blob format for BufferActor outline snapshots
* COPYRIGHT  : (C) 2026  Nuaptan F. Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "outline_snapshot.hpp"

#include <cstring>
#include <limits>
#include <stdexcept>

namespace {

std::size_t
path_size (const path& value) {
  std::size_t count= 0;
  const path* cursor= &value;
  while (!is_nil (*cursor)) {
    ++count;
    cursor= &((*cursor)->next);
  }
  return count;
}

void
write_path (std::byte* destination, const path& value) {
  const path* cursor= &value;
  while (!is_nil (*cursor)) {
    std::int32_t item= static_cast<std::int32_t> ((*cursor)->item);
    std::memcpy (destination, &item, sizeof (item));
    destination += sizeof (item);
    cursor= &((*cursor)->next);
  }
}

void
add_size (std::size_t& total, std::size_t amount) {
  if (amount > std::numeric_limits<std::size_t>::max () - total)
    throw std::length_error ("outline snapshot is too large");
  total += amount;
}

} // namespace

std::uint64_t
athena_outline_signature (const tree& document) {
  return (static_cast<std::uint64_t> (
            static_cast<std::uint32_t> (hash (document))) << 32) |
         static_cast<std::uint32_t> (N (document));
}

athena_blob_id
athena_pack_outline_snapshot (
  const array<heading_word_count_entry>& entries, std::uint64_t signature) {
  std::size_t count= static_cast<std::size_t> (N (entries));
  if (count > std::numeric_limits<std::uint32_t>::max ())
    throw std::length_error ("outline has too many entries");

  std::size_t path_bytes= 0;
  std::size_t title_bytes= 0;
  for (int i= 0; i < N (entries); ++i) {
    add_size (path_bytes, path_size (entries[i].tree_path) *
                          sizeof (std::int32_t));
    add_size (title_bytes, static_cast<std::size_t> (N (entries[i].title)));
  }

  std::size_t total= sizeof (actor_outline_snapshot_header);
  add_size (total, count * sizeof (actor_outline_snapshot_entry));
  std::size_t paths_offset= total;
  add_size (total, path_bytes);
  std::size_t titles_offset= total;
  add_size (total, title_bytes);
  if (total > std::numeric_limits<std::uint32_t>::max ())
    throw std::length_error ("outline snapshot exceeds its offset space");

  actor_blob_reservation reservation=
    actor_blob_registry::instance ().allocate (total);
  std::byte* data= reservation.data ();
  actor_outline_snapshot_header header {
    signature, ATHENA_OUTLINE_SNAPSHOT_VERSION,
    static_cast<std::uint32_t> (count)};
  std::memcpy (data, &header, sizeof (header));

  std::size_t next_path= paths_offset;
  std::size_t next_title= titles_offset;
  for (int i= 0; i < N (entries); ++i) {
    std::size_t count2= path_size (entries[i].tree_path);
    std::size_t title_size= static_cast<std::size_t> (N (entries[i].title));
    actor_outline_snapshot_entry record {
      static_cast<std::int32_t> (entries[i].level),
      static_cast<std::int32_t> (entries[i].words),
      static_cast<std::uint32_t> (next_title),
      static_cast<std::uint32_t> (title_size),
      static_cast<std::uint32_t> (next_path),
      static_cast<std::uint32_t> (count2)};
    std::memcpy (data + sizeof (header) +
                   static_cast<std::size_t> (i) * sizeof (record),
                 &record, sizeof (record));
    write_path (data + next_path, entries[i].tree_path);
    if (title_size != 0)
      std::memcpy (data + next_title, entries[i].title.data (), title_size);
    next_path += count2 * sizeof (std::int32_t);
    next_title += title_size;
  }
  return reservation.publish ();
}
