/******************************************************************************
* MODULE     : persistent_shaping_cache.cpp
* DESCRIPTION: Asynchronously maintained, disposable LMDB shaping cache
* COPYRIGHT  : (C) 2026 ATHENA contributors
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
******************************************************************************/
#include "Rendering/Fonts/persistent_shaping_cache.hpp"
#include "Rendering/Fonts/font_domain.hpp"

#include <lmdb++.h>
#include <QCryptographicHash>
#include <QByteArrayView>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <limits>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace athena::text {
namespace {
namespace fs= std::filesystem;

constexpr std::uint32_t schema= 1;
constexpr std::size_t body_limit= std::size_t (700) << 20;
constexpr std::size_t math_limit= std::size_t (256) << 20;
constexpr std::size_t queue_limit= std::size_t (64) << 20;
constexpr std::size_t hit_slots= 65536;

void u8 (std::string& out, std::uint8_t value) { out.push_back (char (value)); }
void u32 (std::string& out, std::uint32_t value) {
  for (int i=0; i<4; ++i) u8 (out, (value >> (8*i)) & 0xff);
}
void u64 (std::string& out, std::uint64_t value) {
  for (int i=0; i<8; ++i) u8 (out, (value >> (8*i)) & 0xff);
}
void i64 (std::string& out, std::int64_t value) { u64 (out, std::uint64_t (value)); }
void bytes (std::string& out, std::string_view value) {
  u64 (out, value.size ());
  out.append (value.data (), value.size ());
}
bool get_u8 (std::string_view& in, std::uint8_t& out) {
  if (in.empty ()) return false;
  out= (unsigned char) in.front ();
  in.remove_prefix (1);
  return true;
}
bool get_u32 (std::string_view& in, std::uint32_t& out) {
  if (in.size () < 4) return false;
  out= 0;
  for (int i=0; i<4; ++i) out|= std::uint32_t ((unsigned char) in[i]) << (8*i);
  in.remove_prefix (4);
  return true;
}
bool get_u64 (std::string_view& in, std::uint64_t& out) {
  if (in.size () < 8) return false;
  out= 0;
  for (int i=0; i<8; ++i) out|= std::uint64_t ((unsigned char) in[i]) << (8*i);
  in.remove_prefix (8);
  return true;
}
bool get_i64 (std::string_view& in, std::int64_t& out) {
  std::uint64_t value;
  if (!get_u64 (in, value)) return false;
  out= std::int64_t (value);
  return true;
}
bool get_si (std::string_view& in, SI& out) {
  std::int64_t value;
  if (!get_i64 (in, value) ||
      value < std::numeric_limits<SI>::min () ||
      value > std::numeric_limits<SI>::max ()) return false;
  out= SI (value);
  return true;
}

std::string encode (const shaped_text& run) {
  std::string data;
  data.reserve (72 + run.glyphs.size ()*45 + run.carets.size ()*12);
  u32 (data, schema);
  u32 (data, std::uint32_t (run.byte_begin));
  u32 (data, std::uint32_t (run.byte_end));
  u8 (data, run.direction == run_direction::right_to_left);
  i64 (data, run.advance_x); i64 (data, run.advance_y);
  for (SI value: {run.ink_x1, run.ink_y1, run.ink_x2, run.ink_y2})
    i64 (data, value);
  u8 (data, run.has_ink); u8 (data, run.missing_glyphs);
  u8 (data, bool (run.math));
  if (run.math) {
    i64 (data, run.math->italic_correction);
    i64 (data, run.math->top_accent_attachment);
    u32 (data, run.math->glyph_index);
  }
  u32 (data, std::uint32_t (run.glyphs.size ()));
  for (const auto& glyph: run.glyphs) {
    u32 (data, glyph.index); u32 (data, std::uint32_t (glyph.byte));
    for (SI value: {glyph.x, glyph.y, glyph.advance_x, glyph.advance_y})
      i64 (data, value);
    u8 (data, glyph.unsafe_to_break);
  }
  u32 (data, std::uint32_t (run.carets.size ()));
  for (const auto& caret: run.carets) {
    u32 (data, std::uint32_t (caret.byte)); i64 (data, caret.x);
  }
  return data;
}

std::optional<cached_shaping_run> decode (
    std::string_view in, const shaping_options& options) {
  cached_shaping_run result;
  std::uint32_t version= 0, begin= 0, end= 0, count= 0;
  std::uint8_t bit= 0;
  if (!get_u32 (in, version) || version != schema ||
      !get_u32 (in, begin) || !get_u32 (in, end) || begin > end ||
      !get_u8 (in, bit) || bit > 1) return {};
  result.byte_begin= begin; result.byte_end= end;
  result.direction= bit ? run_direction::right_to_left : run_direction::left_to_right;
  if (!get_si (in, result.advance_x) || !get_si (in, result.advance_y) ||
      !get_si (in, result.ink_x1) || !get_si (in, result.ink_y1) ||
      !get_si (in, result.ink_x2) || !get_si (in, result.ink_y2) ||
      !get_u8 (in, bit) || bit > 1) return {};
  result.has_ink= bit;
  if (!get_u8 (in, bit) || bit > 1) return {};
  result.missing_glyphs= bit;
  if (!get_u8 (in, bit) || bit > 1) return {};
  result.has_math= bit;
  if (result.has_math) {
    if (!get_si (in, result.italic_correction) ||
        !get_si (in, result.top_accent_attachment) ||
        !get_u32 (in, result.math_glyph)) return {};
  }
  if (!get_u32 (in, count) || count > options.max_glyphs ||
      count > 4096 || in.size () < count * 41ULL) return {};
  result.glyphs.reserve (count);
  for (std::uint32_t i=0; i<count; ++i) {
    positioned_glyph glyph {};
    std::uint32_t byte= 0;
    if (!get_u32 (in, glyph.index) || !get_u32 (in, byte) ||
        byte < begin || byte >= end ||
        !get_si (in, glyph.x) || !get_si (in, glyph.y) ||
        !get_si (in, glyph.advance_x) || !get_si (in, glyph.advance_y) ||
        !get_u8 (in, bit) || bit > 1) return {};
    glyph.byte= byte;
    glyph.unsafe_to_break= bit;
    result.glyphs.push_back (glyph);
  }
  if (!get_u32 (in, count) || count > options.max_carets ||
      count > 4097 || in.size () != count * 12ULL) return {};
  result.carets.reserve (count);
  for (std::uint32_t i=0; i<count; ++i) {
    std::uint32_t byte= 0;
    SI x= 0;
    if (!get_u32 (in, byte) || byte < begin || byte > end ||
        !get_si (in, x) || (!result.carets.empty () &&
        byte <= result.carets.back ().byte)) return {};
    result.carets.push_back ({byte, x});
  }
  return result;
}

std::uint64_t fingerprint (std::string_view digest) {
  std::uint64_t result= 0;
  for (int i=0; i<8; ++i)
    result|= std::uint64_t ((unsigned char) digest[i]) << (8*i);
  return result & ((std::uint64_t (1) << 63) - 1);
}
std::uint32_t current_epoch () {
  using namespace std::chrono;
  return std::uint32_t (
    duration_cast<minutes> (system_clock::now ().time_since_epoch ()).count () / 30);
}
struct usage_record { std::uint32_t score= 0, epoch= 0; };
usage_record usage (std::string_view value) {
  usage_record result;
  if (!get_u32 (value, result.score) || !get_u32 (value, result.epoch) ||
      !value.empty ()) return {};
  return result;
}
std::string encode_usage (usage_record record) {
  std::string result;
  u32 (result, record.score); u32 (result, record.epoch);
  return result;
}
std::uint32_t decayed (usage_record record, std::uint32_t epoch) {
  return epoch <= record.epoch ? record.score :
    (record.score >> std::min<std::uint32_t> (epoch-record.epoch, 31));
}

struct cache_database {
  lmdb::env env= lmdb::env::create ();
  lmdb::dbi entries, popularity, meta;
  std::size_t limit;
  explicit cache_database (const fs::path& dir, std::size_t maximum): limit (maximum) {
    fs::create_directories (dir);
    // Map size is the actual on-disk ceiling, including index, free pages and
    // copy-on-write overhead; reserve transaction headroom below that ceiling.
    env.set_max_dbs (4).set_max_readers (512).set_mapsize (limit);
    env.open (dir.string ().c_str (), MDB_NOTLS | MDB_NOMETASYNC, 0600);
    auto txn= lmdb::txn::begin (env);
    entries= lmdb::dbi::open (txn, "runs", MDB_CREATE);
    popularity= lmdb::dbi::open (txn, "usage", MDB_CREATE);
    meta= lmdb::dbi::open (txn, "meta", MDB_CREATE);
    std::string_view previous;
    const std::string expected= encode_usage ({schema, 0});
    if (!meta.get (txn, "schema", previous) || previous != expected) {
      entries.drop (txn); popularity.drop (txn); meta.drop (txn);
      meta.put (txn, "schema", expected);
    }
    txn.commit ();
  }
  std::size_t active_bytes (MDB_txn* transaction) {
    const auto a= entries.stat (transaction), b= popularity.stat (transaction),
               c= meta.stat (transaction);
    const auto pages= [&] (const MDB_stat& v) {
      return std::size_t (v.ms_branch_pages + v.ms_leaf_pages + v.ms_overflow_pages);
    };
    return (pages (a) + pages (b) + pages (c) + 2) * a.ms_psize;
  }
};

struct cache_pair {
  cache_database body, math;
  explicit cache_pair (const fs::path& root):
    body (root / "body", body_limit), math (root / "math", math_limit) {}
  cache_database& select (bool math_run) { return math_run ? math : body; }
};

struct queued_run {
  shaping_cache_key key;
  std::string data;
  unsigned frequency= 1;
};

class shaping_service {
  std::shared_ptr<cache_pair> mapped;
  std::mutex mutex;
  std::condition_variable changed;
  // A single batch contains each complete request at most once. Repeated
  // corpus shaping increases its initial frequency instead of writing copies.
  std::unordered_map<std::string, queued_run> pending;
  std::size_t pending_bytes= 0;
  bool stopping= false, writing= false, failed= false;
  struct hit_slot {
    std::atomic<std::uint64_t> key {0};
    std::atomic<std::uint64_t> sequence {0};
  };
  std::array<hit_slot, hit_slots> hits;
  std::atomic<std::uint64_t> produced {0};
  std::uint64_t consumed= 0;
  // Start after all state accessed by run() has been constructed.
  std::thread writer;

