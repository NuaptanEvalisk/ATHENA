/******************************************************************************
* MODULE     : vault_map_sqlite_test.cpp
* DESCRIPTION: Tests for non-temporal SQLite Vault maps
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
*******************************************************************************/

#include <QtTest/QtTest>
#include <QTemporaryDir>

#include "vault_map_sqlite.hpp"
#include "reference_graph_cache.hpp"
#include "Data/Convert/Xml/document_file_codec.hpp"
#include "Data/Convert/Xml/document_upgrade_file.hpp"
#include "Subsystems/RAG/rag_index.hpp"
#include "vault_safe_rename.hpp"
#include "vaultfile_json.hpp"
#include "vault.hpp"
#include "transclusion_cache.hpp"
#include "link_peek.hpp"
#include "node_metadata.hpp"
#include "Scheme/Scheme/native_interfaces.hpp"
#include "convert.hpp"
#include "drd_std.hpp"
#include "file.hpp"
#include "url.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <sqlite3.h>

bool headless_mode= true;
bool is_headless () { return true; }

class TestVaultMapSqlite: public QObject {
  Q_OBJECT

private slots:
  void initTestCase ();
  void crudAndReverseLookup ();
  void rewriteAnchorsTransactionally ();
  void validatesSqliteMapPaths ();
  void pathRenamePreservesIdentityAndBoundaries ();
  void structuralRewritePreservesRelativePathsAndHints ();
  void recoversInterruptedDirectoryRename ();
  void extractsDocumentReferencesWithoutHints ();
  void cachesBoundedAndUnlimitedReferenceGraphs ();
  void gatesBareWikilinksOnNodeModelVersion ();
  void cachesAndInvalidatesStructuralTransclusions ();
  void rebuildsOldRagDatabaseAndPreservesV3SemanticRewrite ();
  void ragGenerationCommitIsAtomicAndReusesShiftedChunks ();
  void ragSweepExcludesInternalTrees ();
  void ragEnunciationChunksAreUnique ();
};

namespace {

bool
tree_contains_label (tree value) {
  if (is_func (value, LABEL)) return true;
  if (is_atomic (value)) return false;
  for (int i=0; i<N(value); ++i)
    if (tree_contains_label (value[i])) return true;
  return false;
}

bool
tree_contains_text (tree value, string text) {
  if (is_atomic (value)) return occurs (text, value->label);
  for (int i=0; i<N(value); ++i)
    if (tree_contains_text (value[i], text)) return true;
  return false;
}

bool
sqlite_exec_test (const std::filesystem::path& database,
                  const std::string& sql, std::string& error) {
  sqlite3* db= nullptr;
  if (sqlite3_open (database.string ().c_str (), &db) != SQLITE_OK) {
    error= db ? sqlite3_errmsg (db) : "failed to open sqlite database";
    if (db) sqlite3_close (db);
    return false;
  }
  char* message= nullptr;
  const int rc= sqlite3_exec (db, sql.c_str (), nullptr, nullptr, &message);
  if (rc != SQLITE_OK) {
    error= message ? message : sqlite3_errmsg (db);
    sqlite3_free (message);
    sqlite3_close (db);
    return false;
  }
  sqlite3_close (db);
  return true;
}

bool
sqlite_scalar_test (const std::filesystem::path& database,
                    const std::string& sql, std::string& value,
                    std::string& error) {
  sqlite3* db= nullptr;
  if (sqlite3_open_v2 (database.string ().c_str (), &db, SQLITE_OPEN_READONLY,
                       nullptr) != SQLITE_OK) {
    error= db ? sqlite3_errmsg (db) : "failed to open sqlite database";
    if (db) sqlite3_close (db);
    return false;
  }
  sqlite3_stmt* statement= nullptr;
  if (sqlite3_prepare_v2 (db, sql.c_str (), -1, &statement, nullptr) != SQLITE_OK) {
    error= sqlite3_errmsg (db);
    sqlite3_close (db);
    return false;
  }
  const int rc= sqlite3_step (statement);
  if (rc != SQLITE_ROW) {
    error= rc == SQLITE_DONE ? "query returned no row" : sqlite3_errmsg (db);
    sqlite3_finalize (statement);
    sqlite3_close (db);
    return false;
  }
  const unsigned char* text= sqlite3_column_text (statement, 0);
  value= text ? reinterpret_cast<const char*> (text) : std::string ();
  sqlite3_finalize (statement);
  sqlite3_close (db);
  return true;
}

string
first_image_path (tree value) {
  if (is_func (value, IMAGE) && N(value) > 0 && is_atomic (value[0]))
    return value[0]->label;
  if (!is_atomic (value))
    for (int i=0; i<N(value); ++i) {
      string found= first_image_path (value[i]);
      if (found != "") return found;
    }
  return "";
}

string
first_hlink_target (tree value) {
  if (is_compound (value, "hlink", 2) && is_atomic (value[1]))
    return value[1]->label;
  if (!is_atomic (value))
    for (int i=0; i<N(value); ++i) {
      string found= first_hlink_target (value[i]);
      if (found != "") return found;
    }
  return "";
}

} // namespace

void
TestVaultMapSqlite::initTestCase () {
  init_std_drd ();
}

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
TestVaultMapSqlite::validatesSqliteMapPaths () {
  std::string resolved;
  std::string error;
  QVERIFY2 (athena_vault_map_prepare ("data/map.sqlite", resolved, error),
            error.c_str ());
  QCOMPARE (resolved, std::string ("data/map.sqlite"));
  for (const std::string& path:
       {"map.tmdb", "map.json", "../map.sqlite", "/tmp/map.sqlite", ""}) {
    error.clear ();
    QVERIFY (!athena_vault_map_prepare (path, resolved, error));
    QVERIFY (!error.empty ());
  }
}

