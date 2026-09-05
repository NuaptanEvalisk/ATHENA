/******************************************************************************
* MODULE     : namespace_ontology_test.cpp
* DESCRIPTION: Tests for the incremental namespace ontology cache
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
*******************************************************************************/

#include <QtTest/QtTest>
#include <QTemporaryDir>

#include "namespace_ontology.hpp"
#include "namespaces.hpp"
#include "vault.hpp"
#include "vaultfile_json.hpp"

#include <sqlite3.h>

#include <filesystem>
#include <fstream>

bool headless_mode= true;
bool is_headless () { return true; }

namespace fs= std::filesystem;

class NamespaceOntologyTest: public QObject {
  Q_OBJECT

private slots:
  void incrementallyMaintainsMembers ();
};

namespace {

void
write_document (const fs::path& path) {
  fs::create_directories (path.parent_path ());
  std::ofstream output (path, std::ios::binary | std::ios::trunc);
  output << "<TeXmacs|2.1.4>\n<style|generic>\n<\\body>test</body>\n";
}

int
query_count (const fs::path& database, const char* sql) {
  sqlite3* db= nullptr;
  if (sqlite3_open_v2 (database.c_str (), &db, SQLITE_OPEN_READONLY,
                       nullptr) != SQLITE_OK)
    return -1;
  sqlite3_stmt* statement= nullptr;
  int result= -1;
  if (sqlite3_prepare_v2 (db, sql, -1, &statement, nullptr) == SQLITE_OK &&
      sqlite3_step (statement) == SQLITE_ROW)
    result= sqlite3_column_int (statement, 0);
  if (statement != nullptr) sqlite3_finalize (statement);
  sqlite3_close (db);
  return result;
}

bool
execute_sql (const fs::path& database, const char* sql) {
  sqlite3* db= nullptr;
  if (sqlite3_open_v2 (database.c_str (), &db, SQLITE_OPEN_READWRITE,
                       nullptr) != SQLITE_OK)
    return false;
  bool ok= sqlite3_exec (db, sql, nullptr, nullptr, nullptr) == SQLITE_OK;
  sqlite3_close (db);
  return ok;
}

} // namespace

