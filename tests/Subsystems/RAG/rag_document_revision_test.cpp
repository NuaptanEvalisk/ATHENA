/******************************************************************************
* MODULE     : rag_document_revision_test.cpp
* DESCRIPTION: Native XML storage versions in local and delegated RAG revisions
* COPYRIGHT  : (C) 2026 ATHENA contributors
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
******************************************************************************/

#include <QtTest/QtTest>
#include <QCryptographicHash>
#include <QFile>
#include <QTemporaryDir>
#include <chrono>

#include "Subsystems/RAG/rag_index.hpp"
#include "Subsystems/RAG/rag_delegation_patch.hpp"
#include "Data/Convert/Xml/document_file_codec.hpp"
#include "Data/Convert/Xml/document_upgrade_file.hpp"
#include "Data/Tree/math_tree_v3.hpp"
#include "ATHENA/Data/vaultfile_json.hpp"
#include "node_metadata.hpp"
#include "drd_std.hpp"

bool headless_mode= true;
bool is_headless () { return true; }

namespace {
namespace fs= std::filesystem;
using namespace athena::document;
using namespace athena::rag;

std::string
digest (const std::string& bytes) {
  return "sha256:" + QCryptographicHash::hash (
    QByteArray (bytes.data (), qsizetype (bytes.size ())),
    QCryptographicHash::Sha256).toHex ().toStdString ();
}

bool
write_bytes (const fs::path& path, const std::string& bytes) {
  QFile file (QString::fromStdString (path.string ()));
  return file.open (QIODevice::WriteOnly | QIODevice::Truncate) &&
         file.write (bytes.data (), qint64 (bytes.size ())) ==
           qint64 (bytes.size ()) && file.flush ();
}

tree
source_document (bool metadata, bool native_math) {
  tree paragraph ("Semantic indexing preserves explicit XML storage versions.");
  if (native_math)
    paragraph= tree (CONCAT, paragraph,
      athena::math_v3::big_operator ("\xe2\x88\x91"));
  tree document (DOCUMENT, compound ("body", tree (DOCUMENT, paragraph)));
  if (metadata) {
    athena::node::metadata info;
    info.id= "12345678-1234-4234-8234-123456789abc";
    athena::node::set (document[0][0][0], info);
  }
  return document;
}

RagConfig
index_config (const fs::path& root) {
  RagConfig config;
  config.vault_root= root;
  config.db_path= root / "local.sqlite";
  config.load_embedding_model= false;
  config.progress= false;
  return config;
}
} // namespace

class TestRagDocumentRevision: public QObject {
  Q_OBJECT

private slots:
  void initTestCase () { init_std_drd (); }

  void explicitStorageVersion_data () {
    QTest::addColumn<int> ("version");
    QTest::addColumn<bool> ("metadata");
    QTest::addColumn<bool> ("native_math");
    QTest::newRow ("v1") << 1 << false << false;
    QTest::newRow ("v2-without-metadata") << 2 << false << false;
    QTest::newRow ("v2-with-metadata") << 2 << true << false;
    QTest::newRow ("v3-without-metadata") << 3 << false << false;
    QTest::newRow ("v3-with-metadata") << 3 << true << false;
    QTest::newRow ("v3-native-math-without-metadata") << 3 << false << true;
  }

