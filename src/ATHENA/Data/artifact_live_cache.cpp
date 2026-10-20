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
struct source_delta {
  std::vector<int> where;
  std::unique_ptr<tree> value;
  std::vector<std::unique_ptr<tree>> headers;
};
struct source_request {
  vault_context_handle vault;
  std::string file;
  std::uint64_t owner= 0, revision= 0;
  std::shared_ptr<live_generation> generation;
  document::xml_format_version format= document::xml_format_version::v3;
  std::vector<source_delta> changes;
};
struct source_mirror {
  tree body;
  std::shared_ptr<live_generation> generation;
  std::map<std::string, record_batch> units;
  std::set<int> dirty;
  bool all= true;
};
struct queues {
  std::mutex lock;
  std::map<std::uint64_t, publication> live;
  std::map<std::uint64_t, source_request> sources;
  std::map<std::uint64_t, std::weak_ptr<live_generation>> generations;
  std::set<std::uint64_t> closed;
  std::map<std::string, saved_revision> saves;
};
queues& pending () { static auto* value= new queues; return *value; }

void enqueue (source_request request) {
  auto& q= pending ();
  std::lock_guard<std::mutex> guard (q.lock);
  q.closed.erase (request.owner);
  q.generations[request.owner]= request.generation;
  auto& target= q.sources[request.owner];
  if (target.generation != request.generation ||
      request.changes.front ().where.empty ()) {
    // Retired snapshots are destroyed with the parameter after unlocking.
    std::swap (target, request);
    return;
  }
  target.revision= request.revision;
  for (auto& change: request.changes) {
    if (!target.changes.empty () && target.changes.back ().where == change.where)
      std::swap (target.changes.back (), change);
    else target.changes.push_back (std::move (change));
  }
}

publication extract_pending (source_mirror& mirror, const source_request& request) {
  std::map<std::string, record_batch> next;
  std::vector<record_batch> batches;
  const tree& body= mirror.body;
  for (int i=0; i<N(body); ++i) {
    if (request.generation->cancelled.load () ||
        request.generation->revision.load () != request.revision)
      return {};
    const auto id= node::id (body[i]);
    const auto key= id.empty () ? "index:" + std::to_string (i) : id;
    auto cached= mirror.units.find (key);
    record_batch batch;
    if (mirror.all || mirror.dirty.count (i) || cached == mirror.units.end ()) {
      tree slice (DOCUMENT);
      const int first= std::max (0, i-1), last= std::min (N(body), i+2);
      for (int j=first; j<last; ++j) slice << body[j];
      std::vector<AthenaArtifactRecord> records;
      std::string error;
      if (!athena_artifacts_extract_structure (
            slice, request.file, records, error, request.format))
        throw std::runtime_error (error);
      records.erase (std::remove_if (records.begin (), records.end (), [&] (auto& record) {
        if (record.source_path.empty ()) throw std::runtime_error ("Artifact has no source path");
        record.source_path[0] += first;
        return record.source_path[0] != i || record.uuid.empty ();
      }), records.end ());
      batch= std::make_shared<const std::vector<AthenaArtifactRecord>> (std::move (records));
    }
    else batch= cached->second;
    batches.push_back (batch);
    next.emplace (key, std::move (batch));
  }
  mirror.units= std::move (next);
  mirror.dirty.clear (); mirror.all= false;
  return {request.vault, request.file, request.owner, std::move (batches)};
}

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
  return initialized && generation && !generation->cancelled.load () &&
    incarnation == vault->incarnation && file == rel;
}

void live_cache::update (buffer_document_state& state, tree& body,
                         const std::vector<int>& dirty) {
  const auto vault= vault_capture_context ();
  const string name= as_string (state.name, URL_SYSTEM);
  if (!state.node_identities) {
    if (initialized) close (state.actor->id ());
    reset (); return;
  }
  update (vault, std::filesystem::path (std::string (name.data (), N(name))),
    state.actor->id (), state.storage_version == document::xml_storage_version::v3
      ? document::xml_format_version::v3 : document::xml_format_version::v1,
    state.read_only, body, dirty);
}

void live_cache::update (const vault_context_handle& vault,
                         const std::filesystem::path& absolute,
                         std::uint64_t owner, document::xml_format_version format,
                         bool read_only, tree& body, const std::vector<int>& dirty) {
  const auto rel= relative (vault, absolute);
  if (rel.empty () || !is_func (body, DOCUMENT)) {
    if (initialized) close (owner);
    reset (); return;
  }
  if (incarnation != vault->incarnation || file != rel ||
      (generation && generation->cancelled.load ())) reset ();
  if (!initialized) {
    // Containment is checked once at adoption, not on each keystroke. A path
    // lexically inside a vault may still be a symlink to an external source.
    std::error_code error;
    const auto physical= std::filesystem::weakly_canonical (absolute, error);
    if (error || relative (vault, physical).empty ()) return;
    std::vector<AthenaArtifactRecord> baseline;
    (void) athena_artifact_radioactive_baseline (vault->incarnation, rel, baseline);
    previous_bindings.clear ();
    for (const auto& record: baseline)
      if (!record.source_uuid.empty ())
        previous_bindings[record.source_uuid + ":" + record.source_role]= record.uuid;
    incarnation= vault->incarnation; file= rel;
    generation= std::make_shared<live_generation> ();
  }
  std::string error;
  const auto scope= initialized ? dirty : std::vector<int> {};
  if (!read_only &&
      !athena_artifacts_ensure_bindings (body, scope, previous_bindings, error))
    throw std::runtime_error (error);

  source_delta change;
  const tree* source= &body;
  for (int index: scope) {
    if (!is_compound (*source) || index < 0 || index >= N(*source)) break;
    tree header ("");
    node::copy_metadata (*source, header);
    change.headers.push_back (std::make_unique<tree> (std::move (header)));
    change.where.push_back (index);
    source= &(*source)[index];
  }
  change.value= std::make_unique<tree> (copy (*source));
  source_request request;
  request.vault= vault; request.file= rel; request.owner= owner;
  request.generation= generation;
  request.revision= generation->revision.fetch_add (1) + 1;
  request.format= format;
  request.changes.push_back (std::move (change));
  enqueue (std::move (request));
  initialized= true;
}