  static fs::path location () {
    const char* configured= std::getenv ("ATHENA_HOME_PATH");
    if (configured && *configured)
      return fs::path (configured) / "system/cache/native-shaping-v1";
    const char* home= std::getenv ("HOME");
    if (!home || !*home) return {};
    return fs::path (home) / ".ATHENA/system/cache/native-shaping-v1";
  }

  void flush_hits (cache_pair& storage) {
    const auto end= produced.load (std::memory_order_acquire);
    if (end == consumed) return;
    const auto begin= end > consumed + hit_slots ? end-hit_slots : consumed;
    std::unordered_map<std::uint64_t, std::uint32_t> body, math;
    for (auto i=begin; i<end; ++i) {
      auto& slot= hits[i % hit_slots];
      const auto sequence= slot.sequence.load (std::memory_order_acquire);
      if (sequence != i+1) continue;
      const auto id= slot.key.load (std::memory_order_relaxed);
      // A foreground writer can reuse the ring slot while this batch is
      // being copied. Ignore a replaced record rather than charging an
      // unrelated font run for the old ticket.
      if (slot.sequence.load (std::memory_order_acquire) != sequence) continue;
      const bool math_run= (id & 1) != 0;
      auto& counts= math_run ? math : body;
      ++counts[id >> 1];
    }
    consumed= end;
    const auto update= [&] (cache_database& db,
                           const std::unordered_map<std::uint64_t,std::uint32_t>& counts) {
      if (counts.empty ()) return;
      auto txn= lmdb::txn::begin (db.env);
      const auto epoch= current_epoch ();
      for (const auto& [id,count]: counts) {
        std::string key; u64 (key, id);
        std::string_view previous;
        auto record= db.popularity.get (txn, key, previous) ?
          usage (previous) : usage_record {};
        record.score= std::min<std::uint64_t> (
          std::uint64_t (decayed (record, epoch)) + count, 0x7fffffff);
        record.epoch= epoch;
        db.popularity.put (txn, key, encode_usage (record));
      }
      txn.commit ();
    };
    update (storage.body, body);
    update (storage.math, math);
  }