void
TestVaultMapSqlite::pathRenamePreservesIdentityAndBoundaries () {
  QTemporaryDir temporary;
  QVERIFY (temporary.isValid ());
  std::filesystem::path db= std::filesystem::path (
    temporary.path ().toStdString ()) / "map.sqlite";
  AthenaVaultMapSqlite map;
  std::string error;
  QVERIFY2 (map.open (db, true, error), error.c_str ());
  QVERIFY2 (map.set_node ({"a", "Old/A.ath", "begin", "end"}, error),
            error.c_str ());
  QVERIFY2 (map.set_node ({"b", "Old/Sub/B.ath", "", "anchor"}, error),
            error.c_str ());
  QVERIFY2 (map.set_node ({"c", "Oldish/C.ath", "", ""}, error),
            error.c_str ());
  size_t count= 0;
  QVERIFY2 (map.count_path_rename ("Old", true, count, error), error.c_str ());
  QCOMPARE (count, (size_t) 2);
  AthenaVaultMapRenameOperation operation;
  operation.operation_id= "rename-test";
  operation.old_path= "Old";
  operation.new_path= "New";
  operation.is_directory= true;
  operation.phase= "prepared";
  QVERIFY2 (map.prepare_path_rename (operation, error), error.c_str ());
  size_t changed= 0;
  QVERIFY2 (map.apply_path_rename (operation.operation_id, changed, error),
            error.c_str ());
  QCOMPARE (changed, (size_t) 2);
  AthenaVaultMapNode node;
  bool found= false;
  QVERIFY2 (map.get_node ("a", node, found, error), error.c_str ());
  QCOMPARE (node.path, std::string ("New/A.ath"));
  QCOMPARE (node.anchor_begin, std::string ("begin"));
  QCOMPARE (node.anchor_end, std::string ("end"));
  QVERIFY2 (map.get_node ("c", node, found, error), error.c_str ());
  QCOMPARE (node.path, std::string ("Oldish/C.ath"));
  QVERIFY2 (map.finish_path_rename (operation.operation_id, error),
            error.c_str ());
}

void
TestVaultMapSqlite::structuralRewritePreservesRelativePathsAndHints () {
  QTemporaryDir temporary;
  QVERIFY (temporary.isValid ());
  std::filesystem::path root (temporary.path ().toStdString ());
  std::filesystem::create_directories (root / "Old/assets");
  tree image (IMAGE);
  image << tree ("Old/assets/pic.png") << tree ("") << tree ("")
        << tree ("") << tree ("");
  tree link (HLINK);
  link << tree ("hint") << tree ("tmfs://wikilink/uuid/Old/");
  tree document (DOCUMENT);
  document << image << link;
  size_t replacements= 0;
  tree rewritten= vault_safe_rename_rewrite_tree (
    document, root / "Outside.ath", root / "Outside.ath", root / "Old",
    root / "New", replacements);
  QCOMPARE (replacements, (size_t) 1);
  QCOMPARE (std::string (as_charp (tree_as_string (rewritten[0][0]))),
            std::string ("New/assets/pic.png"));
  QCOMPARE (std::string (as_charp (tree_as_string (rewritten[1][1]))),
            std::string ("tmfs://wikilink/uuid/Old/"));

  tree internal_image (IMAGE);
  internal_image << tree ("assets/pic.png") << tree ("") << tree ("")
                 << tree ("") << tree ("");
  replacements= 0;
  tree internal= vault_safe_rename_rewrite_tree (
    internal_image, root / "Old/Inside.ath", root / "New/Inside.ath",
    root / "Old", root / "New", replacements);
  QCOMPARE (replacements, (size_t) 0);
  QCOMPARE (std::string (as_charp (tree_as_string (internal[0]))),
            std::string ("assets/pic.png"));
}

void
TestVaultMapSqlite::recoversInterruptedDirectoryRename () {
  QTemporaryDir temporary;
  QVERIFY (temporary.isValid ());
  std::filesystem::path root (temporary.path ().toStdString ());
  AthenaVaultfileInfo info;
  info.map_path= "map.sqlite";
  std::string error;
  QVERIFY2 (athena_vaultfile_write (root, info, error), error.c_str ());
  std::filesystem::create_directories (root / "Old");
  {
    std::ofstream original (root / "Old/Note.ath");
    original << "original";
    std::ofstream stage (
      root / "Old/Note.ath.athena-safe-rename-recovery.tmp");
    stage << "rewritten";
  }

  AthenaVaultMapSqlite map;
  QVERIFY2 (map.open (root / "map.sqlite", true, error), error.c_str ());
  QVERIFY2 (map.set_node ({"note", "Old/Note.ath", "", ""}, error),
            error.c_str ());
  AthenaVaultMapRenameOperation operation;
  operation.operation_id= "recovery";
  operation.old_path= "Old";
  operation.new_path= "New";
  operation.is_directory= true;
  operation.phase= "prepared";
  QVERIFY2 (map.prepare_path_rename (operation, error), error.c_str ());
  map.close ();
  std::filesystem::rename (root / "Old", root / "New");

  QVERIFY2 (vault_safe_rename_recover (root, "map.sqlite", error),
            error.c_str ());
  std::ifstream result (root / "New/Note.ath");
  QCOMPARE (std::string (std::istreambuf_iterator<char> (result), {}),
            std::string ("rewritten"));
  QVERIFY (!std::filesystem::exists (
    root / "New/Note.ath.athena-safe-rename-recovery.tmp"));

  QVERIFY2 (map.open (root / "map.sqlite", false, error), error.c_str ());
  AthenaVaultMapNode node;
  bool found= false;
  QVERIFY2 (map.get_node ("note", node, found, error), error.c_str ());
  QVERIFY (found);
  QCOMPARE (node.path, std::string ("New/Note.ath"));
  std::vector<AthenaVaultMapRenameOperation> pending;
  QVERIFY2 (map.pending_path_renames (pending, error), error.c_str ());
  QVERIFY (pending.empty ());
  QVERIFY (std::filesystem::exists (
    root / ".backup/safe-rename/recovery/New/Note.ath"));
}

