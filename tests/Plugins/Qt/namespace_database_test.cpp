/******************************************************************************
* MODULE     : namespace_database_test.cpp
* DESCRIPTION: Namespace migrations, stable identities and vault contexts
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
*******************************************************************************/

#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QUuid>

#include "namespaces.hpp"
#include "namespaces_schema.hpp"
#include "namespace_ontology.hpp"
#include "vaultfile_json.hpp"

#include <sqlite3.h>
#include <future>
#include "../../../src/ATHENA/Interop/resources.hpp"
#include <fstream>

bool headless_mode= true;
bool is_headless () { return true; }

namespace {

struct Database {
  sqlite3* db= nullptr;
  ~Database () { if (db) sqlite3_close (db); }
  bool raw (const std::filesystem::path& path) {
    return sqlite3_open (path.c_str (), &db) == SQLITE_OK;
  }
  bool exec (const char* sql) {
    return sqlite3_exec (db, sql, nullptr, nullptr, nullptr) == SQLITE_OK;
  }
  QString scalar (const char* sql) {
    sqlite3_stmt* stmt= nullptr;
    QString result;
    if (sqlite3_prepare_v2 (db, sql, -1, &stmt, nullptr) == SQLITE_OK &&
        sqlite3_step (stmt) == SQLITE_ROW)
      result= QString::fromUtf8 (
        reinterpret_cast<const char*> (sqlite3_column_text (stmt, 0)));
    sqlite3_finalize (stmt);
    return result;
  }
};

const char* legacy_schema=
  "CREATE TABLE meta(key TEXT PRIMARY KEY,value TEXT NOT NULL);"
  "INSERT INTO meta VALUES('schema-version','1'),('custom','preserve me');"
  "CREATE TABLE namespaces(name TEXT PRIMARY KEY,kind TEXT NOT NULL,"
  "template TEXT NOT NULL DEFAULT '',sorter_path TEXT NOT NULL DEFAULT '',"
  "style_path TEXT NOT NULL DEFAULT '');"
  "CREATE TABLE namespace_parents(child TEXT,parent TEXT,source TEXT,ord INTEGER,"
  "PRIMARY KEY(child,parent,source));"
  "CREATE TABLE relation_decisions(parent TEXT,child TEXT,decision TEXT,"
  "source TEXT,PRIMARY KEY(parent,child));"
  "INSERT INTO namespaces VALUES('Parent','abstract','','',''),"
  "('Child','concrete','Note %s','sort.c','style.ts');"
  "INSERT INTO namespace_parents VALUES('Child','Parent','declared',7),"
  "('Child','Derived','derived',3);"
  "INSERT INTO relation_decisions VALUES('Parent','Child','allow','user'),"
  "('Blocked','Child','deny','user');";

struct CloseVault { ~CloseVault () { if (vault_active ()) vault_close (); } };

string
open_vault (const std::filesystem::path& root) {
  std::string error;
  if (!athena_vaultfile_write (root, AthenaVaultfileInfo {}, error))
    return string (error.c_str ());
  return vault_load (url_system (string (root.string ().c_str ())),
                     "Namespace test", "map.sqlite", "ns.sqlite");
}

} // namespace

class NamespaceDatabaseTest: public QObject {
  Q_OBJECT
private slots:
  void migratesLegacy_data ();
  void migratesLegacy ();
  void rollsBackAndRejectsUnknownVersions ();
  void concurrentOpen ();
  void ontologyOpenMigrates ();
  void vaultOpenMigratesAndIdentitiesSurviveRename ();
  void distinguishesMissingStaleAndFault ();
  void nativeInteropResolution ();
};

void
NamespaceDatabaseTest::migratesLegacy_data () {
  QTest::addColumn<bool> ("modernColumns");
  QTest::addColumn<bool> ("versioned");
  QTest::newRow ("old-columns") << false << true;
  QTest::newRow ("latest-v1") << true << true;
  QTest::newRow ("unversioned") << false << false;
}

