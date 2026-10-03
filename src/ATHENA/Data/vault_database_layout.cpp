/******************************************************************************
* MODULE     : vault_database_layout.cpp
* DESCRIPTION: Journaled SQLite relocation before opening a vault's databases
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#include "vault_database_layout.hpp"
#include "vault_directory_lease.hpp"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <sqlite3.h>
#include <fcntl.h>
#include <unistd.h>
#include <memory>
#include <set>
#include <stdexcept>
#include <system_error>

namespace fs= std::filesystem;
namespace {
constexpr const char* journal_name= ".athena/database-layout-migration.json";
struct database_path { const char* key; const char* old_default; const char* canonical; };
constexpr database_path paths[]= {
  {"namespace_db_path", "ns.sqlite", ".athena/namespaces.sqlite"},
  {"artifacts_path", "artifacts.db", ".athena/artifacts.sqlite"},
  {"enunciations_path", "enunciations.db", ".athena/enunciations.sqlite"},
  {"bold_text_path", "bold-text.db", ".athena/bold-text.sqlite"},
  {"materials_db_path", "materials.sqlite", ".athena/materials.sqlite"},
  {"rag_index_path", "rag.sqlite", ".athena/rag.sqlite"}
};
QString qs (const fs::path& p) { return QString::fromStdString (p.string ()); }
QJsonObject read_json (const fs::path& p) {
  QFile f (qs (p));
  if (!f.open (QIODevice::ReadOnly)) throw std::runtime_error ("Cannot read " + p.string ());
  QJsonParseError error;
  auto doc= QJsonDocument::fromJson (f.readAll (), &error);
  if (error.error != QJsonParseError::NoError || !doc.isObject ())
    throw std::runtime_error ("Invalid JSON object: " + p.string ());
  return doc.object ();
}
void sync_directory (const fs::path& p) {
  const int fd= ::open (p.c_str (), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  if (fd < 0) throw std::system_error (errno, std::generic_category (), "Open " + p.string ());
  const int result= ::fsync (fd), error= errno;
  ::close (fd);
  if (result != 0) throw std::system_error (error, std::generic_category (), "Sync " + p.string ());
}
void write_json (const fs::path& p, const QJsonObject& obj) {
  QSaveFile out (qs (p));
  const auto bytes= QJsonDocument (obj).toJson (QJsonDocument::Indented);
  if (!out.open (QIODevice::WriteOnly) || out.write (bytes) != bytes.size () || !out.commit ())
    throw std::runtime_error ("Cannot atomically write " + p.string ());
  sync_directory (p.parent_path ());
}
fs::path local_path (const fs::path& root, const QString& name) {
  fs::path relative (name.toStdString ()), current= root;
  if (relative.empty () || relative.is_absolute ()) throw std::runtime_error ("Expected vault-relative database path");
  for (const auto& part: relative) {
    if (part == ".." || part == ".") throw std::runtime_error ("Invalid vault-relative database path");
    current/= part;
    if (fs::is_symlink (fs::symlink_status (current)))
      throw std::runtime_error ("Database relocation does not follow symlinks: " + current.string ());
  }
  return current;
}
void checkpoint (const fs::path& p) {
  if (fs::file_size (p) == 0) return;
  sqlite3* raw= nullptr;
  const int opened= sqlite3_open_v2 (p.c_str (), &raw, SQLITE_OPEN_READWRITE, nullptr);
  std::unique_ptr<sqlite3, decltype (&sqlite3_close)> db (raw, sqlite3_close);
  if (opened != SQLITE_OK) throw std::runtime_error ("Cannot open database " + p.string ());
  if (sqlite3_exec (raw, "PRAGMA locking_mode=EXCLUSIVE; BEGIN EXCLUSIVE; COMMIT;", nullptr, nullptr, nullptr) != SQLITE_OK ||
      sqlite3_wal_checkpoint_v2 (raw, nullptr, SQLITE_CHECKPOINT_TRUNCATE, nullptr, nullptr) != SQLITE_OK)
    throw std::runtime_error ("Cannot checkpoint " + p.string () + ": " + sqlite3_errmsg (raw));
}
void remove_sidecars (const fs::path& p) {
  for (const char* suffix: {"-wal", "-shm", "-journal"}) fs::remove (p.string () + suffix);
}
}

bool athena_vault_database_layout_pending (const fs::path& root) {
  return fs::exists (root / journal_name);
}

bool athena_vault_canonical_database_preference (const fs::path& root,
    bool fallback, bool use_vault_preferences) {
  if (!use_vault_preferences) return fallback;
  auto info= read_json (root / "Vaultfile.json");
  auto name= info.value ("preferences_path").toString ();
  if (name.isEmpty ()) name= "vprefs.json";
  const auto p= local_path (root, name);
  if (!fs::exists (p)) return fallback;
  const auto pref= read_json (p).value ("preferences").toObject ()
    .value ("vault canonical database positions");
  return pref.isString () ? pref.toString () == "on" : fallback;
}

bool athena_vault_canonicalize_databases (const fs::path& root,
    bool enabled, std::string& error) {
  try {
    const bool recovery= athena_vault_database_layout_pending (root);
    if (!enabled && !recovery) return true;
    auto before= read_json (root / "Vaultfile.json"), after= before;
    bool needed= recovery;
    for (const auto& p: paths)
      needed|= before.value (p.key).toString () != p.canonical;
    if (!needed) return true;
    athena::filesystem::vault_directory_lease exclusive (root, true);
    before= read_json (root / "Vaultfile.json");
    after= before;
    local_path (root, journal_name);
    QJsonArray moves;
    if (recovery) {
      const auto journal= read_json (root / journal_name);
      if (journal.value ("version").toInt () != 1 || !journal.value ("moves").isArray () ||
          !journal.value ("before").isObject () || !journal.value ("after").isObject ())
        throw std::runtime_error ("Invalid database layout recovery journal");
      after= journal.value ("after").toObject ();
      const auto original= journal.value ("before").toObject ();
      if (before != original && before != after)
        throw std::runtime_error ("Vaultfile.json changed during database relocation");
      moves= journal.value ("moves").toArray ();
    }
    else {
      std::set<fs::path> sources, targets;
      for (const auto& p: paths) {
        QString old= before.value (p.key).toString ();
        if (old.isEmpty ()) old= p.old_default;
        const auto source= local_path (root, old), target= local_path (root, p.canonical);
        if (!sources.insert (source).second || !targets.insert (target).second)
          throw std::runtime_error ("Database paths overlap");
        after[p.key]= p.canonical;
        if (source == target) continue;
        if (fs::exists (target) && (!fs::is_regular_file (target) || fs::file_size (target) != 0))
          throw std::runtime_error ("Canonical database already exists: " + target.string ());
        for (const char* suffix: {"-wal", "-shm", "-journal"})
          if (fs::exists (target.string () + suffix))
            throw std::runtime_error ("Canonical database has existing journal files: " + target.string ());
        if (fs::exists (source)) {
          if (!fs::is_regular_file (source)) throw std::runtime_error ("Not a database file: " + source.string ());
          moves.append (QJsonObject {{"source", old}, {"target", p.canonical}});
        }
        else {
          for (const char* suffix: {"-wal", "-shm", "-journal"})
            if (fs::exists (source.string () + suffix))
              throw std::runtime_error ("Missing database has journal files: " + source.string ());
        }
      }
      for (const auto& move: moves) {
        auto m= move.toObject ();
        if (sources.count (local_path (root, m.value ("target").toString ())))
          throw std::runtime_error ("A canonical target is another database's source");
      }
      // Flush committed WAL pages before publishing any rename intent.
      for (const auto& move: moves) checkpoint (local_path (root, move.toObject ().value ("source").toString ()));
      fs::create_directories (root / ".athena");
      sync_directory (root);
      write_json (root / journal_name, {{"version", 1}, {"before", before}, {"after", after}, {"moves", moves}});
    }
    for (const auto& move: moves) {
      const auto m= move.toObject ();
      const auto source= local_path (root, m.value ("source").toString ());
      const auto target= local_path (root, m.value ("target").toString ());
      if (fs::exists (source)) {
        if (fs::exists (target) && fs::file_size (target) != 0)
          throw std::runtime_error ("Database relocation conflict: " + target.string ());
        // A previous process may have died after journaling but before renaming.
        if (recovery) checkpoint (source);
        fs::rename (source, target);
        sync_directory (source.parent_path ());
        sync_directory (target.parent_path ());
      }
      else if (!fs::is_regular_file (target))
        throw std::runtime_error ("Both database relocation paths are missing");
      remove_sidecars (source);
      sync_directory (source.parent_path ());
    }
    if (read_json (root / "Vaultfile.json") != before)
      throw std::runtime_error ("Vaultfile.json changed before database relocation commit");
    write_json (root / "Vaultfile.json", after);
    fs::remove (root / journal_name);
    sync_directory (root / ".athena");
    return true;
  }
  catch (const std::exception& e) { error= e.what (); return false; }
}