void
TestVaultMapSqlite::extractsDocumentReferencesWithoutHints () {
  tree wikilink (HLINK);
  wikilink << tree ("visible")
           << tree ("tmfs://wikilink/uuid%20one/ignored/file/hints");
  tree cardlink (make_tree_label ("cardlink"));
  cardlink << tree ("card") << tree ("tmfs://wikilink/uuid-two/");
  tree transclusion (TRANSCLUDE);
  transclusion << tree ("uuid-three") << tree ("ignored path hint")
               << tree ("ignored anchor hint");
  tree external (HLINK);
  external << tree ("web") << tree ("https://example.com/uuid-four");
  tree document (DOCUMENT);
  document << wikilink << cardlink << transclusion << external;
  QVERIFY (is_func (wikilink, HLINK));
  QVERIFY (is_compound (cardlink, "cardlink"));
  QVERIFY (is_func (transclusion, TRANSCLUDE));

  std::vector<AthenaDocumentReference> references=
    athena_collect_document_references (document);
  QCOMPARE (references.size (), (size_t) 3);
  QCOMPARE (references[0].uuid, std::string ("uuid one"));
  QCOMPARE (references[0].kind, std::string ("wikilink"));
  QCOMPARE (references[1].uuid, std::string ("uuid-three"));
  QCOMPARE (references[1].kind, std::string ("transclusion"));
  QCOMPARE (references[2].uuid, std::string ("uuid-two"));
  QCOMPARE (references[2].kind, std::string ("wikilink"));
}

