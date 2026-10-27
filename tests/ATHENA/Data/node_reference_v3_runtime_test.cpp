/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#include <QtTest/QtTest>
#include <QCryptographicHash>
#include <QFile>
#include <QTemporaryDir>
#include "ATHENA/Data/node_location.hpp"
#include "ATHENA/Data/node_reference.hpp"
#include "ATHENA/Data/node_reference_export.hpp"
#include "ATHENA/Data/source_identity_repair.hpp"
#include "ATHENA/Data/document_history_store.hpp"
#include "Data/Convert/Xml/document_file_codec.hpp"
#include "Data/NodeContracts/execution_state.hpp"
#include "math_tree_v3.hpp"
#include "node_metadata.hpp"
#include <chrono>
#include <thread>

bool headless_mode= true;
bool is_headless () { return true; }

namespace loc= athena::node_location;
namespace ref= athena::node_reference;
namespace node= athena::node;
using namespace athena::document;

namespace {
const std::string a= "11111111-1111-4111-8111-111111111111";
const std::string b= "22222222-2222-4222-8222-222222222222";
const std::string c= "33333333-3333-4333-8333-333333333333";

void save (const std::filesystem::path& path, const std::string& bytes) {
  QFile file (QString::fromStdString (path.string ()));
  if (!file.open (QIODevice::WriteOnly | QIODevice::Truncate) ||
      file.write (bytes.data (), qsizetype (bytes.size ())) != qint64 (bytes.size ()))
    throw std::runtime_error ("Cannot write node-reference fixture");
}
std::string read (const std::filesystem::path& path) {
  QFile file (QString::fromStdString (path.string ()));
  if (!file.open (QIODevice::ReadOnly)) throw std::runtime_error ("Cannot read fixture");
  return file.readAll ().toStdString ();
}
tree identified (tree source, const std::string& id) {
  node::metadata meta= node::get (source) ? *node::get (source) : node::metadata {};
  meta.id= id;
  node::set (source, meta);
  return source;
}
tree fixture (xml_format_version version) {
  tree expression= version == xml_format_version::v3 ? athena::math_v3::scripts (
    tree ("x"), tree (""), tree (""), tree ("i"), tree ("2"), 'o') : tree ("legacy");
  expression= identified (expression, b);
  tree rich= identified (tree ("Rich context"), c);
  tree body= identified (tree (DOCUMENT, expression), a);
  node::metadata meta= *node::get (body);
  meta.properties.emplace ("fixture:rich", node::property (node::rich_text {rich}));
  node::set (body, meta);
  return tree (DOCUMENT, compound ("style", tree (TUPLE, tree ("generic"))),
               compound ("body", body));
}
loc::snapshot wait (const std::shared_ptr<loc::query>& query) {
  const auto until= std::chrono::steady_clock::now () + std::chrono::seconds (5);
  do {
    const auto value= query->read ();
    if (value->state != loc::status::pending) return value;
    std::this_thread::sleep_for (std::chrono::milliseconds (5));
  } while (std::chrono::steady_clock::now () < until);
  throw std::runtime_error ("Node-reference fixture timed out");
}
ref::prepared_snapshot wait (const ref::export_preparation& job) {
  const auto until= std::chrono::steady_clock::now () + std::chrono::seconds (5);
  do {
    if (auto value= job.read ()) return value;
    std::this_thread::sleep_for (std::chrono::milliseconds (5));
  } while (std::chrono::steady_clock::now () < until);
  throw std::runtime_error ("Export preparation fixture timed out");
}
bool contains (const tree& source, const tree& expected) {
  if (node::content_equal (source, expected)) return true;
  if (is_compound (source))
    for (int i=0; i<N (source); ++i) if (contains (source[i], expected)) return true;
  return false;
}
std::string digest (const std::string& bytes) {
  return QCryptographicHash::hash (QByteArrayView (bytes.data (), bytes.size ()),
    QCryptographicHash::Sha256).toHex ().toStdString ();
}
}

class TestNodeReferenceV3Runtime: public QObject {
  Q_OBJECT
private slots:
  void initTestCase () { athena::contracts::initialize_builtins (); }

  void diskCensusAndPreviewPreserveSourceVersion () {
    for (const auto version: {xml_format_version::v2, xml_format_version::v3}) {
      QTemporaryDir directory;
      QVERIFY (directory.isValid ());
      const std::filesystem::path root= directory.path ().toStdString ();
      const tree source= fixture (version);
      save (root / "source.ath", encode_native_xml_bytes (source, version));
      loc::service service (root);
      const auto result= wait (service.request ({b}, {}, true));
      QVERIFY (result->state == loc::status::resolved);
      QCOMPARE (result->items.size (), std::size_t (1));
      const auto& target= result->items[0];
      const auto selected= decode_native_xml_bytes (target.fragment_xml, xml_kind::fragment);
      QVERIFY (selected.version == version);
      QVERIFY (selected.document == source[1][0][0]);
      QVERIFY (decode_native_xml_bytes (target.preview_context_xml,
                                        xml_kind::fragment).version == version);
      QVERIFY (loc::read_disk (root, target) == source[1][0][0]);
      url location;
      const tree preview= ref::preview_document ({result, 1}, location);
      QVERIFY (contains (preview, source[1][0][0]));
      QVERIFY (loc::collect (preview).empty ());
      const tree context= ref::preview_context_document ({result, 1}, location);
      QVERIFY (contains (context, source[1][0][0]));
      const auto rich= wait (service.request ({c}, {}, true));
      QVERIFY (rich->state == loc::status::resolved);
      QVERIFY (decode_native_xml_bytes (rich->items[0].fragment_xml,
                                        xml_kind::fragment).version == version);
      QCOMPARE (node::id (loc::read_disk (root, rich->items[0])), c);
    }
  }