void
NamespaceDatabaseTest::migratesLegacy () {
  QFETCH (bool, modernColumns);
  QFETCH (bool, versioned);
  QTemporaryDir temporary;
  QVERIFY (temporary.isValid ());
  auto path= std::filesystem::path (temporary.path ().toStdString ()) / "ns.sqlite";
  Database old;
  QVERIFY (old.raw (path));
  QVERIFY (old.exec (legacy_schema));
  if (!versioned) QVERIFY (old.exec ("DELETE FROM meta WHERE key='schema-version';"));
  if (modernColumns) QVERIFY (old.exec (
    "ALTER TABLE namespaces ADD COLUMN sorter_trivial INTEGER NOT NULL DEFAULT 0;"
    "ALTER TABLE namespaces ADD COLUMN initial_content_path TEXT NOT NULL DEFAULT '';"
    "ALTER TABLE namespaces ADD COLUMN homepage_path TEXT NOT NULL DEFAULT '';"
    "UPDATE namespaces SET sorter_trivial=1,initial_content_path='seed.ath',"
    "homepage_path='home.ath' WHERE name='Child';"));

  std::string error;
  Database migrated;
  QVERIFY2 (athena_namespace_database_open (path, false, migrated.db, error), error.c_str ());
  QCOMPARE (migrated.scalar ("SELECT value FROM meta WHERE key='schema-version';"), "2");
  QCOMPARE (migrated.scalar ("SELECT value FROM meta WHERE key='custom';"), "preserve me");
  QCOMPARE (migrated.scalar ("SELECT count(DISTINCT uuid) FROM namespaces;"), "2");
  QString id= migrated.scalar ("SELECT uuid FROM namespaces WHERE name='Child';");
  QVERIFY (!QUuid (id).isNull ());
  QCOMPARE (migrated.scalar ("SELECT template FROM namespaces WHERE name='Child';"), "Note %s");
  QCOMPARE (migrated.scalar ("SELECT sorter_path FROM namespaces WHERE name='Child';"), "sort.c");
  QCOMPARE (migrated.scalar ("SELECT style_path FROM namespaces WHERE name='Child';"), "style.ts");
  QCOMPARE (migrated.scalar ("SELECT initial_content_path FROM namespaces WHERE name='Child';"),
            modernColumns ? "seed.ath" : "");
  QCOMPARE (migrated.scalar ("SELECT homepage_path FROM namespaces WHERE name='Child';"),
            modernColumns ? "home.ath" : "");
  QCOMPARE (migrated.scalar ("SELECT sorter_trivial FROM namespaces WHERE name='Child';"),
            modernColumns ? "1" : "0");
  QCOMPARE (migrated.scalar ("SELECT count(*) FROM namespace_parents;"), "2");
  QCOMPARE (migrated.scalar ("SELECT ord FROM namespace_parents WHERE source='declared';"), "7");
  QCOMPARE (migrated.scalar ("SELECT count(*) FROM relation_decisions;"), "2");
  QCOMPARE (migrated.scalar ("PRAGMA integrity_check;"), "ok");

  auto mtime= std::filesystem::last_write_time (path);
  Database reopened;
  QVERIFY2 (athena_namespace_database_open (path, false, reopened.db, error), error.c_str ());
  QCOMPARE (reopened.scalar ("SELECT uuid FROM namespaces WHERE name='Child';"), id);
  QVERIFY (std::filesystem::last_write_time (path) == mtime);
  QVERIFY (!reopened.exec ("UPDATE namespaces SET uuid='changed' WHERE name='Child';"));
  QVERIFY (!reopened.exec ("INSERT INTO namespaces(name,kind) VALUES('No UUID','abstract');"));
  QVERIFY (!reopened.exec (
    "INSERT INTO namespaces(name,kind,uuid) SELECT 'Duplicate','abstract',uuid "
    "FROM namespaces WHERE name='Child';"));
}

