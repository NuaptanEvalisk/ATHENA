/******************************************************************************
* MODULE     : namespace_sorter_migration.cpp
* DESCRIPTION: Offline explicit-mapping migration for namespace sorters
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "namespace_sorter_migration.hpp"

#include "namespaces_private.hpp"
#include "vault_directory_lease.hpp"
#include "vaultfile_json.hpp"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>

#include <sqlite3.h>

#include <fcntl.h>
#include <unistd.h>

#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <system_error>
#include <vector>

namespace {

namespace fs= std::filesystem;
using athena_namespaces::load_sorter;
using athena_namespaces::ns_field_type;
using athena_namespaces::parse_template_std;
using athena_namespaces::sort_namespace_members;
using athena_namespaces::sorter_handle;
using athena_namespaces::template_token;

using db_ptr= std::unique_ptr<sqlite3, decltype (&sqlite3_close)>;
using stmt_ptr= std::unique_ptr<sqlite3_stmt, decltype (&sqlite3_finalize)>;

struct mapping_entry {
  std::string from_text;
  std::string to_text;
  fs::path from_resolved;
  fs::path to_resolved;
  std::string from_stored;
  std::string to_stored;
  bool used= false;
  bool already_applied= false;
};

struct namespace_row {
  std::string uuid;
  std::string name;
  std::string templ;
  std::string sorter_path;
};

struct pending_update {
  namespace_row row;
  mapping_entry* mapping= nullptr;
};

static bool
read_bytes (const fs::path& path, std::string& bytes, std::string& error) {
  std::ifstream in (path, std::ios::binary);
  if (!in.good ()) {
    error= "Cannot open " + path.string ();
    return false;
  }
  std::ostringstream out;
  out << in.rdbuf ();
  if (!in.good () && !in.eof ()) {
    error= "Cannot read " + path.string ();
    return false;
  }
  bytes= out.str ();
  return true;
}
static uint64_t
hash_bytes (std::string_view text) {
  uint64_t hash= 1469598103934665603ULL;
  for (unsigned char c: text) {
    hash ^= c;
    hash *= 1099511628211ULL;
  }
  return hash;
}

static std::string
hash_string (uint64_t hash) {
  return std::to_string (hash);
}

static bool
path_is_beneath_root (const fs::path& relative) {
  if (relative.empty () || relative.is_absolute ()) return false;
  for (const fs::path& part: relative)
    if (part == "..") return false;
  return true;
}

static bool
resolved_path_is_within_root (const fs::path& root, const fs::path& path) {
  std::error_code ec;
  fs::path canonical_root= fs::weakly_canonical (root, ec);
  if (ec) canonical_root= root.lexically_normal ();
  fs::path canonical_path= fs::weakly_canonical (path, ec);
  if (ec) canonical_path= path.lexically_normal ();
  fs::path relative= canonical_path.lexically_relative (canonical_root);
  return !relative.empty () && !relative.is_absolute () &&
         path_is_beneath_root (relative);
}

static bool
resolve_user_path (const fs::path& root, const std::string& text,
                   fs::path& resolved, std::string& stored,
                   std::string& error) {
  if (text.empty ()) {
    error= "Sorter mapping paths must not be empty.";
    return false;
  }
  fs::path supplied (text);
  std::error_code ec;
  if (supplied.is_absolute ()) {
    resolved= fs::weakly_canonical (supplied, ec);
    if (ec) resolved= supplied.lexically_normal ();
    stored= resolved.generic_string ();
    return true;
  }
  fs::path relative= supplied.lexically_normal ();
  if (!path_is_beneath_root (relative)) {
    error= "Vault-relative sorter path escapes the vault: " + text;
    return false;
  }
  fs::path absolute= root / relative;
  resolved= fs::weakly_canonical (absolute, ec);
  if (ec) resolved= absolute.lexically_normal ();
  if (!resolved_path_is_within_root (root, resolved)) {
    error= "Vault-relative sorter path resolves outside the vault: " + text;
    return false;
  }
  stored= relative.generic_string ();
  return true;
}

static fs::path
resolve_database_sorter_path (const fs::path& root, const std::string& text) {
  fs::path supplied (text);
  fs::path absolute= supplied.is_absolute () ? supplied : root / supplied;
  std::error_code ec;
  fs::path resolved= fs::weakly_canonical (absolute, ec);
  return ec ? absolute.lexically_normal () : resolved;
}

static bool
parse_mapping_json (const fs::path& root, const std::string& bytes,
                    std::vector<mapping_entry>& entries, std::string& error) {
  QJsonParseError parse_error;
  QJsonDocument document= QJsonDocument::fromJson (
    QByteArray (bytes.data (), (qsizetype) bytes.size ()), &parse_error);
  if (parse_error.error != QJsonParseError::NoError) {
    error= "Sorter map must be a JSON array: " +
           parse_error.errorString ().toStdString ();
    return false;
  }
  if (!document.isArray ()) {
    error= "Sorter map top level must be a JSON array.";
    return false;
  }
  std::map<std::string, size_t> by_from;
  entries.clear ();
  QJsonArray array= document.array ();
  for (int i=0; i<array.size (); ++i) {
    if (!array[i].isObject ()) {
      error= "Sorter map entry " + std::to_string (i + 1) +
             " must be an object.";
      return false;
    }
    QJsonObject object= array[i].toObject ();
    if (object.size () != 2 || !object.value ("from").isString () ||
        !object.value ("to").isString ()) {
      error= "Sorter map entry " + std::to_string (i + 1) +
             " must contain exactly string fields 'from' and 'to'.";
      return false;
    }
    mapping_entry entry;
    entry.from_text= object.value ("from").toString ().toStdString ();
    entry.to_text= object.value ("to").toString ().toStdString ();
    std::string from_stored;
    if (!resolve_user_path (root, entry.from_text, entry.from_resolved,
                             from_stored, error) ||
        !resolve_user_path (root, entry.to_text, entry.to_resolved,
                             entry.to_stored, error))
      return false;
    entry.from_stored= std::move (from_stored);
    std::string key= entry.from_resolved.generic_string ();
    auto found= by_from.find (key);
    if (found != by_from.end ()) {
      const mapping_entry& previous= entries[found->second];
      if (previous.to_resolved != entry.to_resolved) {
        error= "Conflicting sorter mappings normalize to the same source path: " +
               entry.from_text;
        return false;
      }
      continue;
    }
    by_from[key]= entries.size ();
    entries.push_back (std::move (entry));
  }
  if (entries.empty ()) {
    error= "Sorter map array is empty.";
    return false;
  }
  return true;
}

static bool
sqlite_exec (sqlite3* db, const char* sql, std::string& error) {
  char* message= nullptr;
  int rc= sqlite3_exec (db, sql, nullptr, nullptr, &message);
  if (rc == SQLITE_OK) return true;
  error= message ? message : sqlite3_errmsg (db);
  sqlite3_free (message);
  return false;
}

static stmt_ptr
prepare (sqlite3* db, const char* sql, std::string& error) {
  sqlite3_stmt* raw= nullptr;
  if (sqlite3_prepare_v2 (db, sql, -1, &raw, nullptr) != SQLITE_OK)
    error= sqlite3_errmsg (db);
  return stmt_ptr (raw, sqlite3_finalize);
}

static bool
open_database (const fs::path& path, int flags, db_ptr& out,
               std::string& error) {
  sqlite3* raw= nullptr;
  int rc= sqlite3_open_v2 (path.string ().c_str (), &raw, flags, nullptr);
  if (rc != SQLITE_OK) {
    error= raw ? sqlite3_errmsg (raw) : "sqlite3_open_v2 failed";
    if (raw) sqlite3_close (raw);
    return false;
  }
  out= db_ptr (raw, sqlite3_close);
  return true;
}

static bool
read_namespace_rows (sqlite3* db, std::vector<namespace_row>& rows,
                     std::string& error) {
  auto statement= prepare (
    db,
    "SELECT uuid,name,template,sorter_path FROM namespaces "
    "WHERE sorter_path<>'' ORDER BY name;",
    error);
  if (!statement) return false;
  rows.clear ();
  int rc;
  while ((rc= sqlite3_step (statement.get ())) == SQLITE_ROW) {
    namespace_row row;
    for (int column=0; column<4; ++column)
      if (sqlite3_column_type (statement.get (), column) == SQLITE_NULL) {
        error= "Namespace sorter migration found a null database field.";
        return false;
      }
    row.uuid= reinterpret_cast<const char*> (
      sqlite3_column_text (statement.get (), 0));
    row.name= reinterpret_cast<const char*> (
      sqlite3_column_text (statement.get (), 1));
    row.templ= reinterpret_cast<const char*> (
      sqlite3_column_text (statement.get (), 2));
    row.sorter_path= reinterpret_cast<const char*> (
      sqlite3_column_text (statement.get (), 3));
    rows.push_back (std::move (row));
  }
  if (rc != SQLITE_DONE) {
    error= sqlite3_errmsg (db);
    return false;
  }
  return true;
}

static const char*
field_type_text (ns_field_type type) {
  switch (type) {
  case athena_namespaces::ns_string_field: return "string";
  case athena_namespaces::ns_word_field: return "word";
  case athena_namespaces::ns_char_field: return "char";
  case athena_namespaces::ns_int_field: return "int";
  case athena_namespaces::ns_pos_int_field: return "positive-int";
  case athena_namespaces::ns_roman_field: return "roman";
  }
  return "string";
}

static std::pair<const char*, const char*>
representative_values (ns_field_type type) {
  switch (type) {
  case athena_namespaces::ns_string_field: return {"alpha", "beta"};
  case athena_namespaces::ns_word_field: return {"alpha", "beta"};
  case athena_namespaces::ns_char_field: return {"a", "b"};
  case athena_namespaces::ns_int_field:
    return {"9007199254740993", "9007199254740992"};
  case athena_namespaces::ns_pos_int_field:
    return {"9007199254740993", "9007199254740992"};
  case athena_namespaces::ns_roman_field: return {"I", "II"};
  }
  return {"alpha", "beta"};
}

static bool
representative_members (const namespace_row& row,
                        namespace_records<athena_namespace_match>& members,
                        string& error) {
  std::vector<template_token> tokens;
  std::string parse_error;
  if (!parse_template_std (row.templ, tokens, parse_error)) {
    error= string (("Cannot parse namespace template for '" + row.name +
                    "': " + parse_error).c_str ());
    return false;
  }
  std::vector<athena_namespace_match> fixtures (2);
  fixtures[0].stem= "migration-fixture-a";
  fixtures[1].stem= "migration-fixture-b";
  for (const template_token& token: tokens) {
    if (!token.field) continue;
    auto values= representative_values (token.type);
    for (size_t i=0; i<2; ++i) {
      fixtures[i].captures.push_back (
        string (i == 0 ? values.first : values.second));
      fixtures[i].capture_types.push_back (
        string (field_type_text (token.type)));
    }
  }
  members= namespace_records<athena_namespace_match> (std::move (fixtures));
  return true;
}

static bool
validate_replacement (const namespace_row& row, const mapping_entry& mapping,
                      std::map<std::string, uint64_t>& target_hashes,
                      std::string& error) {
  if (mapping.to_resolved.extension () != ".luau") {
    error= "Replacement sorter for namespace '" + row.name +
           "' must have .luau extension: " + mapping.to_text;
    return false;
  }
  std::error_code ec;
  if (!fs::is_regular_file (mapping.to_resolved, ec) || ec) {
    error= "Replacement Luau sorter does not exist as a regular file: " +
           mapping.to_resolved.string ();
    return false;
  }
  std::string bytes;
  if (!read_bytes (mapping.to_resolved, bytes, error)) return false;
  target_hashes[mapping.to_resolved.generic_string ()]= hash_bytes (bytes);

  string sorter_error;
  sorter_handle sorter= load_sorter (
    string (mapping.to_resolved.string ().c_str ()), sorter_error);
  if (!sorter || sorter_error != "") {
    error= "Replacement Luau sorter failed validation for namespace '" +
           row.name + "': " +
           std::string (sorter_error.data (), (size_t) N(sorter_error));
    return false;
  }
  namespace_records<athena_namespace_match> fixtures;
  if (!representative_members (row, fixtures, sorter_error)) {
    error= std::string (sorter_error.data (), (size_t) N(sorter_error));
    return false;
  }
  if (!sort_namespace_members (sorter, fixtures, sorter_error)) {
    error= "Replacement Luau sorter rejected representative typed fields for "
           "namespace '" + row.name + "': " +
           std::string (sorter_error.data (), (size_t) N(sorter_error));
    return false;
  }
  return true;
}

static bool
fsync_file (const fs::path& path, std::string& error) {
  int fd= ::open (path.c_str (), O_RDONLY | O_CLOEXEC);
  if (fd < 0) {
    error= "Cannot open file for fsync: " + path.string () + ": " +
           std::generic_category ().message (errno);
    return false;
  }
  int rc;
  do rc= ::fsync (fd); while (rc < 0 && errno == EINTR);
  int saved= errno;
  ::close (fd);
  if (rc == 0) return true;
  error= "Cannot fsync file " + path.string () + ": " +
         std::generic_category ().message (saved);
  return false;
}

static bool
fsync_directory (const fs::path& directory, std::string& error) {
  int fd= ::open (directory.c_str (), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  if (fd < 0) {
    error= "Cannot open directory for fsync: " + directory.string () +
           ": " + std::generic_category ().message (errno);
    return false;
  }
  int rc;
  do rc= ::fsync (fd); while (rc < 0 && errno == EINTR);
  int saved= errno;
  ::close (fd);
  if (rc == 0) return true;
  error= "Cannot fsync directory " + directory.string () + ": " +
         std::generic_category ().message (saved);
  return false;
}

static bool
write_json_atomic (const fs::path& path, const QJsonObject& object,
                   std::string& error) {
  QSaveFile file (QString::fromStdString (path.string ()));
  if (!file.open (QIODevice::WriteOnly)) {
    error= "Cannot open migration journal for writing: " +
           file.errorString ().toStdString ();
    return false;
  }
  QByteArray bytes= QJsonDocument (object).toJson (QJsonDocument::Indented);
  if (file.write (bytes) != bytes.size () || !file.commit ()) {
    error= "Cannot commit migration journal: " +
           file.errorString ().toStdString ();
    return false;
  }
  return fsync_file (path, error) &&
         fsync_directory (path.parent_path (), error);
}

static bool
sqlite_backup_to (sqlite3* source, const fs::path& destination,
                  std::string& error) {
  db_ptr target (nullptr, sqlite3_close);
  if (!open_database (destination,
                      SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
                      target, error))
    return false;
  sqlite3_backup* backup= sqlite3_backup_init (
    target.get (), "main", source, "main");
  if (!backup) {
    error= sqlite3_errmsg (target.get ());
    return false;
  }
  int rc= sqlite3_backup_step (backup, -1);
  int finish_rc= sqlite3_backup_finish (backup);
  if (rc != SQLITE_DONE || finish_rc != SQLITE_OK) {
    error= sqlite3_errmsg (target.get ());
    return false;
  }
  target.reset ();
  return fsync_file (destination, error);
}

static bool
remove_journal (const fs::path& journal_path, std::string& error) {
  std::error_code ec;
  fs::remove (journal_path, ec);
  if (ec) {
    error= "Cannot remove completed migration journal: " + ec.message ();
    return false;
  }
  return fsync_directory (journal_path.parent_path (), error);
}

static bool delete_legacy_sources (const QJsonArray& sources,
                                   std::string& error);

enum class journal_database_state { before, after, external };

static bool
inspect_journal_database_state (const fs::path& database_path,
                                const QJsonObject& journal,
                                journal_database_state& state,
                                std::string& error) {
  db_ptr db (nullptr, sqlite3_close);
  if (!open_database (database_path, SQLITE_OPEN_READONLY, db, error))
    return false;
  auto statement= prepare (
    db.get (), "SELECT sorter_path FROM namespaces WHERE uuid=?;", error);
  if (!statement) return false;
  bool all_before= true;
  bool all_after= true;
  for (const QJsonValue& value: journal.value ("updates").toArray ()) {
    if (!value.isObject ()) {
      error= "Corrupt namespace sorter migration journal.";
      return false;
    }
    QJsonObject update= value.toObject ();
    QByteArray uuid= update.value ("uuid").toString ().toUtf8 ();
    std::string before= update.value ("from").toString ().toStdString ();
    std::string after= update.value ("to").toString ().toStdString ();
    sqlite3_reset (statement.get ());
    sqlite3_clear_bindings (statement.get ());
    if (sqlite3_bind_text (statement.get (), 1, uuid.constData (), uuid.size (),
                           SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_step (statement.get ()) != SQLITE_ROW) {
      error= "Cannot inspect namespace sorter migration journal state.";
      return false;
    }
    const char* current= reinterpret_cast<const char*> (
      sqlite3_column_text (statement.get (), 0));
    if (!current) { state= journal_database_state::external; return true; }
    all_before &= before == current;
    all_after &= after == current;
  }
  state= all_before ? journal_database_state::before :
         all_after ? journal_database_state::after :
                     journal_database_state::external;
  return true;
}

static bool
recover_journal_if_needed (const fs::path& journal_path,
                             const fs::path& database_path,
                             std::string& error) {
  if (!fs::exists (journal_path)) return true;
  std::string bytes;
  if (!read_bytes (journal_path, bytes, error)) return false;
  QJsonParseError parse_error;
  QJsonDocument document= QJsonDocument::fromJson (
    QByteArray (bytes.data (), (qsizetype) bytes.size ()), &parse_error);
  if (parse_error.error != QJsonParseError::NoError || !document.isObject ()) {
    error= "Cannot recover namespace sorter migration: journal is invalid JSON.";
    return false;
  }
  QJsonObject journal= document.object ();
  if (journal.value ("format").toString () !=
        "athena-namespace-sorter-migration" ||
      journal.value ("version").toInt (-1) != 1 ||
      !journal.value ("updates").isArray () ||
      !journal.value ("legacy_sources").isArray ()) {
    error= "Cannot recover namespace sorter migration: journal format is invalid.";
    return false;
  }
  std::string state= journal.value ("state").toString ().toStdString ();
  fs::path recorded_db= journal.value ("database").toString ().toStdString ();
  fs::path backup= journal.value ("backup").toString ().toStdString ();
  if (recorded_db.lexically_normal () != database_path.lexically_normal () ||
      !fs::is_regular_file (backup)) {
    error= "Cannot recover namespace sorter migration: journal paths are invalid.";
    return false;
  }
  journal_database_state database_state;
  if (!inspect_journal_database_state (
        database_path, journal, database_state, error)) return false;
  if (database_state == journal_database_state::external) {
    error= "Cannot recover namespace sorter migration because the namespace "
           "database changed outside the journaled transaction. The consistent "
           "pre-migration backup is " + backup.string () + ".";
    return false;
  }
  if (state == "prepared") {
    if (database_state == journal_database_state::after) {
      if (!delete_legacy_sources (
            journal.value ("legacy_sources").toArray (), error)) return false;
      std::cerr << "Recovered namespace sorter migration committed before its "
                   "journal state was finalized.\n";
    }
    else
      std::cerr << "Recovered namespace sorter migration interrupted before "
                   "database commit.\n";
  }
  else if (state == "committed") {
    if (database_state != journal_database_state::after) {
      error= "Committed namespace sorter migration journal disagrees with the "
             "namespace database; no recovery overwrite was performed.";
      return false;
    }
    if (!delete_legacy_sources (
          journal.value ("legacy_sources").toArray (), error)) return false;
    std::cerr << "Finalized previously committed namespace sorter migration.\n";
  }
  else {
    error= "Cannot recover namespace sorter migration: unknown journal state.";
    return false;
  }
  return remove_journal (journal_path, error);
}

static bool
unchanged_file_hashes (const std::map<std::string, uint64_t>& hashes,
                       std::string& error) {
  for (const auto& [path, expected]: hashes) {
    std::string bytes;
    if (!read_bytes (fs::path (path), bytes, error)) return false;
    if (hash_bytes (bytes) != expected) {
      error= "Replacement Luau sorter changed during migration: " + path;
      return false;
    }
  }
  return true;
}

static QJsonArray
journal_updates (const std::vector<pending_update>& updates) {
  QJsonArray out;
  for (const pending_update& update: updates)
    out.append (QJsonObject {
      {"uuid", QString::fromStdString (update.row.uuid)},
      {"name", QString::fromStdString (update.row.name)},
      {"from", QString::fromStdString (update.row.sorter_path)},
      {"to", QString::fromStdString (update.mapping->to_stored)}
    });
  return out;
}

static QJsonArray
journal_legacy_sources (const fs::path& root,
                         const std::vector<mapping_entry*>& sources,
                         std::string& error) {
  QJsonArray out;
  std::set<std::string> seen;
  for (const mapping_entry* mapping: sources) {
    if (!resolved_path_is_within_root (root, mapping->from_resolved)) continue;
    fs::path written (mapping->from_text);
    fs::path delete_path= written.is_absolute ()
      ? written.lexically_normal ()
      : (root / fs::path (mapping->from_stored)).lexically_normal ();
    std::string path= delete_path.generic_string ();
    if (!seen.insert (path).second) continue;
    std::error_code ec;
    if (!fs::exists (delete_path, ec)) continue;
    if (ec || !fs::is_regular_file (delete_path, ec) || ec) {
      error= "Legacy sorter source is not a regular file: " + path;
      return {};
    }
    std::string bytes;
    if (!read_bytes (delete_path, bytes, error)) return {};
    out.append (QJsonObject {
      {"path", QString::fromStdString (path)},
      {"hash", QString::fromStdString (hash_string (hash_bytes (bytes)))}
    });
  }
  return out;
}

static bool
delete_legacy_sources (const QJsonArray& sources, std::string& error) {
  for (const QJsonValue& value: sources) {
    if (!value.isObject ()) {
      error= "Corrupt namespace sorter migration legacy-source journal.";
      return false;
    }
    QJsonObject source= value.toObject ();
    fs::path path= source.value ("path").toString ().toStdString ();
    std::string expected_hash= source.value ("hash").toString ().toStdString ();
    if (path.empty () || expected_hash.empty ()) {
      error= "Corrupt namespace sorter migration legacy-source entry.";
      return false;
    }
    std::error_code ec;
    if (!fs::exists (path, ec)) continue;
    if (ec || !fs::is_regular_file (path, ec) || ec) {
      error= "Legacy sorter source changed type before deletion: " + path.string ();
      return false;
    }
    std::string bytes;
    if (!read_bytes (path, bytes, error)) return false;
    if (hash_string (hash_bytes (bytes)) != expected_hash) {
      error= "Legacy sorter source changed after migration validation; refusing "
             "to delete externally modified file: " + path.string ();
      return false;
    }
    fs::remove (path, ec);
    if (ec) {
      error= "Cannot delete migrated legacy sorter source " + path.string () +
             ": " + ec.message ();
      return false;
    }
    if (!fsync_directory (path.parent_path (), error)) return false;
  }
  return true;
}

static void
print_report (const fs::path& root, const fs::path& database,
               bool dry_run, const std::vector<pending_update>& updates,
               const std::vector<std::string>& unresolved,
               const std::vector<mapping_entry>& mappings,
               size_t legacy_count, size_t already_applied,
               const std::vector<mapping_entry*>& deleted_sources,
               const fs::path& backup= {}) {
  std::cout << "ATHENA namespace sorter migration"
            << (dry_run ? " (dry-run)" : "") << "\n"
            << "  vault: " << root << "\n"
            << "  namespace database: " << database << "\n"
            << "  legacy C references: " << legacy_count << "\n"
            << "  updates: " << updates.size () << "\n"
            << "  already applied: " << already_applied << "\n"
            << "  unresolved: " << unresolved.size () << "\n";
  if (!backup.empty ()) std::cout << "  backup: " << backup << "\n";
  for (const pending_update& update: updates)
    std::cout << "  " << (dry_run ? "would update " : "updated ")
              << update.row.name << ": " << update.row.sorter_path
              << " -> " << update.mapping->to_stored << "\n";
  std::set<std::string> reported_sources;
  for (const mapping_entry* mapping: deleted_sources) {
    std::string source= mapping->from_resolved.generic_string ();
    if (!reported_sources.insert (source).second) continue;
    std::cout << "  " << (dry_run ? "would delete legacy source: " :
                                      "deleted legacy source: ")
              << source << "\n";
  }
  for (const std::string& item: unresolved)
    std::cout << "  unresolved: " << item << "\n";
  for (const mapping_entry& mapping: mappings)
    if (!mapping.used && !mapping.already_applied)
      std::cout << "  unused mapping: " << mapping.from_text
                << " -> " << mapping.to_text << "\n";
}

} // namespace

int
athena_upgrade_vault_sorters_cli (
  const std::filesystem::path& vault_root_input,
  const std::optional<std::string>& inline_map,
  const std::optional<std::filesystem::path>& map_file,
  bool dry_run) {
  namespace fs= std::filesystem;
  if (inline_map.has_value () == map_file.has_value ()) {
    std::cerr << "Exactly one of --sorter-map or --sorter-map-file is required.\n";
    return 2;
  }

  std::error_code ec;
  fs::path root= fs::weakly_canonical (vault_root_input, ec);
  if (ec || !fs::is_directory (root)) {
    std::cerr << "Vault directory is not accessible: " << vault_root_input << "\n";
    return 2;
  }

  try {
    athena::filesystem::vault_directory_lease lease (root, true);

    AthenaVaultfileInfo vaultfile;
    std::string error;
    if (!athena_vaultfile_read (root, vaultfile, error)) {
      std::cerr << "Cannot read Vaultfile.json: " << error << "\n";
      return 2;
    }
    fs::path database= fs::path (vaultfile.namespace_db_path);
    if (!database.is_absolute ()) database= root / database;
    database= database.lexically_normal ();
    if (!fs::is_regular_file (database)) {
      std::cerr << "Namespace database does not exist: " << database << "\n";
      return 2;
    }

    std::string map_bytes;
    if (inline_map) map_bytes= *inline_map;
    else if (!read_bytes (*map_file, map_bytes, error)) {
      std::cerr << error << "\n";
      return 2;
    }
    std::vector<mapping_entry> mappings;
    if (!parse_mapping_json (root, map_bytes, mappings, error)) {
      std::cerr << error << "\n";
      return 2;
    }

    fs::path journal_path= root / ".athena" / "namespace-sorter-migration.json";
    if (!dry_run && !recover_journal_if_needed (
          journal_path, database, error)) {
      std::cerr << "Namespace sorter migration recovery failed: " << error << "\n";
      return 2;
    }
    if (dry_run && fs::exists (journal_path)) {
      std::cerr << "An interrupted namespace sorter migration journal exists; "
                   "run the non-dry migration to recover it before dry-run.\n";
      return 2;
    }

    db_ptr db (nullptr, sqlite3_close);
    if (!open_database (database, SQLITE_OPEN_READONLY, db, error)) {
      std::cerr << "Cannot open namespace database: " << error << "\n";
      return 2;
    }
    std::vector<namespace_row> rows;
    if (!read_namespace_rows (db.get (), rows, error)) {
      std::cerr << "Cannot read namespace sorter references: " << error << "\n";
      return 2;
    }
    db.reset ();

    std::map<std::string, mapping_entry*> mapping_by_from;
    std::multimap<std::string, mapping_entry*> mapping_by_to;
    for (mapping_entry& mapping: mappings) {
      mapping_by_from[mapping.from_resolved.generic_string ()]= &mapping;
      mapping_by_to.emplace (mapping.to_resolved.generic_string (), &mapping);
    }

    std::vector<pending_update> updates;
    std::vector<std::string> unresolved;
    size_t legacy_count= 0;
    size_t already_applied= 0;
    for (const namespace_row& row: rows) {
      fs::path resolved= resolve_database_sorter_path (root, row.sorter_path);
      std::string resolved_key= resolved.generic_string ();
      if (resolved.extension () == ".c") {
        ++legacy_count;
        auto found= mapping_by_from.find (resolved_key);
        if (found == mapping_by_from.end ()) {
          unresolved.push_back (
            row.name + " [" + row.uuid + "] -> " + row.sorter_path);
          continue;
        }
        found->second->used= true;
        updates.push_back ({row, found->second});
      }
      else {
        auto range= mapping_by_to.equal_range (resolved_key);
        for (auto it= range.first; it != range.second; ++it) {
          if (!it->second->already_applied) {
            it->second->already_applied= true;
            ++already_applied;
          }
        }
      }
    }

    std::vector<mapping_entry*> legacy_sources;
    for (mapping_entry& mapping: mappings) {
      std::error_code source_error;
      if ((mapping.used || mapping.already_applied) &&
          mapping.from_resolved.extension () == ".c" &&
          resolved_path_is_within_root (root, mapping.from_resolved) &&
          fs::exists (mapping.from_resolved, source_error))
        legacy_sources.push_back (&mapping);
      if (source_error) {
        std::cerr << "Cannot inspect legacy sorter source "
                  << mapping.from_resolved << ": " << source_error.message ()
                  << "\n";
        return 2;
      }
    }

    std::map<std::string, uint64_t> target_hashes;
    for (const pending_update& update: updates)
      if (!validate_replacement (update.row, *update.mapping,
                                 target_hashes, error)) {
        std::cerr << error << "\n";
        return 2;
      }

    if (!unresolved.empty ()) {
      print_report (root, database, dry_run, updates, unresolved, mappings,
                    legacy_count, already_applied, {});
      std::cerr << "Migration aborted: every legacy .c sorter reference requires "
                   "an explicit mapping.\n";
      return 3;
    }

    if (dry_run) {
      print_report (root, database, true, updates, unresolved, mappings,
                    legacy_count, already_applied, legacy_sources);
      return 0;
    }

    if (updates.empty ()) {
      QJsonArray source_plan= journal_legacy_sources (root, legacy_sources, error);
      if (!error.empty ()) {
        std::cerr << error << "\n";
        return 2;
      }
      if (!delete_legacy_sources (source_plan, error)) {
        std::cerr << error << "\n";
        return 2;
      }
      print_report (root, database, false, updates, unresolved, mappings,
                    legacy_count, already_applied, legacy_sources);
      return 0;
    }

    fs::path migration_dir= root / ".athena" / "migrations";
    fs::create_directories (migration_dir, ec);
    if (ec) {
      std::cerr << "Cannot create migration backup directory: "
                << ec.message () << "\n";
      return 2;
    }
    if (!fsync_directory (migration_dir.parent_path (), error)) {
      std::cerr << error << "\n";
      return 2;
    }

    db_ptr write_db (nullptr, sqlite3_close);
    if (!open_database (database, SQLITE_OPEN_READWRITE, write_db, error) ||
        !sqlite_exec (write_db.get (), "BEGIN IMMEDIATE;", error)) {
      std::cerr << "Cannot lock namespace database for migration: " << error << "\n";
      return 2;
    }

    if (!unchanged_file_hashes (target_hashes, error)) {
      sqlite3_exec (write_db.get (), "ROLLBACK;", nullptr, nullptr, nullptr);
      std::cerr << error << "\n";
      return 2;
    }

    std::string stamp= QDateTime::currentDateTimeUtc ()
      .toString ("yyyyMMdd-HHmmss-zzz").toStdString ();
    fs::path backup= migration_dir /
      ("namespace-sorters-" + stamp + ".sqlite.bak");
    db_ptr backup_source (nullptr, sqlite3_close);
    if (!open_database (database, SQLITE_OPEN_READONLY, backup_source, error) ||
        !sqlite_backup_to (backup_source.get (), backup, error)) {
      sqlite3_exec (write_db.get (), "ROLLBACK;", nullptr, nullptr, nullptr);
      std::cerr << "Cannot create namespace database backup: " << error << "\n";
      return 2;
    }
    backup_source.reset ();
    if (!fsync_directory (migration_dir, error)) {
      sqlite3_exec (write_db.get (), "ROLLBACK;", nullptr, nullptr, nullptr);
      std::cerr << error << "\n";
      return 2;
    }

    fs::create_directories (journal_path.parent_path (), ec);
    if (ec) {
      sqlite3_exec (write_db.get (), "ROLLBACK;", nullptr, nullptr, nullptr);
      std::cerr << "Cannot create migration journal directory: " << ec.message () << "\n";
      return 2;
    }
    QJsonArray source_plan= journal_legacy_sources (root, legacy_sources, error);
    if (!error.empty ()) {
      sqlite3_exec (write_db.get (), "ROLLBACK;", nullptr, nullptr, nullptr);
      std::cerr << error << "\n";
      return 2;
    }
    QJsonObject journal {
      {"format", "athena-namespace-sorter-migration"},
      {"version", 1},
      {"state", "prepared"},
      {"database", QString::fromStdString (database.string ())},
      {"backup", QString::fromStdString (backup.string ())},
      {"updates", journal_updates (updates)},
      {"legacy_sources", source_plan}
    };
    if (!write_json_atomic (journal_path, journal, error)) {
      sqlite3_exec (write_db.get (), "ROLLBACK;", nullptr, nullptr, nullptr);
      std::cerr << error << "\n";
      return 2;
    }

    auto update_stmt= prepare (
      write_db.get (),
      "UPDATE namespaces SET sorter_path=? WHERE uuid=? AND sorter_path=?;",
      error);
    if (!update_stmt) {
      sqlite3_exec (write_db.get (), "ROLLBACK;", nullptr, nullptr, nullptr);
      std::cerr << error << "\n";
      return 2;
    }
    for (const pending_update& update: updates) {
      sqlite3_reset (update_stmt.get ());
      sqlite3_clear_bindings (update_stmt.get ());
      if (sqlite3_bind_text (update_stmt.get (), 1,
                             update.mapping->to_stored.c_str (), -1,
                             SQLITE_TRANSIENT) != SQLITE_OK ||
          sqlite3_bind_text (update_stmt.get (), 2, update.row.uuid.c_str (), -1,
                             SQLITE_TRANSIENT) != SQLITE_OK ||
          sqlite3_bind_text (update_stmt.get (), 3,
                             update.row.sorter_path.c_str (), -1,
                             SQLITE_TRANSIENT) != SQLITE_OK ||
          sqlite3_step (update_stmt.get ()) != SQLITE_DONE ||
          sqlite3_changes (write_db.get ()) != 1) {
        error= "Namespace database changed during migration at '" +
               update.row.name + "'.";
        sqlite3_exec (write_db.get (), "ROLLBACK;", nullptr, nullptr, nullptr);
        std::cerr << error << "\n";
        return 2;
      }
    }
    if (!sqlite_exec (write_db.get (), "COMMIT;", error)) {
      sqlite3_exec (write_db.get (), "ROLLBACK;", nullptr, nullptr, nullptr);
      std::cerr << "Cannot commit namespace sorter migration: " << error << "\n";
      return 2;
    }
    write_db.reset ();

    journal["state"]= "committed";
    if (!write_json_atomic (journal_path, journal, error) ||
        !delete_legacy_sources (
          journal.value ("legacy_sources").toArray (), error) ||
        !remove_journal (journal_path, error)) {
      std::cerr << "Migration committed but journal finalization failed: "
                << error << "\n"
                << "Re-run the same migration command to finalize recovery.\n";
      return 4;
    }

    print_report (root, database, false, updates, unresolved, mappings,
                  legacy_count, already_applied, legacy_sources, backup);
    return 0;
  }
  catch (const std::system_error& exception) {
    std::cerr << "Cannot acquire exclusive vault migration lease: "
              << exception.what () << "\n";
    return 2;
  }
}
