/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#include "vault_xml_v3_hodarium.hpp"
#include "ATHENA/Hodarium/revisions.hpp"

#include <QByteArray>
#include <QCryptographicHash>
#include <QFile>
#include <QUrl>
#include <QUrlQuery>
#include <nlohmann/json.hpp>
#include <sqlite3.h>

#include <cstdint>
#include <filesystem>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace athena::document {
namespace fs= std::filesystem;
namespace {

QByteArray file_digest (const fs::path& path) {
  QFile source (QString::fromStdString (path.string ()));
  if (!source.open (QIODevice::ReadOnly))
    throw std::runtime_error (
      "Cannot hash external Hodarium database without writing: " +
      path.string ());
  QCryptographicHash hash (QCryptographicHash::Sha256);
  while (!source.atEnd ()) {
    const QByteArray chunk= source.read (1024 * 1024);
    if (chunk.isEmpty ())
      throw std::runtime_error ("Cannot read entire external Hodarium database: " +
                                path.string ());
    hash.addData (chunk);
  }
  return hash.result ();
}

class immutable_db {
  sqlite3* db_= nullptr;
  fs::path path_;
  std::uintmax_t bytes_= 0;
  fs::file_time_type modified_;
  QByteArray digest_;
public:
  explicit immutable_db (const fs::path& path): path_ (path) {
    if (!fs::is_regular_file (fs::symlink_status (path)))
      throw std::runtime_error ("Hodarium audit database is not a regular file: " +
                                path.string ());
    bytes_= fs::file_size (path);
    modified_= fs::last_write_time (path);
    const fs::path wal (path.string () + "-wal");
    if (fs::exists (wal) && fs::file_size (wal) != 0)
      throw std::runtime_error (
        "Hodarium state has uncheckpointed WAL; stop synchronization and "
        "checkpoint it before XML v3 auditing: " + path.string ());
    // A same-size SQLite rewrite with a restored mtime is invisible to a
    // filesystem-stat check. Compare the exact database bytes at both ends
    // of an offline audit, without opening any journal or shared-memory file.
    digest_= file_digest (path_);
    QUrl uri= QUrl::fromLocalFile (QString::fromStdString (path.string ()));
    QUrlQuery query;
    query.addQueryItem ("immutable", "1");
    uri.setQuery (query);
    const QByteArray encoded= uri.toEncoded (QUrl::FullyEncoded);
    if (sqlite3_open_v2 (encoded.constData (), &db_,
                         SQLITE_OPEN_READONLY | SQLITE_OPEN_NOFOLLOW |
                         SQLITE_OPEN_URI, nullptr) != SQLITE_OK) {
      const std::string error= db_ ? sqlite3_errmsg (db_) : "open failed";
      if (db_) sqlite3_close (db_);
      db_= nullptr;
      throw std::runtime_error ("Cannot audit Hodarium database " +
                                path.string () + ": " + error);
    }
  }
  ~immutable_db () { if (db_) sqlite3_close (db_); }
  immutable_db (const immutable_db&)= delete;
  sqlite3* get () const { return db_; }
  QByteArray fingerprint () const { return digest_; }
  void verify_unchanged () const {
    if (!fs::is_regular_file (fs::symlink_status (path_)) ||
        fs::file_size (path_) != bytes_ ||
        fs::last_write_time (path_) != modified_)
      throw std::runtime_error (
        "External Hodarium database changed during immutable audit: " +
        path_.string ());
    const fs::path wal (path_.string () + "-wal");
    if (fs::exists (wal) && fs::file_size (wal) != 0)
      throw std::runtime_error (
        "External Hodarium WAL changed during immutable audit: " +
        path_.string ());
    if (file_digest (path_) != digest_)
      throw std::runtime_error (
        "External Hodarium database bytes changed during immutable audit: " +
        path_.string ());
  }
};

class statement {
  sqlite3_stmt* st_= nullptr;
public:
  statement (immutable_db& db, const std::string& sql) {
    if (sqlite3_prepare_v2 (db.get (), sql.c_str (), -1, &st_, nullptr) !=
        SQLITE_OK)
      throw std::runtime_error ("Hodarium audit SQL: " +
                                std::string (sqlite3_errmsg (db.get ())));
  }
  ~statement () { if (st_) sqlite3_finalize (st_); }
  statement (const statement&)= delete;
  bool step () {
    const int result= sqlite3_step (st_);
    if (result == SQLITE_ROW) return true;
    if (result == SQLITE_DONE) return false;
    throw std::runtime_error (sqlite3_errmsg (sqlite3_db_handle (st_)));
  }
  std::string bytes (int column) const {
    const char* data= (const char*) sqlite3_column_blob (st_, column);
    const int size= sqlite3_column_bytes (st_, column);
    if (!data || size < 0) return {};
    return {data, std::size_t (size)};
  }
  std::int64_t integer (int column) const {
    return sqlite3_column_int64 (st_, column);
  }
  void bind (int index, const std::string& value) {
    if (sqlite3_bind_text (st_, index, value.data (), int (value.size ()),
                           SQLITE_TRANSIENT) != SQLITE_OK)
      throw std::runtime_error ("Cannot bind Hodarium audit SQL value");
  }
};

void integrity (immutable_db& database) {
  statement check (database, "PRAGMA quick_check");
  if (!check.step () || check.bytes (0) != "ok")
    throw std::runtime_error ("Corrupt external Hodarium SQLite database");
  statement references (database, "PRAGMA foreign_key_check");
  if (references.step ())
    throw std::runtime_error ("Broken external Hodarium revision references");
}

std::size_t count_rows (immutable_db& database, const std::string& table,
                        const std::string& condition= {},
                        const std::string& argument= {}) {
  const std::string query= "SELECT count(*) FROM " + table +
    (condition.empty () ? "" : " WHERE " + condition);
  statement rows (database, query);
  if (!condition.empty ()) rows.bind (1, argument);
  if (!rows.step () || rows.integer (0) < 0)
    throw std::runtime_error ("Cannot read external Hodarium record count");
  return std::size_t (rows.integer (0));
}

std::string path_string (const fs::path& path) {
  return path.lexically_normal ().generic_string ();
}

void audit_revision_store (const fs::path& file, const std::string& vault,
                           vault_v3_hodarium_audit& stats,
                           std::map<std::string,QByteArray>& fingerprints) {
  immutable_db db (file);
  integrity (db);
  statement cursor (db, "SELECT id,descriptor,payload FROM revisions");
  while (cursor.step ()) {
    const std::string id= cursor.bytes (0);
    const std::string source= cursor.bytes (1);
    const std::string payload= cursor.bytes (2);
    try {
      const auto data= nlohmann::json::parse (source);
      if (data.at ("protocol") != 1)
        throw std::runtime_error ("unsupported revision protocol");
      athena::hodarium::revision r;
      r.id= id;
      r.vault= data.at ("vault").get<std::string> ();
      r.object= data.at ("object").get<std::string> ();
      r.origin_member= data.at ("origin_member").get<std::string> ();
      r.format= data.at ("format").get<std::string> ();
      r.semantic_version= data.at ("semantic_version").get<std::uint32_t> ();
      r.relative_path= data.at ("path").get<std::string> ();
      r.deleted= data.at ("deleted").get<bool> ();
      r.parents= data.at ("parents").get<std::vector<std::string>> ();
      r.payload= payload;
      const auto sealed= athena::hodarium::seal_revision (r);
      if (sealed.id != id || sealed.parents != r.parents)
        throw std::runtime_error ("stored ID/parent set differs from sealed revision");
      if (r.vault == vault && r.format == "ath-xml-v2") {
        if (r.semantic_version != 3)
          throw std::runtime_error ("unsupported v2 document semantic version");
        ++stats.v2_document_revisions;
      }
      ++stats.revisions_verified;
    }
    catch (const std::exception& failure) {
      throw std::runtime_error (
        "Hodarium revision " + id + " in " + file.string () +
        " cannot be audited: " + failure.what ());
    }
  }
  stats.applied_revisions+= count_rows (db, "applied", "vault=?1", vault);
  stats.pending_applications+= count_rows (
    db, "apply_intents", "vault=?1 AND completed=0", vault);
  // Transfers and approvals are scoped by group, not just Vault. Conservatively
  // count them all: even another Vault's pending write prevents claiming that
  // the shared group journal is at a coordinated idle cutover boundary.
  stats.incomplete_receives+= count_rows (db, "incoming");
  stats.conflict_approvals+= count_rows (db, "resolution_approvals");
  db.verify_unchanged ();
  fingerprints[file.filename ().string ()]= db.fingerprint ();
  ++stats.revision_stores;
}

} // namespace