void
NamespaceDatabaseTest::rollsBackAndRejectsUnknownVersions () {
  QTemporaryDir temporary;
  auto path= std::filesystem::path (temporary.path ().toStdString ()) / "ns.sqlite";
  Database old;
  QVERIFY (old.raw (path));
  QVERIFY (old.exec (legacy_schema));
  QVERIFY (old.exec ("CREATE TRIGGER reject_change BEFORE UPDATE ON namespaces "
                    "BEGIN SELECT RAISE(ABORT,'test migration failure'); END;"));
  std::string error;
  Database failed;
  QVERIFY (!athena_namespace_database_open (path, false, failed.db, error));
  QVERIFY (!error.empty ());
  QVERIFY (!failed.db);
  QCOMPARE (old.scalar ("SELECT value FROM meta WHERE key='schema-version';"), "1");
  QCOMPARE (old.scalar ("SELECT count(*) FROM pragma_table_info('namespaces') WHERE name='uuid';"), "0");
  QCOMPARE (old.scalar ("SELECT count(*) FROM namespaces;"), "2");
  QVERIFY (old.exec ("DROP TRIGGER reject_change; UPDATE meta SET value='999' WHERE key='schema-version';"));
  QVERIFY (!athena_namespace_database_open (path, false, failed.db, error));
  QVERIFY (error.find ("Unsupported") != std::string::npos);
  QCOMPARE (old.scalar ("SELECT value FROM meta WHERE key='schema-version';"), "999");
  QCOMPARE (old.scalar ("SELECT count(*) FROM pragma_table_info('namespaces') WHERE name='uuid';"), "0");
  QVERIFY (old.exec ("UPDATE meta SET value='1' WHERE key='schema-version';"));
  QVERIFY2 (athena_namespace_database_open (path, false, failed.db, error), error.c_str ());
  QCOMPARE (old.scalar ("SELECT value FROM meta WHERE key='schema-version';"), "2");
}

void
NamespaceDatabaseTest::concurrentOpen () {
  QTemporaryDir temporary;
  auto path= std::filesystem::path (temporary.path ().toStdString ()) / "ns.sqlite";
  Database old;
  QVERIFY (old.raw (path));
  QVERIFY (old.exec (legacy_schema));
  std::promise<void> start;
  auto ready= start.get_future ().share ();
  std::vector<std::future<std::string>> jobs;
  for (int i=0; i<4; ++i) jobs.push_back (std::async (std::launch::async, [&, ready] {
    ready.wait ();
    std::string error;
    Database db;
    if (!athena_namespace_database_open (path, false, db.db, error)) return error;
    return db.scalar ("SELECT uuid FROM namespaces WHERE name='Child';").toStdString ();
  }));
  start.set_value ();
  std::string first= jobs[0].get ();
  QVERIFY2 (!QUuid (QString::fromStdString (first)).isNull (), first.c_str ());
  for (size_t i=1; i<jobs.size (); ++i) QCOMPARE (jobs[i].get (), first);
}

void
NamespaceDatabaseTest::ontologyOpenMigrates () {
  QTemporaryDir temporary;
  auto root= std::filesystem::path (temporary.path ().toStdString ());
  struct Stop { ~Stop () { athena_namespace_ontology_stop (); } } stop;
  Database old;
  QVERIFY (old.raw (root / "ns.sqlite"));
  QVERIFY (old.exec (legacy_schema));
  athena_namespace_ontology_start (
    url_system (string (root.string ().c_str ())),
    url_system (string ((root / "ns.sqlite").string ().c_str ())));
  string error;
  QVERIFY2 (athena_namespace_ontology_refresh (true, error), as_charp (error));
  std::shared_ptr<const athena_namespace_definition> definition;
  QVERIFY (athena_namespace_ontology_namespace ("Child", definition));
  QVERIFY (definition->uuid != "");
  QCOMPARE (old.scalar ("SELECT value FROM meta WHERE key='schema-version';"), "2");
  QCOMPARE (old.scalar ("SELECT uuid FROM namespaces WHERE name='Child';"),
            QString::fromUtf8 (as_charp (definition->uuid)));
}

