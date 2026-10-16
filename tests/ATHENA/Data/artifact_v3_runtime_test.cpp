/******************************************************************************
* MODULE     : artifact_v3_runtime_test.cpp
* DESCRIPTION: Versioned Artifact payloads and format-preserving source bindings
* COPYRIGHT  : (C) 2026 ATHENA contributors
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
******************************************************************************/
#include <QtTest/QtTest>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include "ATHENA/Data/artifacts.hpp"
#include "ATHENA/Data/artifact_radioactive_links.hpp"
#include "ATHENA/Data/artifact_title_filter.hpp"
#include "ATHENA/Data/document_node_model.hpp"
#include "ATHENA/Data/document_node_copy.hpp"
#include "ATHENA/Data/enunciation_model.hpp"
#include "ATHENA/Data/vaultfile_json.hpp"
#include "Data/Convert/Xml/document_upgrade_file.hpp"
#include "math_tree_v3.hpp"
#include "node_metadata.hpp"
#include "drd_std.hpp"
#include <sqlite3.h>
#include <map>

bool headless_mode= true;
bool is_headless () { return true; }

namespace fs= std::filesystem;
using namespace athena::document;

namespace {
void save (const fs::path& file, const std::string& bytes) {
  QFile out (QString::fromStdString (file.string ()));
  if (!out.open (QIODevice::WriteOnly | QIODevice::Truncate) ||
      out.write (bytes.data (), qsizetype (bytes.size ())) != qint64 (bytes.size ()))
    throw std::runtime_error ("Cannot write Artifact fixture");
}

std::string read (const fs::path& file) {
  QFile in (QString::fromStdString (file.string ()));
  if (!in.open (QIODevice::ReadOnly)) throw std::runtime_error ("Cannot read Artifact fixture");
  return in.readAll ().toStdString ();
}

std::string sql (const fs::path& file, const char* command,
                 const std::string* parameter= nullptr) {
  sqlite3* db= nullptr;
  if (sqlite3_open_v2 (file.c_str (), &db, SQLITE_OPEN_READWRITE, nullptr) != SQLITE_OK) {
    if (db) sqlite3_close (db);
    throw std::runtime_error ("Cannot open Artifact fixture database");
  }
  struct close_db { sqlite3* db; ~close_db () { sqlite3_close (db); } } guard {db};
  sqlite3_stmt* query= nullptr;
  if (sqlite3_prepare_v2 (db, command, -1, &query, nullptr) != SQLITE_OK)
    throw std::runtime_error (sqlite3_errmsg (db));
  struct close_query { sqlite3_stmt* query; ~close_query () { sqlite3_finalize (query); } } cursor {query};
  if (parameter && sqlite3_bind_text (query, 1, parameter->data (), int (parameter->size ()),
                                     SQLITE_TRANSIENT) != SQLITE_OK)
    throw std::runtime_error (sqlite3_errmsg (db));
  const int status= sqlite3_step (query);
  if (status != SQLITE_ROW && status != SQLITE_DONE)
    throw std::runtime_error (sqlite3_errmsg (db));
  if (status == SQLITE_DONE || !sqlite3_column_text (query, 0)) return {};
  return std::string (reinterpret_cast<const char*> (sqlite3_column_text (query, 0)),
                      std::size_t (sqlite3_column_bytes (query, 0)));
}

tree fixture_document (bool native_math) {
  using athena::node::property;
  tree keyword= compound ("strong", native_math
    ? tree (CONCAT, compound ("math", athena::math_v3::scripts (
        tree ("x"), tree (""), tree (""), tree ("i"), tree (""), 'o')),
        tree (" module"))
    : tree ("compact operator"));
  athena::node::metadata keyword_meta;
  keyword_meta.id= "22222222-2222-4222-8222-222222222222";
  keyword_meta.properties["fixture:description"]= property (athena::node::rich_text {
    native_math ? athena::math_v3::delimited ("(", ")", 'a', tree ("x")) : tree ("legacy")});
  keyword_meta.properties["fixture:rank"]= property (std::int64_t (7));
  athena::node::set (keyword, keyword_meta);

  tree theorem (athena::enunciation::label (), tree (DOCUMENT, tree ("A theorem body")));
  athena::node::metadata theorem_meta;
  theorem_meta.id= "33333333-3333-4333-8333-333333333333";
  theorem_meta.properties["kind"]= property (std::string ("theorem"));
  theorem_meta.properties["numbered"]= property (true);
  theorem_meta.properties["name"]= property (athena::node::rich_text {tree ("Named theorem")});
  athena::node::set (theorem, theorem_meta);
  tree paragraph (CONCAT, tree ("A "), keyword, tree (" is defined here."));
  athena::node::metadata paragraph_meta;
  paragraph_meta.id= "11111111-1111-4111-8111-111111111111";
  athena::node::set (paragraph, paragraph_meta);
  return tree (DOCUMENT, compound ("body", tree (DOCUMENT, theorem, paragraph)));
}

std::string binding (const tree& source, const std::string& role) {
  const auto* meta= athena::node::get (source);
  if (!meta) return {};
  auto found= meta->properties.find (athena::document_node::artifact_bindings_property);
  if (found == meta->properties.end ()) return {};
  const auto& values= std::get<athena::node::property::dictionary> (found->second.data);
  auto id= values.find (role);
  return id == values.end () ? std::string () : std::get<std::string> (id->second.data);
}

struct fixture {
  QTemporaryDir temp;
  fs::path root;
  AthenaVaultfileInfo info;
  tree original;
  std::string error;
  fixture (int version): root (temp.path ().toStdString ()), original (fixture_document (version == 3)) {
    if (!temp.isValid ()) throw std::runtime_error ("Cannot allocate Artifact fixture");
    info.node_model_version= version == 3 ? 2 : 1;
    if (!athena_vaultfile_write (root, info, error)) throw std::runtime_error (error);
    save (root / "Source.ath", version == 3 ? write_xml_v3 (original) : write_xml_v2 (original));
  }
  bool build (AthenaArtifactsBuildResult& result, bool closed= true) {
    error.clear ();
    AthenaArtifactsBuildOptions options;
    options.structural_only= true;
    options.closed_sources_only= closed;
    return athena_artifacts_build (root, {root / "Source.ath"}, false, {}, result, error, options);
  }
  bool query (std::vector<AthenaArtifactRecord>& records) {
    error.clear ();
    return athena_artifacts_query (root, records, error, true, false);
  }
};
}

