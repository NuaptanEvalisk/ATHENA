/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#include "vault_xml_v3_hodarium.hpp"
#include "athena_document_xml.hpp"
#include "ATHENA/Hodarium/revisions.hpp"
#include "ATHENA/Data/hodarium_application.hpp"
#include "ATHENA/Data/hodarium_resources.hpp"
#include "ATHENA/Data/hodarium_logical_sync.hpp"
#include "node_metadata.hpp"
#include "interop_document_source.hpp"

#include <QByteArray>
#include <QCryptographicHash>
#include <QFile>
#include <QUrl>
#include <QUrlQuery>
#include <nlohmann/json.hpp>
#include <sqlite3.h>

#include <cstdint>
#include <cstddef>
#include <algorithm>
#include <filesystem>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace athena::document {
namespace fs= std::filesystem;
namespace {

// Temporary offline audit only; the live exchange no longer accepts v2.
void validate_legacy_document (const athena::hodarium::revision& r) {
  athena::hodarium::detail::validate_document_application_path (r.relative_path);
  if (r.semantic_version != 3 || !athena::node::valid_id (r.object))
    throw std::invalid_argument ("Invalid legacy Hodarium document descriptor");
  if (r.deleted) return;
  if (inspect_xml_version (r.payload) != xml_format_version::v2)
    throw std::invalid_argument ("Legacy Hodarium document requires XML v2");
  const auto source= read_xml_v2 (r.payload);
  const auto error= interop_document_source_error (source);
  if (!error.empty ()) throw std::invalid_argument (error);
  std::string id;
  for (int i= 0; i < N (source); ++i)
    if (is_compound (source[i], "body", 1)) id= athena::node::id (source[i][0]);
  if (id != r.object)
    throw std::invalid_argument ("Legacy Hodarium payload differs from its source identity");
}

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

void integrity (immutable_db& database, int max_schema_version) {
  // SQLite integrity checks know nothing about application schema semantics.
  // A future Hodarium component can preserve familiar table names while
  // changing their meaning. The live stores each enforce separate version
  // ceilings; an immutable migration audit must never interpret a future
  // journal using the older cutover rules.
  statement version (database, "PRAGMA user_version");
  if (!version.step () || version.integer (0) < 0 ||
      version.integer (0) > max_schema_version)
    throw std::runtime_error (
      "Unsupported external Hodarium SQLite schema version");
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

nlohmann::json parse_unambiguous_descriptor (const std::string& source) {
  // sqlite3_json_extract() selects the first occurrence of a duplicate JSON
  // object key, whereas the normal nlohmann parser retains the last. Both
  // systems inspect the same stored revision/apply-intent descriptor. Do not
  // let a valid content-addressed revision hash hide contradictory SQL and
  // C++ interpretations of its format, Vault, object, or causal parent IDs.
  if (source.size () > 512 * 1024)
    throw std::runtime_error ("Hodarium descriptor exceeds audit input budget");
  std::vector<std::set<std::string>> fields;
  return nlohmann::json::parse (source,
    [&] (int depth, nlohmann::json::parse_event_t event,
         nlohmann::json& value) {
      if (depth > 32)
        throw std::runtime_error ("Hodarium descriptor exceeds audit depth budget");
      if (event == nlohmann::json::parse_event_t::object_start)
        fields.emplace_back ();
      else if (event == nlohmann::json::parse_event_t::object_end)
        fields.pop_back ();
      else if (event == nlohmann::json::parse_event_t::key &&
               !fields.back ().insert (value.get<std::string> ()).second)
        throw std::runtime_error ("Duplicate Hodarium revision descriptor field");
      return true;
    });
}

std::string path_string (const fs::path& path) {
  return path.lexically_normal ().generic_string ();
}

std::map<std::string, fs::file_type> state_entries (const fs::path& root) {
  // The set of journal files is part of an immutable audit input, not just
  // the bytes of the journals we happened to discover. A second recovery
  // generation or SQLite sidecar created while walking the first could be
  // completely absent from the receipt even if all previously read files
  // still have the same hash. Directory enumeration remains intentionally
  // conservative: any changed entry requires repeating preflight on a
  // quiescent external-state snapshot.
  std::map<std::string, fs::file_type> result;
  for (const auto& entry: fs::directory_iterator (root))
    result.emplace (entry.path ().filename ().string (),
                    entry.symlink_status ().type ());
  return result;
}

void audit_revision_store (const fs::path& file, const std::set<std::string>& vaults,
                           vault_v3_hodarium_audit& stats,
                           std::map<std::string,QByteArray>& fingerprints) {
  immutable_db db (file);
  integrity (db, 5);
  statement cursor (db, "SELECT id,vault,object,descriptor,payload FROM revisions");
  while (cursor.step ()) {
    const std::string id= cursor.bytes (0);
    const std::string indexed_vault= cursor.bytes (1);
    const std::string indexed_object= cursor.bytes (2);
    const std::string source= cursor.bytes (3);
    const std::string payload= cursor.bytes (4);
    try {
      const auto data= parse_unambiguous_descriptor (source);
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
      // `revisions.vault/object` are duplicated, mutable SQLite index columns.
      // Hash verification alone checks only descriptor/payload. An altered
      // index could make a perfectly sealed revision appear to belong to a
      // different Vault or object, poisoning lookup, ancestry and cutover
      // attribution without violating SQLite's foreign keys.
      if (indexed_vault != r.vault || indexed_object != r.object)
        throw std::runtime_error (
          "revision Vault/object index disagrees with sealed descriptor");
      // Hodarium's normal apply path trusts a previously stored payload
      // fingerprint cache when it exists. That cache is not covered by the
      // revision digest, so verify it independently against the exact payload
      // bytes before using the journal as cutover evidence. A missing cache
      // is legal (the ordinary store lazily creates it), an incorrect one is
      // not: it can break the applied-source preimage comparison.
      statement cached (db,
        "SELECT hash FROM payload_fingerprints WHERE revision=?1");
      cached.bind (1, r.id);
      if (cached.step ()) {
        const auto digest= QCryptographicHash::hash (
          QByteArrayView (payload.data (), qsizetype (payload.size ())),
          QCryptographicHash::Sha256).toHex ().toStdString ();
        if (cached.bytes (0) != digest)
          throw std::runtime_error (
            "cached Hodarium revision payload fingerprint disagrees with sealed content");
      }
      // The content-addressed descriptor owns the causal parent set. The mutable SQLite
      // index is only an acceleration structure; foreign_key_check cannot
      // detect a missing parent edge or a fabricated extra edge when both
      // referenced revision IDs exist. An audit for a protocol cutover must
      // prove that the materialized DAG exactly matches its hashed payloads.
      statement edges (db, "SELECT parent FROM parents WHERE child=?1 ORDER BY parent");
      edges.bind (1, r.id);
      std::vector<std::string> indexed_parents;
      while (edges.step ()) indexed_parents.push_back (edges.bytes (0));
      if (indexed_parents != r.parents)
        throw std::runtime_error ("causal parent index disagrees with sealed revision");
      if (r.format == "ath-xml-v2") {
        validate_legacy_document (r);
        if (vaults.count (r.vault)) ++stats.v2_document_revisions;
      }
      else if (r.format.rfind ("ath-xml-", 0) == 0)
        throw std::runtime_error (
          "unsupported document format inside the legacy Hodarium revision epoch");
      else if (athena::hodarium::resource_revision_format (r.format) ||
               athena::hodarium::logical_revision_format (r.format)) {
        // Resources and logical records have independent format contracts,
        // but none of the current v1 implementations accept semantic v2+.
        // Do not mistake a hash-valid future record for an applicable old
        // format during whole-group migration evidence collection.
        if (r.semantic_version != 1)
          throw std::runtime_error (
            "Unsupported Hodarium non-document semantic version");
        if (athena::hodarium::resource_revision_format (r.format)) {
          try {
            (void) athena::hodarium::validate_resource_revision_metadata (r);
          }
          catch (const std::exception& error) {
            throw std::runtime_error (
              "Invalid Hodarium resource revision path, format or identity: " +
              std::string (error.what ()));
          }
        }
        else
          athena::hodarium::validate_logical_revision_metadata (r);
      }
      else
        throw std::runtime_error (
          "Unsupported Hodarium revision format in legacy epoch");
      ++stats.revisions_verified;
    }
    catch (const std::exception& failure) {
      throw std::runtime_error (
        "Hodarium revision " + id + " in " + file.string () +
        " cannot be audited: " + failure.what ());
    }
  }
  for (const auto& vault: vaults) {
    stats.applied_revisions+= count_rows (db, "applied", "vault=?1", vault);
    stats.pending_applications+= count_rows (
      db, "apply_intents", "vault=?1 AND completed=0", vault);
  }
  // A foreign key only binds the applied pointer to an existing revision ID.
  // It does not ensure that the applied (vault, object) actually owns it.
  statement applied_owners (db,
    "SELECT count(*) FROM applied a JOIN revisions r ON a.revision=r.id "
    "WHERE a.vault<>r.vault OR a.object<>r.object");
  if (!applied_owners.step () || applied_owners.integer (0) != 0)
    throw std::runtime_error ("Applied Hodarium revision belongs to another source object");
  // The live store only records an applied revision after the conflict and
  // causal eligibility gate authorizes it. Missing eligible rows for an
  // already-applied head are not a harmless cache miss: they make a resume
  // or cross-device replay ambiguous even if the content hash is unchanged.
  statement invalid_applied_eligibility (db,
    "SELECT count(*) FROM applied a LEFT JOIN eligible e ON e.id=a.revision "
    "WHERE e.id IS NULL");
  if (!invalid_applied_eligibility.step () ||
      invalid_applied_eligibility.integer (0) != 0)
    throw std::runtime_error ("Applied Hodarium revision has lost its eligibility record");
  // Content addressing also does not prove semantic parent scope. A
  // self-consistent descriptor plus its physical `parents` index might still
  // connect two different Vaults or objects, which the live revision store
  // rejects when accepting a new revision. Enforce the same invariant in the
  // immutable audit before treating the DAG as valid migration evidence.
  statement invalid_parent_scope (db,
    "SELECT count(*) FROM parents p "
    "JOIN revisions child ON child.id=p.child "
    "JOIN revisions parent ON parent.id=p.parent "
    "WHERE child.vault<>parent.vault OR child.object<>parent.object");
  if (!invalid_parent_scope.step () || invalid_parent_scope.integer (0) != 0)
    throw std::runtime_error (
      "Hodarium causal parent crosses a Vault/object boundary");
  // Eligibility is the journal's operational gate for remote applications,
  // independent of whether the underlying revision bytes are authentic.
  // A forged eligible row can bypass the conflict-approval requirement or an
  // ineligible causal parent, so verify exactly the invariants used by
  // revision_store::admit_revision before considering a DAG cutover.
  statement invalid_eligibility (db,
    "SELECT count(*) FROM eligible e JOIN revisions r ON r.id=e.id WHERE "
    "((SELECT count(*) FROM parents p WHERE p.child=e.id)>1 AND "
    "NOT EXISTS(SELECT 1 FROM resolution_approvals a WHERE a.revision=e.id)) "
    "OR EXISTS(SELECT 1 FROM parents p LEFT JOIN eligible pe ON pe.id=p.parent "
    "WHERE p.child=e.id AND pe.id IS NULL)");
  if (!invalid_eligibility.step () || invalid_eligibility.integer (0) != 0)
    throw std::runtime_error (
      "Hodarium eligible revision bypasses its causal or conflict approval gate");
  statement invalid_approval (db,
    "SELECT count(*) FROM resolution_approvals a WHERE "
    "(SELECT count(*) FROM parents p WHERE p.child=a.revision)<2");
  if (!invalid_approval.step () || invalid_approval.integer (0) != 0)
    throw std::runtime_error ("Hodarium conflict approval has no merge revision");
  // A pending or completed intent belongs to its exact revision object, not
  // to a different object with the same content-addressed ID in a mutable SQL index.
  statement invalid_intent (db,
    "SELECT count(*) FROM apply_intents i JOIN revisions r ON r.id=i.target "
    "WHERE i.vault<>r.vault OR i.object<>r.object OR "
    "json_extract(i.descriptor,'$.revision') IS NULL OR "
    "json_extract(i.descriptor,'$.revision')<>i.target");
  if (!invalid_intent.step () || invalid_intent.integer (0) != 0)
    throw std::runtime_error (
      "Hodarium application intent does not belong to its target revision");
  statement invalid_intent_eligibility (db,
    "SELECT count(*) FROM apply_intents i LEFT JOIN eligible e ON e.id=i.target "
    "WHERE e.id IS NULL");
  if (!invalid_intent_eligibility.step () ||
      invalid_intent_eligibility.integer (0) != 0)
    throw std::runtime_error (
      "Hodarium application intent target has lost its eligibility record");
  statement intent_payloads (db,
    "SELECT i.descriptor,i.target,i.vault,i.object,i.completed,"
    "COALESCE((SELECT a.revision FROM applied a WHERE a.vault=i.vault "
    "AND a.object=i.object),''),"
    "json_extract(r.descriptor,'$.format'),i.operation FROM apply_intents i "
    "JOIN revisions r ON r.id=i.target");
  while (intent_payloads.step ()) {
    const auto header= parse_unambiguous_descriptor (intent_payloads.bytes (0));
    if (!header.is_object () ||
        header.at ("revision").get<std::string> () != intent_payloads.bytes (1))
      throw std::runtime_error ("Hodarium application intent descriptor target differs");
    // Also require the same typed fields read by revision_store::read_intent.
    // An unknown duplicate field is dangerous even if SQL currently ignores
    // it, because an old revision may be resumed after restart via C++.
    const std::string path= header.at ("source_path").get<std::string> ();
    if (intent_payloads.bytes (6) == "ath-xml-v2")
      athena::hodarium::detail::validate_document_application_path (path);
    else if (path.size () > 64 * 1024 ||
             !athena::history::valid_relative_document_path (path) ||
             std::filesystem::path (path).lexically_normal ().generic_string () != path)
      throw std::runtime_error ("Invalid Hodarium non-document application path");
    const std::string target= intent_payloads.bytes (1);
    const std::string vault= intent_payloads.bytes (2);
    const std::string object= intent_payloads.bytes (3);
    if (intent_payloads.integer (4) != 0 && intent_payloads.integer (4) != 1)
      throw std::runtime_error ("Invalid protected application intent");
    const bool pending= intent_payloads.integer (4) == 0;
    const std::string applied= intent_payloads.bytes (5);
    const auto& expected= header.at ("expected");
    athena::hodarium::apply_intent metadata;
    metadata.operation= intent_payloads.bytes (7);
    metadata.revision_id= target;
    metadata.source_path= path;
    metadata.completed= !pending;
    metadata.protected_history_version=
      header.at ("protected_history_version").get<std::int64_t> ();
    if (!expected.is_null ())
      metadata.expected_revision= expected.get<std::string> ();
    if (!header.at ("source_fingerprint").is_null ())
      metadata.source_fingerprint=
        header.at ("source_fingerprint").get<std::string> ();
    athena::hodarium::validate_apply_intent_metadata (metadata, true);
    if (!expected.is_null ()) {
      const std::string base= expected.get<std::string> ();
      statement parent (db,
        "SELECT vault,object,json_extract(descriptor,'$.path') "
        "FROM revisions WHERE id=?1");
      parent.bind (1, base);
      if (!parent.step () || parent.bytes (0) != vault ||
          parent.bytes (1) != object || parent.bytes (2) != path ||
          base == target)
        throw std::runtime_error (
          "Hodarium application intent base does not match applied causal state");
      statement ancestry (db,
        "WITH RECURSIVE ancestors(id) AS (VALUES(?1) UNION "
        "SELECT p.parent FROM parents p JOIN ancestors a ON p.child=a.id) "
        "SELECT 1 FROM ancestors WHERE id=?2 LIMIT 1");
      ancestry.bind (1, target);
      ancestry.bind (2, base);
      if (!ancestry.step () || (pending && applied != base))
        throw std::runtime_error (
          "Hodarium application intent base does not match applied causal state");
    }
    else {
      statement initial (db,
        "SELECT json_extract(descriptor,'$.path') FROM revisions WHERE id=?1");
      initial.bind (1, target);
      if (!initial.step () || initial.bytes (0) != path ||
          (pending && !applied.empty ()))
        throw std::runtime_error (
          "Hodarium application intent base does not match applied causal state");
    }
  }
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
  const auto initial_entries= state_entries (external);
  const fs::path settings_path= external / "device.sqlite";
  if (!fs::exists (settings_path))
    throw std::runtime_error ("Explicit Hodarium state has no device.sqlite");
  immutable_db settings (settings_path);
  integrity (settings, 6);
  vault_v3_hodarium_audit stats;
  std::map<std::string,QByteArray> fingerprints;
  // A group journal is shared by every Vault bound to the group. First
  // collect the unique (group, vault) associations; then audit each physical
  // revision journal exactly once. Otherwise multiple Vault bindings inflated
  // revision/incoming/approval totals, and multiple records for one binding
  // could count the same protected revision repeatedly.
  std::map<std::string, std::set<std::string>> group_vaults;
  std::set<std::string> enabled_groups;
  std::set<fs::path> occupied_roots;
  statement bindings (settings,
    "SELECT hodarium,vault,root,enabled FROM vault_bindings");
  while (bindings.step ()) {
    const std::string source_root= bindings.bytes (2);
    const fs::path recorded_root (source_root);
    const std::string group= bindings.bytes (0);
    const std::string vault_id= bindings.bytes (1);
    // Enforce the same identity and root-uniqueness contract as the real
    // client_settings schema, even if the external SQLite schema was copied
    // incorrectly or its constraints were removed after a recovery.
    if (group.size () != 43 ||
        !std::all_of (group.begin (), group.end (), [] (unsigned char c) {
          return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                 (c >= '0' && c <= '9') || c == '-' || c == '_';
        }) || !athena::node::valid_id (vault_id))
      throw std::runtime_error ("Malformed external Hodarium Vault binding");
    if (recorded_root.empty () || !recorded_root.is_absolute ())
      throw std::runtime_error ("Hodarium Vault binding root is not absolute");
    if (!occupied_roots.insert (recorded_root.lexically_normal ()).second)
      throw std::runtime_error ("Duplicate external Hodarium Vault root binding");
    if (recorded_root.lexically_normal () != root) {
      // client_settings persists canonical Vault roots. A forged or stale
      // symlinked alias of THIS source root must not be silently skipped,
      // making it appear unbound and safe to publish. `weakly_canonical`
      // handles still-existing symlinked parents without requiring all
      // unrelated historic Vault paths to be mounted during the audit.
      std::error_code error;
      const auto physical= fs::weakly_canonical (recorded_root, error);
      if (!error && physical == root)
        throw std::runtime_error (
          "External Hodarium Vault binding aliases this source root");
      continue;
    }
    ++stats.bindings;
    if (bindings.integer (3) != 0) ++stats.enabled_bindings;
    stats.coordinated_cutover_required= true;
    // A group journal may be shared with other Vaults rooted elsewhere, but
    // a second binding for this same Vault must never appear under this group.
    // Real client_settings enforces uniqueness of (group,vault) and root.
    // An immutable migration audit must not turn a tampered, duplicate device
    // binding into a plausible larger inventory or count the same archive
    // object twice. The group-specific journal may still serve OTHER Vaults
    // rooted elsewhere; those are not counted as this Vault's bindings.
    if (group_vaults[group].count (vault_id))
      throw std::runtime_error ("Duplicate external Hodarium group/Vault binding");
    group_vaults[group].insert (vault_id);
    if (bindings.integer (3) != 0) enabled_groups.insert (group);
  }
  for (const auto& [group, vaults]: group_vaults) {
    const std::string prefix= "revisions-" + group;
    std::size_t group_stores= 0;
    for (const auto& entry: fs::directory_iterator (external)) {
      const auto filename= entry.path ().filename ().string ();
      const bool legacy= filename == prefix + ".sqlite";
      const bool generation= filename.rfind (prefix + "-", 0) == 0 &&
        filename.size () >= 7 &&
        filename.compare (filename.size ()-7, 7, ".sqlite") == 0;
      if (generation) {
        const auto first= filename.begin () + std::ptrdiff_t (prefix.size () + 1);
        const auto last= filename.end () - 7;
        if (filename.size () != prefix.size () + 1 + 43 + 7 ||
            !std::all_of (first, last, [] (unsigned char c) {
              return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                     (c >= '0' && c <= '9') || c == '-' || c == '_';
            }))
          throw std::runtime_error (
            "Malformed external Hodarium revision generation journal name: " + filename);
      }
      if (!legacy && !generation) continue;
      audit_revision_store (entry.path (), vaults, stats, fingerprints);
      ++group_stores;
    }
    if (enabled_groups.count (group) && group_stores == 0)
      throw std::runtime_error (
        "Enabled Hodarium Vault binding has no auditable revision journal");

    // A group's external causal state also depends on its membership pin,
    // conflict proposals and authenticated decision-feed progress. They are
    // not part of any Vault directory and are never rewritten by the v3
    // staged migrator. Treat every existing group-scoped sidecar (including
    // retained recovery generations) as immutable external evidence: fail on
    // corrupt SQLite/nonempty WAL and fingerprint it with the revision DAG.
    // This is an inventory and integrity check, NOT a replacement for later
    // cross-device membership/decision signature verification.
    for (const auto& entry: fs::directory_iterator (external)) {
      const std::string name= entry.path ().filename ().string ();
      bool belongs= false;
      int max_version= 0;
      for (const std::string kind: {"membership", "conflicts", "decisions"}) {
        const std::string stem= kind + "-" + group;
        if (name == stem + ".sqlite" && kind == "membership") {
          belongs= true;
          max_version= 1;
          break;
        }
        // Current generation-scoped databases have a 43-character base64url
        // generation handle. Older journals must remain part of the proof.
        // A group-matching but malformed `.sqlite` name may hide protected
        // decision/trust state, so fail closed rather than skipping it.
        const bool generation= name.rfind (stem + "-", 0) == 0 &&
          name.size () >= 7 &&
          name.compare (name.size ()-7, 7, ".sqlite") == 0;
        if (!generation) continue;
        if (name.size () != stem.size () + 1 + 43 + 7 ||
            !std::all_of (
              name.begin () + std::ptrdiff_t (stem.size () + 1),
              name.end () - 7, [] (unsigned char c) {
                return (c >= 'A' && c <= 'Z') ||
                       (c >= 'a' && c <= 'z') ||
                       (c >= '0' && c <= '9') || c == '-' || c == '_';
              }))
          throw std::runtime_error (
            "Malformed external Hodarium group sidecar generation: " + name);
        belongs= true;
        max_version= kind == "conflicts" ? 2 : 1;
        break;
      }
      if (!belongs) continue;
      immutable_db db (entry.path ());
      integrity (db, max_version);
      db.verify_unchanged ();
      fingerprints[name]= db.fingerprint ();
    }
  }
  settings.verify_unchanged ();
  fingerprints[settings_path.filename ().string ()]= settings.fingerprint ();
  // Journals are read one after the other. A writer can mutate the first one
  // after its per-database verification while later journals are still being
  // scanned. Re-hash every file at the common end boundary as well, rather
  // than issuing a misleading inventory from different points in time.
  for (const auto& [filename, expected]: fingerprints) {
    const fs::path file= external / filename;
    if (!fs::is_regular_file (fs::symlink_status (file)) ||
        file_digest (file) != expected)
      throw std::runtime_error (
        "External Hodarium audit input changed before the final snapshot: " + filename);
    const fs::path wal (file.string () + "-wal");
    if (fs::exists (wal) && fs::file_size (wal) != 0)
      throw std::runtime_error ("External Hodarium WAL appeared during audit: " + filename);
  }
  if (state_entries (external) != initial_entries)
    throw std::runtime_error (
      "External Hodarium database directory changed during immutable audit");
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