void
NamespaceDatabaseTest::vaultOpenMigratesAndIdentitiesSurviveRename () {
  QTemporaryDir temporary;
  auto root= std::filesystem::path (temporary.path ().toStdString ());
  CloseVault close;
  Database old;
  QVERIFY (old.raw (root / "ns.sqlite"));
  QVERIFY (old.exec (legacy_schema));
  string error= open_vault (root);
  QVERIFY2 (error == "", as_charp (error));
  QCOMPARE (old.scalar ("SELECT value FROM meta WHERE key='schema-version';"), "2");
  auto context= vault_capture_context ();
  std::shared_ptr<const athena_namespace_definition> child;
  QCOMPARE (athena_namespace_get (context, "Child", child, error), namespace_query_status::ok);
  QVERIFY (child->uuid != "");
  string id= child->uuid;
  QVERIFY2 (athena_namespace_ontology_refresh (true, error), as_charp (error));
  std::shared_ptr<const athena_namespace_definition> cached;
  QVERIFY (athena_namespace_ontology_namespace ("Child", cached));
  QCOMPARE (cached->uuid, id);

  athena_namespace_definition edited= *child;
  edited.name= "Renamed";
  QVERIFY2 (athena_namespace_save (context, edited, error), as_charp (error));
  QCOMPARE (athena_namespace_get_by_uuid (context, id, child, error), namespace_query_status::ok);
  QCOMPARE (child->name, string ("Renamed"));
  QCOMPARE (old.scalar ("SELECT child FROM namespace_parents WHERE parent='Parent';"), "Renamed");
  QCOMPARE (old.scalar ("SELECT child FROM relation_decisions WHERE parent='Blocked';"), "Renamed");
  QCOMPARE (athena_namespace_get (context, "Child", cached, error), namespace_query_status::not_found);
  QVERIFY (!cached);
  edited.name= "Parent";
  QVERIFY (!athena_namespace_save (context, edited, error));
  QCOMPARE (athena_namespace_get_by_uuid (context, id, cached, error), namespace_query_status::ok);
  QCOMPARE (cached->name, string ("Renamed"));

  QCOMPARE (athena_namespace_remove_by_uuid (context, id, error), namespace_query_status::ok);
  athena_namespace_definition replacement;
  replacement.name= "Renamed";
  replacement.kind= "abstract";
  QVERIFY2 (athena_namespace_save (context, replacement, error), as_charp (error));
  QCOMPARE (athena_namespace_get (context, "Renamed", cached, error), namespace_query_status::ok);
  QVERIFY (cached->uuid != id);
  string replacement_id= cached->uuid;
  QCOMPARE (athena_namespace_get_by_uuid (context, id, cached, error), namespace_query_status::not_found);
  QVERIFY (!cached);
  edited.name= "Renamed";
  QVERIFY (!athena_namespace_save (context, edited, error));
  QCOMPARE (athena_namespace_remove_by_uuid (context, id, error), namespace_query_status::not_found);
  QVERIFY (athena_namespace_save (context, replacement, error));
  QCOMPARE (athena_namespace_get (context, "Renamed", cached, error), namespace_query_status::ok);
  QCOMPARE (cached->uuid, replacement_id);
}