void
TestVaultMapSqlite::cachesBoundedAndUnlimitedReferenceGraphs () {
  QTemporaryDir temporary;
  QVERIFY (temporary.isValid ());
  std::filesystem::path root (temporary.path ().toStdString ());
  AthenaVaultfileInfo info;
  std::string vaultfile_error;
  QVERIFY2 (athena_vaultfile_write (root, info, vaultfile_error),
            vaultfile_error.c_str ());

  auto write_document= [&] (const std::string& name, tree body) {
    tree content (DOCUMENT);
    content << body;
    tree document (DOCUMENT);
    document << compound ("TeXmacs", "2.1.4")
             << compound ("style", tuple ("generic"))
             << compound ("body", content);
    return !save_string (
      url_system (string ((root / name).string ().c_str ())),
      tree_to_texmacs (document));
  };
  auto wikilink= [] (const char* uuid) {
    tree link (make_tree_label ("hlink"));
    link << tree ("display")
         << tree (string ("tmfs://wikilink/") * string (uuid) * "/hint");
    return link;
  };
  auto transclusion= [] (const char* uuid) {
    tree link (make_tree_label ("transclude"));
    link << tree (uuid) << tree ("wrong/file/hint.ath") << tree ("hint");
    return link;
  };

  tree a_body (CONCAT);
  a_body << wikilink ("uuid-b") << transclusion ("uuid-c");
  QVERIFY (write_document ("A.ath", a_body));
  QVERIFY (write_document ("B.ath", wikilink ("uuid-c")));
  QVERIFY (write_document ("C.ath", wikilink ("uuid-d")));
  QVERIFY (write_document ("D.ath", wikilink ("uuid-e")));
  QVERIFY (write_document ("E.ath", tree ("E")));

  string serialized;
  QVERIFY (!load_string (
    url_system (string ((root / "A.ath").string ().c_str ())), serialized,
    false));
  std::vector<AthenaDocumentReference> parsedReferences=
    athena_collect_document_references (texmacs_document_to_tree (serialized));
  QCOMPARE (parsedReferences.size (), (size_t) 2);

  string load_error= vault_load (
    url_system (string (root.string ().c_str ())), "Reference test",
    "maps.sqlite");
  QVERIFY2 (load_error == "", as_charp (load_error));
  vault_set_node ("uuid-b", "B.ath", "", "");
  vault_set_node ("uuid-c", "C.ath", "", "");
  vault_set_node ("uuid-d", "D.ath", "", "");
  vault_set_node ("uuid-e", "E.ath", "", "");

  std::vector<AthenaReferenceGraphEdge> edges;
  std::string error;
  QVERIFY2 (athena_reference_graph_query (
    "A.ath", 1, edges, {}, error), error.c_str ());
  QCOMPARE (edges.size (), (size_t) 2);
  QCOMPARE (edges[0].referenced_path, std::string ("B.ath"));
  QCOMPARE (edges[0].referencing_path, std::string ("A.ath"));
  QCOMPARE (edges[1].referenced_path, std::string ("C.ath"));
  QCOMPARE (edges[1].referencing_path, std::string ("A.ath"));

  edges.clear ();
  QVERIFY2 (athena_reference_graph_query (
    "A.ath", 2, edges, {}, error), error.c_str ());
  QCOMPARE (edges.size (), (size_t) 4);
  QVERIFY (std::any_of (edges.begin (), edges.end (), [] (const auto& edge) {
    return edge.referenced_path == "C.ath" &&
           edge.referencing_path == "B.ath";
  }));
  QVERIFY (std::any_of (edges.begin (), edges.end (), [] (const auto& edge) {
    return edge.referenced_path == "D.ath" &&
           edge.referencing_path == "C.ath";
  }));
  QVERIFY (!std::any_of (edges.begin (), edges.end (), [] (const auto& edge) {
    return edge.referenced_path == "E.ath";
  }));

  edges.clear ();
  QVERIFY2 (athena_reference_graph_query (
    "A.ath", 0, edges, {}, error), error.c_str ());
  QCOMPARE (edges.size (), (size_t) 5);
  QVERIFY (std::any_of (edges.begin (), edges.end (), [] (const auto& edge) {
    return edge.referenced_path == "E.ath" &&
           edge.referencing_path == "D.ath";
  }));
  QVERIFY (std::filesystem::exists (
    root / ".athena/reference-graph.sqlite"));

  // A storage-only rewrite must not rebuild the logical reference rows.
  const std::filesystem::path graph_db= root / ".athena/reference-graph.sqlite";
  std::string semantic_before;
  QVERIFY2 (sqlite_scalar_test (
    graph_db, "SELECT semantic_hash FROM documents WHERE path='A.ath';",
    semantic_before, error), error.c_str ());
  QVERIFY (!semantic_before.empty ());
  QVERIFY2 (sqlite_exec_test (graph_db,
    "CREATE TABLE IF NOT EXISTS test_reference_deletes(n INTEGER);"
    "DELETE FROM test_reference_deletes;"
    "DROP TRIGGER IF EXISTS test_a_reference_delete;"
    "CREATE TRIGGER test_a_reference_delete AFTER DELETE ON document_references "
    "WHEN OLD.source_path='A.ath' BEGIN "
    "INSERT INTO test_reference_deletes VALUES(1); END;", error), error.c_str ());
  // Downgrade to the v1 shape and ensure reopening migrates in place without
  // deleting the already cached logical edges.
  QVERIFY2 (sqlite_exec_test (graph_db,
    "PRAGMA user_version=1;"
    "ALTER TABLE documents DROP COLUMN semantic_hash;", error), error.c_str ());
  edges.clear ();
  QVERIFY2 (athena_reference_graph_query (
    "A.ath", 1, edges, {}, error), error.c_str ());
  QCOMPARE (edges.size (), (size_t) 2);
  std::string migrated_version;
  QVERIFY2 (sqlite_scalar_test (
    graph_db, "PRAGMA user_version;", migrated_version, error), error.c_str ());
  QCOMPARE (migrated_version, std::string ("2"));
  std::string semantic_migrated;
  QVERIFY2 (sqlite_scalar_test (
    graph_db, "SELECT semantic_hash FROM documents WHERE path='A.ath';",
    semantic_migrated, error), error.c_str ());
  QCOMPARE (semantic_migrated, semantic_before);
  std::string migration_deletes;
  QVERIFY2 (sqlite_scalar_test (
    graph_db, "SELECT COUNT(*) FROM test_reference_deletes;",
    migration_deletes, error), error.c_str ());
  QCOMPARE (migration_deletes, std::string ("0"));

  auto decoded_a= athena::document::decode_document_bytes (
    std::string_view (as_charp (serialized), (std::size_t) N(serialized)));
  const std::string xml_a= athena::document::write_xml (decoded_a.document);
  {
    std::ofstream output (root / "A.ath", std::ios::binary | std::ios::trunc);
    output.write (xml_a.data (), std::streamsize (xml_a.size ()));
  }
  edges.clear ();
  QVERIFY2 (athena_reference_graph_query (
    "A.ath", 1, edges, {}, error), error.c_str ());
  QCOMPARE (edges.size (), (size_t) 2);
  QVERIFY (std::any_of (edges.begin (), edges.end (), [] (const auto& edge) {
    return edge.referenced_path == "B.ath" && edge.referencing_path == "A.ath";
  }));
  QVERIFY (std::any_of (edges.begin (), edges.end (), [] (const auto& edge) {
    return edge.referenced_path == "C.ath" && edge.referencing_path == "A.ath";
  }));
  std::string semantic_after;
  QVERIFY2 (sqlite_scalar_test (
    graph_db, "SELECT semantic_hash FROM documents WHERE path='A.ath';",
    semantic_after, error), error.c_str ());
  QCOMPARE (semantic_after, semantic_before);
  std::string delete_count;
  QVERIFY2 (sqlite_scalar_test (
    graph_db, "SELECT COUNT(*) FROM test_reference_deletes;",
    delete_count, error), error.c_str ());
  QCOMPARE (delete_count, std::string ("0"));

  // Changing only the authoritative map must redirect the cached edge even
  // though the source document and its optional hints are unchanged.
  vault_set_node ("uuid-b", "D.ath", "", "");
  edges.clear ();
  QVERIFY2 (athena_reference_graph_query (
    "A.ath", 1, edges, {}, error), error.c_str ());
  QCOMPARE (edges.size (), (size_t) 2);
  QVERIFY (std::any_of (edges.begin (), edges.end (), [] (const auto& edge) {
    return edge.referenced_path == "D.ath" &&
           edge.referencing_path == "A.ath";
  }));
  vault_close ();
}