void publish (publication value) {
  auto& q= pending ();
  std::lock_guard<std::mutex> guard (q.lock);
  q.closed.erase (value.owner);
  q.live[value.owner]= std::move (value);
}
void close (std::uint64_t owner) {
  auto& q= pending ();
  decltype(q.sources)::node_type retired_source;
  decltype(q.live)::node_type retired_publication;
  std::lock_guard<std::mutex> guard (q.lock);
  retired_publication= q.live.extract (owner); q.closed.insert (owner);
  auto generation= q.generations.find (owner);
  if (generation != q.generations.end ()) {
    if (auto token= generation->second.lock ()) token->cancelled.store (true);
    q.generations.erase (generation);
  }
  retired_source= q.sources.extract (owner);
  if (!retired_source.empty ())
    retired_source.mapped ().generation->cancelled.store (true);
}
void saved (const std::filesystem::path& file, const std::string& sha256) {
  const auto vault= vault_capture_context ();
  if (sha256.empty () || relative (vault, file).empty ()) return;
  auto& q= pending ();
  std::lock_guard<std::mutex> guard (q.lock);
  q.saves[file.string ()]= {vault, file, sha256};
}
void publish_pending () {
  // Called only by the matching worker. These trees never return to an actor.
  static auto* storage= new std::map<std::uint64_t, source_mirror>;
  auto& mirrors= *storage;
  auto& q= pending ();
  std::map<std::uint64_t, publication> live;
  std::map<std::uint64_t, source_request> sources;
  std::set<std::uint64_t> closed;
  {
    std::lock_guard<std::mutex> guard (q.lock);
    live.swap (q.live); closed.swap (q.closed); sources.swap (q.sources);
  }
  for (auto owner: closed) {
    mirrors.erase (owner);
    athena_artifact_radioactive_remove_overlay (owner);
  }
  closed.clear ();
  std::string failure;
  for (auto& [owner, request]: sources) {
    try {
      if (request.generation->cancelled.load () ||
          !vault_context_is_current (request.vault)) {
        mirrors.erase (owner); continue;
      }
      auto& mirror= mirrors[owner];
      if (mirror.generation != request.generation) {
        mirror= source_mirror {};
        mirror.generation= request.generation;
      }
      for (auto& change: request.changes) {
        tree* target= &mirror.body;
        for (std::size_t i=0; i<change.where.size (); ++i) {
          const int index= change.where[i];
          if (!is_compound (*target) || index < 0 || index >= N(*target))
            throw std::runtime_error ("Artifact mirror source path diverged");
          node::copy_metadata (*change.headers[i], *target);
          target= &(*target)[index];
        }
        *target= std::move (*change.value);
        if (change.where.empty ()) mirror.all= true;
        else for (int offset=-1; offset<=1; ++offset)
          mirror.dirty.insert (change.where.front () + offset);
      }
      if (request.generation->revision.load () != request.revision) continue;
      auto result= extract_pending (mirror, request);
      if (!request.generation->cancelled.load () &&
          request.generation->revision.load () == request.revision)
        live[owner]= std::move (result);
    }
    catch (const std::exception& error) {
      request.generation->cancelled.store (true);
      mirrors.erase (owner);
      athena_artifact_radioactive_remove_overlay (owner);
      failure= request.file + ": " + error.what ();
    }
    catch (...) {
      request.generation->cancelled.store (true);
      mirrors.erase (owner);
      athena_artifact_radioactive_remove_overlay (owner);
      failure= request.file + ": artifact extraction failed";
    }
  }
  for (const auto& item: live) {
    const auto& value= item.second;
    auto source= sources.find (item.first);
    if (source != sources.end () &&
        (source->second.generation->cancelled.load () ||
         source->second.generation->revision.load () != source->second.revision))
      continue;
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
      std::function<bool()> current;
      if (source != sources.end ()) {
        const auto token= source->second.generation;
        const auto revision= source->second.revision;
        current= [token, revision] {
          return !token->cancelled.load () && token->revision.load () == revision;
        };
      }
      athena_artifact_radioactive_overlay (value.vault->incarnation,
        value.relative_path, value.owner, records, current);
    }
  }
  // Publication can take longer than a concurrent close. Remove such owners
  // before returning, without making actor input wait for index construction.
  {
    std::lock_guard<std::mutex> guard (q.lock);
    closed.swap (q.closed);
  }
  for (auto owner: closed) {
    mirrors.erase (owner);
    athena_artifact_radioactive_remove_overlay (owner);
  }
  if (!failure.empty ()) throw std::runtime_error (failure);
}
std::vector<saved_revision> take_saved () {
  auto& q= pending ();
  std::lock_guard<std::mutex> guard (q.lock);
  std::vector<saved_revision> result;
  for (auto& item: q.saves) result.push_back (std::move (item.second));
  q.saves.clear (); return result;
}
} // namespace athena::artifact