class TestArtifactV3Runtime: public QObject {
  Q_OBJECT
private slots:
  void initTestCase () { init_std_drd (); }

  void extractionRetainsSemanticMetadataAndStableKeys () {
    tree document= fixture_document (true);
    std::vector<AthenaArtifactRecord> records;
    std::string error;
    QVERIFY2 (athena_artifacts_extract_structure (
      document, "Source.ath", records, error, xml_format_version::v3), error.c_str ());
    QCOMPARE (records.size (), std::size_t (2));
    const auto keyword= records.back ().keyword_tree;
    QCOMPARE (inspect_xml_version (keyword), xml_format_version::v3);
    const auto restored= read_xml_v3 (keyword, xml_kind::fragment);
    const auto expected= athena::node::content_projection (document[0][0][1][1]);
    QVERIFY (restored == expected);
    QVERIFY (athena::node::equal_metadata (restored, expected));
    QVERIFY (athena::node::id (restored).empty ());
    QVERIFY2 (athena_artifacts_bind_structure (document, records, error), error.c_str ());
    std::vector<AthenaArtifactRecord> after;
    QVERIFY2 (athena_artifacts_extract_structure (
      document, "Source.ath", after, error, xml_format_version::v3), error.c_str ());
    QCOMPARE (after.back ().keyword_tree, keyword);
    QVERIFY (!binding (document[0][0][1][1], "bold-text-definition").empty ());
  }

  void sourceBindingPreservesStorageVersion_data () {
    QTest::addColumn<int> ("version");
    QTest::addColumn<bool> ("closed");
    QTest::newRow ("v2-confined") << 2 << true;
    QTest::newRow ("v2-document-file") << 2 << false;
    QTest::newRow ("v3-confined") << 3 << true;
    QTest::newRow ("v3-document-file") << 3 << false;
  }