void
TestVaultMapSqlite::gatesBareWikilinksOnNodeModelVersion () {
  QTemporaryDir temporary;
  QVERIFY (temporary.isValid ());
  const std::filesystem::path base (temporary.path ().toStdString ());
  const std::string target_id= "11111111-1111-4111-8111-111111111111";
  const std::string hinted=
    "tmfs://wikilink/" + target_id + "/Wrong.ath/Wrong-anchor";
  const std::string bare= "tmfs://wikilink/" + target_id;

  auto make_document= [] (tree body) {
    tree content (DOCUMENT);
    content << body;
    tree document (DOCUMENT);
    document << compound ("style", tuple ("generic"))
             << compound ("body", content);
    return document;
  };
  auto wikilink= [&] {
    tree link (make_tree_label ("hlink"));
    link << tree ("target") << tree (hinted.c_str ());
    return link;
  };
  auto write_v1= [&] (const std::filesystem::path& root,
                       const char* name, tree body) {
    const std::string xml= athena::document::write_xml (make_document (body));
    std::ofstream output (root / name, std::ios::binary | std::ios::trunc);
    output.write (xml.data (), std::streamsize (xml.size ()));
  };
  auto write_v2= [&] (const std::filesystem::path& root,
                       const char* name, tree body) {
    const std::string xml= athena::document::write_xml_v2 (make_document (body));
    std::ofstream output (root / name, std::ios::binary | std::ios::trunc);
    output.write (xml.data (), std::streamsize (xml.size ()));
  };

  const std::filesystem::path migrated= base / "migrated";
  std::filesystem::create_directory (migrated);
  AthenaVaultfileInfo migrated_info;
  migrated_info.map_path= "maps.sqlite";
  migrated_info.node_model_version= 1;
  std::string error;
  QVERIFY2 (athena_vaultfile_write (migrated, migrated_info, error),
            error.c_str ());
  write_v2 (migrated, "A.ath", wikilink ());
  tree identified ("Native target");
  athena::node::metadata metadata;
  metadata.id= target_id;
  athena::node::set (identified, metadata);
  write_v2 (migrated, "B.ath", identified);
  write_v2 (migrated, "C.ath", tree ("Wrong map target"));

  string load_error= vault_load (
    url_system (string (migrated.string ().c_str ())), "Migrated",
    "maps.sqlite");
  QVERIFY2 (load_error == "", as_charp (load_error));
  QCOMPARE (vault_get_node_model_version (), 1);
  vault_set_node (string (target_id.c_str ()), "C.ath", "wrong", "wrong");
  tree stale_map= vault_get_node (string (target_id.c_str ()));
  QVERIFY (is_tuple (stale_map));
  QCOMPARE (stale_map[0], tree ("C.ath"));

  QVERIFY (athena_link_peek_native_target (string (hinted.c_str ())));
  QVERIFY (athena_node_reference_target (string (bare.c_str ())));
  std::vector<AthenaReferenceGraphEdge> edges;
  QVERIFY2 (athena_reference_graph_query (
    "A.ath", 1, edges, {}, error), error.c_str ());
  QCOMPARE (edges.size (), (size_t) 1);
  QCOMPARE (edges.front ().referenced_path, std::string ("B.ath"));
  QCOMPARE (edges.front ().referencing_path, std::string ("A.ath"));

  // Changing compatibility map state must not redirect a migrated wikilink.
  vault_set_node (string (target_id.c_str ()), "A.ath", "", "");
  edges.clear ();
  QVERIFY2 (athena_reference_graph_query (
    "A.ath", 1, edges, {}, error), error.c_str ());
  QCOMPARE (edges.size (), (size_t) 1);
  QCOMPARE (edges.front ().referenced_path, std::string ("B.ath"));
  vault_close ();

  const std::filesystem::path legacy= base / "legacy";
  std::filesystem::create_directory (legacy);
  AthenaVaultfileInfo legacy_info;
  legacy_info.map_path= "maps.sqlite";
  legacy_info.node_model_version= 0;
  error.clear ();
  QVERIFY2 (athena_vaultfile_write (legacy, legacy_info, error), error.c_str ());
  write_v1 (legacy, "A.ath", wikilink ());
  write_v1 (legacy, "B.ath", tree ("Legacy B"));
  write_v1 (legacy, "C.ath", tree ("Legacy C"));

  load_error= vault_load (
    url_system (string (legacy.string ().c_str ())), "Legacy", "maps.sqlite");
  QVERIFY2 (load_error == "", as_charp (load_error));
  QCOMPARE (vault_get_node_model_version (), 0);
  vault_set_node (string (target_id.c_str ()), "C.ath", "", "");
  QVERIFY (!athena_link_peek_native_target (string (hinted.c_str ())));
  QVERIFY (!athena_node_reference_target (string (bare.c_str ())));
  edges.clear ();
  QVERIFY2 (athena_reference_graph_query (
    "A.ath", 1, edges, {}, error), error.c_str ());
  QCOMPARE (edges.size (), (size_t) 1);
  QCOMPARE (edges.front ().referenced_path, std::string ("C.ath"));

  // Unmigrated vaults deliberately keep map.sqlite as identity authority.
  vault_set_node (string (target_id.c_str ()), "B.ath", "", "");
  edges.clear ();
  QVERIFY2 (athena_reference_graph_query (
    "A.ath", 1, edges, {}, error), error.c_str ());
  QCOMPARE (edges.size (), (size_t) 1);
  QCOMPARE (edges.front ().referenced_path, std::string ("B.ath"));
  vault_close ();
}

