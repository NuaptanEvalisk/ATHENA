/******************************************************************************
* MODULE     : artifact_title_filter_v3_test.cpp
* DESCRIPTION: Version-preserving rejection lists and offline v3 conversion
* COPYRIGHT  : (C) 2026 ATHENA contributors
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
******************************************************************************/
#include <QtTest/QtTest>
#include <QFile>
#include <QTemporaryDir>
#include "ATHENA/Data/artifact_title_filter.hpp"
#include "ATHENA/Data/hodarium_logical_database.hpp"
#include "ATHENA/Data/vaultfile_json.hpp"
#include "Data/Convert/Xml/document_file_codec.hpp"
#include "Data/Convert/Xml/vault_format_upgrade.hpp"
#include "Data/Convert/Xml/vault_xml_v3_indexes.hpp"
#include "math_tree_v3.hpp"
#include "node_metadata.hpp"
#include "drd_std.hpp"

bool headless_mode= true;
bool is_headless () { return true; }

namespace fs= std::filesystem;
using namespace athena::document;
namespace {
std::string read (const fs::path& path) {
  QFile file (QString::fromStdString (path.string ()));
  if (!file.open (QIODevice::ReadOnly)) throw std::runtime_error ("Cannot read filter fixture");
  return file.readAll ().toStdString ();
}
void write (const fs::path& path, const std::string& bytes) {
  QFile file (QString::fromStdString (path.string ()));
  if (!file.open (QIODevice::WriteOnly | QIODevice::Truncate) ||
      file.write (bytes.data (), qint64 (bytes.size ())) != qint64 (bytes.size ()))
    throw std::runtime_error ("Cannot write filter fixture");
}
AthenaVaultfileInfo setup (const fs::path& root, int model) {
  fs::create_directories (root);
  AthenaVaultfileInfo info;
  info.node_model_version= model;
  std::string error;
  if (!athena_vaultfile_write (root, info, error)) throw std::runtime_error (error);
  return info;
}
tree native_name (char policy= 'o') {
  return compound ("math", athena::math_v3::scripts (
    tree ("x"), tree (""), tree (""), tree ("i"), tree (""), policy));
}
}

class TestArtifactTitleFilterV3: public QObject {
  Q_OBJECT
private slots:
  void initTestCase () { init_std_drd (); }

  void encodedFragmentsKeepVersionAndSemanticProperties () {
    auto filter= athena_artifact_title_filter_from_entries ({"Notice"});
    const tree plain= compound ("math", tree ("legacy"));
    for (auto version: {xml_format_version::v1, xml_format_version::v2}) {
      const auto encoded= encode_native_xml_bytes (plain, version, xml_kind::fragment);
      athena_artifact_title_filter_add_fragment (filter, encoded);
      QVERIFY (filter.structured.count (encoded));
    }
    athena_artifact_title_filter_add (filter, native_name (), xml_format_version::v3);
    const auto serialized= athena_artifact_title_filter_serialize (filter);
    const auto decoded= athena_artifact_title_filter_parse (serialized);
    QCOMPARE (decoded.structured_entries, filter.structured_entries);
    QCOMPARE (athena_artifact_title_filter_fingerprint (decoded),
              athena_artifact_title_filter_fingerprint (filter));
    QVERIFY (athena_artifact_title_filter_contains (decoded, native_name ()));
    QVERIFY (!athena_artifact_title_filter_contains (decoded, native_name ('r')));
    QVERIFY_THROWS_EXCEPTION (std::exception,
      athena_artifact_title_filter_add_fragment (filter, "<a v=\"3\" k=\"t\"><bad/></a>"));
    QCOMPARE (filter.structured_entries, decoded.structured_entries);
  }

  void contextualRejectPersistsNativeName () {
    QTemporaryDir temporary;
    const fs::path root (temporary.path ().toStdString ());
    const auto info= setup (root, 2);
    std::string error;
    QVERIFY2 (athena_artifact_title_filter_reject (root, native_name (), error), error.c_str ());
    const auto first= read (root / info.artifact_title_filter_path);
    AthenaArtifactTitleFilter restored;
    QVERIFY2 (athena_artifact_title_filter_read (root, restored, error), error.c_str ());
    QCOMPARE (restored.structured_entries.size (), std::size_t (1));
    QVERIFY (inspect_xml_version (restored.structured_entries[0]) == xml_format_version::v3);
    QVERIFY (athena_artifact_title_filter_contains (restored, native_name ()));
    QVERIFY2 (athena_artifact_title_filter_reject (root, native_name (), error), error.c_str ());
    QCOMPARE (read (root / info.artifact_title_filter_path), first);

    // The old format must fail explicitly, not erase the script policy.
    setup (root, 1);
    QVERIFY (!athena_artifact_title_filter_reject (root, native_name ('r'), error));
    QVERIFY (!error.empty ());
    QCOMPARE (read (root / info.artifact_title_filter_path), first);
    restored.structured_entries.push_back ("<a v=\"3\" k=\"t\"><bad/></a>");
    QVERIFY (!athena_artifact_title_filter_write (root, restored, error));
    QCOMPARE (read (root / info.artifact_title_filter_path), first);
  }