vault_v3_hodarium_audit audit_external_hodarium_v3 (
    const fs::path& vault, const fs::path& state) {
  if (fs::is_symlink (fs::symlink_status (state)))
    throw std::runtime_error ("Hodarium state cannot be a symlink");
  const fs::path root= fs::canonical (vault);
  const fs::path external= fs::canonical (state);
  auto nested= [] (const fs::path& base, const fs::path& candidate) {
    const auto relative= candidate.lexically_relative (base);
    return !relative.empty () && !relative.is_absolute () &&
           *relative.begin () != "..";
  };
  if (!fs::is_directory (external) || nested (root, external) ||
      nested (external, root))
    throw std::runtime_error ("Hodarium state must be a separate directory");
  const fs::path settings_path= external / "device.sqlite";
  if (!fs::exists (settings_path))
    throw std::runtime_error ("Explicit Hodarium state has no device.sqlite");
  immutable_db settings (settings_path);
  integrity (settings);
  vault_v3_hodarium_audit stats;
  std::map<std::string,QByteArray> fingerprints;
  statement bindings (settings,
    "SELECT hodarium,vault,root,enabled FROM vault_bindings");
  while (bindings.step ()) {
    const std::string source_root= bindings.bytes (2);
    if (fs::path (source_root).lexically_normal () != root) continue;
    ++stats.bindings;
    if (bindings.integer (3) != 0) ++stats.enabled_bindings;
    stats.coordinated_cutover_required= true;
    const std::string group= bindings.bytes (0);
    const std::string vault_id= bindings.bytes (1);
    if (group.empty () || vault_id.empty ())
      throw std::runtime_error ("Malformed external Hodarium Vault binding");
    const std::string prefix= "revisions-" + group;
    std::size_t group_stores= 0;
    for (const auto& entry: fs::directory_iterator (external)) {
      const auto filename= entry.path ().filename ().string ();
      if (filename == prefix + ".sqlite" ||
          (filename.rfind (prefix + "-", 0) == 0 &&
           filename.size () > prefix.size () + 8 &&
           filename.compare (filename.size ()-7, 7, ".sqlite") == 0))
        {
          audit_revision_store (entry.path (), vault_id, stats, fingerprints);
          ++group_stores;
        }
    }
    if (bindings.integer (3) != 0 && group_stores == 0)
      throw std::runtime_error (
        "Enabled Hodarium Vault binding has no auditable revision journal");
  }
  settings.verify_unchanged ();
  fingerprints[settings_path.filename ().string ()]= settings.fingerprint ();
  // Dictionary order, not directory iteration or SQLite row order, defines
  // the same fingerprint across repeated disconnected preflight/staging.
  QCryptographicHash complete (QCryptographicHash::Sha256);
  for (const auto& [name, digest]: fingerprints) {
    complete.addData (name.data (), qsizetype (name.size ()));
    complete.addData ("\0", 1);
    complete.addData (digest);
  }
  stats.state_fingerprint= complete.result ().toHex ().toStdString ();
  return stats;
}

} // namespace athena::document
