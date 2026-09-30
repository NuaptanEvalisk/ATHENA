/******************************************************************************
* MODULE     : artifact_live_cache.cpp
* DESCRIPTION: Incremental live artifact extraction and coalesced publication
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#include "artifact_live_cache.hpp"
#include "artifact_radioactive_links.hpp"
#include "buffer_actor.hpp"
#include "buffer_state.hpp"
#include "node_metadata.hpp"
#include <algorithm>
#include <cstdlib>
#include <mutex>
#include <set>

namespace athena::artifact {
namespace {
struct queues {
  std::mutex lock;
  std::map<std::uint64_t, publication> live;
  std::set<std::uint64_t> closed;
  std::map<std::string, saved_revision> saves;
};
queues& pending () { static auto* value= new queues; return *value; }

std::string relative (const vault_context_handle& vault, const std::filesystem::path& file) {
  if (!vault || !file.is_absolute () || file.extension () != ".ath") return {};
  const auto rel= file.lexically_normal ().lexically_relative (vault->root.lexically_normal ());
  if (rel.empty ()) return {};
  for (const auto& component: rel)
    if (component == ".." || component == ".athena" || component == ".backup" || component == ".git")
      return {};
  return rel.generic_string ();
}
}

bool live_cache::ready (const buffer_document_state& state) const {
  const auto vault= vault_capture_context ();
  const string name= as_string (state.name, URL_SYSTEM);
  const auto rel= relative (vault, std::filesystem::path (std::string (name.data (), N(name))));
  if (rel.empty ()) return !initialized;
  return initialized && incarnation == vault->incarnation && file == rel;
}

void live_cache::update (buffer_document_state& state, tree& body,
                         const std::vector<int>& dirty) {
  const auto vault= vault_capture_context ();
  const string name= as_string (state.name, URL_SYSTEM);
  const auto rel= relative (vault, std::filesystem::path (std::string (name.data (), N(name))));
  if (rel.empty () || !state.node_identities || !is_func (body, DOCUMENT)) {
    if (initialized) close (state.actor->id ());
    reset (); return;
  }
  if (incarnation != vault->incarnation || file != rel) reset ();
  if (!initialized) {
    // Containment is checked once at adoption, not on each keystroke. A path
    // lexically inside a vault may still be a symlink to an external source.
    std::error_code error;
    const auto physical= std::filesystem::weakly_canonical (
      std::filesystem::path (std::string (name.data (), N(name))), error);
    if (error || relative (vault, physical).empty ()) return;
    std::vector<AthenaArtifactRecord> baseline;
    (void) athena_artifact_radioactive_baseline (vault->incarnation, rel, baseline);
    previous_bindings.clear ();
    for (const auto& record: baseline)
      if (!record.source_uuid.empty ())
        previous_bindings[record.source_uuid + ":" + record.source_role]= record.uuid;
    incarnation= vault->incarnation; file= rel;
  }

  std::map<std::string, record_batch> next;
  std::vector<record_batch> batches;
  for (int i=0; i<N(body); ++i) {
    const std::string key= node::id (body[i]);
    auto cached= units.find (key);
    // Adjacent units participate in implicit proof association and neighbor
    // evidence. A root edit may shift all paths and conservatively refreshes.
    const bool changed= !initialized || dirty.empty () ||
      std::abs (dirty.front () - i) <= 1 || key.empty () || cached == units.end ();
    record_batch batch;
    if (changed) {
      std::vector<AthenaArtifactRecord> records;
      const int first= std::max (0, i-1), last= std::min (N(body), i+2);
      tree slice (DOCUMENT);
      for (int j=first; j<last; ++j) slice << body[j];
      std::string error;
      if (!athena_artifacts_extract_structure (slice, rel, records, error))
        throw std::runtime_error (error);
      for (auto& record: records) {
        if (record.source_path.empty ()) throw std::runtime_error ("Artifact has no source path");
        record.source_path[0] += first;
        auto old= previous_bindings.find (record.source_uuid + ":" + record.source_role);
        if (old != previous_bindings.end ()) record.uuid= old->second;
      }
      if (!state.read_only && !athena_artifacts_bind_structure (body, records, error))
        throw std::runtime_error (error);
      records.erase (std::remove_if (records.begin (), records.end (), [&] (const auto& record) {
        return record.source_path[0] != i || record.uuid.empty ();
      }), records.end ());
      for (const auto& record: records)
        previous_bindings[record.source_uuid + ":" + record.source_role]= record.uuid;
      batch= std::make_shared<const std::vector<AthenaArtifactRecord>> (std::move (records));
    }
    else batch= cached->second;
    batches.push_back (batch);
    next.emplace (key.empty () ? "index:" + std::to_string (i) : key, std::move (batch));
  }
  units= std::move (next); initialized= true;
  publish ({vault, rel, state.actor->id (), std::move (batches)});
}

void publish (publication value) {
  auto& q= pending ();
  std::lock_guard<std::mutex> guard (q.lock);
  q.closed.erase (value.owner);
  q.live[value.owner]= std::move (value);
}
void close (std::uint64_t owner) {
  auto& q= pending ();
  std::lock_guard<std::mutex> guard (q.lock);
  q.live.erase (owner); q.closed.insert (owner);
}
void saved (const std::filesystem::path& file, const std::string& sha256) {
  const auto vault= vault_capture_context ();
  if (sha256.empty () || relative (vault, file).empty ()) return;
  auto& q= pending ();
  std::lock_guard<std::mutex> guard (q.lock);
  q.saves[file.string ()]= {vault, file, sha256};
}
void publish_pending () {
  auto& q= pending ();
  std::map<std::uint64_t, publication> live;
  std::set<std::uint64_t> closed;
  {
    std::lock_guard<std::mutex> guard (q.lock);
    live.swap (q.live); closed.swap (q.closed);
  }
  for (auto owner: closed) athena_artifact_radioactive_remove_overlay (owner);
  closed.clear ();
  for (const auto& item: live) {
    const auto& value= item.second;
    if (vault_context_is_current (value.vault)) {
      std::vector<AthenaArtifactRecord> records;
      std::map<std::string, int> occurrences;
      for (std::size_t i=0; i<value.units.size (); ++i)
        for (auto record: *value.units[i]) {
          record.source_path[0]= (int) i;
          record.document_order= (int) records.size ();
          if (record.origin == "bold-text") record.keyword_occurrence= ++occurrences[record.keyword_tree];
          records.push_back (std::move (record));
        }
      athena_artifact_radioactive_overlay (value.vault->incarnation,
        value.relative_path, value.owner, records);
    }
  }
  // Publication can take longer than a concurrent close. Remove such owners
  // before returning, without making actor input wait for index construction.
  {
    std::lock_guard<std::mutex> guard (q.lock);
    closed.swap (q.closed);
  }
  for (auto owner: closed) athena_artifact_radioactive_remove_overlay (owner);
}
std::vector<saved_revision> take_saved () {
  auto& q= pending ();
  std::lock_guard<std::mutex> guard (q.lock);
  std::vector<saved_revision> result;
  for (auto& item: q.saves) result.push_back (std::move (item.second));
  q.saves.clear (); return result;
}
} // namespace athena::artifact