void
TestVaultMapSqlite::rebuildsOldRagDatabaseAndPreservesV3SemanticRewrite () {
  QTemporaryDir temporary;
  QVERIFY (temporary.isValid ());
  std::filesystem::path root (temporary.path ().toStdString ());
  AthenaVaultfileInfo info;
  std::string error;
  QVERIFY2 (athena_vaultfile_write (root, info, error), error.c_str ());

  tree body (DOCUMENT);
  body << tree ("Alpha topology studies open and closed subsets.")
       << tree ("Continuous maps preserve the semantic document revision.");
  tree legacy (DOCUMENT);
  legacy << compound ("TeXmacs", "2.1.4")
         << compound ("style", tuple ("generic"))
         << compound ("body", body);
  string legacy_bytes= tree_to_texmacs (legacy);
  {
    std::ofstream output (root / "Alpha.ath", std::ios::binary | std::ios::trunc);
    output.write (as_charp (legacy_bytes), N(legacy_bytes));
  }

  const std::filesystem::path database= root / ".athena/rag.sqlite";
  athena::rag::RagConfig config;
  config.vault_root= root;
  config.db_path= database;
  config.load_embedding_model= false;
  config.progress= false;
  {
    athena::rag::RagIndex index;
    QVERIFY (index.open (config));
    QVERIFY (index.scan_once ());
  }

  std::string chunk_count_before, chunk_id_before;
  std::string storage_before, semantic_before;
  QVERIFY2 (sqlite_scalar_test (
    database, "SELECT COUNT(*) FROM chunks;", chunk_count_before, error),
    error.c_str ());
  QVERIFY (std::stoll (chunk_count_before) > 0);
  QVERIFY2 (sqlite_scalar_test (
    database, "SELECT chunk_id FROM chunks ORDER BY chunk_id LIMIT 1;",
    chunk_id_before, error), error.c_str ());
  QVERIFY2 (sqlite_scalar_test (
    database, "SELECT storage_revision FROM documents WHERE rel_path='Alpha.ath';",
    storage_before, error), error.c_str ());
  QVERIFY2 (sqlite_scalar_test (
    database, "SELECT semantic_revision FROM documents WHERE rel_path='Alpha.ath';",
    semantic_before, error), error.c_str ());
  QVERIFY (!semantic_before.empty ());

  QVERIFY2 (sqlite_exec_test (database,
    "UPDATE chunks SET embedding_space='sentinel-v3' WHERE rel_path='Alpha.ath';"
    "INSERT OR REPLACE INTO embeddings(space_id,input_hash,embedding,embedding_dim) "
    "SELECT 'sentinel-v3',embedding_input_hash,x'01020304',1 FROM chunks "
    "WHERE rel_path='Alpha.ath' AND embedding_input_hash!='' LIMIT 1;", error),
    error.c_str ());

  auto migrated= athena::document::decode_document_bytes (
    std::string_view (as_charp (legacy_bytes), (std::size_t) N(legacy_bytes)));
  const std::string xml= athena::document::write_xml (migrated.document);
  {
    std::ofstream output (root / "Alpha.ath", std::ios::binary | std::ios::trunc);
    output.write (xml.data (), std::streamsize (xml.size ()));
  }
  athena::rag::RagIndex index;
  QVERIFY (index.open (config));
  QVERIFY (index.scan_once ());

  std::string chunk_count_after, chunk_id_after, embedding_hex, embedding_space;
  std::string storage_after, semantic_after;
  QVERIFY2 (sqlite_scalar_test (
    database, "SELECT COUNT(*) FROM chunks;", chunk_count_after, error),
    error.c_str ());
  QVERIFY2 (sqlite_scalar_test (
    database, "SELECT chunk_id FROM chunks ORDER BY chunk_id LIMIT 1;",
    chunk_id_after, error), error.c_str ());
  QVERIFY2 (sqlite_scalar_test (
    database, "SELECT hex(embedding) FROM embeddings "
              "WHERE space_id='sentinel-v3' LIMIT 1;",
    embedding_hex, error), error.c_str ());
  QVERIFY2 (sqlite_scalar_test (
    database, "SELECT embedding_space FROM chunks ORDER BY chunk_id LIMIT 1;",
    embedding_space, error), error.c_str ());
  QVERIFY2 (sqlite_scalar_test (
    database, "SELECT storage_revision FROM documents WHERE rel_path='Alpha.ath';",
    storage_after, error), error.c_str ());
  QVERIFY2 (sqlite_scalar_test (
    database, "SELECT semantic_revision FROM documents WHERE rel_path='Alpha.ath';",
    semantic_after, error), error.c_str ());
  QCOMPARE (chunk_count_after, chunk_count_before);
  QCOMPARE (chunk_id_after, chunk_id_before);
  QCOMPARE (embedding_hex, std::string ("01020304"));
  QCOMPARE (embedding_space, std::string ("sentinel-v3"));
  QVERIFY (storage_after != storage_before);
  QCOMPARE (semantic_after, semantic_before);

  // A pre-v3 RAG database is derived data. Reopening deliberately discards it
  // instead of migrating embeddings or schema state, then the next scan
  // rebuilds from the authoritative .ath files.
  QVERIFY2 (sqlite_exec_test (database,
    "UPDATE meta SET value='2' WHERE key='schema-version';", error),
    error.c_str ());
  {
    athena::rag::RagIndex rebuilt;
    QVERIFY (rebuilt.open (config));
    QVERIFY (rebuilt.scan_once ());
  }
  std::string rebuilt_version, rebuilt_embedding;
  QVERIFY2 (sqlite_scalar_test (
    database, "SELECT value FROM meta WHERE key='schema-version';",
    rebuilt_version, error), error.c_str ());
  QVERIFY2 (sqlite_scalar_test (
    database, "SELECT count(*) FROM embeddings;",
    rebuilt_embedding, error), error.c_str ());
  QCOMPARE (rebuilt_version, std::string ("3"));
  QCOMPARE (rebuilt_embedding, std::string ("0"));
}

void
TestVaultMapSqlite::ragSweepExcludesInternalTrees () {
  QTemporaryDir temporary;
  QVERIFY (temporary.isValid ());
  const std::filesystem::path root (temporary.path ().toStdString ());
  for (const char* rel: {"Alpha.ath", "nested/Beta.ath", ".athena/cache/C.ath",
                        ".backup/D.ath", "nested/.git/E.ath"}) {
    std::filesystem::path file= root / rel;
    std::filesystem::create_directories (file.parent_path ());
    std::ofstream output (file);
    output << "fixture";
  }
  const auto files= athena::rag::rag_document_files (root);
  QCOMPARE (files.size (), std::size_t (2));
  QVERIFY (files[0] == root / "Alpha.ath");
  QVERIFY (files[1] == root / "nested/Beta.ath");
  QVERIFY (athena::rag::rag_document_files (root, [] { return false; }).empty ());
}