  void sourceBindingPreservesStorageVersion () {
    QFETCH (int, version);
    QFETCH (bool, closed);
    fixture f (version);
    AthenaArtifactsBuildResult result;
    QVERIFY2 (f.build (result, closed), f.error.c_str ());
    QCOMPARE (result.documents_changed, std::size_t (1));
    const auto bytes= read (f.root / "Source.ath");
    QCOMPARE (inspect_xml_version (bytes), version == 3 ? xml_format_version::v3 : xml_format_version::v2);
    const auto persisted= version == 3 ? read_xml_v3 (bytes) : read_xml_v2 (bytes);
    QCOMPARE (athena::node::id (persisted[0][0][1][1]), athena::node::id (f.original[0][0][1][1]));
    const auto* meta= athena::node::get (persisted[0][0][1][1]);
    QVERIFY (meta);
    QVERIFY (athena::node::equal (meta->properties.at ("fixture:description"),
      athena::node::get (f.original[0][0][1][1])->properties.at ("fixture:description")));
    std::vector<AthenaArtifactRecord> records;
    QVERIFY2 (f.query (records), f.error.c_str ());
    QCOMPARE (records.size (), std::size_t (2));
    std::map<std::string,std::string> ids;
    for (const auto& record: records) {
      ids[record.source_role]= record.uuid;
      const auto source= record.origin == "bold-text" ? persisted[0][0][1][1] : persisted[0][0][0];
      QCOMPARE (binding (source, record.source_role), record.uuid);
      if (record.origin == "bold-text") {
        QCOMPARE (inspect_xml_version (record.keyword_tree),
          version == 3 ? xml_format_version::v3 : xml_format_version::v1);
        if (version == 3) {
          const auto restored= read_xml_v3 (record.keyword_tree, xml_kind::fragment);
          const auto expected= athena::node::content_projection (f.original[0][0][1][1]);
          QVERIFY (restored == expected);
          QVERIFY (athena::node::equal_metadata (restored, expected));
        }
      }
    }
    QCOMPARE (sql (f.root / f.info.bold_text_path, "SELECT substr(keyword_tree,1,10) FROM entries"),
      version == 3 ? std::string ("base64-v3:") : std::string ("base64-v1:"));
    QVERIFY2 (f.build (result, closed), f.error.c_str ());
    QCOMPARE (result.documents_changed, std::size_t (0));
    QCOMPARE (read (f.root / "Source.ath"), bytes);

    for (const auto& path: {f.info.artifacts_path, f.info.bold_text_path, f.info.enunciations_path})
      QVERIFY (fs::remove (f.root / path));
    QVERIFY2 (f.build (result, closed), f.error.c_str ());
    QVERIFY2 (f.query (records), f.error.c_str ());
    for (const auto& record: records) QCOMPARE (record.uuid, ids.at (record.source_role));
    QCOMPARE (inspect_xml_version (read (f.root / "Source.ath")),
      version == 3 ? xml_format_version::v3 : xml_format_version::v2);
  }

  void rejectsMalformedIndexPayloads_data () {
    QTest::addColumn<QString> ("problem");
    for (const char* name: {"bad-base64", "unknown-prefix", "v1-wrapper-v3-xml",
         "v3-wrapper-v1-xml", "raw-v3", "unknown-schema", "wrong-name-envelope"})
      QTest::newRow (name) << QString (name);
  }

  void rejectsMalformedIndexPayloads () {
    QFETCH (QString, problem);
    fixture f (3);
    AthenaArtifactsBuildResult result;
    QVERIFY2 (f.build (result), f.error.c_str ());
    const auto source= read (f.root / "Source.ath");
    std::string value= sql (f.root / f.info.bold_text_path, "SELECT keyword_tree FROM entries");
    if (problem == "bad-base64") value= "base64-v3:%%%";
    else if (problem == "unknown-prefix") value.replace (0, 10, "base64-v9:");
    else if (problem == "v1-wrapper-v3-xml") value.replace (0, 10, "base64-v1:");
    else if (problem == "v3-wrapper-v1-xml")
      value= "base64-v3:" + QByteArray::fromStdString (write_xml (tree ("legacy"), xml_kind::fragment)).toBase64 ().toStdString ();
    else if (problem == "raw-v3") value= write_xml_v3 (tree ("native"), xml_kind::fragment);
    else if (problem == "unknown-schema")
      sql (f.root / f.info.bold_text_path, "UPDATE bold_text_metadata SET value='utf8-xml-v99' WHERE key='tree-format'");
    else {
      const auto legacy= write_xml (tree ("wrong format"), xml_kind::fragment);
      sql (f.root / f.info.artifacts_path, "UPDATE artifact_names SET name_tree=?1", &legacy);
    }
    if (problem != "unknown-schema" && problem != "wrong-name-envelope")
      sql (f.root / f.info.bold_text_path, "UPDATE entries SET keyword_tree=?1", &value);
    std::vector<AthenaArtifactRecord> records;
    QVERIFY (!f.query (records));
    QVERIFY (!f.error.empty ());
    QVERIFY (records.empty ());
    QCOMPARE (read (f.root / "Source.ath"), source);
  }

  void populatedLegacyIndexesRequireOfflineMigration () {
    fixture f (2);
    AthenaArtifactsBuildResult result;
    QVERIFY2 (f.build (result), f.error.c_str ());
    const auto source= read (f.root / "Source.ath");
    f.info.node_model_version= 2;
    QVERIFY2 (athena_vaultfile_write (f.root, f.info, f.error), f.error.c_str ());
    QVERIFY (!f.build (result));
    QVERIFY (f.error.find ("offline XML v3 migration") != std::string::npos);
    QCOMPARE (read (f.root / "Source.ath"), source);
    QCOMPARE (sql (f.root / f.info.artifacts_path,
      "SELECT value FROM artifact_metadata WHERE key='tree-format'"), std::string ("utf8-xml-v1"));
  }

