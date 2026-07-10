/******************************************************************************
* MODULE     : vault_map_sqlite_test.cpp
* DESCRIPTION: Tests for non-temporal SQLite Vault maps and TMDB migration
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
*******************************************************************************/

#include <QtTest/QtTest>
#include <QTemporaryDir>

#include "vault_map_sqlite.hpp"
#include "vaultfile_json.hpp"
#include "Database/database.hpp"
#include "tm_timer.hpp"
#include "url.hpp"

#include <filesystem>

class TestVaultMapSqlite: public QObject {
  Q_OBJECT

private slots:
  void crudAndReverseLookup ();
  void rewriteAnchorsTransactionally ();
  void migrateCurrentTmdbSnapshot ();
  void migrateRealMapCopy ();
};

void
TestVaultMapSqlite::crudAndReverseLookup () {
  QTemporaryDir temporary;
  QVERIFY (temporary.isValid ());
  std::filesystem::path path =
    std::filesystem::path (temporary.path ().toStdString ()) / "map.sqlite";
  AthenaVaultMapSqlite map;
  std::string error;
  QVERIFY2 (map.open (path, true, error), error.c_str ());
  QVERIFY2 (map.set_node ({"first", "数学/Note.ath", "", "定理"}, error),
            error.c_str ());
  QVERIFY2 (map.set_node ({"second", "数学/Note.ath", "", "定理"}, error),
            error.c_str ());

  AthenaVaultMapNode node;
  bool found = false;
  QVERIFY2 (map.get_node ("first", node, found, error), error.c_str ());
  QVERIFY (found);
  QCOMPARE (QString::fromStdString (node.path), QString::fromUtf8 ("数学/Note.ath"));

  std::string uuid;
  QVERIFY2 (map.find_uuid ("数学/Note.ath", "", "定理", uuid, error),
            error.c_str ());
  QCOMPARE (uuid, std::string ("first"));
  QVERIFY2 (map.remove_node ("first", error), error.c_str ());
  QVERIFY2 (map.has_node ("first", found, error), error.c_str ());
  QVERIFY (!found);
}

void
TestVaultMapSqlite::rewriteAnchorsTransactionally () {
  QTemporaryDir temporary;
  QVERIFY (temporary.isValid ());
  AthenaVaultMapSqlite map;
  std::string error;
  QVERIFY2 (map.open (
    std::filesystem::path (temporary.path ().toStdString ()) / "map.sqlite",
    true, error), error.c_str ());
  QVERIFY2 (map.set_node ({"range", "A.ath", "old", "old"}, error),
            error.c_str ());
  QVERIFY2 (map.set_node ({"other", "B.ath", "old", "old"}, error),
            error.c_str ());
  size_t changed = 0;
  QVERIFY2 (map.rewrite_anchors ("A.ath", {{"old", "new"}}, changed, error),
            error.c_str ());
  QCOMPARE (changed, (size_t) 2);
  AthenaVaultMapNode node;
  bool found = false;
  QVERIFY2 (map.get_node ("range", node, found, error), error.c_str ());
  QCOMPARE (node.anchor_begin, std::string ("new"));
  QCOMPARE (node.anchor_end, std::string ("new"));
  QVERIFY2 (map.get_node ("other", node, found, error), error.c_str ());
  QCOMPARE (node.anchor_begin, std::string ("old"));
}

void
TestVaultMapSqlite::migrateCurrentTmdbSnapshot () {
  QTemporaryDir temporary;
  QVERIFY (temporary.isValid ());
  std::filesystem::path root (temporary.path ().toStdString ());
  AthenaVaultfileInfo info;
  info.name = "Migration test";
  info.map_path = "map.tmdb";
  std::string error;
  QVERIFY2 (athena_vaultfile_write (root, info, error), error.c_str ());

  url legacy = url_system (string ((root / "map.tmdb").string ().c_str ()));
  db_time now = (db_time) (raw_time () / 1000);
  strings old_path; old_path << string ("Old/Test.ath");
  set_field (legacy, "uuid", "v-path", old_path, now - 10);
  strings path; path << string ("Folder/Test.ath");
  strings begin; begin << string ("begin");
  strings end; end << string ("end");
  set_field (legacy, "uuid", "v-path", path, now);
  set_field (legacy, "uuid", "v-anchor-begin", begin, now);
  set_field (legacy, "uuid", "v-anchor-end", end, now);
  sync_databases ();
  QVERIFY (std::filesystem::exists (root / "map.tmdb"));

  std::string resolved;
  QVERIFY2 (athena_vault_map_prepare (root, "map.tmdb", resolved, error),
            error.c_str ());
  QCOMPARE (resolved, std::string ("map.sqlite"));
  QVERIFY (std::filesystem::exists (root / "map.sqlite"));
  QVERIFY (std::filesystem::exists (root / "map.tmdb.old.bak"));
  QVERIFY (!std::filesystem::exists (root / "map.tmdb"));

  AthenaVaultfileInfo migrated_info;
  QVERIFY2 (athena_vaultfile_read (root, migrated_info, error), error.c_str ());
  QCOMPARE (migrated_info.map_path, std::string ("map.sqlite"));
  AthenaVaultMapSqlite map;
  QVERIFY2 (map.open (root / "map.sqlite", false, error), error.c_str ());
  AthenaVaultMapNode node;
  bool found = false;
  QVERIFY2 (map.get_node ("uuid", node, found, error), error.c_str ());
  QVERIFY (found);
  QCOMPARE (node.path, std::string ("Folder/Test.ath"));
  QCOMPARE (node.anchor_begin, std::string ("begin"));
  QCOMPARE (node.anchor_end, std::string ("end"));

  // Simulate a crash after the legacy map was archived but before Vaultfile
  // replacement became durable.
  migrated_info.map_path = "map.tmdb";
  QVERIFY2 (athena_vaultfile_write (root, migrated_info, error), error.c_str ());
  resolved.clear ();
  QVERIFY2 (athena_vault_map_prepare (root, "map.tmdb", resolved, error),
            error.c_str ());
  QCOMPARE (resolved, std::string ("map.sqlite"));
}

void
TestVaultMapSqlite::migrateRealMapCopy () {
  QByteArray source_root = qgetenv ("ATHENA_REAL_MAP_TEST_ROOT");
  if (source_root.isEmpty ()) QSKIP ("No real-map test root was requested");
  std::filesystem::path source (source_root.constData ());
  QTemporaryDir temporary;
  QVERIFY (temporary.isValid ());
  std::filesystem::path root (temporary.path ().toStdString ());
  std::filesystem::copy_file (source / "map.tmdb", root / "map.tmdb");
  std::filesystem::copy_file (source / "Vaultfile.json",
                              root / "Vaultfile.json");
  std::string resolved;
  std::string error;
  QVERIFY2 (athena_vault_map_prepare (root, "map.tmdb", resolved, error),
            error.c_str ());
  AthenaVaultMapSqlite map;
  QVERIFY2 (map.open (root / resolved, false, error), error.c_str ());
  std::vector<AthenaVaultMapNode> nodes;
  QVERIFY2 (map.read_all (nodes, error), error.c_str ());
  QVERIFY (!nodes.empty ());
}

QTEST_MAIN(TestVaultMapSqlite)
#include "vault_map_sqlite_test.moc"