void
NamespaceDatabaseTest::distinguishesMissingStaleAndFault () {
  QTemporaryDir first, second;
  auto root= std::filesystem::path (first.path ().toStdString ());
  CloseVault close;
  string error= open_vault (root);
  QVERIFY2 (error == "", as_charp (error));
  auto context= vault_capture_context ();
  namespace_records<athena_namespace_definition> rows;
  QCOMPARE (athena_namespaces_list (context, rows, error), namespace_query_status::ok);
  QVERIFY (rows.empty ());
  std::shared_ptr<const athena_namespace_definition> out;
  QCOMPARE (athena_namespace_get (context, "missing", out, error), namespace_query_status::not_found);
  QCOMPARE (error, string (""));
  athena_namespace_definition ns;
  ns.name= "Same name"; ns.kind= "abstract";
  QVERIFY (athena_namespace_save (context, ns, error));
  QCOMPARE (athena_namespace_get (context, ns.name, out, error), namespace_query_status::ok);
  string id= out->uuid;
  vault_close ();
  QCOMPARE (athena_namespace_get_by_uuid (context, id, out, error), namespace_query_status::stale);
  QVERIFY (!out);
  QVERIFY (!athena_namespace_save (context, ns, error));
  error= open_vault (root);
  QVERIFY2 (error == "", as_charp (error));
  auto reopened= vault_capture_context ();
  QVERIFY (context->incarnation != reopened->incarnation);
  QCOMPARE (athena_namespace_get_by_uuid (context, id, out, error), namespace_query_status::stale);
  QCOMPARE (athena_namespace_get_by_uuid (reopened, id, out, error), namespace_query_status::ok);
  error= open_vault (std::filesystem::path (second.path ().toStdString ()));
  QVERIFY2 (error == "", as_charp (error));
  auto other= vault_capture_context ();
  QVERIFY (athena_namespace_save (other, ns, error));
  QCOMPARE (athena_namespace_get (other, ns.name, out, error), namespace_query_status::ok);
  QVERIFY (out->uuid != id);
  QCOMPARE (athena_namespace_get (reopened, ns.name, out, error), namespace_query_status::stale);
  QVERIFY (!athena_namespace_save (reopened, ns, error));

  athena_namespace_ontology_stop ();
  Database broken;
  QVERIFY (broken.raw (other->namespace_db));
  QVERIFY (broken.exec ("DROP TABLE namespace_parents;"));
  QCOMPARE (athena_namespace_get (other, ns.name, out, error), namespace_query_status::error);
  QVERIFY (!out);
  QVERIFY (error != "");
  QCOMPARE (athena_namespaces_list (other, rows, error), namespace_query_status::error);
  QVERIFY (rows.empty ());
  namespace_records<athena_namespace_relation> relations;
  QCOMPARE (athena_namespace_relations_list (other, relations, error), namespace_query_status::error);
  QVERIFY (relations.empty ());
  // A failed new vault must not replace the currently published context.
  Database invalid;
  QVERIFY (invalid.raw (root / "ns.sqlite"));
  QVERIFY (invalid.exec ("UPDATE meta SET value='999' WHERE key='schema-version';"));
  QVERIFY (open_vault (root) != "");
  QVERIFY (vault_context_is_current (other));
}