  void evict (cache_database& db) {
    auto txn= lmdb::txn::begin (db.env);
    const auto used= db.active_bytes (txn);
    if (used < db.limit * 3 / 4) return;
    struct candidate { std::string key; std::uint32_t score, epoch; };
    std::vector<candidate> all;
    const auto epoch= current_epoch ();
    {
      auto cursor= lmdb::cursor::open (txn, db.entries);
      std::string_view key, value;
      if (cursor.get (key, value, MDB_FIRST)) do {
        std::string popularity_key; u64 (popularity_key, fingerprint (key));
        std::string_view metadata;
        auto score= db.popularity.get (txn, popularity_key, metadata) ?
          usage (metadata) : usage_record {};
        all.push_back ({std::string (key), decayed (score, epoch), score.epoch});
        key= {}; value= {};
      } while (cursor.get (key, value, MDB_NEXT));
    }
    // Eviction is background-only. Rank the whole database, then free a batch
    // of cold pages before the next copy-on-write transaction needs them.
    std::sort (all.begin (), all.end (), [] (const auto& a, const auto& b) {
      if (a.score != b.score) return a.score < b.score;
      return a.epoch < b.epoch;
    });
    const auto trim= std::max<std::size_t> (1, all.size () / 8);
    for (std::size_t i=0; i<std::min (trim, all.size ()); ++i) {
      db.entries.del (txn, all[i].key);
      std::string popularity_key; u64 (popularity_key, fingerprint (all[i].key));
      db.popularity.del (txn, popularity_key);
    }
    txn.commit ();
  }