  void explicitStorageVersion () {
    QFETCH (int, version);
    QFETCH (bool, metadata);
    QFETCH (bool, native_math);
    QTemporaryDir temporary;
    QVERIFY (temporary.isValid ());
    const fs::path root (temporary.path ().toStdString ());
    std::string error;
    QVERIFY2 (athena_vaultfile_write (root, AthenaVaultfileInfo {}, error),
              error.c_str ());
    const tree document= source_document (metadata, native_math);
    QCOMPARE (athena::node::contains_metadata (document), metadata);
    const auto storage_version= version == 1 ? xml_format_version::v1 :
      version == 2 ? xml_format_version::v2 : xml_format_version::v3;
    const auto bytes= encode_native_xml_bytes (document, storage_version);
    const auto canonical= version == 3 ? write_xml_v3 (document) :
      metadata ? write_xml_v2 (document) : write_xml (document);
    const auto expected= digest (canonical);
    if (version == 3 && !native_math)
      QVERIFY (expected != semantic_document_fingerprint (document));
    QVERIFY (write_bytes (root / "Document.ath", bytes));

    RagIndex index;
    QVERIFY (index.open (index_config (root)));
    RagPreparedDocument prepared;
    QVERIFY2 (index.prepare_document ("Document.ath", "", "", prepared),
              index.status ().last_error.c_str ());
    QCOMPARE (prepared.semantic_revision, expected);
    QCOMPARE (prepared.storage_revision, storage_bytes_fingerprint (bytes));
    QVERIFY (!prepared.unchanged);
    QVERIFY (!prepared.chunks.empty ());
    QVERIFY2 (index.commit_document (prepared, {}),
              index.status ().last_error.c_str ());
    QVERIFY (index.prepare_document ("Document.ath", "", "", prepared));
    QVERIFY (prepared.unchanged);
    QCOMPARE (prepared.semantic_revision, expected);

    delegation::DelegatedJob job;
    QVERIFY2 (delegation::collect_delegated_job (
      root, root / "delegated.sqlite", "", job, error), error.c_str ());
    QCOMPARE (job.files.size (), std::size_t (1));
    QCOMPARE (job.files[0].semantic_revision, expected);
    QCOMPARE (job.files[0].storage_revision, storage_bytes_fingerprint (bytes));
    QCOMPARE (job.files[0].content, bytes);
    std::string stored;
    QVERIFY (delegation::read_file_bytes (root / "Document.ath", stored));
    QCOMPARE (stored, bytes);
  }

  void metadataFreeVersionRewrite () {
    QTemporaryDir temporary;
    QVERIFY (temporary.isValid ());
    const fs::path root (temporary.path ().toStdString ());
    std::string error;
    QVERIFY2 (athena_vaultfile_write (root, AthenaVaultfileInfo {}, error),
              error.c_str ());
    const tree document= source_document (false, false);
    QVERIFY (!athena::node::contains_metadata (document));
    const std::string bytes[]= {
      write_xml (document), write_xml_v2 (document), write_xml_v3 (document)};
    const auto old_revision= digest (bytes[0]);
    const auto v3_revision= digest (bytes[2]);
    QVERIFY (old_revision != v3_revision);
    const auto config= index_config (root);
    RagIndex index;
    QVERIFY (index.open (config));

    for (int version=0; version<3; ++version) {
      QVERIFY (write_bytes (root / "Document.ath", bytes[version]));
      // Force a distinct physical revision even on coarse timestamp filesystems.
      fs::last_write_time (root / "Document.ath",
        fs::file_time_type::clock::now () + std::chrono::seconds (version));
      RagPreparedDocument prepared;
      QVERIFY2 (index.prepare_document ("Document.ath",
        storage_bytes_fingerprint (bytes[version]), "", prepared),
        index.status ().last_error.c_str ());
      QCOMPARE (prepared.semantic_revision,
                version == 2 ? v3_revision : old_revision);
      QCOMPARE (prepared.metadata_only, version == 1);
      QVERIFY (!prepared.unchanged);

      delegation::DelegatedJob job;
      QVERIFY2 (delegation::collect_delegated_job (
        root, config.db_path, "", job, error), error.c_str ());
      QCOMPARE (job.files.size (), std::size_t (version == 1 ? 0 : 1));
      if (!job.files.empty ())
        QCOMPARE (job.files[0].semantic_revision, prepared.semantic_revision);
      QVERIFY2 (index.commit_document (prepared, {}),
                index.status ().last_error.c_str ());
    }
    delegation::DelegatedJob job;
    QVERIFY2 (delegation::collect_delegated_job (
      root, config.db_path, "", job, error), error.c_str ());
    QVERIFY (job.files.empty ());
  }
};

QTEST_MAIN (TestRagDocumentRevision)
#include "rag_document_revision_test.moc"
