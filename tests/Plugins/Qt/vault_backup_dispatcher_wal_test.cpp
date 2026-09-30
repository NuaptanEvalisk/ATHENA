/******************************************************************************
* MODULE     : vault_backup_dispatcher_wal_test.cpp
* DESCRIPTION: Isolated live-WAL regression for vault backup dispatch
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "ATHENA/Data/vault_backup_dispatcher.hpp"
#include "ATHENA/Data/vaultfile_json.hpp"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <sqlite3.h>
#include <atomic>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace fs= std::filesystem;

static void require (bool ok, const std::string& error) {
  if (!ok) throw std::runtime_error (error);
}

static void sql (sqlite3* db, const char* text) {
  require (sqlite3_exec (db, text, nullptr, nullptr, nullptr) == SQLITE_OK,
           sqlite3_errmsg (db));
}

int main (int argc, char** argv) {
  QCoreApplication app (argc, argv);
  try {
    QTemporaryDir temporary;
    require (temporary.isValid (), "temporary directory");
    fs::path root= fs::path (temporary.path ().toStdString ()) / "vault";
    fs::path target= root.parent_path () / "mirror";
    fs::create_directories (root / "indexes");
    fs::create_directories (target);
    AthenaVaultfileInfo info;
    info.rag_index_path= "indexes/rag[1].store";
    std::string error;
    require (athena_vaultfile_write (root, info, error), error);
    fs::path source= root / info.rag_index_path;
    sqlite3* db= nullptr;
    require (sqlite3_open (source.c_str (), &db) == SQLITE_OK, "open source");
    sql (db, "PRAGMA journal_mode=WAL; PRAGMA wal_autocheckpoint=0;"
             "CREATE TABLE data(value); INSERT INTO data VALUES(12345)");
    require (fs::file_size (source.string () + "-wal") > 0, "uncheckpointed WAL");
    fs::create_directories (target / "indexes");
    std::ofstream (target / (info.rag_index_path + "-shm")) << "stale shm";
    std::ofstream (target / (info.rag_index_path + "-wal")) << "stale wal";
    std::ofstream (root / "ordinary.db") << "not SQLite";
    std::ofstream (target / "removed.sqlite") << "obsolete database";

    std::atomic<bool> stop {false}, writer_failed {false};
    std::thread writer ([&] {
      while (!stop.load ()) {
        if (sqlite3_exec (db, "INSERT INTO data VALUES(7)", nullptr, nullptr,
                         nullptr) != SQLITE_OK) writer_failed= true;
        std::this_thread::sleep_for (std::chrono::milliseconds (2));
      }
    });
    bool ok= athena_backup_dispatch_run (root, target.string (), error);
    stop= true;
    writer.join ();
    sqlite3_close (db);
    require (ok, error);
    require (!writer_failed, "backup blocked writer");
    require (!fs::exists (target / "removed.sqlite"), "stale database retained");
    require (fs::file_size (target / "ordinary.db") == 10, "non-SQLite .db lost");
    for (const char* suffix: {"-shm", "-wal", "-journal"})
      require (!fs::exists (target / (info.rag_index_path + suffix)), "sidecar copied");
    require (sqlite3_open_v2 ((target / info.rag_index_path).c_str (), &db,
                             SQLITE_OPEN_READONLY, nullptr) == SQLITE_OK, "open backup");
    sqlite3_stmt* statement= nullptr;
    require (sqlite3_prepare_v2 (db, "SELECT value FROM data WHERE value=12345", -1,
                                 &statement, nullptr) == SQLITE_OK, "prepare backup");
    require (sqlite3_step (statement) == SQLITE_ROW, "committed WAL row missing");
    sqlite3_finalize (statement);
    sqlite3_prepare_v2 (db, "PRAGMA integrity_check", -1, &statement, nullptr);
    require (sqlite3_step (statement) == SQLITE_ROW &&
             std::string ((const char*) sqlite3_column_text (statement, 0)) == "ok",
             "backup integrity");
    sqlite3_finalize (statement);
    sqlite3_close (db);
    std::cout << "PASS: concurrent WAL, custom path, sidecar cleanup, stale deletion, integrity\n";
  }
  catch (const std::exception& ex) {
    std::cerr << ex.what () << '\n';
    return 1;
  }
}
