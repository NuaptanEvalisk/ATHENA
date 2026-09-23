/******************************************************************************
* MODULE     : document_upgrade_file_test.cpp
* DESCRIPTION: Isolated legacy-file upgrade, backup and stale-source regressions
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#include <QtTest/QtTest>
#include <QTemporaryDir>
#include "Xml/document_upgrade_file.hpp"
#include <fstream>
#include <future>
#include <sys/stat.h>
#include <unistd.h>

using namespace athena::document;
namespace fs= std::filesystem;
bool headless_mode= true;
bool is_headless () { return true; }

namespace {
const std::string legacy= "<TeXmacs|2.1.4>\n<\\body>\ncaf\xe9\n</body>\n";
void put (const fs::path& file, const std::string& bytes) {
  std::ofstream output (file, std::ios::binary);
  output.write (bytes.data (), std::streamsize (bytes.size ()));
  output.close ();
  if (!output) throw std::runtime_error ("Write test fixture");
}
std::string get (const fs::path& file) {
  std::ifstream input (file, std::ios::binary);
  if (!input) throw std::runtime_error ("Read test fixture");
  return {std::istreambuf_iterator<char> (input), std::istreambuf_iterator<char> ()};
}
tree migrated () { return tree (DOCUMENT, tree (u8"caf\u00e9")); }
} // namespace

class TestDocumentUpgrade: public QObject {
  Q_OBJECT
private slots:
  void initTestCase () { make_tree_label (DOCUMENT, "document"); }
  void persistentXmlStorage () {
    QTemporaryDir temporary;
    QVERIFY (temporary.isValid ());
    fs::path root (temporary.path ().toStdString ());
    auto file= root / "created.ath";
    document_save_result created;
    auto storage= document_file::create (file, migrated (), created);
    QVERIFY (created.durability == upgrade_durability::durable);
    QVERIFY (!storage.legacy ());
    QCOMPARE (storage.source_sha256 (), created.xml_sha256);
    QCOMPARE (storage.source_sha256 (), storage_bytes_fingerprint (get (file)));
    QCOMPARE (read_xml (get (file)), migrated ());
    tree second (DOCUMENT, "second");
    auto saved= storage.save (second);
    QVERIFY (!saved.upgraded_legacy);
    QVERIFY (saved.durability == upgrade_durability::durable);
    QCOMPARE (storage.source_sha256 (), saved.xml_sha256);
    QCOMPARE (storage.source_sha256 (), storage_bytes_fingerprint (get (file)));
    QCOMPARE (read_xml (get (file)), second);
    const std::string external= write_xml (tree (DOCUMENT, "external"));
    put (file, external);
    QVERIFY_THROWS_EXCEPTION (std::system_error, storage.save (tree (DOCUMENT, "third")));
    QCOMPARE (get (file), external);
    document_save_result overwrite;
    QVERIFY_THROWS_EXCEPTION (std::system_error,
      document_file::create (file, tree (DOCUMENT, "overwrite"), overwrite));
  }
  void legacyStorageTransitionsToPinnedXml () {
    QTemporaryDir temporary;
    QVERIFY (temporary.isValid ());
    fs::path root (temporary.path ().toStdString ());
    auto file= root / "legacy.ath";
    put (file, legacy);
    auto storage= document_file::capture (file, root);
    QVERIFY (storage.legacy ());
    QCOMPARE (storage.source_sha256 (), storage_bytes_fingerprint (legacy));
    auto upgraded= storage.save (migrated ());
    QVERIFY (upgraded.upgraded_legacy);
    QVERIFY (upgraded.backup.has_value ());
    QCOMPARE (get (*upgraded.backup), legacy);
    QVERIFY (!storage.legacy ());
    QCOMPARE (storage.source_sha256 (), upgraded.xml_sha256);
    QCOMPARE (storage.source_sha256 (), storage_bytes_fingerprint (get (file)));
    tree second (DOCUMENT, "second");
    auto saved= storage.save (second);
    QVERIFY (!saved.upgraded_legacy);
    QCOMPARE (read_xml (get (file)), second);
    QCOMPARE (get (*upgraded.backup), legacy);
  }
  void backupAndUpgrade () {
    QTemporaryDir temporary;
    QVERIFY (temporary.isValid ());
    fs::path vault (temporary.path ().toStdString ());
    fs::create_directory (vault / "documents");
    auto file= vault / "documents/test.ath";
    put (file, legacy);
    auto source= legacy_file::capture (file, vault);
    QCOMPARE (source.original_bytes (), legacy);
    QVERIFY (!fs::exists (source.backup_path ()));
    QCOMPARE (get (file), legacy);
    tree old_envelope (DOCUMENT, compound ("TeXmacs", "2.1.4"), migrated ()[0]);
    auto result= source.commit (old_envelope);
    QVERIFY (result.root_child_map == std::vector<int> ({-1, 0}));
    QCOMPARE (N (old_envelope), 2);
    QVERIFY (result.durability == upgrade_durability::durable);
    QCOMPARE (get (result.backup), legacy);
    QCOMPARE (result.original_sha256, source.original_sha256 ());
    QVERIFY (result.xml_sha256 != result.original_sha256);
    QVERIFY (read_xml (get (file)) == migrated ());
    QCOMPARE (result.file.read (1024 * 1024), get (file));
    QVERIFY (result.backup.string ().find (".backup/format-migration/v1/") != std::string::npos);
    QVERIFY_EXCEPTION_THROWN (source.commit (migrated ()), std::system_error);
    QVERIFY_EXCEPTION_THROWN (legacy_file::capture (file, vault), codec_exception);
  }
  void outsideVaultAndScheme () {
    QTemporaryDir temporary;
    QVERIFY (temporary.isValid ());
    fs::path root (temporary.path ().toStdString ());
    fs::create_directory (root / "vault");
    auto file= root / "outside.ath";
    const std::string scheme= "(document (TeXmacs \"2.1.4\") (body (document \"text\")))";
    put (file, scheme);
    auto source= legacy_file::capture (file, root / "vault");
    QVERIFY (source.format () == legacy_format::scheme);
    QVERIFY (source.backup_path ().parent_path () == root);
    QVERIFY (source.backup_path ().filename ().string ().find (source.original_sha256 ()) != std::string::npos);
    source.commit (migrated ());
    QCOMPARE (get (source.backup_path ()), scheme);
  }
  void staleSource () {
    QTemporaryDir temporary;
    QVERIFY (temporary.isValid ());
    fs::path root (temporary.path ().toStdString ());
    auto file= root / "test.ath";
    put (file, legacy);
    auto source= legacy_file::capture (file, root);
    put (file, "external edit");
    QVERIFY_EXCEPTION_THROWN (source.commit (migrated ()), std::system_error);
    QCOMPARE (get (file), std::string ("external edit"));
    QVERIFY (!fs::exists (source.backup_path ()));
    fs::remove (file);
    put (file, legacy);
    QVERIFY_EXCEPTION_THROWN (source.commit (migrated ()), std::system_error);
    QCOMPARE (get (file), legacy);
  }
  void failurePreservesOriginal () {
    QTemporaryDir temporary;
    QVERIFY (temporary.isValid ());
    fs::path root (temporary.path ().toStdString ());
    auto file= root / "test.ath";
    put (file, legacy);
    auto source= legacy_file::capture (file, root);
    codec_limits limit;
    limit.output_bytes= 8;
    QVERIFY_EXCEPTION_THROWN (source.commit (migrated (), limit), codec_exception);
    QCOMPARE (get (file), legacy);
    QVERIFY (!fs::exists (source.backup_path ()));
    // A blocked backup location must prevent the original replacement.
    put (root / ".backup", "not a directory");
    QVERIFY_EXCEPTION_THROWN (source.commit (migrated ()), std::system_error);
    QCOMPARE (get (file), legacy);
    fs::remove (root / ".backup");
    fs::create_directories (source.backup_path ().parent_path ());
    put (source.backup_path (), "corrupt backup");
    QVERIFY_EXCEPTION_THROWN (source.commit (migrated ()), std::exception);
    QCOMPARE (get (file), legacy);
    fs::remove (source.backup_path ());
    // A complete backup left by an interrupted attempt is verified and reused.
    put (source.backup_path (), legacy);
    source.commit (migrated ());
    QCOMPARE (get (source.backup_path ()), legacy);
  }
  void readOnlySource () {
    if (::geteuid () == 0) QSKIP ("Root bypasses file permission tests");
    QTemporaryDir temporary;
    QVERIFY (temporary.isValid ());
    fs::path root (temporary.path ().toStdString ());
    auto file= root / "test.ath";
    put (file, legacy);
    QVERIFY (::chmod (file.c_str (), 0400) == 0);
    auto source= legacy_file::capture (file, root);
    QVERIFY_EXCEPTION_THROWN (source.commit (migrated ()), std::system_error);
    QCOMPARE (get (file), legacy);
    QVERIFY (::chmod (file.c_str (), 0600) == 0);
  }
  void concurrentUpgrade () {
    QTemporaryDir temporary;
    QVERIFY (temporary.isValid ());
    fs::path root (temporary.path ().toStdString ());
    auto file= root / "test.ath";
    put (file, legacy);
    const auto source= legacy_file::capture (file, root);
    std::promise<void> start;
    const auto ready= start.get_future ().share ();
    const auto save= [&] {
      ready.wait ();
      try {
        source.commit (migrated ());
        return true;
      }
      catch (const std::system_error& error) {
        if (error.code ().value () != ESTALE && error.code ().value () != EAGAIN) throw;
        return false;
      }
    };
    auto first= std::async (std::launch::async, save);
    auto second= std::async (std::launch::async, save);
    start.set_value ();
    const bool first_saved= first.get (), second_saved= second.get ();
    QVERIFY (first_saved != second_saved);
    QCOMPARE (get (source.backup_path ()), legacy);
    QVERIFY (read_xml (get (file)) == migrated ());
  }
};
QTEST_GUILESS_MAIN (TestDocumentUpgrade)
#include "document_upgrade_file_test.moc"