  void emptyPreviewContextStillUsesExplicitV3 () {
    tree source= identified (tree (DOCUMENT, tree ("plain")), a);
    loc::item target;
    target.id= a;
    target.state= loc::status::resolved;
    target.candidates.emplace_back ();
    const auto payload= loc::capture_content (source, target, "source.ath", xml_format_version::v3);
    const auto context= decode_native_xml_bytes (payload.preview_context_xml, xml_kind::fragment);
    QVERIFY (context.version == xml_format_version::v3);
    QVERIFY (!node::contains_metadata (context.document));
    QVERIFY (is_func (context.document, DOCUMENT, 0));
  }

  void v3ExportPreparationFollowsNestedReferences () {
    QTemporaryDir directory;
    const std::filesystem::path root= directory.path ().toStdString ();
    tree source= fixture (xml_format_version::v3);
    source[1][0] << tree (TRANSCLUDE, tree (TUPLE, tree (c.c_str ())));
    save (root / "source.ath", write_xml_v3 (source));
    auto service= std::make_shared<loc::service> (root);
    ref::export_preparation job (service, {{{a}, {}}});
    const auto prepared= wait (job);
    QVERIFY2 (prepared->error.empty (), prepared->error.c_str ());
    QCOMPARE (prepared->selections.size (), std::size_t (2));
    QVERIFY (prepared->selections.at ({{c}, {a}})->state == loc::status::resolved);
  }

  void exportRevisionKeepsV2DigestAndExplicitV3 () {
    const tree plain (DOCUMENT, tree ("plain"));
    QCOMPARE (ref::export_source_revision (plain), digest (write_xml_v2 (plain, xml_kind::fragment)));
    QCOMPARE (ref::export_source_revision (plain, xml_format_version::v2),
              ref::export_source_revision (plain));
    QCOMPARE (ref::export_source_revision (plain, xml_format_version::v3),
              digest (write_xml_v3 (plain, xml_kind::fragment)));
    QVERIFY (ref::export_source_revision (plain, xml_format_version::v3) !=
             ref::export_source_revision (plain, xml_format_version::v2));
    const tree source= fixture (xml_format_version::v3);
    QCOMPARE (ref::export_source_revision (source, xml_format_version::v3),
              digest (write_xml_v3 (source, xml_kind::fragment)));
  }

  void malformedV3IsUnreadableNotLegacyFallback () {
    QTemporaryDir directory;
    const std::filesystem::path root= directory.path ().toStdString ();
    save (root / "bad.ath", "<a v='3' k='d'><node tag='document'/></a>");
    loc::service service (root);
    QVERIFY (wait (service.request ({a}))->state == loc::status::unreadable);
  }

  void closedIdentityRepairPreservesFormatAndHistory () {
    for (const auto version: {xml_format_version::v2, xml_format_version::v3}) {
      QTemporaryDir directory;
      const std::filesystem::path root= directory.path ().toStdString ();
      tree expected= fixture (version);
      const auto original= encode_native_xml_bytes (expected, version);
      save (root / "source.ath", original);
      bool called= false, live= true;
      std::string error;
      athena::document_node::repair_source_identity (root, "source.ath", a,
        [&] (bool is_live, std::string failure) {
          called= true; live= is_live; error= std::move (failure);
        });
      QVERIFY (called);
      QVERIFY (!live);
      QVERIFY2 (error.empty (), error.c_str ());
      const auto repaired= decode_native_xml_bytes (read (root / "source.ath"));
      QVERIFY (repaired.version == version);
      const auto new_id= node::id (repaired.document[1][0]);
      QVERIFY (node::valid_id (new_id));
      QVERIFY (new_id != a);
      auto meta= *node::get (expected[1][0]); meta.id= new_id;
      node::set (expected[1][0], meta);
      QVERIFY (repaired.document == expected);
      athena::history::document_history_store history;
      QVERIFY2 (history.open (root, error), error.c_str ());
      std::vector<athena::history::version_entry> versions;
      QVERIFY2 (history.list ("source.ath", versions, error), error.c_str ());
      QCOMPARE (versions.size (), std::size_t (1));
      QVERIFY (versions[0].protected_snapshot);
      std::string saved;
      QVERIFY2 (history.reconstruct (versions[0].id, saved, error), error.c_str ());
      QCOMPARE (saved, original);
      called= false;
      athena::document_node::repair_source_identity (root, "source.ath", a,
        [&] (bool, std::string failure) { called= true; error= std::move (failure); });
      QVERIFY (called && !error.empty ());
      QCOMPARE (read (root / "source.ath"), encode_native_xml_bytes (expected, version));
    }
  }
};

QTEST_GUILESS_MAIN (TestNodeReferenceV3Runtime)
#include "node_reference_v3_runtime_test.moc"
