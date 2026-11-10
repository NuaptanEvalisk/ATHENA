// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#include "Document/State/auxiliary_cache.hpp"
#include <sqlite3.h>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace athena::derived {
namespace {
using records= std::map<std::string, std::shared_ptr<const cache_record>>;
struct work {
  std::uint64_t ticket= 0;
  cache_request request;
};

struct database_error: std::runtime_error {
  int code;
  explicit database_error (sqlite3* db):
    std::runtime_error (sqlite3_errmsg (db)), code (sqlite3_errcode (db)) {}
  bool disposable () const {
    return code == SQLITE_CORRUPT || code == SQLITE_NOTADB || code == SQLITE_ERROR;
  }
};

struct database {
  sqlite3* value= nullptr;
  database (const std::filesystem::path& file, bool write) {
    const int flags= write ? SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE : SQLITE_OPEN_READONLY;
    int code= sqlite3_open_v2 (file.string ().c_str (), &value, flags, nullptr);
    if (code != SQLITE_OK) {
      const database_error error (value);
      sqlite3_close (value); value= nullptr;
      throw error;
    }
    sqlite3_busy_timeout (value, 100);
  }
  ~database () { sqlite3_close (value); }
  database (const database&)= delete;
  void sql (const char* sql) {
    if (sqlite3_exec (value, sql, nullptr, nullptr, nullptr) != SQLITE_OK)
      throw database_error (value);
  }
};
struct statement {
  database& db;
  sqlite3_stmt* value= nullptr;
  statement (database& owner, const char* sql): db (owner) {
    if (sqlite3_prepare_v2 (db.value, sql, -1, &value, nullptr) != SQLITE_OK)
      throw database_error (db.value);
  }
  ~statement () { sqlite3_finalize (value); }
  statement (const statement&)= delete;
  void text (int at, const std::string& text) {
    if (sqlite3_bind_text64 (value, at, text.data (), text.size (),
                            SQLITE_TRANSIENT, SQLITE_UTF8) != SQLITE_OK)
      throw database_error (db.value);
  }
  void blob (int at, const cache_bytes& bytes) {
    const void* pointer= bytes.empty () ? static_cast<const void*> ("") : bytes.data ();
    if (sqlite3_bind_blob64 (value, at, pointer, bytes.size (), SQLITE_TRANSIENT) != SQLITE_OK)
      throw database_error (db.value);
  }
  bool row () {
    int code= sqlite3_step (value);
    if (code == SQLITE_ROW) return true;
    if (code == SQLITE_DONE) return false;
    throw database_error (db.value);
  }
  std::string text (int at) const {
    const auto* bytes= static_cast<const char*> (sqlite3_column_blob (value, at));
    return bytes ? std::string (bytes, sqlite3_column_bytes (value, at)) : std::string ();
  }
};

std::string format_of (database& db) {
  statement version (db, "SELECT format FROM metadata WHERE id=1");
  return version.row () ? version.text (0) : std::string ();
}

records read_records (const std::filesystem::path& file, const std::string& format) {
  records result;
  if (file.empty () || !std::filesystem::exists (file)) return result;
  database db (file, false);
  if (format_of (db) != format) return result;
  statement rows (db, "SELECT key,receipt,value FROM records");
  while (rows.row ()) {
    auto entry= std::make_shared<cache_record> ();
    entry->receipt= rows.text (1);
    const auto* bytes= static_cast<const std::uint8_t*> (sqlite3_column_blob (rows.value, 2));
    const int size= sqlite3_column_bytes (rows.value, 2);
    if (size) entry->bytes.assign (bytes, bytes + size);
    result.emplace (rows.text (0), std::move (entry));
  }
  return result;
}

void write_records (const std::filesystem::path& file, const std::string& format,
                    const records& changed) {
  std::filesystem::create_directories (file.parent_path ());
  database db (file, true);
  db.sql ("CREATE TABLE IF NOT EXISTS metadata(id INTEGER PRIMARY KEY,format TEXT NOT NULL);"
          "CREATE TABLE IF NOT EXISTS records(key TEXT PRIMARY KEY,receipt TEXT NOT NULL,"
          "value BLOB NOT NULL) WITHOUT ROWID;");
  db.sql ("BEGIN IMMEDIATE");
  try {
    if (format_of (db) != format) {
      db.sql ("DELETE FROM records;DELETE FROM metadata;");
      statement version (db, "INSERT INTO metadata VALUES(1,?)");
      version.text (1, format); version.row ();
    }
    for (const auto& [key, entry]: changed) {
      if (!entry) {
        statement remove (db, "DELETE FROM records WHERE key=?");
        remove.text (1, key); remove.row ();
      }
      else {
        statement put (db, "INSERT INTO records VALUES(?,?,?) ON CONFLICT(key) DO UPDATE "
                           "SET receipt=excluded.receipt,value=excluded.value");
        put.text (1, key); put.text (2, entry->receipt); put.blob (3, entry->bytes); put.row ();
      }
    }
    db.sql ("COMMIT");
  }
  catch (...) { sqlite3_exec (db.value, "ROLLBACK", nullptr, nullptr, nullptr); throw; }
}
}

