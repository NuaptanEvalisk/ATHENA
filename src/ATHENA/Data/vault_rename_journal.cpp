/******************************************************************************
* MODULE     : vault_rename_journal.cpp
* DESCRIPTION: Synchronous SQLite recovery journal for vault filesystem renames
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#include "vault_rename_journal.hpp"
#include <sqlite3.h>
#include <algorithm>

namespace {
struct Statement {
  sqlite3_stmt* value= nullptr;
  ~Statement () { if (value) sqlite3_finalize (value); }
};
bool sql (sqlite3* db, const char* query, std::string& error) {
  if (sqlite3_exec (db, query, nullptr, nullptr, nullptr) == SQLITE_OK) return true;
  error= sqlite3_errmsg (db); return false;
}
bool prepare (sqlite3* db, Statement& st, const char* query, std::string& error) {
  if (sqlite3_prepare_v2 (db, query, -1, &st.value, nullptr) == SQLITE_OK) return true;
  error= sqlite3_errmsg (db); return false;
}
bool bind (Statement& st, int i, const std::string& text) {
  return sqlite3_bind_text (st.value, i, text.data (), (int) text.size (), SQLITE_TRANSIENT) == SQLITE_OK;
}
std::string text (Statement& st, int column) {
  const auto* value= sqlite3_column_text (st.value, column);
  return value ? std::string ((const char*) value, sqlite3_column_bytes (st.value, column)) : "";
}
bool valid (const VaultRenameOperation& op) {
  const auto relative= [] (const std::string& value) {
    std::filesystem::path path (value);
    if (value.empty () || value.find ('\0') != std::string::npos || path.is_absolute ()) return false;
    if (*path.begin () == ".athena" || *path.begin () == ".backup" ||
        *path.begin () == ".git" || path == "Vaultfile.json") return false;
    for (const auto& part: path) if (part == ".." || part == ".") return false;
    return true;
  };
  return !op.operation_id.empty () &&
    std::all_of (op.operation_id.begin (), op.operation_id.end (), [] (char c) {
      return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
             (c >= 'A' && c <= 'Z') || c == '-' || c == '_';
    }) && relative (op.old_path) && relative (op.new_path) && op.old_path != op.new_path &&
    std::filesystem::path (op.old_path).parent_path () == std::filesystem::path (op.new_path).parent_path ();
}
}
VaultRenameJournal::~VaultRenameJournal () { if (db) sqlite3_close (db); }
bool VaultRenameJournal::open (const std::filesystem::path& root, std::string& error) {
  namespace fs= std::filesystem;
  std::error_code ec;
  const auto directory= root / ".athena";
  const auto file= directory / "safe-rename.sqlite";
  if (fs::is_symlink (directory, ec) || fs::is_symlink (file, ec)) {
    error= "Safe rename journal cannot be a symbolic link"; return false;
  }
  fs::create_directories (directory, ec);
  if (ec) { error= ec.message (); return false; }
  if (db) { sqlite3_close (db); db= nullptr; }
  if (sqlite3_open_v2 (file.string ().c_str (), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK) {
    error= db ? sqlite3_errmsg (db) : "Cannot open safe rename journal"; return false;
  }
  sqlite3_busy_timeout (db, 5000);
  Statement version;
  if (!::prepare (db, version, "PRAGMA user_version;", error)) return false;
  if (sqlite3_step (version.value) != SQLITE_ROW || sqlite3_column_int (version.value, 0) > 1) {
    error= "Unsupported safe rename journal version"; return false;
  }
  sqlite3_finalize (version.value); version.value= nullptr;
  if (!sql (db, "PRAGMA synchronous=FULL;"
      "CREATE TABLE IF NOT EXISTS operations("
      "operation_id TEXT PRIMARY KEY,old_path TEXT NOT NULL,new_path TEXT NOT NULL,"
      "is_directory INTEGER NOT NULL,phase TEXT NOT NULL);"
      "PRAGMA user_version=1;", error)) return false;
  Statement check;
  if (!::prepare (db, check, "PRAGMA quick_check;", error)) return false;
  if (sqlite3_step (check.value) != SQLITE_ROW || text (check, 0) != "ok") {
    error= "Safe rename recovery journal is damaged"; return false;
  }
  return true;
}
bool VaultRenameJournal::prepare (const VaultRenameOperation& op, std::string& error) {
  if (!valid (op)) { error= "Invalid safe rename journal operation"; return false; }
  Statement st;
  if (!::prepare (db, st, "INSERT INTO operations VALUES(?1,?2,?3,?4,?5) "
      "ON CONFLICT(operation_id) DO NOTHING;", error)) return false;
  if (!bind (st, 1, op.operation_id) || !bind (st, 2, op.old_path) || !bind (st, 3, op.new_path) ||
      sqlite3_bind_int (st.value, 4, op.is_directory) != SQLITE_OK || !bind (st, 5, op.phase) ||
      sqlite3_step (st.value) != SQLITE_DONE) { error= sqlite3_errmsg (db); return false; }
  std::vector<VaultRenameOperation> operations;
  if (!pending (operations, error)) return false;
  for (const auto& stored: operations) if (stored.operation_id == op.operation_id) {
    if (stored.old_path == op.old_path && stored.new_path == op.new_path &&
        stored.is_directory == op.is_directory) return true;
    error= "Conflicting safe rename journal operation"; return false;
  }
  error= "Missing safe rename journal operation"; return false;
}
bool VaultRenameJournal::pending (std::vector<VaultRenameOperation>& operations, std::string& error) {
  operations.clear ();
  Statement st;
  if (!::prepare (db, st, "SELECT operation_id,old_path,new_path,is_directory,phase FROM operations ORDER BY rowid;", error)) return false;
  int status;
  while ((status= sqlite3_step (st.value)) == SQLITE_ROW) {
    VaultRenameOperation op {text (st, 0), text (st, 1), text (st, 2),
      sqlite3_column_int (st.value, 3) != 0, text (st, 4)};
    if (!valid (op)) { error= "Invalid stored safe rename operation"; return false; }
    operations.push_back (std::move (op));
  }
  if (status == SQLITE_DONE) return true;
  error= sqlite3_errmsg (db); return false;
}
bool VaultRenameJournal::finish (const std::string& id, std::string& error) {
  Statement st;
  if (!::prepare (db, st, "DELETE FROM operations WHERE operation_id=?1;", error)) return false;
  if (bind (st, 1, id) && sqlite3_step (st.value) == SQLITE_DONE) return true;
  error= sqlite3_errmsg (db); return false;
}
