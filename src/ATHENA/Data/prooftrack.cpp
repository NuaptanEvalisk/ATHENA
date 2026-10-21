#include "prooftrack.hpp"
#include "background_workers.hpp"
#include "Data/Convert/Xml/document_file_codec.hpp"
#include "node_metadata.hpp"
#include <lmdb++.h>
#include <nlohmann/json.hpp>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <set>
#include <thread>

namespace athena::prooftrack {
namespace {
using json= nlohmann::json;
namespace fs= athena::filesystem;
struct worker_state {
  std::atomic<bool> stopping {false};
  std::mutex mutex;
  std::condition_variable wake;
  std::thread thread;
  std::shared_ptr<const snapshot> published= std::make_shared<snapshot> ();
  ~worker_state () {
    stopping= true; wake.notify_all ();
    if (thread.joinable ()) thread.join ();
  }
};
worker_state& state () { static worker_state value; return value; }

json revision (const fs::metadata& m) {
  return json::array ({m.device, m.inode, m.size, m.modified.seconds,
    m.modified.nanoseconds, m.changed.seconds, m.changed.nanoseconds});
}

void collect (const tree& t, json& nodes) {
  if (is_atomic (t)) return;
  if (is_compound (t, "enunciation")) {
    const auto* meta= node::get (t);
    if (meta && !meta->id.empty ()) {
      const auto k= meta->properties.find ("kind");
      const auto* kind= k == meta->properties.end () ? nullptr :
        std::get_if<std::string> (&k->second.data);
      if (kind && (*kind == "proof" || *kind == "theorem" ||
          *kind == "proposition" || *kind == "lemma" || *kind == "corollary")) {
        std::string target;
        const auto p= meta->properties.find ("target");
        if (*kind == "proof" && p != meta->properties.end ())
          if (const auto* ref= std::get_if<node::reference> (&p->second.data))
            target= ref->id;
        nodes.push_back (json::array ({meta->id, target}));
      }
    }
  }
  for (int i=0; i<N(t); ++i) collect (t[i], nodes);
}

class store {
  lmdb::env env= lmdb::env::create ();
  lmdb::dbi files, targets;
public:
  std::map<std::string, json> records;
  explicit store (const std::filesystem::path& root) {
    const auto directory= root / ".athena" / "prooftrack";
    std::filesystem::create_directories (directory);
    env.set_max_dbs (3).set_mapsize (std::size_t (256) << 20);
    env.open (directory.string ().c_str (), MDB_NOTLS | MDB_NOMETASYNC, 0644);
    auto txn= lmdb::txn::begin (env);
    auto meta= lmdb::dbi::open (txn, "meta", MDB_CREATE);
    files= lmdb::dbi::open (txn, "files", MDB_CREATE);
    targets= lmdb::dbi::open (txn, "by_target", MDB_CREATE | MDB_DUPSORT);
    std::string_view version;
    if (!meta.get (txn, "schema", version) || version != "1") {
      files.drop (txn); targets.drop (txn);
      meta.put (txn, "schema", std::string_view ("1"));
    }
    auto cursor= lmdb::cursor::open (txn, files);
    std::string_view key, value;
    if (cursor.get (key, value, MDB_FIRST)) do {
      records.emplace (key, json::parse (value));
      key= {}; value= {};
    } while (cursor.get (key, value, MDB_NEXT));
    cursor.close ();
    txn.commit ();
  }
  void replace (const std::string& file, const json* record) {
    auto txn= lmdb::txn::begin (env);
    auto previous= records.find (file);
    if (previous != records.end ())
      for (const auto& n: previous->second.at ("nodes")) {
        const auto id= n[0].get<std::string> (), target= n[1].get<std::string> ();
        if (!target.empty ()) targets.del (txn, target, id);
      }
    if (record) {
      files.put (txn, file, record->dump ());
      for (const auto& n: record->at ("nodes")) {
        const auto id= n[0].get<std::string> (), target= n[1].get<std::string> ();
        if (!target.empty ()) targets.put (txn, target, id);
      }
    }
    else files.del (txn, file);
    txn.commit ();
    if (record) records[file]= *record;
    else records.erase (file);
  }
  void publish () {
    auto value= std::make_shared<snapshot> ();
    for (const auto& [file, record]: records)
      for (const auto& n: record.at ("nodes")) {
        const auto id= n[0].get<std::string> (), target= n[1].get<std::string> ();
        value->files[id]= file;
        if (!target.empty ()) value->by_target[target].push_back ({id, target, file});
      }
    std::atomic_store (&state ().published, std::shared_ptr<const snapshot> (value));
  }
};

void run (vault_context_handle vault) {
  auto& worker= state ();
  try {
    store cache (vault->root);
    cache.publish ();
    fs::confined_root root (vault->root);
    background::source_watch watch;
    std::uint64_t observed= 0;
    bool first= true, retry= false;
    while (!worker.stopping) {
      const auto current= watch.revision ();
      if (first || retry || current != observed) {
        observed= current; first= false; retry= false;
        auto inventory= background::inventory (vault->root, &worker.stopping, &watch);
        if (worker.stopping) break;
        std::set<std::string> present;
        bool changed= false;
        background::progress progress;
        progress.state= background::phase::working;
        progress.total= inventory.size ();
        for (const auto& file: inventory) {
          if (worker.stopping) return;
          present.insert (file.path);
          ++progress.current;
          auto old= cache.records.find (file.path);
          if (old != cache.records.end () && old->second["revision"] == revision (file.revision))
            continue;
          progress.detail= file.path;
          background::publish (background::worker::prooftrack, progress);
          try {
            auto input= root.open (file.path);
            auto before= input.stat ();
            document::codec_limits limits;
            tree doc= document::decode_native_xml_bytes (input.read (limits.input_bytes)).document;
            json nodes= json::array ();
            collect (doc, nodes);
            auto after= root.open (file.path);
            if (!input.same_object (after) || !fs::same_revision (before, after.stat ())) {
              retry= true; continue;
            }
            json record {{"revision", revision (before)}, {"nodes", std::move (nodes)}};
            cache.replace (file.path, &record); changed= true;
          }
          catch (const std::exception& e) {
            retry= true; ++progress.errors;
            progress.error_detail= file.path + ": " + e.what ();
          }
        }
        std::vector<std::string> deleted;
        for (const auto& [file, record]: cache.records)
          if (!present.count (file)) deleted.push_back (file);
        for (const auto& file: deleted) { cache.replace (file, nullptr); changed= true; }
        if (changed) cache.publish ();
        progress.state= background::phase::idle;
        progress.detail= "Proof associations up to date";
        background::publish (background::worker::prooftrack, progress);
      }
      std::unique_lock<std::mutex> lock (worker.mutex);
      worker.wake.wait_for (lock, std::chrono::seconds (2), [&] { return worker.stopping.load (); });
    }
  }
  catch (const std::exception& e) {
    background::progress progress;
    progress.state= background::phase::error;
    progress.detail= e.what ();
    background::publish (background::worker::prooftrack, progress);
  }
}
}

std::shared_ptr<const snapshot> read () { return std::atomic_load (&state ().published); }
void stop () {
  auto& worker= state ();
  worker.stopping= true; worker.wake.notify_all ();
  if (worker.thread.joinable ()) worker.thread.join ();
  std::atomic_store (&worker.published, std::shared_ptr<const snapshot> (std::make_shared<snapshot> ()));
  background::publish (background::worker::prooftrack, {});
}
void start (vault_context_handle vault) {
  stop ();
  state ().stopping= false;
  state ().thread= std::thread ([vault] { run (vault); });
}
}
