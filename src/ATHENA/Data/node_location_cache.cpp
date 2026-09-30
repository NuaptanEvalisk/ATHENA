/******************************************************************************
* MODULE     : node_location_cache.cpp
* DESCRIPTION: Persistent LMDB UUID locator and continuous vault index worker
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "node_location_cache.hpp"
#include "background_workers.hpp"
#include "Data/Convert/Xml/athena_document_xml.hpp"
#include "System/Files/confined_filesystem.hpp"
#include "node_metadata.hpp"
#include "tm_ostream.hpp"

#include <lmdb++.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <set>
#include <stdexcept>
#include <thread>
#include <unordered_map>

namespace athena::node_location {
namespace fs= athena::filesystem;
namespace stdfs= std::filesystem;
namespace {

constexpr std::uint32_t schema_version= 1;
constexpr std::size_t hot_limit= 65536;
// LMDB map size is virtual address space, not preallocated resident memory.
// Keep it deliberately large so an interactive vault never stops for a map
// resize while the writer is publishing locations.
constexpr std::size_t map_size= std::size_t (16) << 30;
constexpr auto idle_delay= std::chrono::seconds (3);

void append_u32 (std::string& out, std::uint32_t n) {
  for (int i=0; i<4; ++i) out.push_back (char ((n >> (i*8)) & 0xff));
}
void append_u64 (std::string& out, std::uint64_t n) {
  for (int i=0; i<8; ++i) out.push_back (char ((n >> (i*8)) & 0xff));
}
void append_i64 (std::string& out, std::int64_t n) {
  append_u64 (out, static_cast<std::uint64_t> (n));
}
bool take_u32 (std::string_view& in, std::uint32_t& n) {
  if (in.size () < 4) return false;
  n= 0;
  for (int i=0; i<4; ++i) n|= std::uint32_t ((unsigned char) in[i]) << (i*8);
  in.remove_prefix (4); return true;
}
bool take_u64 (std::string_view& in, std::uint64_t& n) {
  if (in.size () < 8) return false;
  n= 0;
  for (int i=0; i<8; ++i) n|= std::uint64_t ((unsigned char) in[i]) << (i*8);
  in.remove_prefix (8); return true;
}
bool take_i64 (std::string_view& in, std::int64_t& n) {
  std::uint64_t raw= 0;
  if (!take_u64 (in, raw)) return false;
  n= static_cast<std::int64_t> (raw); return true;
}
void append_varint (std::string& out, std::uint64_t n) {
  while (n >= 0x80) { out.push_back (char ((n & 0x7f) | 0x80)); n >>= 7; }
  out.push_back (char (n));
}
bool take_varint (std::string_view& in, std::uint64_t& n) {
  n= 0; unsigned shift= 0;
  while (!in.empty () && shift < 64) {
    const unsigned char c= (unsigned char) in.front (); in.remove_prefix (1);
    n|= std::uint64_t (c & 0x7f) << shift;
    if (!(c & 0x80)) return true;
    shift+= 7;
  }
  return false;
}

std::string encode_revision (const fs::metadata& r, std::uint64_t nodes,
                             bool error= false) {
  std::string out;
  out.reserve (8*7 + 4*3 + 1);
  out.push_back (char (error ? 1 : 0));
  append_u64 (out, r.device); append_u64 (out, r.inode); append_u64 (out, r.size);
  append_i64 (out, r.modified.seconds); append_u32 (out, r.modified.nanoseconds);
  append_i64 (out, r.changed.seconds); append_u32 (out, r.changed.nanoseconds);
  append_u64 (out, nodes);
  return out;
}

bool decode_revision (std::string_view in, fs::metadata& r, std::uint64_t& nodes,
                      bool& error) {
  if (in.empty ()) return false;
  error= (unsigned char) in.front () != 0; in.remove_prefix (1);
  r= {}; r.directory= false;
  if (!take_u64 (in, r.device) || !take_u64 (in, r.inode) ||
      !take_u64 (in, r.size) || !take_i64 (in, r.modified.seconds) ||
      !take_u32 (in, r.modified.nanoseconds) ||
      !take_i64 (in, r.changed.seconds) ||
      !take_u32 (in, r.changed.nanoseconds) || !take_u64 (in, nodes)) return false;
  return in.empty ();
}

std::string encode_address (const address& where) {
  std::string out;
  append_varint (out, where.size ());
  for (const auto& step: where) {
    out.push_back (char (step.kind));
    if (step.kind == step_kind::child || step.kind == step_kind::list_item)
      append_varint (out, step.index);
    else if (step.kind == step_kind::property ||
             step.kind == step_kind::dictionary_item) {
      append_varint (out, step.key.size ()); out+= step.key;
    }
  }
  return out;
}

bool decode_address (std::string_view in, address& where) {
  std::uint64_t count= 0;
  if (!take_varint (in, count) || count > 256) return false;
  where.clear (); where.reserve (std::size_t (count));
  for (std::uint64_t i=0; i<count; ++i) {
    if (in.empty ()) return false;
    step next {static_cast<step_kind> ((unsigned char) in.front ()), 0, {}};
    in.remove_prefix (1);
    if (next.kind == step_kind::child || next.kind == step_kind::list_item) {
      std::uint64_t index= 0;
      if (!take_varint (in, index)) return false;
      next.index= std::size_t (index);
    }
    else if (next.kind == step_kind::property ||
             next.kind == step_kind::dictionary_item) {
      std::uint64_t length= 0;
      if (!take_varint (in, length) || length > in.size ()) return false;
      next.key.assign (in.data (), std::size_t (length)); in.remove_prefix (length);
    }
    else if (next.kind != step_kind::rich_text) return false;
    where.push_back (std::move (next));
  }
  return in.empty ();
}

bool uuid_binary (const std::string& id, std::string& out) {
  if (!node::valid_id (id)) return false;
  out.clear (); out.reserve (16);
  int high= -1;
  for (char c: id) {
    if (c == '-') continue;
    int n= c >= '0' && c <= '9' ? c-'0' : c-'a'+10;
    if (high < 0) high= n;
    else { out.push_back (char ((high << 4) | n)); high= -1; }
  }
  return out.size () == 16;
}

std::string node_value (const std::string& path, const fs::metadata& revision,
                        const address& where) {
  std::string out;
  append_varint (out, path.size ()); out+= path;
  auto encoded_revision= encode_revision (revision, 0, false);
  out+= encoded_revision;
  out+= encode_address (where);
  return out;
}

bool decode_node_value (std::string_view in, std::string& path,
                        fs::metadata& revision, address& where) {
  std::uint64_t length= 0;
  if (!take_varint (in, length) || length > in.size ()) return false;
  path.assign (in.data (), std::size_t (length)); in.remove_prefix (length);
  constexpr std::size_t revision_bytes= 1 + 8*3 + 8 + 4 + 8 + 4 + 8;
  if (in.size () < revision_bytes) return false;
  std::uint64_t ignored= 0; bool error= false;
  std::string_view revision_bytes_view= in.substr (0, revision_bytes);
  if (!decode_revision (revision_bytes_view, revision, ignored, error) || error) return false;
  in.remove_prefix (revision_bytes);
  return decode_address (in, where);
}

std::string file_node_value (const std::string& uuid, const address& where) {
  std::string key;
  if (!uuid_binary (uuid, key)) return {};
  return key + encode_address (where);
}

struct hot_entry {
  std::vector<location> locations;
};

class store {
public:
  explicit store (stdfs::path root):
    root_ (std::move (root)), env_ (lmdb::env::create ()) {
    stdfs::path directory= root_ / ".athena" / "node-locations";
    std::error_code ec;
    stdfs::create_directories (directory, ec);
    if (ec) throw std::runtime_error ("Cannot create UUID index directory: " + ec.message ());
    env_.set_max_dbs (8).set_max_readers (256).set_mapsize (map_size);
    // NOMETASYNC preserves database integrity while allowing the newest cache
    // transaction to be lost after a machine crash.  That is exactly the
    // durability/performance tradeoff wanted for a fully rebuildable index.
    env_.open (directory.string ().c_str (), MDB_NOTLS | MDB_NOMETASYNC, 0644);
    auto txn= lmdb::txn::begin (env_);
    meta_= lmdb::dbi::open (txn, "meta", MDB_CREATE);
    files_= lmdb::dbi::open (txn, "files", MDB_CREATE);
    nodes_= lmdb::dbi::open (txn, "nodes", MDB_CREATE | MDB_DUPSORT);
    file_nodes_= lmdb::dbi::open (txn, "file_nodes", MDB_CREATE | MDB_DUPSORT);
    std::string_view existing;
    const std::string schema_key= "schema";
    if (!meta_.get (txn, schema_key, existing)) {
      std::string schema; append_u32 (schema, schema_version);
      meta_.put (txn, schema_key, schema);
      meta_.put (txn, "bootstrap", std::string (1, '\0'));
    }
    else {
      std::string_view copy= existing; std::uint32_t version= 0;
      if (!take_u32 (copy, version) || !copy.empty () || version != schema_version) {
        nodes_.drop (txn); file_nodes_.drop (txn); files_.drop (txn); meta_.drop (txn);
        std::string schema; append_u32 (schema, schema_version);
        meta_.put (txn, "schema", schema);
        meta_.put (txn, "bootstrap", std::string (1, '\0'));
      }
    }
    txn.commit ();
    refresh_counters ();
  }

  persistent_lookup lookup (const std::vector<std::string>& ids) {
    persistent_lookup result;
    result.generation= generation_.load (std::memory_order_acquire);
    {
      std::lock_guard<std::mutex> guard (hot_lock_);
      for (const auto& id: ids) {
        std::string key;
        if (!uuid_binary (id, key)) continue;
        auto found= hot_.find (key);
        if (found != hot_.end ())
          result.locations[id]= found->second.locations;
      }
    }
    auto txn= lmdb::txn::begin (env_, nullptr, MDB_RDONLY);
    std::string_view bootstrap;
    result.bootstrap_complete= meta_.get (txn, "bootstrap", bootstrap) &&
      bootstrap.size () == 1 && bootstrap[0] != 0;
    result.errors= error_count_.load (std::memory_order_relaxed);
    auto cursor= lmdb::cursor::open (txn, nodes_);
    for (const auto& id: ids) {
      if (result.locations.count (id)) continue;
      std::string key;
      if (!uuid_binary (id, key)) continue;
      std::string_view k (key), value;
      std::vector<location> locations;
      if (cursor.get (k, value, MDB_SET)) {
        do {
          std::string path; fs::metadata revision; address where;
          if (decode_node_value (value, path, revision, where))
            locations.push_back ({std::move (path), std::move (where), 0, 0, revision});
          k= std::string_view (key); value= {};
        } while (cursor.get (k, value, MDB_NEXT_DUP));
      }
      // Cache negative LMDB lookups too.  During bootstrap a later writer
      // may still discover the UUID, so negative entries are only retained
      // after the first complete sweep.
      result.locations[id]= locations;
      if (!locations.empty () || result.bootstrap_complete)
        cache_hot (key, locations);
    }
    return result;
  }

  bool bootstrap_complete () {
    auto txn= lmdb::txn::begin (env_, nullptr, MDB_RDONLY);
    std::string_view value;
    return meta_.get (txn, "bootstrap", value) && value.size () == 1 && value[0] != 0;
  }

  struct file_record {
    bool found= false, error= false;
    fs::metadata revision {};
    std::uint64_t nodes= 0;
  };

  file_record file (const std::string& path) {
    auto txn= lmdb::txn::begin (env_, nullptr, MDB_RDONLY);
    std::string_view value;
    file_record result;
    result.found= files_.get (txn, path, value);
    if (result.found && !decode_revision (value, result.revision, result.nodes, result.error))
      result= {};
    return result;
  }

  std::unordered_map<std::string,file_record> file_records () {
    std::unordered_map<std::string,file_record> out;
    auto txn= lmdb::txn::begin (env_, nullptr, MDB_RDONLY);
    auto cursor= lmdb::cursor::open (txn, files_);
    std::string_view key, value;
    if (cursor.get (key, value, MDB_FIRST)) do {
      file_record record;
      record.found= true;
      if (!decode_revision (value, record.revision, record.nodes, record.error))
        record= {};
      out.emplace (std::string (key), record);
      key= {}; value= {};
    } while (cursor.get (key, value, MDB_NEXT));
    return out;
  }

  std::vector<std::string> indexed_files () {
    std::vector<std::string> out;
    auto txn= lmdb::txn::begin (env_, nullptr, MDB_RDONLY);
    auto cursor= lmdb::cursor::open (txn, files_);
    std::string_view key, value;
    if (cursor.get (key, value, MDB_FIRST))
      do { out.emplace_back (key); key= {}; value= {}; }
      while (cursor.get (key, value, MDB_NEXT));
    return out;
  }

  void replace_file (const std::string& path, const fs::metadata& revision,
                     const census& nodes, bool error= false) {
    const file_record previous= file (path);
    std::set<std::string> affected;
    auto txn= lmdb::txn::begin (env_);
    erase_file_rows (txn, path, &affected);
    files_.put (txn, path, encode_revision (revision, nodes.size (), error));
    if (!error) for (const auto& node: nodes) {
      std::string key;
      if (!uuid_binary (node.id, key)) continue;
      affected.insert (key);
      nodes_.put (txn, key, node_value (path, revision, node.where));
      file_nodes_.put (txn, path, file_node_value (node.id, node.where));
    }
    txn.commit ();
    if (!previous.found) file_count_.fetch_add (1, std::memory_order_relaxed);
    const auto old_nodes= previous.found ? previous.nodes : 0;
    if (nodes.size () >= old_nodes)
      node_count_.fetch_add (nodes.size () - old_nodes, std::memory_order_relaxed);
    else node_count_.fetch_sub (old_nodes - nodes.size (), std::memory_order_relaxed);
    if ((!previous.found || !previous.error) && error)
      error_count_.fetch_add (1, std::memory_order_relaxed);
    else if (previous.found && previous.error && !error)
      error_count_.fetch_sub (1, std::memory_order_relaxed);
    changed (affected);
  }

  void erase_file (const std::string& path) {
    const file_record previous= file (path);
    if (!previous.found) return;
    std::set<std::string> affected;
    auto txn= lmdb::txn::begin (env_);
    erase_file_rows (txn, path, &affected);
    files_.del (txn, path);
    txn.commit ();
    file_count_.fetch_sub (1, std::memory_order_relaxed);
    node_count_.fetch_sub (previous.nodes, std::memory_order_relaxed);
    if (previous.error) error_count_.fetch_sub (1, std::memory_order_relaxed);
    changed (affected);
  }

  void mark_bootstrap_complete () {
    auto txn= lmdb::txn::begin (env_);
    meta_.put (txn, "bootstrap", std::string (1, '\1'));
    txn.commit ();
    generation_.fetch_add (1, std::memory_order_acq_rel);
  }

  std::pair<std::size_t,std::size_t> totals () {
    return {file_count_.load (std::memory_order_relaxed),
            node_count_.load (std::memory_order_relaxed)};
  }

  std::size_t errors () {
    return error_count_.load (std::memory_order_relaxed);
  }

  std::uint64_t generation () const {
    return generation_.load (std::memory_order_acquire);
  }

private:
  void cache_hot (const std::string& key, const std::vector<location>& locations) {
    std::lock_guard<std::mutex> guard (hot_lock_);
    auto found= hot_.find (key);
    if (found != hot_.end ()) { found->second.locations= locations; return; }
    while (hot_.size () >= hot_limit && !hot_order_.empty ()) {
      const std::string oldest= std::move (hot_order_.front ());
      hot_order_.pop_front ();
      hot_.erase (oldest);
    }
    hot_.emplace (key, hot_entry {locations});
    hot_order_.push_back (key);
  }

  void refresh_counters () {
    auto txn= lmdb::txn::begin (env_, nullptr, MDB_RDONLY);
    file_count_.store (files_.size (txn), std::memory_order_relaxed);
    node_count_.store (nodes_.size (txn), std::memory_order_relaxed);
    error_count_.store (error_count (txn), std::memory_order_relaxed);
  }

  std::size_t error_count (MDB_txn* txn) {
    std::size_t errors= 0;
    auto cursor= lmdb::cursor::open (txn, files_);
    std::string_view key, value;
    if (cursor.get (key, value, MDB_FIRST)) do {
      fs::metadata revision; std::uint64_t nodes= 0; bool error= false;
      if (!decode_revision (value, revision, nodes, error) || error) ++errors;
      key= {}; value= {};
    } while (cursor.get (key, value, MDB_NEXT));
    return errors;
  }

  void erase_file_rows (MDB_txn* txn, const std::string& path,
                        std::set<std::string>* affected= nullptr) {
    std::string_view ignored;
    fs::metadata old_revision {}; std::uint64_t count= 0; bool old_error= false;
    if (files_.get (txn, path, ignored))
      (void) decode_revision (ignored, old_revision, count, old_error);
    auto cursor= lmdb::cursor::open (txn, file_nodes_);
    std::string_view key (path), value;
    if (cursor.get (key, value, MDB_SET)) {
      std::vector<std::pair<std::string,std::string>> remove;
      do {
        if (value.size () >= 16) {
          std::string uuid_key (value.substr (0, 16));
          if (affected) affected->insert (uuid_key);
          address where;
          if (decode_address (value.substr (16), where))
            remove.emplace_back (uuid_key, node_value (path, old_revision, where));
        }
        key= std::string_view (path); value= {};
      } while (cursor.get (key, value, MDB_NEXT_DUP));
      file_nodes_.del (txn, path);
      for (const auto& item: remove) nodes_.del (txn, item.first, item.second);
    }
  }

  void changed (const std::set<std::string>& affected) {
    generation_.fetch_add (1, std::memory_order_acq_rel);
    std::lock_guard<std::mutex> guard (hot_lock_);
    for (const auto& key: affected) hot_.erase (key);
  }

  stdfs::path root_;
  lmdb::env env_;
  lmdb::dbi meta_, files_, nodes_, file_nodes_;
  std::atomic<std::uint64_t> generation_ {1};
  std::atomic<std::size_t> file_count_ {0};
  std::atomic<std::size_t> node_count_ {0};
  std::atomic<std::size_t> error_count_ {0};
  std::mutex hot_lock_;
  std::unordered_map<std::string,hot_entry> hot_;
  std::deque<std::string> hot_order_;
};

struct manager_state {
  std::mutex lock;
  std::condition_variable wake;
  vault_context_handle vault;
  std::shared_ptr<store> index;
  std::thread worker;
  std::atomic<bool> stopping {false};
  bool wake_requested= false;
  persistent_status status;
  std::uint64_t epoch= 0;
  ~manager_state () {
    stopping.store (true, std::memory_order_release);
    wake.notify_all ();
    if (worker.joinable ()) worker.join ();
  }
};

manager_state manager;

void publish_status (persistent_phase phase, std::size_t current,
                     std::size_t total, const std::shared_ptr<store>& index,
                     std::size_t errors= std::size_t (-1)) {
  auto totals= index ? index->totals () : std::pair<std::size_t,std::size_t> {0,0};
  if (index && errors == std::size_t (-1)) errors= index->errors ();
  std::lock_guard<std::mutex> guard (manager.lock);
  manager.status.phase= phase;
  manager.status.current= current;
  manager.status.total= total;
  manager.status.files= totals.first;
  manager.status.nodes= totals.second;
  manager.status.errors= errors == std::size_t (-1) ? 0 : errors;
  manager.status.generation= index ? index->generation () : 0;
  background::phase state= background::phase::working;
  if (phase == persistent_phase::inactive) state= background::phase::inactive;
  else if (phase == persistent_phase::idle) state= background::phase::idle;
  else if (phase == persistent_phase::error || phase == persistent_phase::degraded)
    state= background::phase::error;
  background::publish (background::worker::uuid,
    {state, current, total, manager.status.errors,
     std::to_string (totals.first) + " files, " + std::to_string (totals.second) + " nodes"});
}

bool index_one (const vault_context_handle& vault, const std::shared_ptr<store>& index,
                const std::string& path, std::string& error) {
  error.clear ();
  try {
    fs::confined_root root (vault->root);
    auto entry= root.open (path);
    auto revision= entry.stat ();
    auto known= index->file (path);
    if (known.found && !known.error && fs::same_revision (known.revision, revision)) return true;
    const document::codec_limits limits;
    auto bytes= entry.read (limits.input_bytes);
    auto source= document::read_xml_v2 (bytes);
    auto nodes= collect (source);
    auto now= root.open (path);
    if (!entry.same_object (now) || !fs::same_revision (revision, now.stat ()))
      throw std::runtime_error ("source changed while indexing");
    index->replace_file (path, revision, nodes, false);
    return true;
  }
  catch (const std::exception& e) { error= e.what (); }
  catch (const string& e) { error.assign (e.data (), N(e)); }
  catch (...) { error= "unknown indexing failure"; }
  if (error == "source changed while indexing") return false;
  try {
    fs::confined_root root (vault->root);
    auto revision= root.open (path).stat ();
    index->replace_file (path, revision, {}, true);
  }
  catch (...) {}
  return false;
}

void worker_main (vault_context_handle vault, std::shared_ptr<store> index,
                  std::uint64_t epoch) {
  try {
    bool bootstrap= !index->bootstrap_complete ();
    for (;;) {
      {
        std::lock_guard<std::mutex> guard (manager.lock);
        if (manager.stopping.load (std::memory_order_acquire) ||
            epoch != manager.epoch ||
            !vault_context_is_current (vault)) return;
      }
      auto files= background::inventory (vault->root, &manager.stopping);
      if (bootstrap) {
        publish_status (persistent_phase::bootstrap, 0, files.size (), index);
        std::set<std::string> present;
        for (const auto& file: files) present.insert (file.path);
        for (std::size_t i=0; i<files.size (); ++i) {
          {
            std::lock_guard<std::mutex> guard (manager.lock);
            if (manager.stopping.load (std::memory_order_acquire) ||
                epoch != manager.epoch) return;
          }
          std::string error;
          (void) index_one (vault, index, files[i].path, error);
          publish_status (persistent_phase::bootstrap, i+1, files.size (), index);
        }
        for (const auto& stale: index->indexed_files ())
          if (!present.count (stale)) index->erase_file (stale);
        index->mark_bootstrap_complete ();
        bootstrap= false;
        publish_status (index->errors () ? persistent_phase::degraded :
                                         persistent_phase::idle, 0, 0, index);
      }
      else {
        publish_status (persistent_phase::sweep, 0, files.size (), index);
        std::vector<std::string> changed;
        std::set<std::string> present;
        const auto known_files= index->file_records ();
        for (std::size_t i=0; i<files.size (); ++i) {
          present.insert (files[i].path);
          const auto known= known_files.find (files[i].path);
          if (known == known_files.end () || !known->second.found ||
              !fs::same_revision (known->second.revision, files[i].revision))
            changed.push_back (files[i].path);
          publish_status (persistent_phase::sweep, i+1, files.size (), index);
        }
        for (const auto& pair: known_files)
          if (pair.second.found && !present.count (pair.first)) changed.push_back (pair.first);
        if (!changed.empty ()) {
          publish_status (persistent_phase::work, 0, changed.size (), index);
          for (std::size_t i=0; i<changed.size (); ++i) {
            if (!present.count (changed[i])) index->erase_file (changed[i]);
            else {
              std::string error;
              (void) index_one (vault, index, changed[i], error);
            }
            publish_status (persistent_phase::work, i+1, changed.size (), index);
          }
        }
        publish_status (index->errors () ? persistent_phase::degraded :
                                         persistent_phase::idle, 0, 0, index);
      }

      std::unique_lock<std::mutex> guard (manager.lock);
      manager.wake.wait_for (guard, idle_delay, [&] {
        return manager.stopping.load (std::memory_order_acquire) ||
               epoch != manager.epoch || manager.wake_requested;
      });
      manager.wake_requested= false;
      if (manager.stopping.load (std::memory_order_acquire) ||
          epoch != manager.epoch) return;
    }
  }
  catch (const std::exception& e) {
    athena_spdlog_error (std::string ("UUID index worker: ") + e.what ());
    publish_status (persistent_phase::error, 0, 0, index);
  }
}

} // namespace

void persistent_index_start (vault_context_handle vault) {
  persistent_index_stop ();
  if (!vault || vault->node_model_version < 1) return;
  std::shared_ptr<store> index;
  try { index= std::make_shared<store> (vault->root); }
  catch (const std::exception& e) {
    athena_spdlog_error (std::string ("UUID index open: ") + e.what ());
    std::lock_guard<std::mutex> guard (manager.lock);
    manager.status.phase= persistent_phase::error;
    background::publish (background::worker::uuid, {background::phase::error, 0, 0, 1, e.what ()});
    return;
  }
  std::uint64_t epoch;
  {
    std::lock_guard<std::mutex> guard (manager.lock);
    manager.vault= vault; manager.index= index;
    manager.stopping.store (false, std::memory_order_release);
    manager.wake_requested= false; epoch= ++manager.epoch;
    manager.status= {};
    manager.status.phase= index->bootstrap_complete () ? persistent_phase::sweep :
                                                        persistent_phase::bootstrap;
  }
  manager.worker= std::thread (worker_main, std::move (vault), std::move (index), epoch);
}

void persistent_index_stop () {
  std::thread worker;
  {
    std::lock_guard<std::mutex> guard (manager.lock);
    manager.stopping.store (true, std::memory_order_release);
    ++manager.epoch; manager.wake.notify_all ();
    worker= std::move (manager.worker);
  }
  if (worker.joinable ()) worker.join ();
  std::lock_guard<std::mutex> guard (manager.lock);
  manager.vault.reset (); manager.index.reset ();
  manager.stopping.store (false, std::memory_order_release);
  manager.status= {};
  background::publish (background::worker::uuid, {});
}

void persistent_index_wake () {
  std::lock_guard<std::mutex> guard (manager.lock);
  manager.wake_requested= true; manager.wake.notify_one ();
}

persistent_status persistent_index_status () {
  std::lock_guard<std::mutex> guard (manager.lock);
  auto result= manager.status;
  if (manager.index) result.generation= manager.index->generation ();
  return result;
}

std::uint64_t persistent_index_generation () {
  std::lock_guard<std::mutex> guard (manager.lock);
  return manager.index ? manager.index->generation () : 0;
}

persistent_lookup persistent_index_lookup (
    const stdfs::path& root, const std::vector<std::string>& ids) {
  std::shared_ptr<store> index;
  {
    std::lock_guard<std::mutex> guard (manager.lock);
    if (!manager.index || !manager.vault || manager.vault->root != root) return {};
    index= manager.index;
  }
  try { return index->lookup (ids); }
  catch (const std::exception& e) {
    athena_spdlog_warning (std::string ("UUID index lookup: ") + e.what ());
    return {};
  }
}

} // namespace athena::node_location