  void sourceAndIndexFormatsCannotImplicitlyConvert () {
    for (int version: {2, 3}) {
      fixture f (version);
      f.info.node_model_version= version == 3 ? 1 : 2;
      QVERIFY2 (athena_vaultfile_write (f.root, f.info, f.error), f.error.c_str ());
      const auto source= read (f.root / "Source.ath");
      AthenaArtifactsBuildResult result;
      QVERIFY (!f.build (result));
      QVERIFY (f.error.find ("source/index format mismatch") != std::string::npos);
      QCOMPARE (read (f.root / "Source.ath"), source);
      QCOMPARE (sql (f.root / f.info.artifacts_path, "SELECT count(*) FROM artifacts"), std::string ("0"));
    }
  }

  void workerRoundtripUsesV3OpaqueFragments () {
    fixture f (3);
    const auto source= read (f.root / "Source.ath");
    const QJsonObject request {
      {"structural_only", true},
      {"structured_title_filter", QJsonArray {QString::fromStdString (write_xml_v3 (
        compound ("math", athena::math_v3::scripts (
          tree ("z"), tree (""), tree (""), tree ("j"), tree (""), 'o')),
        xml_kind::fragment))}},
      {"documents", QJsonArray {QJsonObject {
        {"file", QString::fromStdString ((f.root / "Source.ath").string ())},
        {"path", "Source.ath"}, {"fragment_format", 3}}}}};
    save (f.root / "manifest.json", QJsonDocument (request).toJson ().toStdString ());
    QVERIFY2 (athena_artifacts_run_extract_worker (
      f.root / "manifest.json", f.root / "result.json", f.error), f.error.c_str ());
    const auto response= QJsonDocument::fromJson (
      QByteArray::fromStdString (read (f.root / "result.json"))).object ();
    const auto documents= response.value ("documents").toArray ();
    QCOMPARE (documents.size (), qsizetype (1));
    const auto records= documents[0].toObject ().value ("records").toArray ();
    QCOMPARE (records.size (), qsizetype (2));
    const auto encoded= records[1].toObject ().value ("keyword").toString ().toUtf8 ();
    QVERIFY (encoded.startsWith ("base64-v3:"));
    const auto decoded= QByteArray::fromBase64 (encoded.mid (10));
    const auto keyword= read_xml_v3 (decoded.toStdString (), xml_kind::fragment);
    QVERIFY (athena::node::equal_metadata (keyword,
      athena::node::content_projection (f.original[0][0][1][1])));
    QCOMPARE (read (f.root / "Source.ath"), source);
  }

  void nativeNameFragmentsReachLinkMatcher () {
    const tree math= compound ("math", athena::math_v3::scripts (
      tree ("x"), tree (""), tree (""), tree ("i"), tree (""), 'o'));
    const tree name (CONCAT, math, tree (" module"));
    AthenaArtifactRecord record;
    record.uuid= "artifact";
    record.origin= "bold-text";
    record.type= "definition";
    record.semantic_names= {"x module"};
    record.semantic_name_trees= {write_xml_v3 (name, xml_kind::fragment)};
    AthenaArtifactRadioactiveMatcher matcher ({record});
    const auto matches= matcher.matches_tree (name);
    QCOMPARE (matches.size (), std::size_t (1));
    QCOMPARE (matches[0].link.uuids, std::vector<std::string> {"artifact"});
  }

  void structuredFilterLookupDoesNotDowngradeNativeCandidate () {
    auto filter= athena_artifact_title_filter_defaults ();
    athena_artifact_title_filter_add (filter, compound ("math", tree ("legacy")));
    const auto native= athena::math_v3::scripts (
      tree ("x"), tree (""), tree (""), tree ("i"), tree (""), 'o');
    QVERIFY (!athena_artifact_title_filter_contains (filter, native));
    filter.structured.insert (write_xml_v3 (native, xml_kind::fragment));
    QVERIFY (athena_artifact_title_filter_contains (filter, native));
    const auto other_policy= athena::math_v3::scripts (
      tree ("x"), tree (""), tree (""), tree ("i"), tree (""), 'r');
    QVERIFY (!athena_artifact_title_filter_contains (filter, other_policy));
  }
};

QTEST_GUILESS_MAIN (TestArtifactV3Runtime)
#include "artifact_v3_runtime_test.moc"