void
TestVaultMapSqlite::ragEnunciationChunksAreUnique () {
  QTemporaryDir temporary;
  QVERIFY (temporary.isValid ());
  const std::filesystem::path root (temporary.path ().toStdString ());
  AthenaVaultfileInfo info;
  std::string error;
  QVERIFY2 (athena_vaultfile_write (root, info, error), error.c_str ());

  tree body (DOCUMENT);
  body << compound ("definition", "Repeated definition text")
       << compound ("definition", "Repeated definition text")
       << compound ("quote-env", compound ("note*", "Nested note text"))
       << compound ("with", "color", "red", compound ("warning", "Warning text"));
  tree document (DOCUMENT);
  document << compound ("style", tuple ("generic")) << compound ("body", body);
  const std::string xml= athena::document::write_xml (document);
  std::ofstream output (root / "Enunciations.ath", std::ios::binary);
  output.write (xml.data (), std::streamsize (xml.size ()));
  output.close ();

  athena::rag::RagConfig config;
  config.vault_root= root;
  config.db_path= root / "rag.sqlite";
  config.load_embedding_model= false;
  config.progress= false;
  athena::rag::RagIndex index;
  QVERIFY (index.open (config));
  athena::rag::RagPreparedDocument prepared;
  QVERIFY (index.prepare_document ("Enunciations.ath", "", "", prepared));
  QCOMPARE (prepared.chunks.size (), std::size_t (6));
  QVERIFY (prepared.chunks[0].chunk.chunk_id != prepared.chunks[1].chunk.chunk_id);
  QCOMPARE (prepared.chunks[3].chunk.kind, std::string ("note"));
  QCOMPARE (prepared.chunks[3].chunk.tree_path, std::string ("2.0"));
  QCOMPARE (prepared.chunks[5].chunk.kind, std::string ("warning"));
  QCOMPARE (prepared.chunks[5].chunk.tree_path, std::string ("3.2"));
  QVERIFY2 (index.commit_document (prepared, {}), index.status ().last_error.c_str ());
  // Replacing the same document must remain transactional and nonduplicating.
  QVERIFY2 (index.commit_document (prepared, {}), index.status ().last_error.c_str ());
  std::string count;
  QVERIFY2 (sqlite_scalar_test (config.db_path,
    "SELECT count(*) FROM chunks;", count, error), error.c_str ());
  QCOMPARE (count, std::string ("6"));
}

void
TestVaultMapSqlite::ragGenerationCommitIsAtomicAndReusesShiftedChunks () {
  QTemporaryDir temporary;
  QVERIFY (temporary.isValid ());
  const std::filesystem::path root (temporary.path ().toStdString ());
  AthenaVaultfileInfo info;
  std::string error;
  QVERIFY2 (athena_vaultfile_write (root, info, error), error.c_str ());

  auto write_document= [&] (std::initializer_list<const char*> paragraphs) {
    tree body (DOCUMENT);
    for (const char* paragraph: paragraphs) body << tree (paragraph);
    tree document (DOCUMENT);
    document << compound ("style", tuple ("generic"))
             << compound ("body", body);
    const std::string xml= athena::document::write_xml (document);
    std::ofstream output (root / "Alpha.ath", std::ios::binary | std::ios::trunc);
    output.write (xml.data (), std::streamsize (xml.size ()));
    output.close ();
    return athena::document::storage_bytes_fingerprint (xml);
  };

  athena::rag::RagConfig config;
  config.vault_root= root;
  config.db_path= root / "rag.sqlite";
  config.load_embedding_model= false;
  config.progress= false;
  athena::rag::RagIndex index;
  QVERIFY (index.open (config));

  const std::string first_revision= write_document ({
    "Alpha topology paragraph has enough searchable semantic content.",
    "Beta geometry paragraph also has enough searchable semantic content."
  });
  athena::rag::RagPreparedDocument first;
  QVERIFY (index.prepare_document (
    "Alpha.ath", first_revision, "test-embedding-space", first));
  QCOMPARE (first.missing_embedding_indices.size (), std::size_t (2));
  const std::vector<std::vector<float>> first_vectors {
    {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}
  };
  QVERIFY (index.commit_document (first, first_vectors, [] { return true; }));

  const std::string second_revision= write_document ({
    "Inserted algebra paragraph is the only new semantic embedding input.",
    "Alpha topology paragraph has enough searchable semantic content.",
    "Beta geometry paragraph also has enough searchable semantic content."
  });
  athena::rag::RagPreparedDocument second;
  QVERIFY (index.prepare_document (
    "Alpha.ath", second_revision, "test-embedding-space", second));
  QCOMPARE (second.chunks.size (), std::size_t (3));
  QCOMPARE (second.missing_embedding_indices.size (), std::size_t (1));
  QCOMPARE (second.missing_embedding_indices.front (), std::size_t (0));
  QVERIFY (first.chunks[0].chunk.chunk_id != second.chunks[1].chunk.chunk_id);
  QVERIFY (first.chunks[1].chunk.chunk_id != second.chunks[2].chunk.chunk_id);
  QVERIFY (!second.chunks[1].cached_embedding.empty ());
  QVERIFY (!second.chunks[2].cached_embedding.empty ());

  // The new save supersedes this prepared generation before commit. Nothing
  // from it, including its newly computed vector, may become durable.
  const std::vector<std::vector<float>> second_vectors {{0.0f, 0.0f, 1.0f}};
  QVERIFY (!index.commit_document (second, second_vectors, [] { return false; }));
  std::string stored_revision, chunk_count, embedding_count;
  QVERIFY2 (sqlite_scalar_test (
    config.db_path,
    "SELECT storage_revision FROM documents WHERE rel_path='Alpha.ath';",
    stored_revision, error), error.c_str ());
  QVERIFY2 (sqlite_scalar_test (
    config.db_path, "SELECT count(*) FROM chunks;", chunk_count, error),
    error.c_str ());
  QVERIFY2 (sqlite_scalar_test (
    config.db_path, "SELECT count(*) FROM embeddings;", embedding_count, error),
    error.c_str ());
  QCOMPARE (stored_revision, first_revision);
  QCOMPARE (chunk_count, std::string ("2"));
  QCOMPARE (embedding_count, std::string ("2"));

  QVERIFY (index.commit_document (second, second_vectors, [] { return true; }));
  QVERIFY2 (sqlite_scalar_test (
    config.db_path,
    "SELECT storage_revision FROM documents WHERE rel_path='Alpha.ath';",
    stored_revision, error), error.c_str ());
  QVERIFY2 (sqlite_scalar_test (
    config.db_path, "SELECT count(*) FROM chunks;", chunk_count, error),
    error.c_str ());
  QVERIFY2 (sqlite_scalar_test (
    config.db_path, "SELECT count(*) FROM embeddings;", embedding_count, error),
    error.c_str ());
  QCOMPARE (stored_revision, second_revision);
  QCOMPARE (chunk_count, std::string ("3"));
  QCOMPARE (embedding_count, std::string ("3"));
}

