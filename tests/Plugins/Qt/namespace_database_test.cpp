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
#include "ATHENA/Interop/traversal.hpp"
#include "ATHENA/Data/interop_filesystem.hpp"
#include "ATHENA/Data/interop_document.hpp"
#include "Data/Convert/Xml/document_file_codec.hpp"
#include <algorithm>
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
  void migratesMaterialsFromVersionTwo ();
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
  QCOMPARE (migrated.scalar ("SELECT value FROM meta WHERE key='schema-version';"), "3");
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
NamespaceDatabaseTest::migratesMaterialsFromVersionTwo () {
  QTemporaryDir temporary;
  QVERIFY (temporary.isValid ());
  auto path= std::filesystem::path (temporary.path ().toStdString ()) / "ns.sqlite";
  Database old;
  std::string error;
  QVERIFY2 (athena_namespace_database_open (path, true, old.db, error), error.c_str ());
  QVERIFY (old.exec (
    "DROP TRIGGER namespace_materials_abstract; DROP TABLE namespace_materials;"
    "UPDATE meta SET value='2' WHERE key='schema-version';"
    "INSERT INTO namespaces(name,kind,uuid) VALUES"
    "('Concrete','concrete','11111111-1111-4111-8111-111111111111'),"
    "('Abstract','abstract','22222222-2222-4222-8222-222222222222');"));
  Database migrated;
  QVERIFY2 (athena_namespace_database_open (path, false, migrated.db, error), error.c_str ());
  QCOMPARE (migrated.scalar ("SELECT value FROM meta WHERE key='schema-version';"), "3");
  QCOMPARE (migrated.scalar ("SELECT uuid FROM namespaces WHERE name='Concrete';"),
            "11111111-1111-4111-8111-111111111111");
  QVERIFY (migrated.exec (
    "INSERT INTO namespace_materials SELECT uuid,'book',0 FROM namespaces WHERE name='Concrete';"));
  QVERIFY (!migrated.exec (
    "INSERT INTO namespace_materials SELECT uuid,'book',0 FROM namespaces WHERE name='Abstract';"));
  QVERIFY (!migrated.exec (
    "INSERT INTO namespace_materials VALUES('missing','book',0);"));
  QVERIFY (migrated.exec ("UPDATE namespaces SET name='Renamed' WHERE name='Concrete';"));
  QCOMPARE (migrated.scalar ("SELECT count(*) FROM namespace_materials;"), "1");
  QVERIFY (migrated.exec ("DELETE FROM namespaces WHERE name='Renamed';"));
  QCOMPARE (migrated.scalar ("SELECT count(*) FROM namespace_materials;"), "0");
  QCOMPARE (migrated.scalar ("PRAGMA integrity_check;"), "ok");
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
  QCOMPARE (old.scalar ("SELECT value FROM meta WHERE key='schema-version';"), "3");
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
  QCOMPARE (old.scalar ("SELECT value FROM meta WHERE key='schema-version';"), "3");
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
  QCOMPARE (old.scalar ("SELECT value FROM meta WHERE key='schema-version';"), "3");
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
  result= run ("@/vaults/@/filesystem");
  QVERIFY2 (result.state == resolution_result::status::complete, result.error.c_str ());
  QCOMPARE (result.leaves.size (), std::size_t (1));
  QCOMPARE (result.tree.back ()->accessor->type (), std::string ("directory"));
  {
    auto filesystem= std::make_shared<athena::filesystem::confined_root> (root);
    auto selector= parse_selection (R"(??($name = "vanishing"))");
    std::atomic<bool> stopped {false};
    for (bool directory: {false, true}) {
      if (directory) std::filesystem::create_directory (root / "vanishing");
      else { std::ofstream transient (root / "vanishing"); transient << "temporary"; }
      auto file= std::make_shared<filesystem_resource> (
        context, filesystem, "vanishing", filesystem->open ("vanishing"));
      auto occurrence= std::make_shared<athena::interop::occurrence> (
        athena::interop::occurrence {1, file, {}});
      std::filesystem::remove (root / "vanishing");
      QCOMPARE (file->operate ("get", value::object ()).status, std::string ("NOT_FOUND"));
      for (auto phase: {traversal::phase::candidate, traversal::phase::descend}) {
        if (!directory && phase == traversal::phase::descend) continue;
        auto state= std::make_shared<traversal> (
          std::make_shared<traversal_budget> (selector.front ().limits), 1, "filesystem", phase);
        resolution_request request {selector, 0, occurrence, state, stopped};
        resolution_output output;
        QCOMPARE (filesystem_resolver ()->resolve (request, output), resolver_outcome::miss);
        QVERIFY (output.branches.empty () && output.redispatch.empty ());
      }
    }
  }
  result= run ("@/vaults/@/filesystem/Note one.ath");
  QVERIFY2 (result.state == resolution_result::status::complete, result.error.c_str ());
  QCOMPARE (result.leaves.size (), std::size_t (1));
  auto file_accessor= result.tree.back ()->accessor;
  const auto info= file_accessor->operate ("get", value::object ());
  QCOMPARE (info.status, std::string ("OK"));
  QCOMPARE (info.data.at ("size").get<int> (), 7);
  QVERIFY (info.data.contains ("created_time"));
  QVERIFY (info.data.at ("modified_time").contains ("nanoseconds"));
  auto checked= file_accessor->operate ("check", value::object ());
  QCOMPARE (checked.status, std::string ("OK"));
  QVERIFY (!checked.data.at ("valid").get<bool> ());
  result= run ("@/vaults/@/filesystem/Note two.ath");
  QVERIFY2 (result.state == resolution_result::status::complete, result.error.c_str ());
  checked= result.tree.back ()->accessor->operate ("check", value::object ());
  QCOMPARE (checked.status, std::string ("OK"));
  QVERIFY2 (checked.data.at ("valid").get<bool> (), checked.data.dump ().c_str ());
  // Use native markup, including a custom field that save/export filtering must not remove.
  const std::string source_markup=
    "<TeXmacs|2.1.4>\n\n<style|generic>\n\n"
    "<\\body>\nfirst\n\n<transclude|other.ath|anchor>\n</body>\n\n"
    "<custom-field|retained>\n";
  {
    std::ofstream document (root / "Source.ath");
    document << source_markup;
  }
  result= run ("@/vaults/@/filesystem/Source.ath/saved");
  QVERIFY2 (result.state == resolution_result::status::complete, result.error.c_str ());
  QCOMPARE (result.leaves.size (), std::size_t (1));
  auto saved= result.tree.back ()->accessor;
  QCOMPARE (saved->type (), std::string ("document"));
  auto source= saved->operate ("get", value::object ());
  QCOMPARE (source.status, std::string ("OK"));
  QCOMPARE (source.data.at ("tree").at ("tag").get<std::string> (), std::string ("document"));
  QCOMPARE (source.data.at ("document_format").get<std::string> (),
            std::string ("legacy-markup"));
  QVERIFY (source.data.at ("legacy_relocation").get<bool> ());
  {
    auto decoded= athena::document::decode_document_bytes (source_markup);
    std::optional<athena::document::document_position> expected;
    athena::document::document_path source_path;
    std::size_t source_byte= 0;
    for (const auto& mapping: decoded.mappings) {
      for (const auto& span: mapping.spans) {
        expected= decoded.relocate (
          mapping.source, span.begin, athena::document::boundary_affinity::following);
        if (expected) {
          source_path= mapping.source;
          source_byte= span.begin;
          break;
        }
      }
      if (expected) break;
    }
    QVERIFY (expected.has_value ());
    auto relocated= saved->operate ("relocate_source_position",
      {{"path", source_path}, {"byte", source_byte}, {"affinity", "following"}});
    QCOMPARE (relocated.status, std::string ("OK"));
    QCOMPARE (relocated.data.at ("path").get<std::vector<int>> (), expected->node);
    QCOMPARE (relocated.data.at ("byte").get<std::size_t> (), expected->offset);
  }
  {
    auto decoded= athena::document::decode_document_bytes (source_markup);
    std::ofstream xml_file (root / "Xml.ath", std::ios::binary);
    xml_file << athena::document::write_xml (decoded.document);
    xml_file.close ();
    auto xml_result= run (R"(@/vaults/@/filesystem/Xml.ath/saved/?($tag = "body")[0]/[0]/[0])");
    QVERIFY2 (xml_result.state == resolution_result::status::complete, xml_result.error.c_str ());
    QCOMPARE (xml_result.leaves.size (), std::size_t (1));
    auto xml_text= xml_result.tree.back ()->accessor;
    QCOMPARE (xml_text->operate ("set", {{"tree", {{"text", "xml changed"}}}}).status,
              std::string ("OK"));
    std::ifstream input (root / "Xml.ath", std::ios::binary);
    std::string persisted ((std::istreambuf_iterator<char> (input)),
                           std::istreambuf_iterator<char> ());
    QVERIFY (persisted.rfind ("<?xml", 0) == 0 ||
             persisted.rfind ("<athena-document", 0) == 0);
    auto xml_saved= run ("@/vaults/@/filesystem/Xml.ath/saved").tree.back ()->accessor;
    auto xml_source= xml_saved->operate ("get", value::object ());
    QCOMPARE (xml_source.data.at ("document_format").get<std::string> (),
              std::string ("xml-v1"));
    QVERIFY (!xml_source.data.at ("legacy_relocation").get<bool> ());
  }
  result= run ("@/vaults/@/filesystem/Source.ath/saved/custom-field/[0]");
  QVERIFY2 (result.state == resolution_result::status::complete, result.error.c_str ());
  QCOMPARE (result.leaves.size (), std::size_t (1));
  auto custom= result.tree.back ()->accessor;
  QCOMPARE (custom->type (), std::string ("node"));
  QCOMPARE (custom->operate ("get", value::object ()).data.at ("tree").at ("text").get<std::string> (),
            std::string ("retained"));
  result= run (R"(@/vaults/@/filesystem/Source.ath/saved/??($tag = "transclude"))");
  QVERIFY2 (result.state == resolution_result::status::complete, result.error.c_str ());
  QCOMPARE (result.leaves.size (), std::size_t (1));
  QCOMPARE (result.tree.back ()->accessor->operate ("get", value::object ()).data.at ("tree").at ("children").size (),
            std::size_t (2));
  result= run (R"(@/vaults/@/filesystem/Source.ath/saved/?($tag = "body")[0]/[0]/[0])");
  QVERIFY2 (result.state == resolution_result::status::complete, result.error.c_str ());
  QCOMPARE (result.leaves.size (), std::size_t (1));
  QCOMPARE (result.tree.back ()->accessor->operate ("get", value::object ()).data.at ("tree").at ("text").get<std::string> (),
            std::string ("first"));
  {
    // Resolver requests can be constructed natively as well as parsed. Honor
    // their budgets even though local-selector bounds are not CLI syntax.
    auto selectors= parse_selection (R"(?($type = "node")[0])");
    selectors.front ().limits.max_matches= 1;
    auto base= std::make_shared<athena::interop::occurrence> (
      athena::interop::occurrence {1, saved, {}});
    std::atomic<bool> stopped {false};
    for (std::uint64_t index: {0, 1}) {
      selectors.front ().positions= {index};
      resolution_request request {selectors, 0, base, {}, stopped};
      resolution_output output;
      document_resolver ()->resolve (request, output);
      QCOMPARE (output.branches.size (), index == 0 ? std::size_t (1) : std::size_t (0));
      QVERIFY (!output.truncated.empty ());
    }
  }
  result= run ("@/vaults/@/filesystem/Source.ath/online/custom-field/[0]");
  QVERIFY2 (result.state == resolution_result::status::complete, result.error.c_str ());
  QCOMPARE (result.leaves.size (), std::size_t (1));
  QCOMPARE (result.tree.back ()->accessor->operate ("get", value::object ()).data.at ("source").get<std::string> (),
            std::string ("online"));
  { std::ofstream not_document (root / "not-document.txt"); not_document << "text"; }
  result= run ("@/vaults/@/filesystem/not-document.txt/saved");
  QVERIFY2 (result.state == resolution_result::status::complete, result.error.c_str ());
  QVERIFY (result.leaves.empty ());
  result= run ("@/vaults/@/filesystem/Note one.ath/saved");
  QVERIFY (result.state == resolution_result::status::fault);
  { std::ofstream changed (root / "Source.ath", std::ios::app); changed << "\n"; }
  QCOMPARE (saved->operate ("get", value::object ()).status, std::string ("OK"));
  QCOMPARE (custom->operate ("get", value::object ()).status, std::string ("STALE"));
  {
    std::filesystem::copy_file (root / "Source.ath", root / "Write.ath");
    auto editing= run (R"(@/vaults/@/filesystem/Write.ath/saved/body/[0]/?($type = "node"))");
    QVERIFY2 (editing.state == resolution_result::status::complete, editing.error.c_str ());
    QCOMPARE (editing.leaves.size (), std::size_t (2));
    binding first, transclusion, body, envelope, body_field;
    for (const auto& item: editing.tree) {
      auto props= item->accessor->properties ();
      if (item->accessor->type () == "document") envelope= item->accessor;
      if (props.value ("tag", "") == "body") body_field= item->accessor;
      if (props.value ("text", "") == "first") {
        first= item->accessor;
        body= item->parent->accessor;
      }
      if (props.value ("tag", "") == "transclude") transclusion= item->accessor;
    }
    QVERIFY (first && transclusion && body && envelope && body_field);
    auto competing= run ("@/vaults/@/filesystem/Write.ath/saved").tree.back ()->accessor;
    auto before= envelope->operate ("get", value::object ()).data;
    auto disk_bytes= [&] {
      std::ifstream input (root / "Write.ath", std::ios::binary);
      return std::string (std::istreambuf_iterator<char> (input), std::istreambuf_iterator<char> ());
    };
    const auto original_bytes= disk_bytes ();
    QVERIFY (envelope->inspect ().contains ("set"));
    QCOMPARE (envelope->operate ("set", {{"tree", {{"tag", "document"}, {"children", value::array ()}}}}).status,
              std::string ("INVALID_ARGUMENT"));
    QCOMPARE (envelope->operate ("erase", value::object ()).status, std::string ("INVALID_ARGUMENT"));
    QCOMPARE (body_field->operate ("insert", {{"index", 1}, {"children", value::array ({value {{"text", "invalid second body"}}})}}).status,
              std::string ("INVALID_ARGUMENT"));
    QCOMPARE (envelope->operate ("get", value::object ()).data, before);
    QCOMPARE (disk_bytes (), original_bytes);
    QCOMPARE (body->operate ("insert", {{"index", -1}, {"children", value::array ()}}).status,
              std::string ("INVALID_ARGUMENT"));
    auto insertion= body->operate ("insert", {{"index", 0}, {"children", value::array ({value {{"text", "prefix"}}})}});
    QVERIFY2 (insertion.status == "OK", insertion.data.dump ().c_str ());
    QCOMPARE (first->operate ("get", value::object ()).data.at ("tree").at ("text").get<std::string> (),
              std::string ("first"));
    QCOMPARE (first->properties ().at ("path").back ().get<int> (), 1);
    QCOMPARE (first->operate ("insert_after", {{"siblings", value::array ({value {{"text", "after"}}})}}).status,
              std::string ("OK"));
    QCOMPARE (first->operate ("insert_before", {{"siblings", value::array ({value {{"text", "before"}}})}}).status,
              std::string ("OK"));
    QCOMPARE (first->properties ().at ("path").back ().get<int> (), 2);
    QCOMPARE (body->operate ("get", value::object ()).data.at ("tree").at ("children")[3],
              value ({{"text", "after"}}));
    auto written= first->operate ("set", {{"tree", {{"text", "changed"}}}});
    QVERIFY2 (written.status == "OK", written.data.dump ().c_str ());
    QVERIFY (written.data.at ("committed").get<bool> ());
    QCOMPARE (first->operate ("get", value::object ()).status, std::string ("STALE"));
    QCOMPARE (transclusion->operate ("set_tag", {{"tag", "custom-link"}}).status, std::string ("OK"));
    QCOMPARE (transclusion->properties ().at ("tag").get<std::string> (), std::string ("custom-link"));
    QCOMPARE (transclusion->operate ("erase", value::object ()).status, std::string ("OK"));
    QCOMPARE (transclusion->operate ("get", value::object ()).status, std::string ("STALE"));
    QCOMPARE (competing->identity (), envelope->identity ());
    QCOMPARE (competing->operate ("get", value::object ()).data,
              envelope->operate ("get", value::object ()).data);
    auto insert= [body] (const char* text) {
      return body->operate ("insert", {{"index", 0}, {"children", value::array ({value {{"text", text}}})}});
    };
    auto one= std::async (std::launch::async, insert, "one");
    auto two= std::async (std::launch::async, insert, "two");
    QCOMPARE (one.get ().status, std::string ("OK"));
    QCOMPARE (two.get ().status, std::string ("OK"));
    QCOMPARE (body->properties ().at ("arity").get<int> (), 6);
    auto persisted= run ("@/vaults/@/filesystem/Write.ath/saved/body/[0]");
    QVERIFY2 (persisted.state == resolution_result::status::complete, persisted.error.c_str ());
    QCOMPARE (persisted.leaves.size (), std::size_t (1));
    QCOMPARE (persisted.tree.back ()->accessor->operate ("get", value::object ()).data.at ("tree"),
              body->operate ("get", value::object ()).data.at ("tree"));
    QCOMPARE (persisted.tree.back ()->accessor->identity (), body->identity ());
    { std::ofstream changed (root / "Write.ath", std::ios::app); changed << "\n"; }
    QCOMPARE (body->operate ("set", {{"tree", {{"text", "outdated node"}}}}).status, std::string ("STALE"));
    QCOMPARE (competing->operate ("get", value::object ()).status, std::string ("OK"));
  }
  {
    std::ofstream empty (root / "Empty.ath");
    empty << "<TeXmacs|" << "2.1.4" << ">\n\n<style|generic>\n";
  }
  auto empty_file= run ("@/vaults/@/filesystem/Empty.ath");
  QVERIFY2 (empty_file.state == resolution_result::status::complete, empty_file.error.c_str ());
  QVERIFY (empty_file.tree.back ()->accessor->operate ("check", value::object ()).data.at ("valid").get<bool> ());
  auto empty_document= run ("@/vaults/@/filesystem/Empty.ath/saved");
  QVERIFY2 (empty_document.state == resolution_result::status::complete, empty_document.error.c_str ());
  QCOMPARE (empty_document.leaves.size (), std::size_t (1));
  auto empty= empty_document.tree.back ()->accessor;
  const value body_text {{"tag", "document"}, {"children", value::array ({value {{"text", "created body"}}})}};
  auto populated= empty->operate ("insert", {{"index", empty->properties ().at ("arity")},
    {"children", value::array ({value {{"tag", "body"}, {"children", value::array ({body_text})}}})}});
  QVERIFY2 (populated.status == "OK", populated.data.dump ().c_str ());
  auto populated_body= run ("@/vaults/@/filesystem/Empty.ath/saved/body/[0]/[0]");
  QVERIFY2 (populated_body.state == resolution_result::status::complete, populated_body.error.c_str ());
  QCOMPARE (populated_body.leaves.size (), std::size_t (1));
  QCOMPARE (populated_body.tree.back ()->accessor->operate ("get", value::object ()).data.at ("tree").at ("text").get<std::string> (),
            std::string ("created body"));
  std::filesystem::create_directories (root / "subdir");
  std::filesystem::create_symlink (root / "Note two.ath", root / "subdir" / "alias.ath");
  std::filesystem::create_directory_symlink (root, root / "subdir" / "cycle");
  std::filesystem::create_symlink ("/etc/passwd", root / "outside");
  result= run (R"(@/vaults/@/filesystem/??($name = "alias.ath"))");
  QVERIFY2 (result.state == resolution_result::status::complete, result.error.c_str ());
  QCOMPARE (result.leaves.size (), std::size_t (1));
  QCOMPARE (result.tree.back ()->accessor->properties ().at ("absolute_path").get<std::string> (),
            (root / "Note two.ath").string ());
  result= run (R"(@/vaults/@/???($name = "alias.ath" | $max_depth = 16))");
  QVERIFY2 (result.state == resolution_result::status::complete, result.error.c_str ());
  QCOMPARE (result.leaves.size (), std::size_t (1));
  result= run ("@/vaults/@/filesystem/outside");
  QVERIFY (result.state == resolution_result::status::fault);
  result= run ("@/vaults/@/filesystem/..");
  QVERIFY (result.state == resolution_result::status::fault);
  result= run (R"(@/vaults/@/filesystem/?($type = "file")[0])");
  QVERIFY (result.state == resolution_result::status::fault);
  result= run (R"(@/vaults/@/namespaces/?($name = "Child")[0])");
  QVERIFY (result.state == resolution_result::status::fault);
  std::filesystem::rename (root / "Note one.ath", root / "old.ath");
  { std::ofstream replacement (root / "Note one.ath"); replacement << "new"; }
  QCOMPARE (file_accessor->operate ("get", value::object ()).status, std::string ("STALE"));
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