struct auxiliary_cache::state {
  const std::filesystem::path source, file;
  const std::string format;
  const std::function<void()> notify;
  std::mutex mutex;
  std::condition_variable completed;
  records published;
  std::map<std::string, work> pending;
  std::map<std::string, std::uint64_t> latest;
  std::uint64_t submitted= 0, finished= 0, persisted= 0, flush_ticket= 0;
  bool loaded= false, queued= false, closed= false;
  // Only the worker touches dirty records and file recovery state.
  records dirty;
  bool reset_file= false;
  std::string error, storage_error;
  state (std::filesystem::path source, std::string format, std::function<void()> notify):
    source (std::move (source)),
    file (this->source.empty () ? std::filesystem::path () : auxiliary_cache::location (this->source)),
    format ("athena-auxiliary-1:" + format), notify (std::move (notify)) {}
};

namespace {
class cache_worker {
  using clock= std::chrono::steady_clock;
  using state_ptr= std::shared_ptr<auxiliary_cache::state>;
  std::mutex mutex;
  std::condition_variable wake;
  std::deque<state_ptr> queue;
  std::map<state_ptr, clock::time_point> writes;
  bool stopping= false;
  std::thread thread;

  static bool current (auxiliary_cache::state& state, const work& job) {
    std::lock_guard<std::mutex> guard (state.mutex);
    return !state.closed && state.latest[job.request.key] == job.ticket;
  }

  void run (const state_ptr& owner) {
    auto& state= *owner;
    bool load;
    std::map<std::string, work> pending;
    std::uint64_t through;
    {
      std::lock_guard<std::mutex> guard (state.mutex);
      state.queued= false;
      if (state.closed) return;
      load= !state.loaded;
      through= state.submitted;
      pending.swap (state.pending);
    }
    records changed;
    std::string error;
    if (load) {
      records loaded;
      try { loaded= read_records (state.file, state.format); }
      catch (const database_error& e) { error= e.what (); state.reset_file= e.disposable (); }
      catch (const std::exception& e) { error= e.what (); }
      {
        std::lock_guard<std::mutex> guard (state.mutex);
        state.published.swap (loaded);
        state.loaded= true;
      }
    }
    for (auto& [key, job]: pending) {
      if (!current (state, job)) continue;
      std::shared_ptr<const cache_record> result;
      if (job.request.compute) {
        {
          std::lock_guard<std::mutex> guard (state.mutex);
          const auto old= state.published.find (key);
          if (job.request.cacheable && job.request.reuse_existing && old != state.published.end () &&
              old->second->receipt == job.request.receipt)
            result= old->second;
        }
        if (result) continue;
        else {
          try {
            auto bytes= job.request.compute ();
            result= std::make_shared<const cache_record> (
              cache_record {job.request.receipt, std::move (bytes)});
          }
          catch (const std::exception& e) { error= e.what (); continue; }
        }
      }
      if (!job.request.cacheable) continue;
      if (!current (state, job)) continue;
      changed[key]= std::move (result);
    }
    records retired;
    bool notify, flush;
    {
      std::lock_guard<std::mutex> guard (state.mutex);
      for (auto& [key, value]: changed) {
        if (state.closed || state.latest[key] != pending.at (key).ticket) continue;
        auto old= state.published.find (key);
        if (old != state.published.end ()) retired.emplace (key, std::move (old->second));
        state.dirty[key]= value;
        if (value) state.published[key]= std::move (value);
        else state.published.erase (key);
      }
      state.error= std::move (error);
      state.finished= through;
      notify= !state.closed;
      flush= state.flush_ticket > state.persisted;
    }
    if (flush) writes[owner]= clock::now ();
    else if (load || !state.dirty.empty ())
      writes.try_emplace (owner, clock::now () + std::chrono::milliseconds (750));
    state.completed.notify_all ();
    if (notify && state.notify) {
      try { state.notify (); }
      catch (const std::exception& e) {
        std::lock_guard<std::mutex> guard (state.mutex);
        state.error= e.what ();
      }
    }
  }