  void write_batch (
      cache_pair& storage,
      const std::unordered_map<std::string, queued_run>& batch) {
    for (int pass=0; pass<2; ++pass) {
      cache_database& db= storage.select (pass == 1);
      std::size_t count= 0;
      for (const auto& [_,entry]: batch)
        if (entry.key.math == (pass == 1)) ++count;
      if (!count) continue;
      evict (db);
      auto txn= lmdb::txn::begin (db.env);
      for (const auto& [_,entry]: batch) {
        if (entry.key.math != (pass == 1)) continue;
        db.entries.put (txn, entry.key.digest, entry.data);
        std::string popularity_key;
        u64 (popularity_key, fingerprint (entry.key.digest));
        std::string_view previous;
        auto score= db.popularity.get (txn, popularity_key, previous) ?
          usage (previous) : usage_record {};
        const auto epoch= current_epoch ();
        score.score= std::min<std::uint64_t> (
          std::uint64_t (decayed (score, epoch)) + entry.frequency, 0x7fffffff);
        score.epoch= epoch;
        db.popularity.put (txn, popularity_key, encode_usage (score));
      }
      txn.commit ();
    }
  }

  void run () {
    try {
      auto root= location ();
      if (root.empty ()) return;
      auto storage= std::make_shared<cache_pair> (root);
      std::atomic_store_explicit (&mapped, storage, std::memory_order_release);
      for (;;) {
        std::unordered_map<std::string, queued_run> batch;
        {
          std::unique_lock<std::mutex> lock (mutex);
          changed.wait_for (lock, std::chrono::milliseconds (250), [&] {
            return stopping || !pending.empty ();
          });
          if (stopping && pending.empty ()) break;
          batch.swap (pending);
          pending_bytes= 0;
          writing= true;
        }
        try {
          if (!batch.empty ()) write_batch (*storage, batch);
          flush_hits (*storage);
          evict (storage->body);
          evict (storage->math);
        }
        catch (const lmdb::error&) {
          // Disposable cache: a full/damaged store must not stop the editor.
          // On a full map the next idle pass can reclaim cold entries.
          try { evict (storage->body); evict (storage->math); }
          catch (...) {}
        }
        {
          std::lock_guard<std::mutex> lock (mutex);
          writing= false;
        }
        changed.notify_all ();
      }
      flush_hits (*storage);
    }
    catch (const std::exception&) {
      // Readiness remains absent; shaping always works without the cache.
      std::lock_guard<std::mutex> lock (mutex);
      failed= true;
      pending.clear ();
      pending_bytes= 0;
    }
    changed.notify_all ();
  }

public:
  shaping_service (): writer ([this] { run (); }) {}
  ~shaping_service () {
    {
      std::lock_guard<std::mutex> lock (mutex);
      stopping= true;
    }
    changed.notify_all ();
    if (writer.joinable ()) writer.join ();
    std::atomic_store_explicit (&mapped, std::shared_ptr<cache_pair> {},
                                std::memory_order_release);
  }

