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
#include "Xml/document_file_codec.hpp"
#include "ATHENA/Data/hodarium_inventory.hpp"
#include "ATHENA/Data/hodarium_application.hpp"
#include "ATHENA/Data/hodarium_rename_records.hpp"
#include "ATHENA/Hodarium/revision_transfer.hpp"
#include "node_metadata.hpp"
#include "ATHENA/Data/enunciation_model.hpp"
#include "Data/NodeContracts/execution_state.hpp"
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
  void initTestCase () { athena::contracts::initialize_builtins (); }
  void provableAttributionSavePreservesNames () {
    namespace node= athena::node;
    namespace en= athena::enunciation;
    QTemporaryDir temporary;
    QVERIFY (temporary.isValid ());
    const fs::path destination= fs::path (temporary.path ().toStdString ()) / "source.ath";
    tree statement (en::label (), tree (DOCUMENT, "Body"));
    node::metadata metadata;
    metadata.properties["kind"]= node::property (std::string ("theorem"));
    metadata.properties["numbered"]= node::property (true);
    metadata.properties["name"]= node::property (node::rich_text {tree ("Gauss's Equation")});
    node::set (statement, metadata);
    tree document (DOCUMENT, compound ("body", tree (DOCUMENT, statement)));
    const auto fixture= qgetenv ("ATHENA_PROVABLE_SAVE_SOURCE");
    if (!fixture.isEmpty ()) {
      const auto bytes= get (fs::path (fixture.toStdString ()));
      document= read_xml_v3 (bytes);
      put (destination, bytes);
    }
    else put (destination, write_xml_v3 (document));
    auto storage= xml_file::capture (destination);
    int changed= 0;
    std::function<void (tree&)> edit= [&] (tree& value) {
      const auto names= en::names (value);
      if (en::is_canonical (value) && names.size () == 1 &&
          node::content_equal (names[0], tree ("Gauss's Equation"))) {
        auto properties= *node::get (value);
        properties.properties["attribution"]= node::property (node::property::list {
          node::property (node::rich_text {tree ("Gauss")})});
        node::set (value, properties);
        ++changed;
      }
      if (is_compound (value)) for (int i=0; i<N(value); ++i) edit (value[i]);
    };
    edit (document);
    QCOMPARE (changed, 1);
    const auto result= storage.commit (document);
    QCOMPARE (result.durability, upgrade_durability::durable);
    QCOMPARE (read_xml_v3 (get (destination)), document);
    QCOMPARE (get (destination), *result.committed_bytes);
  }
  void hodariumV3ReplicaRoundtrip () {
    using namespace athena::hodarium;
    QTemporaryDir temporary;
    QVERIFY (temporary.isValid ());
    const fs::path root (temporary.path ().toStdString ());
    const auto a= root / "a", b= root / "b";
    fs::create_directory (a); fs::create_directory (b);
    for (const auto& dir: {a, b})
      put (dir / "Vaultfile.json", "{\"name\":\"V3 sync test\",\"node_model_version\":2}");
    tree body (DOCUMENT, "initial source");
    athena::node::set (body, {athena::node::new_id (), {}});
    std::string original= write_xml_v3 (tree (DOCUMENT, compound ("body", body)));
    const auto fixture= qgetenv ("ATHENA_V3_SYNC_SOURCE");
    if (!fixture.isEmpty ()) original= get (fs::path (fixture.toStdString ()));
    (void) read_xml_v3 (original);
    put (a / "document.ath", original);
    revision_store ja (a / ".athena-journal.sqlite"), jb (b / ".athena-journal.sqlite");
    athena::history::document_history_store ha, hb;
    std::string error;
    QVERIFY2 (ha.open (a, error), error.c_str ());
    QVERIFY2 (hb.open (b, error), error.c_str ());
    const auto capture= [&] (const fs::path& directory, revision_store& journal) {
      std::atomic<bool> cancelled {false};
      auto inventory= inventory_sources (directory, cancelled);
      if (!inventory.errors.empty ()) throw std::runtime_error (inventory.errors.front ());
      if (inventory.documents.size () != 1) throw std::runtime_error ("Expected one v3 document");
      const auto& file= inventory.documents.front ();
      revision value;
      value.vault= "vault"; value.object= file.object;
      value.origin_member= directory.filename ().string ();
      value.relative_path= file.path; value.payload= *file.bytes;
      value.format= document_revision_format; value.semantic_version= document_revision_semantics;
      detail::validate_document_application_source (value);
      auto expected= journal.applied (value.vault, value.object);
      auto saved= journal.capture_saved (value, expected);
      if (!saved) throw std::runtime_error ("Could not capture v3 source");
      return *saved;
    };
    const auto transfer= [&] (revision_store& from, revision_store& to) {
      auto allowed= [] (const std::string& vault) { return vault == "vault"; };
      vault_revision_sender sender (from, "vault", allowed);
      revision_receiver receiver (to, allowed);
      auto message= sender.begin ();
      int steps= 0;
      while (!sender.complete ()) {
        if (++steps > 10000) throw std::runtime_error ("V3 transfer did not converge");
        message= sender.acknowledge (receiver.accept (message));
      }
    };
    auto first= capture (a, ja);
    transfer (ja, jb);
    QVERIFY (apply_closed_document (b, jb, hb, first));
    QCOMPARE (get (b / "document.ath"), original);

    tree updated= read_xml_v3 (original);
    for (int i= 0; i < N (updated); ++i) if (is_compound (updated[i], "body", 1)) {
      tree paragraph ("Edited on the second replica");
      athena::node::set (paragraph, {athena::node::new_id (), {}});
      updated[i][0] << paragraph;
    }
    const auto changed= write_xml_v3 (updated);
    QVERIFY (changed != original);
    put (b / "document.ath", changed);
    auto second= capture (b, jb);
    transfer (jb, ja);
    QVERIFY (apply_closed_document (a, ja, ha, second));
    QCOMPARE (get (a / "document.ath"), changed);
    auto intent= ja.application ("hodarium-apply-" + second);
    QVERIFY (intent && intent->completed && intent->protected_history_version > 0);
    std::string protected_source;
    QVERIFY (ha.reconstruct (intent->protected_history_version, protected_source, error));
    QCOMPARE (protected_source, original);

    auto removed= *ja.get (second);
    removed.id.clear (); removed.parents.clear (); removed.deleted= true; removed.payload.clear ();
    fs::remove (a / "document.ath");
    auto last= ja.capture_saved (removed, second);
    QVERIFY (last);
    transfer (ja, jb);
    QVERIFY (apply_closed_document (b, jb, hb, *last));
    QVERIFY (!fs::exists (b / "document.ath"));
    intent= jb.application ("hodarium-apply-" + *last);
    QVERIFY (intent && intent->completed);
    QVERIFY (hb.reconstruct (intent->protected_history_version, protected_source, error));
    QCOMPARE (protected_source, changed);
    if (!fixture.isEmpty ()) QCOMPARE (get (fs::path (fixture.toStdString ())), original);
  }
  void hodariumApplication () {
    using namespace athena::hodarium;
    QTemporaryDir temporary;
    QVERIFY (temporary.isValid ());
    const fs::path root (temporary.path ().toStdString ());
    put (root / "Vaultfile.json", "{\"name\":\"Application test\",\"node_model_version\":2}");
    athena::history::document_history_store history;
    std::string error;
    QVERIFY2 (history.open (root, error), error.c_str ());
    revision_store journal (":memory:");
    tree body (DOCUMENT, "first content");
    const std::string object= "10000000-0000-4000-8000-000000000001";
    athena::node::set (body, {object, {}});
    const auto payload= [&] { return write_xml_v3 (tree (DOCUMENT, compound ("body", body))); };
    revision value;
    value.vault= "vault"; value.object= object; value.origin_member= "remote";
    value.format= document_revision_format; value.semantic_version= document_revision_semantics;
    value.relative_path= "one.ath"; value.payload= payload ();
    auto first= seal_revision (value); journal.receive (first);
    QVERIFY (!apply_closed_document (root, journal, history, first.id, [] { return false; }));
    QVERIFY (!fs::exists (root / "one.ath"));
    QVERIFY (journal.pending_applications ("vault").empty ());
    QVERIFY (apply_closed_document (root, journal, history, first.id));
    QCOMPARE (get (root / "one.ath"), first.payload);
    QCOMPARE (journal.applied ("vault", object).value (), first.id);
    value.parents= {first.id}; body[0]= "second content"; value.payload= payload ();
    auto second= seal_revision (value); journal.receive (second);
    int authorization_checks= 0;
    QVERIFY (!apply_closed_document (root, journal, history, second.id,
      [&] { return ++authorization_checks <= 2; }));
    QCOMPARE (get (root / "one.ath"), first.payload);
    QVERIFY (journal.pending_applications ("vault").empty ());
    QVERIFY (apply_closed_document (root, journal, history, second.id));
    auto intent= journal.application ("hodarium-apply-" + second.id);
    QVERIFY (intent && intent->completed && intent->protected_history_version > 0);
    std::string old;
    QVERIFY (history.reconstruct (intent->protected_history_version, old, error));
    QCOMPARE (old, first.payload);

    value.parents= {second.id}; value.relative_path= "renamed.ath";
    auto renamed= seal_revision (value); journal.receive (renamed);
    apply_intent interrupted;
    interrupted.operation= "hodarium-apply-" + renamed.id;
    interrupted.revision_id= renamed.id; interrupted.expected_revision= second.id;
    interrupted.source_path= "one.ath";
    interrupted.source_fingerprint= journal.payload_fingerprint (second.id);
    QVERIFY (history.protect ("one.ath", second.payload, interrupted.operation,
      interrupted.protected_history_version, error));
    QVERIFY (journal.prepare_apply (interrupted));
    // Simulate interruption after installing the new name, before removing old.
    put (root / "renamed.ath", renamed.payload);
    QVERIFY_THROWS_EXCEPTION (std::invalid_argument,
      apply_closed_document (root, journal, history, renamed.id));
    const auto rename_records= make_document_rename_records (root);
    QVERIFY (apply_closed_document (root, journal, history, renamed.id, {}, rename_records));
    QVERIFY (!fs::exists (root / "one.ath"));
    QCOMPARE (get (root / "renamed.ath"), renamed.payload);
    QVERIFY (journal.pending_applications ("vault").empty ());

    value.parents= {renamed.id}; value.deleted= true; value.payload.clear ();
    auto deleted= seal_revision (value); journal.receive (deleted);
    put (root / "renamed.ath", "outside edit");
    QVERIFY_EXCEPTION_THROWN (apply_closed_document (root, journal, history, deleted.id), std::runtime_error);
    QCOMPARE (get (root / "renamed.ath"), std::string ("outside edit"));
    QVERIFY (!journal.application ("hodarium-apply-" + deleted.id));
    put (root / "renamed.ath", renamed.payload);
    QVERIFY (apply_closed_document (root, journal, history, deleted.id));
    QVERIFY (!fs::exists (root / "renamed.ath"));
    QCOMPARE (journal.applied ("vault", object).value (), deleted.id);
    QVERIFY (apply_closed_document (root, journal, history, deleted.id));
  }
  void hodariumInventory () {
    QTemporaryDir temporary;
    QVERIFY (temporary.isValid ());
    fs::path root (temporary.path ().toStdString ());
    fs::create_directory (root / ".athena");
    tree body (DOCUMENT, "saved source");
    athena::node::set (body, {"10000000-0000-4000-8000-000000000001", {}});
    tree source (DOCUMENT, compound ("body", body));
    auto bytes= write_xml_v3 (source);
    put (root / "one.ath", bytes);
    put (root / ".athena/ignored.ath", bytes);
    std::atomic<bool> cancelled {false};
    auto initial= athena::hodarium::inventory_sources (root, cancelled);
    QCOMPARE (initial.documents.size (), std::size_t (1));
    QVERIFY (initial.errors.empty ());
    QCOMPARE (*initial.documents[0].bytes, bytes);
    QCOMPARE (athena::hodarium::inventory_sources (root, cancelled, initial.cache).documents.size (), std::size_t (1));
    initial.cache.at ("one.ath").published= true;
    auto unchanged= athena::hodarium::inventory_sources (root, cancelled, initial.cache);
    QVERIFY (unchanged.documents.empty ());
    QCOMPARE (unchanged.cache.size (), std::size_t (1));
    put (root / "copy.ath", bytes);
    auto duplicate= athena::hodarium::inventory_sources (root, cancelled, unchanged.cache);
    QVERIFY (duplicate.documents.empty ());
    QCOMPARE (duplicate.errors.size (), std::size_t (1));
    QVERIFY (!duplicate.cache.at ("one.ath").published);
    QCOMPARE (get (root / "one.ath"), bytes);
    cancelled.store (true);
    QVERIFY (athena::hodarium::inventory_sources (root, cancelled).documents.empty ());
  }
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
  void persistentV3Storage () {
    QTemporaryDir temporary;
    QVERIFY (temporary.isValid ());
    const fs::path file= fs::path (temporary.path ().toStdString ()) / "v3.ath";
    // No native math node is required to retain the file's version.
    tree source= migrated ();
    athena::node::set (source, {"10000000-0000-4000-8000-000000000002", {}});
    document_save_result created;
    auto storage= document_file::create (
      file, source, created, xml_storage_version::v3);
    QCOMPARE (storage.version (), xml_storage_version::v3);
    QCOMPARE (read_xml_v3 (get (file)), source);
    const auto decoded= decode_document_bytes (get (file));
    QCOMPARE (decoded.format, document_source_format::xml_v3);
    QCOMPARE (decoded.document, source);
    auto reopened= document_file::capture (file);
    QCOMPARE (reopened.version (), xml_storage_version::v3);
    tree second= copy (source);
    second[0]= "changed";
    auto saved= reopened.save (second);
    QVERIFY (saved.committed_bytes);
    QCOMPARE (read_xml_v3 (*saved.committed_bytes), second);
    QCOMPARE (athena::node::id (read_xml_v3 (get (file))), athena::node::id (source));
    QCOMPARE (inspect_xml_version (get (file)), xml_format_version::v3);
    const auto external= write_xml_v3 (tree (DOCUMENT, "external"));
    put (file, external);
    QVERIFY_THROWS_EXCEPTION (std::system_error, reopened.save (source));
    QCOMPARE (get (file), external);
  }
  void strictV3EnvelopeDispatch () {
    QCOMPARE (inspect_xml_version (write_xml (tree ("x"), xml_kind::fragment)),
              xml_format_version::v1);
    QCOMPARE (inspect_xml_version (write_xml_v2 (tree ("x"), xml_kind::fragment)),
              xml_format_version::v2);
    QCOMPARE (inspect_xml_version (write_xml_v3 (tree ("x"), xml_kind::fragment)),
              xml_format_version::v3);
    const std::string plain= "<a v=\"3\" k=\"d\"><n t=\"document\"><t><v>x</v></t></n></a>";
    for (const auto& prefix: {std::string (), std::string ("\xef\xbb\xbf"), std::string (" \n")}) {
      const auto decoded= decode_document_bytes (prefix + plain);
      QCOMPARE (decoded.format, document_source_format::xml_v3);
      QCOMPARE (decoded.document, tree (DOCUMENT, "x"));
    }
    QVERIFY_THROWS_EXCEPTION (codec_exception,
      decode_document_bytes ("<a v=\"3\" k=\"d\"><n t=\"document\"><t>x</t></n></a>"));
    QVERIFY_THROWS_EXCEPTION (codec_exception,
      decode_document_bytes ("<a v=\"4\" k=\"d\"><n t=\"document\"/></a>"));
    QVERIFY_THROWS_EXCEPTION (codec_exception,
      decode_document_bytes ("<!DOCTYPE a [<!ENTITY x 'value'>]>" + plain));
    legacy_import_limits limits;
    limits.codec.input_bytes= 8;
    QVERIFY_THROWS_EXCEPTION (codec_exception, decode_document_bytes (plain, limits));
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
    QVERIFY (upgraded.committed_bytes);
    QCOMPARE (*upgraded.committed_bytes, get (file));
    QVERIFY (upgraded.upgraded_legacy);
    QVERIFY (upgraded.backup.has_value ());
    QCOMPARE (get (*upgraded.backup), legacy);
    QVERIFY (!storage.legacy ());
    QCOMPARE (storage.source_sha256 (), upgraded.xml_sha256);
    QCOMPARE (storage.source_sha256 (), storage_bytes_fingerprint (get (file)));
    tree second (DOCUMENT, "second");
    auto saved= storage.save (second);
    QVERIFY (saved.committed_bytes);
    QCOMPARE (*saved.committed_bytes, get (file));
    QVERIFY (*saved.committed_bytes != *upgraded.committed_bytes);
    QCOMPARE (storage_bytes_fingerprint (*upgraded.committed_bytes), upgraded.xml_sha256);
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
