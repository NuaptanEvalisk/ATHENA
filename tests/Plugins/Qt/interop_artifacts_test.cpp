/******************************************************************************
* MODULE     : interop_artifacts_test.cpp
* DESCRIPTION: Read-only artifact resolution against isolated native vault indexes
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#include <QtTest/QtTest>
#include <QTemporaryDir>
#include "ATHENA/Data/artifacts.hpp"
#include "ATHENA/Data/vault.hpp"
#include "ATHENA/Data/vaultfile_json.hpp"
#include "ATHENA/Interop/resources.hpp"
#include "drd_std.hpp"
#include <sqlite3.h>
#include <future>

bool headless_mode= true;
bool is_headless () { return true; }

namespace {
using namespace athena::interop;
namespace fs= std::filesystem;
struct CloseVault { ~CloseVault () { if (vault_active ()) vault_close (); } };

struct Database {
  sqlite3* db= nullptr;
  explicit Database (const fs::path& file) {
    if (sqlite3_open_v2 (file.c_str (), &db, SQLITE_OPEN_READWRITE, nullptr) != SQLITE_OK) {
      std::string error= db ? sqlite3_errmsg (db) : "Cannot open test database";
      if (db) sqlite3_close (db);
      db= nullptr;
      throw std::runtime_error (error);
    }
  }
  ~Database () { sqlite3_close (db); }
  void exec (const char* sql) {
    char* raw= nullptr;
    if (sqlite3_exec (db, sql, nullptr, nullptr, &raw) != SQLITE_OK) {
      std::string error= raw ? raw : sqlite3_errmsg (db);
      sqlite3_free (raw);
      throw std::runtime_error (error);
    }
  }
};

void initialize (const fs::path& root, AthenaVaultfileInfo& info) {
  info.artifacts_path= "index/artifacts ?.db";
  info.enunciations_path= "index/enunciations #.db";
  info.bold_text_path= "index/bold-text %.db";
  std::string error;
  if (!athena_vaultfile_write (root, info, error)) throw std::runtime_error (error);
  string result= vault_load (url_system (string (root.string ().c_str ())), "Interop test", "map.sqlite", "ns.sqlite");
  if (result != "") throw std::runtime_error (std::string (result.data (), N (result)));
}

void populate (const fs::path& root, const AthenaVaultfileInfo& info) {
  std::vector<AthenaArtifactRecord> records;
  std::string error;
  if (!athena_artifacts_query (root, records, error)) throw std::runtime_error (error);
  Database db (root / info.artifacts_path);
  db.exec (
    "INSERT INTO artifacts(uuid,type,origin,content_uuid,path,anchor_stem,display_text,document_order) VALUES"
    "('a','provable','enunciation','ea','A.ath','not-a-name','Every maximal ideal ...',0),"
    "('b','provable','enunciation','eb','B.ath','anchor-b','A second statement',0),"
    "('c','definition','enunciation','ec','C.ath','anchor-c','A definition',0),"
    "('d','completion','enunciation','ed','A.ath','proof-anchor','Proof of the theorem',1);"
    "INSERT INTO artifact_names(artifact_uuid,name,ordinal,name_tree) VALUES"
    "('a','strong nullstellensatz',0,''),('a','Hilbert theorem',1,"
    "'<athena-tree version=\"1\" text-model=\"utf-8\"><node tag=\"math\"><text>k</text></node></athena-tree>'),"
    "('b','strong nullstellensatz',0,''),('c','weak nullstellensatz',0,'');");
}

resolution_result run (resolution_workers& workers, const std::string& selector) {
  std::promise<resolution_result> completion;
  auto result= completion.get_future ();
  resolution_ticket ticket (workers, native_resolvers (), parse_selection (selector),
    [&] (resolution_result value) { completion.set_value (std::move (value)); });
  if (result.wait_for (std::chrono::seconds (4)) != std::future_status::ready) {
    ticket.cancel ();
    result.wait ();
    throw std::runtime_error ("Artifact resolution timed out");
  }
  return result.get ();
}

std::shared_ptr<const occurrence> leaf (const resolution_result& result, std::size_t index= 0) {
  const auto id= result.leaves.at (index);
  for (const auto& node: result.tree) if (node->id == id) return node;
  throw std::runtime_error ("Missing result handle");
}
} // namespace

class TestInteropArtifacts: public QObject {
  Q_OBJECT
private slots:
  void initTestCase () { init_std_drd (); }

  void unbuiltIndexIsNotCreated () {
    QTemporaryDir temporary;
    CloseVault close;
    const fs::path root (temporary.path ().toStdString ());
    AthenaVaultfileInfo info;
    initialize (root, info);
    std::vector<AthenaArtifactRecord> records;
    std::string error;
    QVERIFY2 (athena_artifacts_query (root, records, error, true), error.c_str ());
    QVERIFY (records.empty ());
    QVERIFY (!fs::exists (root / "index"));
    resolution_workers workers (2);
    auto result= run (workers, R"(@/vaults/@/artifacts/?($type = "provable"))");
    QVERIFY2 (result.state == resolution_result::status::complete, result.error.c_str ());
    QVERIFY (result.leaves.empty ());
    QVERIFY (!fs::exists (root / "index"));
  }

  void queriesNamesTypesAndPreservesLineage () {
    QTemporaryDir temporary;
    CloseVault close;
    const fs::path root (temporary.path ().toStdString ());
    AthenaVaultfileInfo info;
    initialize (root, info);
    populate (root, info);
    resolution_workers workers (3);
    for (const auto* selector: {
      R"(@/vaults/@/?($type = "provable" AND $name = "*strong nullstellensatz*"))",
      R"(@/vaults/@/artifacts/?($type = "provable" AND $name = "strong*"))",
      R"(@/vaults/@/artifacts/??($type = "provable" AND $name contains "null*"))",
      R"(@/vaults/@/???($resource_type = "artifact" AND $type = "provable" AND $name = "*nullstellensatz" | $max_depth = 3))",
      R"(@/vaults/@/artifacts/strong nullstellensatz)"
    }) {
      const auto result= run (workers, selector);
      QVERIFY2 (result.state == resolution_result::status::complete, result.error.c_str ());
      QCOMPARE (result.leaves.size (), std::size_t (2));
      const auto node= leaf (result);
      QCOMPARE (node->accessor->type (), std::string ("artifact"));
      QCOMPARE (node->parent->accessor->type (), std::string ("vault"));
      QCOMPARE (node->parent->parent->accessor->type (), std::string ("root"));
      QVERIFY (node->accessor->identity () != leaf (result, 1)->accessor->identity ());
    }
    auto result= run (workers, R"(@/vaults/@/artifacts/?($uuid = "a"))");
    QCOMPARE (result.leaves.size (), std::size_t (1));
    auto record= leaf (result)->accessor->operate ("get", value::object ());
    QCOMPARE (record.status, std::string ("OK"));
    QVERIFY (record.data["names"] == value ({"strong nullstellensatz", "Hilbert theorem"}));
    QVERIFY (record.data["semantic_name_trees"][0].is_null ());
    QVERIFY (record.data["semantic_name_trees"][1].is_binary ());
    QCOMPARE (record.data["anchor_stem"].get<std::string> (), std::string ("not-a-name"));
    result= run (workers, R"(@/vaults/@/artifacts/?($name = "not-a-name"))");
    QVERIFY (result.leaves.empty ());
    result= run (workers, R"(@/vaults/@/artifacts/??($type = "provable" | $max_matches = 1))");
    QCOMPARE (result.leaves.size (), std::size_t (1));
    QVERIFY (!result.truncated.empty ());
  }

  void handlesReadCurrentRecordsAndExpireWithVault () {
    QTemporaryDir temporary;
    CloseVault close;
    const fs::path root (temporary.path ().toStdString ());
    AthenaVaultfileInfo info;
    initialize (root, info);
    populate (root, info);
    resolution_workers workers (2);
    auto result= run (workers, R"(@/vaults/@/artifacts/?($uuid = "a"))");
    auto accessor= leaf (result)->accessor;
    auto identity= accessor->identity ();
    {
      Database db (root / info.artifacts_path);
      db.exec ("UPDATE artifact_names SET name='updated name' WHERE artifact_uuid='a' AND ordinal=0;");
    }
    auto current= accessor->operate ("get", value::object ());
    QCOMPARE (current.status, std::string ("OK"));
    QCOMPARE (current.data["name"].get<std::string> (), std::string ("updated name"));
    QCOMPARE (accessor->identity (), identity);
    QCOMPARE (accessor->operate ("get", {{"unexpected", 1}}).status, std::string ("INVALID_ARGUMENT"));
    QCOMPARE (accessor->operate ("delete", value::object ()).status, std::string ("UNKNOWN_COMMAND"));
    {
      Database db (root / info.artifacts_path);
      db.exec ("DELETE FROM artifacts WHERE uuid='a';");
    }
    QCOMPARE (accessor->operate ("get", value::object ()).status, std::string ("NOT_FOUND"));
    vault_close ();
    QCOMPARE (accessor->operate ("get", value::object ()).status, std::string ("STALE"));
    initialize (root, info);
    QCOMPARE (accessor->operate ("get", value::object ()).status, std::string ("STALE"));
  }

  void missingOrInvalidIndexIsNotRepairedByResolution () {
    QTemporaryDir temporary;
    CloseVault close;
    const fs::path root (temporary.path ().toStdString ());
    AthenaVaultfileInfo info;
    initialize (root, info);
    populate (root, info);
    fs::remove (root / info.bold_text_path);
    resolution_workers workers (2);
    auto result= run (workers, R"(@/vaults/@/artifacts/?($type = "provable"))");
    QVERIFY (result.state == resolution_result::status::fault);
    QVERIFY (!fs::exists (root / info.bold_text_path));
    // A broken artifact index must not be consulted for another named domain.
    result= run (workers, "@/vaults/@/namespaces/@");
    QVERIFY2 (result.state == resolution_result::status::complete, result.error.c_str ());
    QVERIFY (!fs::exists (root / info.bold_text_path));
  }
};

QTEST_MAIN (TestInteropArtifacts)
#include "interop_artifacts_test.moc"