  std::optional<cached_shaping_run> lookup (
      const shaping_cache_key& key, const shaping_options& options) {
    auto storage= std::atomic_load_explicit (&mapped, std::memory_order_acquire);
    if (!storage) return {};
    try {
      auto& db= storage->select (key.math);
      auto txn= lmdb::txn::begin (db.env, nullptr, MDB_RDONLY);
      std::string_view data;
      if (!db.entries.get (txn, key.digest, data)) return {};
      auto value= decode (data, options);
      if (!value) return {};
      // No transaction, metadata refresh or lock is held during a hit.
      const auto ticket= produced.fetch_add (1, std::memory_order_acq_rel);
      auto& slot= hits[ticket % hit_slots];
      slot.sequence.store (0, std::memory_order_release);
      slot.key.store ((fingerprint (key.digest) << 1) | key.math,
                      std::memory_order_relaxed);
      slot.sequence.store (ticket+1, std::memory_order_release);
      return value;
    }
    catch (const lmdb::error&) { return {}; }
  }

  void submit (const shaping_cache_key& key, const shaped_text& run,
               unsigned frequency) {
    if (run.math_assembly || !run.bitmaps.empty () || run.glyphs.size () > 4096 ||
        run.carets.size () > 4097 || run.byte_end > 4096) return;
    auto value= encode (run);
    std::string id= key.digest;
    id.push_back (key.math ? '\x01' : '\x00');
    const bool builder= std::getenv ("ATHENA_SHAPING_BUILDER") != nullptr;
    std::unique_lock<std::mutex> lock (mutex, std::defer_lock);
    if (builder) {
      lock.lock ();
      // The headless cache builder may await queue space. Interactive shaping
      // is never delayed by a writer mutex or persistence backpressure.
      changed.wait (lock, [&] {
        return failed || stopping || pending.count (id) ||
               pending_bytes + value.size () <= queue_limit;
      });
    }
    else if (!lock.try_lock ()) return;
    if (failed || stopping) return;
    auto existing= pending.find (id);
    if (existing != pending.end ()) {
      existing->second.frequency= static_cast<unsigned> (
        std::min<std::uint64_t> (
          std::uint64_t (existing->second.frequency) + frequency, 0x7fffffff));
      return;
    }
    if (pending_bytes + value.size () > queue_limit) return;
    pending_bytes+= value.size ();
    pending.emplace (std::move (id),
                     queued_run {key, std::move (value), frequency});
    lock.unlock ();
    changed.notify_one ();
  }