  void logicalPayloadRoundtripDoesNotRewriteVersion () {
    namespace h= athena::hodarium;
    QTemporaryDir temporary;
    const fs::path base (temporary.path ().toStdString ());
    const auto source= base / "source", destination= base / "destination";
    setup (source, 2); setup (destination, 2);
    std::string error;
    QVERIFY2 (athena_artifact_title_filter_reject (source, native_name (), error), error.c_str ());
    const auto object= h::export_artifact_rejections (source);
    QVERIFY (object.has_value ());
    h::validate_logical_object (*object);
    QCOMPARE (h::apply_artifact_rejections (destination,
      {object->key, {}, object->payload}), h::logical_apply_result::applied);
    const auto restored= h::export_artifact_rejections (destination);
    QVERIFY (restored.has_value ());
    QCOMPARE (restored->payload, object->payload);
  }

  void stagedMigrationIncludesAuthoritativeFilter () {
    QTemporaryDir temporary;
    const fs::path base (temporary.path ().toStdString ());
    const auto source= base / "source", candidate= base / "candidate";
    const auto info= setup (source, 1);
    write (source / "Document.ath", write_xml_v2 (tree (DOCUMENT, tree ("body"))));
    auto original= athena_artifact_title_filter_from_entries ({"Notice"});
    const tree legacy= compound ("math", tree (CONCAT, tree ("x"), tree (RSUB, "i")));
    athena_artifact_title_filter_add (original, legacy);
    const auto bytes= athena_artifact_title_filter_serialize (original);
    write (source / info.artifact_title_filter_path, bytes);
    const auto prepared= prepare_vault_title_filter_v3 (source, info, true);
    QVERIFY (prepared.has_value ());
    QCOMPARE (prepared->fragments, std::size_t (1));
    QCOMPARE (read (source / info.artifact_title_filter_path), bytes);
    const auto staged= stage_vault_xml_v3_candidate (source, candidate, true);
    QCOMPARE (staged.checked.index_tree_fragments, std::size_t (1));
    QCOMPARE (staged.migrated_index_trees, std::size_t (1));
    QCOMPARE (read (source / info.artifact_title_filter_path), bytes);
    QCOMPARE (read (candidate / info.artifact_title_filter_path), prepared->bytes);
    const auto converted= athena_artifact_title_filter_parse (prepared->bytes);
    QCOMPARE (converted.entries, original.entries);
    QVERIFY (inspect_xml_version (converted.structured_entries[0]) == xml_format_version::v3);
    const auto math= decode_native_xml_bytes (converted.structured_entries[0], xml_kind::fragment).document;
    QVERIFY (is_compound (math, "math") && N(math) == 1);
    QVERIFY (athena_artifact_title_filter_contains (converted, math));
  }

  void badFilterBlocksWholeVaultBeforeStaging () {
    QTemporaryDir temporary;
    const fs::path base (temporary.path ().toStdString ());
    const auto source= base / "source", candidate= base / "candidate";
    const auto info= setup (source, 1);
    write (source / "Document.ath", write_xml_v2 (tree (DOCUMENT, tree ("body"))));
    auto filter= athena_artifact_title_filter_from_entries ({});
    athena_artifact_title_filter_add (filter, tree (NAMED_SYMBOL, "cork:FF"));
    const auto bytes= athena_artifact_title_filter_serialize (filter);
    write (source / info.artifact_title_filter_path, bytes);
    QVERIFY_THROWS_EXCEPTION (std::runtime_error,
      stage_vault_xml_v3_candidate (source, candidate, true));
    QVERIFY (!fs::exists (candidate));
    QCOMPARE (read (source / info.artifact_title_filter_path), bytes);
  }

  void legacyTextListKeepsItsMeaning () {
    const auto plain= athena_artifact_title_filter_parse ("Notice\r\n  however \nNotice\n");
    const auto roundtrip= athena_artifact_title_filter_parse (
      athena_artifact_title_filter_serialize (plain));
    QCOMPARE (roundtrip.entries, plain.entries);
    QVERIFY (athena_artifact_title_filter_contains (roundtrip, std::string ("HOWEVER")));
  }
};

QTEST_GUILESS_MAIN (TestArtifactTitleFilterV3)
#include "artifact_title_filter_v3_test.moc"