void
TestVaultMapSqlite::cachesAndInvalidatesStructuralTransclusions () {
  QTemporaryDir temporary;
  QVERIFY (temporary.isValid ());
  std::filesystem::path root (temporary.path ().toStdString ());
  AthenaVaultfileInfo info;
  std::string vaultfile_error;
  QVERIFY2 (athena_vaultfile_write (root, info, vaultfile_error),
            vaultfile_error.c_str ());
  std::filesystem::create_directories (root / "assets");

  auto write_source= [&] (const char* payload) {
    tree body (DOCUMENT);
    body << tree ("context before range")
         << compound ("label", "begin")
         << tree (payload)
         << compound ("image", "assets/example.png")
         << compound ("label", "end")
         << tree ("context after range");
    tree document (DOCUMENT);
    document << compound ("TeXmacs", "2.1.4")
             << compound ("style", tuple ("generic"))
             << compound ("body", body);
    return !save_string (
      url_system (string ((root / "Source.ath").string ().c_str ())),
      tree_to_texmacs (document));
  };

  QVERIFY (write_source ("first payload"));
  string serialized;
  QVERIFY (!load_string (
    url_system (string ((root / "Source.ath").string ().c_str ())),
    serialized, false));
  tree parsed= texmacs_document_to_tree (serialized);
  string parsed_tree= tree_to_texmacs (parsed);
  QVERIFY2 (tree_contains_label (parsed), as_charp (parsed_tree));
  string load_error= vault_load (
    url_system (string (root.string ().c_str ())), "Transclusion cache test",
    "maps.sqlite");
  QVERIFY2 (load_error == "", as_charp (load_error));
  vault_set_node ("range", "Source.ath", "begin", "end");

  tree transclusion (make_tree_label ("transclude"));
  transclusion << tree ("range") << tree ("Source.ath")
               << tree ("begin") << tree ("end");
  AthenaTransclusionResolution first=
    athena_resolve_transclusion_content (transclusion);
  string first_error= tree_as_string (first.content);
  QVERIFY2 (first.ok, as_charp (first_error));
  QVERIFY (is_func (first.content, DOCUMENT));
  QVERIFY (!tree_contains_label (first.content));
  QVERIFY (tree_contains_text (first.content, "first payload"));
  QVERIFY (!tree_contains_text (first.content, "context before range"));
  QVERIFY (!tree_contains_text (first.content, "context after range"));
  string image_path= first_image_path (first.content);
  QVERIFY (starts (image_path, "/"));
  QVERIFY (ends (image_path, "/assets/example.png"));

  AthenaTransclusionResolution repeated=
    athena_resolve_transclusion_content (transclusion);
  QCOMPARE (repeated.cache_key, first.cache_key);
  QVERIFY (repeated.content == first.content);

  tree displayed= athena_resolve_transclusion_display (transclusion);
  QCOMPARE (first_hlink_target (displayed),
            string ("tmfs://transclusion-source/range"));
  QVERIFY (athena_link_peek_target ("tmfs://transclusion-source/range"));
  url peek_source;
  tree peek= athena_link_peek_document (
    "tmfs://transclusion-source/range", peek_source);
  QVERIFY (tree_contains_text (peek, "first payload"));
  QVERIFY (tree_contains_text (peek, "context before range"));
  QVERIFY (tree_contains_text (peek, "context after range"));
  url expected_source=
    url_system (string ((root / "Source.ath").string ().c_str ()));
  QVERIFY (peek_source == expected_source);

  QVERIFY (write_source ("changed payload with a different size"));
  AthenaTransclusionResolution changed=
    athena_resolve_transclusion_content (transclusion);
  QVERIFY (changed.ok);
  QVERIFY (changed.cache_key != first.cache_key);
  QVERIFY (tree_contains_text (changed.content, "changed payload"));
  QVERIFY (!tree_contains_text (changed.content, "first payload"));
  vault_close ();
}

QTEST_MAIN(TestVaultMapSqlite)
#include "vault_map_sqlite_test.moc"