  void flush () {
    std::unique_lock<std::mutex> lock (mutex);
    changed.wait (lock, [&] { return failed || (pending.empty () && !writing); });
  }
};

shaping_service& service () {
  static shaping_service instance;
  return instance;
}
} // namespace

std::optional<shaping_cache_key> make_shaping_cache_key (
    const font_file_source& font, int size, int hdpi, int vdpi,
    std::string_view text, std::size_t begin, std::size_t end,
    const shaping_options& options) {
  // Explicit one-time compatibility switch for emergency cache bypass.
  static const bool disabled= std::getenv ("ATHENA_SHAPING_CACHE_DISABLED") != nullptr;
  if (disabled) return {};
  if (font.file_utf8.empty () || text.empty () || text.size () > 2048 ||
      begin >= end || end > text.size () || size <= 0 || hdpi <= 0 || vdpi <= 0)
    return {};
  // Keep serialization scratch space and the SHA context in the font owner,
  // not one allocation/provider setup per glyph. No shaped results live here.
  struct key_builder {
    std::unordered_map<std::string, std::string> prefixes;
    std::string input;
    QCryptographicHash hash {QCryptographicHash::Sha256};
  };
  auto& builder= font_domain_local<key_builder> ();
  auto prefix= builder.prefixes.find (font.file_utf8);
  if (prefix == builder.prefixes.end ()) {
    // The face remains open until domain configuration invalidation. Its file
    // revision and serialized identity have the same lifetime.
    std::error_code ec;
    auto length= fs::file_size (font.file_utf8, ec);
    if (ec) return {};
    auto modified= fs::last_write_time (font.file_utf8, ec);
    if (ec) return {};
    std::string identity= "ATHENA native shaping / schema 1";
    bytes (identity, ATHENA_REVISION);
    bytes (identity, font.file_utf8);
    u64 (identity, length);
    i64 (identity, modified.time_since_epoch ().count ());
    prefix= builder.prefixes.emplace (font.file_utf8, std::move (identity)).first;
  }
  auto& input= builder.input;
  input.assign (prefix->second);
  i64 (input, font.face_index);
  u32 (input, font.design_coords.size ());
  for (auto coordinate: font.design_coords) i64 (input, coordinate);
  i64 (input, size); i64 (input, hdpi); i64 (input, vdpi);
  bytes (input, text);
  u64 (input, begin); u64 (input, end);
  u64 (input, options.context_begin);
  u64 (input, options.context_end == std::string_view::npos ?
       text.size () : options.context_end);
  u8 (input, options.direction == run_direction::right_to_left);
  bytes (input, options.script); bytes (input, options.language);
  for (bool flag: {options.ligatures, options.invisible, options.math_semantics,
                   options.uppercase, options.editing_carets,
                   options.grapheme_fragments})
    u8 (input, flag);
  u32 (input, std::uint32_t (options.math_variant));
  u32 (input, options.math_script_level);
  u32 (input, options.features.size ());
  for (const auto& feature: options.features) {
    u32 (input, feature.tag); u32 (input, feature.value);
  }
  builder.hash.reset ();
  builder.hash.addData (QByteArrayView (input.data (), input.size ()));
  const auto hash= builder.hash.resultView ();
  return shaping_cache_key {
    std::string (hash.data (), std::size_t (hash.size ())),
    options.math_semantics || options.math_variant != math_alphabet::normal ||
      options.math_script_level != 0};
}

std::optional<cached_shaping_run> persistent_shaping_lookup (
    const shaping_cache_key& key, const shaping_options& options) {
  return service ().lookup (key, options);
}

void persistent_shaping_submit (const shaping_cache_key& key,
                                const shaped_text& run, unsigned frequency) {
  service ().submit (key, run, frequency);
}

void persistent_shaping_flush () { service ().flush (); }
} // namespace athena::text
