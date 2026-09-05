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
#include "vault_safe_rename.hpp"
#include "vaultfile_json.hpp"
#include "vault.hpp"
#include "transclusion_cache.hpp"
#include "convert.hpp"
#include "drd_std.hpp"
#include "file.hpp"
#include "url.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>

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
  void cachesAndInvalidatesStructuralTransclusions ();
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
    body << compound ("label", "begin")
         << tree (payload)
         << compound ("image", "assets/example.png")
         << compound ("label", "end")
         << tree ("outside range");
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
  QVERIFY (!tree_contains_text (first.content, "outside range"));
  string image_path= first_image_path (first.content);
  QVERIFY (starts (image_path, "/"));
  QVERIFY (ends (image_path, "/assets/example.png"));

  AthenaTransclusionResolution repeated=
    athena_resolve_transclusion_content (transclusion);
  QCOMPARE (repeated.cache_key, first.cache_key);
  QVERIFY (repeated.content == first.content);

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
