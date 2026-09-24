/******************************************************************************
* MODULE     : vault_format_upgrade_test.cpp
* DESCRIPTION: Isolated whole-vault commit, cancellation and index preservation tests
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#include <QtTest/QtTest>
#include <QTemporaryDir>
#include "Xml/vault_format_upgrade.hpp"
#include "Xml/document_file_codec.hpp"
#include "vault_directory_lease.hpp"
#include "drd_std.hpp"
#include "convert.hpp"
#include <QByteArray>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <sqlite3.h>
#include <sys/stat.h>

bool headless_mode= true;
bool is_headless () { return true; }
using namespace athena::document;
namespace fs= std::filesystem;
namespace {
const std::string legacy= "<TeXmacs|2.1.4>\n\n<\\body>\ncaf\xe9 \\<alpha\\>\n</body>\n";
void put (const fs::path& path, const std::string& text) {
  std::ofstream out (path, std::ios::binary);
  out.write (text.data (), text.size ()); out.close ();
  if (!out) throw std::runtime_error ("Cannot write fixture");
}
std::string get (const fs::path& path) {
  std::ifstream in (path, std::ios::binary);
  return {std::istreambuf_iterator<char> (in), std::istreambuf_iterator<char> ()};
}
fs::path vault (const QTemporaryDir& tmp) {
  auto root= fs::path (tmp.path ().toStdString ()) / "vault";
  fs::create_directory (root);
  put (root / "Vaultfile.json", "{\"name\":\"Migration test\"}");
  put (root / "a.ath", legacy);
  return root;
}
std::string sql (const fs::path& path, const std::string& query) {
  sqlite3* db= nullptr;
  if (sqlite3_open (path.c_str (), &db) != SQLITE_OK) throw std::runtime_error ("Fixture SQLite open");
  std::string out;
  auto callback= [] (void* dest, int n, char** fields, char**) {
    for (int i=0; i<n; ++i) *static_cast<std::string*> (dest) += fields[i] ? fields[i] : "NULL";
    return 0;
  };
  int code= sqlite3_exec (db, query.c_str (), callback, &out, nullptr);
  const std::string error= sqlite3_errmsg (db);
  sqlite3_close (db);
  if (code != SQLITE_OK) throw std::runtime_error (error);
  return out;
}
}
class TestVaultFormatUpgrade: public QObject {
  Q_OBJECT
private slots:
  void initTestCase () { init_std_drd (); }
  void successfulAtomicUpgrade () {
    QTemporaryDir tmp;
    const auto root= vault (tmp);
    fs::create_directory (root / "nested");
    put (root / "nested/b.ath", "(document (TeXmacs \"2.1.4\") (body (document \"hello\")))");
    const auto xml= write_xml (tree (DOCUMENT, compound ("body", tree (DOCUMENT, "already XML"))));
    put (root / "c.ath", xml);
    put (root / "asset.bin", std::string ("\0\xff\x01", 3));
    fs::create_directory (root / ".backup");
    put (root / ".backup/historical.ath", "not a live document");
    auto expected= decode_document_bytes (legacy, root / "a.ath").document;
    const auto result= upgrade_vault_format (root);
    QVERIFY (result.durable);
    QCOMPARE (result.converted, std::size_t (2));
    QCOMPARE (result.already_xml, std::size_t (1));
    QCOMPARE (get (result.backup / "a.ath"), legacy);
    QCOMPARE (get (root / "c.ath"), xml);
    QCOMPARE (read_xml (get (root / "a.ath")), expected);
    QCOMPARE (get (root / "asset.bin"), get (result.backup / "asset.bin"));
    QCOMPARE (get (root / ".backup/historical.ath"), std::string ("not a live document"));
    QVERIFY (fs::exists (result.backup.parent_path () / "manifest.json"));
    const auto repeated= upgrade_vault_format (root);
    QCOMPARE (repeated.converted, std::size_t (0));
    QVERIFY (repeated.backup.empty ());
  }
  void invalidInputDoesNotWrite () {
    QTemporaryDir tmp;
    const auto root= vault (tmp);
    put (root / "z.ath", "<TeXmacs|2.1.4>\n<\\body>broken");
    QVERIFY_THROWS_EXCEPTION (std::exception, upgrade_vault_format (root));
    QCOMPARE (get (root / "a.ath"), legacy);
    QCOMPARE (std::distance (fs::directory_iterator (root.parent_path ()), fs::directory_iterator ()), 1);
  }
  void cancelAndExternalModification () {
    for (bool external: {false, true}) {
      QTemporaryDir tmp;
      const auto root= vault (tmp);
      bool once= false;
      auto progress= [&] (const char* phase, std::size_t, std::size_t, const std::string&) {
        if (once || std::string (phase) != "Validate XML") return;
        once= true;
        if (external) put (root / "new-file", "concurrent writer");
        else throw std::runtime_error ("Cancelled");
      };
      QVERIFY_THROWS_EXCEPTION (std::exception, upgrade_vault_format (root, progress));
      QVERIFY (once);
      QCOMPARE (get (root / "a.ath"), legacy);
      if (external) QCOMPARE (get (root / "new-file"), std::string ("concurrent writer"));
    }
  }
  void readOnlyAndActiveVault () {
    QTemporaryDir tmp;
    const auto root= vault (tmp);
    {
      athena::filesystem::vault_directory_lease active (root);
      QVERIFY_THROWS_EXCEPTION (std::exception, upgrade_vault_format (root));
    }
    ::chmod ((root / "a.ath").c_str (), 0400);
    QVERIFY_THROWS_EXCEPTION (std::exception, upgrade_vault_format (root));
    QCOMPARE (get (root / "a.ath"), legacy);
  }
  void preserveArtifactIdentityAndDecisions_data () {
    QTest::addColumn<QString> ("version");
    QTest::newRow ("unversioned") << QString ();
    QTest::newRow ("version-1") << QString ("1");
    QTest::newRow ("version-2") << QString ("2");
  }
  void preserveArtifactIdentityAndDecisions () {
    QFETCH (QString, version);
    QTemporaryDir tmp;
    const auto root= vault (tmp);
    const auto mtime= fs::last_write_time (root / "a.ath").time_since_epoch ().count ();
    const auto db= root / "artifacts.db";
    sql (db, "CREATE TABLE documents(path TEXT PRIMARY KEY,size INTEGER,mtime_ns INTEGER);"
      "CREATE TABLE artifact_range_cache(path TEXT,size INTEGER,mtime_ns INTEGER,request_hash TEXT,paragraph_offsets TEXT);"
      "CREATE TABLE artifacts(uuid TEXT,decision BLOB);INSERT INTO artifacts VALUES('identity-unchanged',x'0001ff');"
      "CREATE TABLE artifact_names(artifact_uuid TEXT,ordinal INTEGER,name_tree TEXT);"
      "INSERT INTO artifact_names VALUES('identity-unchanged',0,CAST(x'436573E0726F' AS TEXT));"
      "INSERT INTO documents VALUES('a.ath'," + std::to_string (legacy.size ()) + "," + std::to_string (mtime) + ");"
      "INSERT INTO artifact_range_cache VALUES('a.ath'," + std::to_string (legacy.size ()) + "," + std::to_string (mtime) + ",'model-input','[1,2]');");
    if (!version.isEmpty ())
      sql (db, "CREATE TABLE artifact_metadata(key TEXT PRIMARY KEY,value TEXT);"
        "INSERT INTO artifact_metadata VALUES('schema-version','" + version.toStdString () + "');");
    const auto old_db= get (db);
    const auto rag= root / "rag.sqlite";
    std::uint64_t storage_hash= 1469598103934665603ULL;
    for (unsigned char c: legacy) { storage_hash ^= c; storage_hash *= 1099511628211ULL; }
    std::ostringstream encoded;
    encoded << std::hex << std::setw (16) << std::setfill ('0') << storage_hash;
    sql (rag, "CREATE TABLE meta(key TEXT PRIMARY KEY,value TEXT);"
      "INSERT INTO meta VALUES('schema-version','1');"
      "CREATE TABLE documents(rel_path TEXT PRIMARY KEY,size INTEGER,mtime_ns INTEGER,content_hash TEXT,status TEXT);"
      "CREATE TABLE chunks(chunk_id TEXT,embedding BLOB,embedding_model TEXT);"
      "INSERT INTO chunks VALUES('stable-chunk',x'0000803f00000040','existing-model');"
      "INSERT INTO documents VALUES('a.ath'," + std::to_string (legacy.size ()) + "," +
      std::to_string (mtime) + ",'" + encoded.str () + "','ok');");
    const auto old_rag= get (rag);
    const auto expected= semantic_document_fingerprint (decode_document_bytes (legacy, root / "a.ath").document);
    const auto result= upgrade_vault_format (root);
    QCOMPARE (get (result.backup / "artifacts.db"), old_db);
    QCOMPARE (sql (db, "SELECT value FROM artifact_metadata WHERE key='schema-version'"), std::string ("2"));
    QCOMPARE (sql (db, "SELECT semantic_hash FROM documents"), expected);
    QCOMPARE (sql (db, "SELECT semantic_hash FROM artifact_range_cache"), expected);
    QCOMPARE (sql (db, "SELECT uuid,hex(decision) FROM artifacts"), std::string ("identity-unchanged0001FF"));
    QCOMPARE (read_xml (sql (db, "SELECT name_tree FROM artifact_names"), xml_kind::fragment), tree ("Cesàro"));
    QCOMPARE (sql (db, "SELECT value FROM artifact_metadata WHERE key='tree-format'"), std::string ("utf8-xml-v1"));
    QCOMPARE (get (result.backup / "rag.sqlite"), old_rag);
    QCOMPARE (sql (rag, "SELECT content_hash FROM documents"), expected);
    QCOMPARE (sql (rag, "SELECT storage_hash FROM documents"), encoded.str ());
    QCOMPARE (sql (rag, "SELECT chunk_id,hex(embedding),embedding_model FROM chunks"),
              std::string ("stable-chunk0000803F00000040existing-model"));
  }
  void rejectUnknownIndexVersion () {
    QTemporaryDir tmp;
    const auto root= vault (tmp);
    const auto db= root / "artifacts.db";
    sql (db, "CREATE TABLE documents(path TEXT PRIMARY KEY,size INTEGER,mtime_ns INTEGER);"
      "CREATE TABLE artifact_metadata(key TEXT PRIMARY KEY,value TEXT);"
      "INSERT INTO artifact_metadata VALUES('schema-version','99');");
    const auto original= get (db);
    QVERIFY_THROWS_EXCEPTION (std::exception, upgrade_vault_format (root));
    QCOMPARE (get (db), original);
    QCOMPARE (get (root / "a.ath"), legacy);
  }
  void migrateStoredTreesOnce () {
    QTemporaryDir tmp;
    const auto root= vault (tmp);
    const auto db= root / "artifacts.db";
    sql (db, "CREATE TABLE documents(path TEXT PRIMARY KEY,size INTEGER,mtime_ns INTEGER);"
      "CREATE TABLE artifact_names(artifact_uuid TEXT,ordinal INTEGER,name_tree TEXT);"
      "INSERT INTO artifact_names VALUES('accent',0,CAST(x'436573E0726F' AS TEXT));"
      "INSERT INTO artifact_names VALUES('symbol',1,'<math|\\<alpha\\>>-space');"
      "INSERT INTO artifact_names VALUES('whitespace',0,'\\ natural distance ');"
      "INSERT INTO artifact_names VALUES('empty',0,'');");
    const auto bold= root / "bold-text.db";
    sql (bold, "CREATE TABLE entries(uuid TEXT,keyword_tree TEXT,paragraph_offsets TEXT,identity_focus TEXT);"
      "INSERT INTO entries VALUES('bold','base64-v1:PHN0cm9uZ3xDYWbpPg==','[0,1]','keep-original-evidence');");
    const auto original= get (bold);
    const auto result= upgrade_vault_format (root);
    QCOMPARE (get (result.backup / "bold-text.db"), original);
    QCOMPARE (read_xml (sql (db, "SELECT name_tree FROM artifact_names WHERE artifact_uuid='accent'"), xml_kind::fragment), tree ("Cesàro"));
    const auto whitespace= sql (db, "SELECT name_tree FROM artifact_names WHERE artifact_uuid='whitespace'");
    QCOMPARE (read_xml (whitespace, xml_kind::fragment), texmacs_to_tree ("\\ natural distance ")[0]);
    const auto symbol= sql (db, "SELECT name_tree FROM artifact_names WHERE artifact_uuid='symbol'");
    QCOMPARE (read_xml (symbol, xml_kind::fragment), tree (CONCAT, compound ("math", "α"), "-space"));
    const auto encoded= sql (bold, "SELECT keyword_tree FROM entries");
    QCOMPARE (read_xml (QByteArray::fromBase64 (QByteArray::fromStdString (encoded.substr (10))).toStdString (),
                       xml_kind::fragment), compound ("strong", "Café"));
    QCOMPARE (sql (bold, "SELECT uuid,paragraph_offsets,identity_focus FROM entries"),
              std::string ("bold[0,1]keep-original-evidence"));
    prepare_vault_upgrade_indexes (root, {});
    QCOMPARE (read_xml (sql (db, "SELECT name_tree FROM artifact_names WHERE artifact_uuid='accent'"), xml_kind::fragment), tree ("Cesàro"));
    QCOMPARE (sql (bold, "SELECT keyword_tree FROM entries"), encoded);
  }
  void malformedIndexTreeRollsBackVault () {
    QTemporaryDir tmp;
    const auto root= vault (tmp);
    const auto bold= root / "bold-text.db";
    sql (bold, "CREATE TABLE entries(uuid TEXT,keyword_tree TEXT);"
      "INSERT INTO entries VALUES('broken','base64-v1:!!');");
    const auto original= get (bold);
    QVERIFY_THROWS_EXCEPTION (std::exception, upgrade_vault_format (root));
    QCOMPARE (get (root / "a.ath"), legacy);
    QCOMPARE (get (bold), original);
  }
};
QTEST_GUILESS_MAIN (TestVaultFormatUpgrade)
#include "vault_format_upgrade_test.moc"