void
NamespaceDatabaseTest::nativeInteropResolution () {
  using namespace athena::interop;
  QTemporaryDir temporary;
  CloseVault close;
  const auto root= std::filesystem::path (temporary.path ().toStdString ());
  QCOMPARE (open_vault (root), string (""));
  auto context= vault_capture_context ();
  string error;
  athena_namespace_definition parent;
  parent.name= "Root namespace"; parent.kind= "abstract";
  QVERIFY (athena_namespace_create (context, parent, error));
  QVERIFY (!athena_namespace_create (context, parent, error));
  athena_namespace_definition child;
  child.name= "Child"; child.kind= "concrete"; child.templ= "Note %s";
  child.parents.push_back (parent.name);
  QVERIFY (athena_namespace_create (context, child, error));
  {
    std::ofstream configuration (root / "Vaultfile.json");
    configuration << value ({{"root_namespace", "Root namespace"}}).dump ();
    std::ofstream document (root / "Note one.ath"); document << "example";
  }
  resolution_workers workers (3);
  auto run= [&] (const std::string& expression) {
    std::promise<resolution_result> promise;
    auto future= promise.get_future ();
    resolution_ticket ticket (workers, native_resolvers (), parse_selection (expression),
      [&] (resolution_result r) { promise.set_value (std::move (r)); });
    if (future.wait_for (std::chrono::seconds (5)) != std::future_status::ready)
      throw std::runtime_error ("Native resolution timed out");
    return future.get ();
  };
  auto result= run ("@/vaults/@/namespaces/@");
  QVERIFY2 (result.state == resolution_result::status::complete, result.error.c_str ());
  QCOMPARE (result.leaves.size (), std::size_t (1));
  QCOMPARE (result.tree.back ()->accessor->properties ().at ("name").get<std::string> (), std::string ("Root namespace"));
  result= run (R"(@/vaults/@/namespaces/@/??($name = "Child"))");
  QVERIFY2 (result.state == resolution_result::status::complete, result.error.c_str ());
  QCOMPARE (result.leaves.size (), std::size_t (1));
  auto accessor= result.tree.back ()->accessor;
  QCOMPARE (accessor->operate ("members", value::object ()).data.size (), std::size_t (1));
  auto created= accessor->operate ("create_file", {{"directory", "."}, {"values", value::array ({"two"})},
                                                  {"use_initial_content", false}});
  QVERIFY2 (created.status == "OK", created.data.dump ().c_str ());
  QVERIFY (std::filesystem::exists (root / "Note two.ath"));
  QCOMPARE (accessor->operate ("create_file", {{"directory", "."}, {"values", value::array ({"two"})},
                                             {"use_initial_content", false}}).status, std::string ("ERROR"));
  QCOMPARE (accessor->operate ("template_fields", value::object ()).data.size (), std::size_t (1));
  auto replacement= accessor->properties ();
  replacement["name"]= "Renamed";
  replacement["sorter_trivial"]= true;
  QCOMPARE (accessor->operate ("set", {{"definition", replacement}}).status, std::string ("OK"));
  QCOMPARE (accessor->properties ().at ("name").get<std::string> (), std::string ("Renamed"));
  std::shared_ptr<const athena_namespace_definition> stored_parent;
  QVERIFY (athena_namespace_get (context, parent.name, stored_parent, error) == namespace_query_status::ok);
  const auto parent_uuid= std::string (stored_parent->uuid.data (), N(stored_parent->uuid));
  QCOMPARE (accessor->operate ("set_relation", {{"parent_uuid", parent_uuid}, {"decision", "deny"}}).status,
            std::string ("OK"));
  QVERIFY (!accessor->operate ("relations", value::object ()).data.empty ());
  QCOMPARE (accessor->operate ("remove_relation", {{"parent_uuid", parent_uuid}}).status, std::string ("OK"));
  QCOMPARE (accessor->operate ("rename", {{"name", "Renamed again"}}).status, std::string ("OK"));
  athena_namespace_definition other;
  other.name= "Other"; other.kind= "semi-concrete"; other.templ= "Note %w"; other.sorter_trivial= true;
  QVERIFY (athena_namespace_create (context, other, error));
  std::shared_ptr<const athena_namespace_definition> stored_other;
  QVERIFY (athena_namespace_get (context, other.name, stored_other, error) == namespace_query_status::ok);
  const auto other_uuid= std::string (stored_other->uuid.data (), N(stored_other->uuid));
  auto product= accessor->operate ("subproduct", {{"other_uuid", other_uuid}, {"name", "Product"}, {"template", "Note %w"}});
  QVERIFY2 (product.status == "OK", product.data.dump ().c_str ());
  QVERIFY (std::filesystem::exists (root / product.data.at ("sorter_path").get<std::string> ()));
  result= run (R"(@/???($type = "namespace" | $max_depth = 3))");
  QVERIFY2 (result.state == resolution_result::status::complete, result.error.c_str ());
  QVERIFY (result.leaves.size () >= 2);
  result= run (R"(@/???($type = "namespace" | $max_matches = 1))");
  QVERIFY2 (result.state == resolution_result::status::complete, result.error.c_str ());
  QCOMPARE (result.leaves.size (), std::size_t (1));
  QVERIFY (!result.truncated.empty ());
  QCOMPARE (accessor->operate ("delete", value::object ()).status, std::string ("OK"));
  child.name= "Renamed again";
  QVERIFY (athena_namespace_create (context, child, error));
  QCOMPARE (accessor->operate ("get", value::object ()).status, std::string ("NOT_FOUND"));
  result= run ("@/vaults/@/namespaces/does-not-exist");
  QVERIFY (result.tree.empty ());
  vault_close ();
  QCOMPARE (accessor->operate ("get", value::object ()).status, std::string ("STALE"));
}

QTEST_MAIN (NamespaceDatabaseTest)
#include "namespace_database_test.moc"