  static void persist (auxiliary_cache::state& state) {
    std::string error;
    bool deleted= false;
    try {
      deleted= !state.source.empty () && !std::filesystem::exists (state.source);
      if (!state.source.empty () && !deleted && !state.dirty.empty ()) {
        if (state.reset_file) { std::filesystem::remove (state.file); state.reset_file= false; }
        write_records (state.file, state.format, state.dirty);
        deleted= !std::filesystem::exists (state.source);
      }
      if (deleted) std::filesystem::remove (state.file);
      state.dirty.clear ();
    }
    catch (const std::exception& e) { error= e.what (); }
    records retired;
    {
      std::lock_guard<std::mutex> guard (state.mutex);
      if (deleted) retired.swap (state.published);
      state.persisted= state.finished;
      state.storage_error= std::move (error);
    }
    state.completed.notify_all ();
  }
public:
  cache_worker (): thread ([this] {
    for (;;) {
      state_ptr state;
      bool flush= false;
      {
        std::unique_lock<std::mutex> guard (mutex);
        for (;;) {
          auto next= writes.end ();
          for (auto it= writes.begin (); it != writes.end (); ++it)
            if (next == writes.end () || it->second < next->second) next= it;
          if (next != writes.end () && (stopping || next->second <= clock::now ())) {
            state= next->first; writes.erase (next); flush= true; break;
          }
          if (stopping) return;
          if (!queue.empty ()) { state= std::move (queue.front ()); queue.pop_front (); break; }
          if (next == writes.end ()) wake.wait (guard);
          else wake.wait_until (guard, next->second);
        }
      }
      if (flush) persist (*state);
      else run (state);
    }
  }) {}
  ~cache_worker () {
    { std::lock_guard<std::mutex> guard (mutex); stopping= true; }
    wake.notify_all (); thread.join ();
  }
  void enqueue (std::shared_ptr<auxiliary_cache::state> state) {
    { std::lock_guard<std::mutex> guard (mutex); queue.push_back (std::move (state)); }
    wake.notify_one ();
  }
};
cache_worker& worker () { static cache_worker instance; return instance; }
}

std::filesystem::path auxiliary_cache::location (const std::filesystem::path& source) {
  return source.parent_path () / ".cache" / (source.filename ().string () + ".ac");
}

auxiliary_cache::auxiliary_cache (std::filesystem::path source, std::string format,
                                std::function<void()> ready):
  data (std::make_shared<state> (std::move (source), std::move (format), std::move (ready))) {
  data->queued= true;
  worker ().enqueue (data);
}
auxiliary_cache::~auxiliary_cache () { close (); }

std::shared_ptr<const cache_record>
auxiliary_cache::lookup (const std::string& key, const std::string& receipt) const {
  std::lock_guard<std::mutex> guard (data->mutex);
  const auto found= data->published.find (key);
  return found != data->published.end () && found->second->receipt == receipt ?
    found->second : nullptr;
}
std::uint64_t auxiliary_cache::request (cache_request request) {
  work displaced;
  std::uint64_t ticket;
  bool enqueue;
  {
    std::lock_guard<std::mutex> guard (data->mutex);
    if (data->closed) throw std::logic_error ("Closed auxiliary cache");
    ticket= ++data->submitted;
    data->latest[request.key]= ticket;
    auto& slot= data->pending[request.key];
    std::swap (slot, displaced);
    slot= {ticket, std::move (request)};
    enqueue= !data->queued; data->queued= true;
  }
  if (enqueue) worker ().enqueue (data);
  return ticket;
}
std::uint64_t auxiliary_cache::erase (std::string key) {
  return request ({std::move (key), {}, {}});
}
std::uint64_t auxiliary_cache::post (std::string key, std::function<void()> action) {
  return request ({std::move (key), {}, [action= std::move (action)] {
    action (); return cache_bytes {};
  }, false});
}
bool auxiliary_cache::loaded () const {
  std::lock_guard<std::mutex> guard (data->mutex); return data->loaded;
}
std::string auxiliary_cache::error () const {
  std::lock_guard<std::mutex> guard (data->mutex);
  return data->storage_error.empty () ? data->error : data->storage_error;
}
void auxiliary_cache::wait (std::uint64_t ticket) {
  std::unique_lock<std::mutex> guard (data->mutex);
  data->completed.wait (guard, [&] {
    return data->closed || (data->loaded && data->finished >= ticket);
  });
}
void auxiliary_cache::flush () {
  bool enqueue;
  std::uint64_t ticket;
  {
    std::lock_guard<std::mutex> guard (data->mutex);
    if (data->closed) throw std::logic_error ("Closed auxiliary cache");
    ticket= data->flush_ticket= ++data->submitted;
    enqueue= !data->queued; data->queued= true;
  }
  if (enqueue) worker ().enqueue (data);
  std::unique_lock<std::mutex> guard (data->mutex);
  data->completed.wait (guard, [&] { return data->closed || data->persisted >= ticket; });
  if (!data->storage_error.empty ()) throw std::runtime_error (data->storage_error);
}
void auxiliary_cache::close () {
  std::map<std::string, work> retired;
  { std::lock_guard<std::mutex> guard (data->mutex);
    data->closed= true; retired.swap (data->pending); }
  data->completed.notify_all ();
}
}