void
NamespaceOntologyTest::incrementallyMaintainsMembers () {
  QTemporaryDir temporary;
  QVERIFY (temporary.isValid ());
  fs::path root= fs::u8path (temporary.path ().toStdString ());
  std::string error;
  QVERIFY2 (athena_vaultfile_write (root, AthenaVaultfileInfo {}, error),
            error.c_str ());
  write_document (root / "Notes" / "Note Alpha.ath");
  write_document (root / "Unmatched.ath");

  string load_error= vault_load (
    url_system (string (root.string ().c_str ())), "Ontology test",
    "map.sqlite", "ns.sqlite");
  QVERIFY2 (load_error == "", as_charp (load_error));

  athena_namespace_definition notes;
  notes.name= "Notes";
  notes.kind= "concrete";
  notes.templ= "Note %s";
  notes.sorter_trivial= true;
  string tm_error;
  athena_namespace_definition universe;
  universe.name= "Universe";
  universe.kind= "abstract";
  universe.templ= "";
  universe.sorter_trivial= true;
  QVERIFY2 (athena_namespace_save (universe, tm_error), as_charp (tm_error));
  notes.parents << string ("Universe");
  QVERIFY2 (athena_namespace_save (notes, tm_error), as_charp (tm_error));
  athena_namespace_definition special;
  special.name= "Special Notes";
  special.kind= "concrete";
  special.templ= "Special %s";
  special.sorter_trivial= true;
  special.parents << string ("Universe") << string ("Notes");
  QVERIFY2 (athena_namespace_save (special, tm_error), as_charp (tm_error));
  QVERIFY2 (athena_namespace_ontology_refresh (true, tm_error),
            as_charp (tm_error));

  array<string> visible;
  array<string> folded;
  QVERIFY2 (athena_namespace_ontology_children (
              "Universe", false, visible, folded, tm_error),
            as_charp (tm_error));
  QCOMPARE (N(visible), 2);
  QVERIFY (visible[0] == "Notes" || visible[1] == "Notes");
  QVERIFY (visible[0] == "Special Notes" || visible[1] == "Special Notes");
  QVERIFY2 (athena_namespace_ontology_children (
              "Universe", true, visible, folded, tm_error),
            as_charp (tm_error));
  QCOMPARE (N(visible), 1);
  QCOMPARE (visible[0], string ("Notes"));
  QCOMPARE (N(folded), 1);
  QCOMPARE (folded[0], string ("Special Notes"));

  std::vector<athena_namespace_match> members=
    athena_namespace_members ("Notes", tm_error);
  QCOMPARE (members.size (), (size_t) 1);
  QCOMPARE (members[0].stem, string ("Note Alpha"));
  fs::path cache= root / ".athena" / "namespace-ontology.sqlite";
  QCOMPARE (query_count (cache,
                         "SELECT count(*) FROM namespace_cache_files;"), 2);
  QCOMPARE (query_count (cache,
                         "SELECT count(*) FROM namespace_cache_matches;"), 1);
  QVERIFY (execute_sql (
    cache,
    "CREATE TABLE test_match_deletions(count INTEGER NOT NULL);"
    "INSERT INTO test_match_deletions VALUES(0);"
    "CREATE TRIGGER test_match_delete AFTER DELETE ON namespace_cache_matches "
    "BEGIN UPDATE test_match_deletions SET count=count+1; END;"
    "CREATE TABLE test_hierarchy_deletions(count INTEGER NOT NULL);"
    "INSERT INTO test_hierarchy_deletions VALUES(0);"
    "CREATE TRIGGER test_hierarchy_delete AFTER DELETE "
    "ON namespace_cache_children "
    "BEGIN UPDATE test_hierarchy_deletions SET count=count+1; END;"));

  std::error_code timestamp_error;
  fs::file_time_type unchanged_cache_time=
    fs::last_write_time (cache, timestamp_error);
  QVERIFY (!timestamp_error);
  QVERIFY2 (athena_namespace_ontology_refresh (false, tm_error),
            as_charp (tm_error));
  QVERIFY (fs::last_write_time (cache, timestamp_error) ==
           unchanged_cache_time);
  QVERIFY (!timestamp_error);

  write_document (root / "Notes" / "Note Beta.ath");
  QVERIFY2 (athena_namespace_ontology_refresh (false, tm_error),
            as_charp (tm_error));
  members= athena_namespace_members ("Notes", tm_error);
  QCOMPARE (members.size (), (size_t) 2);
  QCOMPARE (query_count (cache, "SELECT count FROM test_match_deletions;"), 0);

  QVERIFY (fs::remove (root / "Notes" / "Note Alpha.ath"));
  QVERIFY2 (athena_namespace_ontology_refresh (false, tm_error),
            as_charp (tm_error));
  members= athena_namespace_members ("Notes", tm_error);
  QCOMPARE (members.size (), (size_t) 1);
  QCOMPARE (members[0].stem, string ("Note Beta"));
  QCOMPARE (query_count (cache, "SELECT count FROM test_match_deletions;"), 1);

  write_document (root / "Notes" / "Note Gamma.ath");
  bool background_updated= false;
  for (int waited=0; waited<5000 && !background_updated; waited+=100) {
    QTest::qWait (100);
    members= athena_namespace_members ("Notes", tm_error);
    background_updated= members.size () == 2;
  }
  QVERIFY2 (background_updated,
            "The background ontology worker did not notice a new file");

  int deletions_before_restart=
    query_count (cache, "SELECT count FROM test_match_deletions;");
  int hierarchy_deletions_before_restart=
    query_count (cache, "SELECT count FROM test_hierarchy_deletions;");

  vault_close ();
  load_error= vault_load (
    url_system (string (root.string ().c_str ())), "Ontology test",
    "map.sqlite", "ns.sqlite");
  QVERIFY2 (load_error == "", as_charp (load_error));
  members= athena_namespace_members ("Notes", tm_error);
  QCOMPARE (members.size (), (size_t) 2);
  QCOMPARE (members[0].stem, string ("Note Beta"));
  QCOMPARE (members[1].stem, string ("Note Gamma"));
  QCOMPARE (query_count (cache, "SELECT count FROM test_match_deletions;"),
            deletions_before_restart);
  QCOMPARE (query_count (cache,
                         "SELECT count FROM test_hierarchy_deletions;"),
            hierarchy_deletions_before_restart);

  special.parents= array<string> ();
  special.parents << string ("Universe");
  QVERIFY2 (athena_namespace_save (special, tm_error), as_charp (tm_error));
  QVERIFY2 (athena_namespace_ontology_refresh (false, tm_error),
            as_charp (tm_error));
  QVERIFY2 (athena_namespace_ontology_children (
              "Universe", true, visible, folded, tm_error),
            as_charp (tm_error));
  QCOMPARE (N(visible), 2);
  QCOMPARE (N(folded), 0);
  QVERIFY (query_count (cache,
                        "SELECT count FROM test_hierarchy_deletions;") >
           hierarchy_deletions_before_restart);
  vault_close ();
}

QTEST_MAIN (NamespaceOntologyTest)
#include "namespace_ontology_test.moc"
