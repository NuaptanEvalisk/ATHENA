/******************************************************************************
* MODULE     : node_location_test.cpp
* DESCRIPTION: Disposable UUID locations, live precedence and reference failure states
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#include <QtTest/QtTest>
#include <QTemporaryDir>
#include "ATHENA/Data/node_location.hpp"
#include "Data/Convert/Xml/athena_document_xml.hpp"
#include "node_metadata.hpp"
#include <condition_variable>
#include <fstream>
#include <mutex>
#include <thread>

bool headless_mode= true;
bool is_headless () { return true; }
namespace loc= athena::node_location;
namespace node= athena::node;
namespace {
const std::string a= "11111111-1111-4111-8111-111111111111";
const std::string b= "22222222-2222-4222-8222-222222222222";
const std::string c= "33333333-3333-4333-8333-333333333333";
tree identified (tree t, const std::string& id) { node::set (t, {id, {}}); return t; }
std::filesystem::path root (const QTemporaryDir& dir) { return dir.path ().toStdString (); }
void bytes (const std::filesystem::path& file, const std::string& data) {
  std::filesystem::create_directories (file.parent_path ());
  std::ofstream out (file, std::ios::binary | std::ios::trunc);
  out.exceptions (std::ios::badbit | std::ios::failbit);
  out.write (data.data (), data.size ());
}
void document (const std::filesystem::path& file, const tree& source) {
  bytes (file, athena::document::write_xml_v2 (source));
}
loc::result wait (const std::shared_ptr<loc::query>& query) {
  const auto until= std::chrono::steady_clock::now () + std::chrono::seconds (10);
  for (;;) {
    auto result= query->poll ();
    if (result.state != loc::status::pending) return result;
    if (std::chrono::steady_clock::now () >= until) throw std::runtime_error ("Location query timed out");
    std::this_thread::sleep_for (std::chrono::milliseconds (10));
  }
}
}

class TestNodeLocation: public QObject {
  Q_OBJECT
private slots:
  void initTestCase () {
    make_tree_label (DOCUMENT, "document");
    make_tree_label (CONCAT, "concat");
  }
  void typedAddressesAndStaleIdentity () {
    tree paragraph= identified (tree ("Text"), b);
    node::metadata metadata {a, {}};
    metadata.properties["example:rich"]= node::property (node::property::list {
      node::property (node::property::dictionary {{"name", node::property (
        node::rich_text {identified (tree (CONCAT, "Title"), c)})}})});
    tree source (DOCUMENT, paragraph);
    node::set (source, metadata);
    auto found= loc::collect (source);
    QCOMPARE (found.size (), std::size_t (3));
    for (const auto& match: found) {
      tree value= loc::lookup (source, match.where, match.id);
      QCOMPARE (node::id (value), match.id);
    }
    QCOMPARE (found[1].where.size (), std::size_t (4));
    QCOMPARE (found[2].where.size (), std::size_t (1));
    QVERIFY_EXCEPTION_THROWN (loc::lookup (source, found[2].where, a), std::runtime_error);
    source[0]= "Replacement";
    QVERIFY_EXCEPTION_THROWN (loc::lookup (source, found[2].where, b), std::runtime_error);
  }
  void rebuildRenameAndRevisionCheck () {
    QTemporaryDir dir;
    const tree source (DOCUMENT, identified (tree ("Original"), a));
    document (root (dir) / "a.ath", source);
    loc::service service (root (dir));
    auto first= wait (service.request ({a}));
    QVERIFY (first.state == loc::status::resolved);
    QVERIFY (loc::read_disk (root (dir), first.items[0]) == source[0]);
    service.clear_cache ();
    auto again= wait (service.request ({a}));
    QCOMPARE (again.items[0].candidates[0].where, first.items[0].candidates[0].where);
    std::filesystem::rename (root (dir) / "a.ath", root (dir) / "renamed.ath");
    auto renamed= wait (service.request ({a}));
    QVERIFY (renamed.state == loc::status::resolved);
    QCOMPARE (renamed.items[0].candidates[0].file, std::string ("renamed.ath"));
    QVERIFY_EXCEPTION_THROWN (loc::read_disk (root (dir), first.items[0]), std::exception);
    document (root (dir) / "renamed.ath", tree (DOCUMENT, identified (tree ("Revised"), a)));
    QVERIFY_EXCEPTION_THROWN (loc::read_disk (root (dir), renamed.items[0]), std::runtime_error);
    auto revised= wait (service.request ({a}));
    QVERIFY (loc::read_disk (root (dir), revised.items[0]) == identified (tree ("Revised"), a));
    document (root (dir) / "renamed.ath", tree (DOCUMENT, identified (tree ("Different identity"), b)));
    QVERIFY (wait (service.request ({a})).state == loc::status::missing);
  }
  void duplicatesErrorsAndExcludedTrees () {
    QTemporaryDir dir;
    const tree source (DOCUMENT, identified (tree ("Target"), a));
    document (root (dir) / "first.ath", source);
    document (root (dir) / "second.ath", source);
    for (const auto* ignored: {".athena", ".backup", ".git"})
      bytes (root (dir) / ignored / "unreadable.ath", "not XML");
    std::filesystem::create_directory_symlink (root (dir) / ".backup", root (dir) / "backup-alias");
    std::filesystem::create_symlink (root (dir) / "first.ath", root (dir) / "alias.ath");
    loc::service service (root (dir));
    auto duplicate= wait (service.request ({a}));
    QVERIFY (duplicate.state == loc::status::conflict);
    QCOMPARE (duplicate.items[0].candidates.size (), std::size_t (2));
    QVERIFY (duplicate.diagnostics.empty ());
    std::filesystem::remove (root (dir) / "second.ath");
    QVERIFY (wait (service.request ({a})).state == loc::status::resolved);
    bytes (root (dir) / "broken.ath", "<athena-document>");
    auto incomplete= wait (service.request ({a, b}));
    QVERIFY (incomplete.state == loc::status::unreadable);
    QVERIFY (incomplete.items[1].state == loc::status::unreadable);
    QCOMPARE (incomplete.diagnostics[0].file, std::string ("broken.ath"));
    std::filesystem::remove (root (dir) / "broken.ath");
    QVERIFY (wait (service.request ({b})).state == loc::status::missing);
  }
  void orderedListsCyclesAndAncestorOverlap () {
    QTemporaryDir dir;
    document (root (dir) / "doc.ath", identified (tree (DOCUMENT,
      identified (tree ("First"), a), identified (tree ("Second"), b)), c));
    loc::service service (root (dir));
    auto ordered= wait (service.request ({b, a, b}));
    QVERIFY (ordered.state == loc::status::resolved);
    QCOMPARE (ordered.items.size (), std::size_t (2));
    QCOMPARE (ordered.items[0].id, b);
    QCOMPARE (ordered.items[1].id, a);
    auto overlap= wait (service.request ({a, c}));
    QVERIFY (overlap.state == loc::status::overlap);
    QVERIFY (overlap.items[0].state == loc::status::overlap);
    auto cycle= wait (service.request ({a, b}, {b}));
    QVERIFY (cycle.state == loc::status::cycle);
    QVERIFY (cycle.items[0].state == loc::status::resolved);
    QVERIFY (cycle.items[1].state == loc::status::cycle);
    QVERIFY (service.request ({"bad"})->poll ().state == loc::status::invalid);
    QVERIFY (service.request ({a}, {"bad"})->poll ().state == loc::status::invalid);
  }
  void liveSourceShadowsDiskEvenOnDeletionOrReadFailure () {
    QTemporaryDir dir;
    document (root (dir) / "doc.ath", tree (DOCUMENT, identified (tree ("Saved"), a)));
    std::mutex lock;
    loc::live_source live {"doc.ath", 42, 7,
      loc::collect (tree (DOCUMENT, identified (tree ("Unsaved"), b))), {}};
    loc::service service (root (dir), [&] (const std::atomic<bool>&) {
      std::lock_guard<std::mutex> guard (lock);
      return std::vector<loc::live_source> {live};
    });
    auto result= wait (service.request ({a, b}));
    QVERIFY (result.items[0].state == loc::status::missing);
    QVERIFY (result.items[1].state == loc::status::resolved);
    QCOMPARE (result.items[1].candidates[0].actor, std::uint64_t (42));
    QCOMPARE (result.items[1].candidates[0].live_capture, std::uint64_t (7));
    QVERIFY_EXCEPTION_THROWN (loc::read_disk (root (dir), result.items[1]), std::invalid_argument);
    { std::lock_guard<std::mutex> guard (lock); live.nodes.clear (); live.capture++; }
    QVERIFY (wait (service.request ({a, b})).state == loc::status::missing);
    { std::lock_guard<std::mutex> guard (lock); live.error= "Actor unavailable"; }
    result= wait (service.request ({a}));
    QVERIFY (result.state == loc::status::unreadable);
    QVERIFY (result.items[0].candidates.empty ());
  }
  void pendingQueriesShareScanAndCancellation () {
    QTemporaryDir dir;
    document (root (dir) / "doc.ath", tree (DOCUMENT, identified (tree ("Target"), a)));
    std::mutex lock;
    std::condition_variable cv;
    bool entered= false, release= false;
    int captures= 0;
    loc::service service (root (dir), [&] (const std::atomic<bool>& stopping) {
      std::unique_lock<std::mutex> guard (lock);
      ++captures; entered= true; cv.notify_all ();
      while (!release && !stopping) cv.wait_for (guard, std::chrono::milliseconds (10));
      return std::vector<loc::live_source> {};
    });
    auto first= service.request ({a});
    {
      std::unique_lock<std::mutex> guard (lock);
      QVERIFY (cv.wait_for (guard, std::chrono::seconds (10), [&] { return entered; }));
    }
    QVERIFY (first->poll ().state == loc::status::pending);
    auto second= service.request ({a});
    auto cancelled= service.request ({b}); cancelled->cancel ();
    { std::lock_guard<std::mutex> guard (lock); release= true; } cv.notify_all ();
    const auto one= wait (first), two= wait (second);
    QVERIFY (one.state == loc::status::resolved);
    QCOMPARE (one.scan, two.scan);
    QCOMPARE (captures, 1);
    QVERIFY (cancelled->poll ().state == loc::status::cancelled);
  }
  void confinementAndLiveConflicts () {
    QTemporaryDir dir, outside;
    document (root (outside) / "secret.ath", tree (DOCUMENT, identified (tree ("Secret"), a)));
    std::filesystem::create_symlink (root (outside) / "secret.ath", root (dir) / "escape.ath");
    loc::service service (root (dir));
    auto result= wait (service.request ({a}));
    QVERIFY (result.state == loc::status::unreadable);
    QVERIFY (result.items[0].candidates.empty ());
    std::filesystem::remove (root (dir) / "escape.ath");
    const auto nodes= loc::collect (tree (DOCUMENT, identified (tree ("Target"), a)));
    loc::service duplicate (root (dir), [nodes] (const std::atomic<bool>&) {
      return std::vector<loc::live_source> {{"", 1, 1, nodes, {}}, {"", 2, 1, nodes, {}}};
    });
    QVERIFY (wait (duplicate.request ({a})).state == loc::status::conflict);
  }
  void unannotatedV1AndReplacedRoot () {
    QTemporaryDir dir;
    const auto vault= root (dir) / "vault";
    bytes (vault / "plain.ath", athena::document::write_xml (tree (DOCUMENT, "Old XML")));
    loc::service service (vault);
    QVERIFY (wait (service.request ({a})).state == loc::status::missing);
    document (vault / "identified.ath", tree (DOCUMENT, identified (tree ("Text"), a)));
    QVERIFY (wait (service.request ({a})).state == loc::status::resolved);
    std::filesystem::rename (vault, root (dir) / "old-vault");
    document (vault / "identified.ath", tree (DOCUMENT, identified (tree ("Other vault"), a)));
    auto stale= wait (service.request ({a}));
    QVERIFY (stale.state == loc::status::unreadable);
    QVERIFY (stale.items[0].candidates.empty ());
    QVERIFY (!stale.diagnostics.empty ());
  }
  void duplicateIdentityInsideOneFileIsAConflict () {
    QTemporaryDir dir;
    auto xml= athena::document::write_xml_v2 (tree (DOCUMENT,
      identified (tree ("First"), a), identified (tree ("Second"), b)));
    xml.replace (xml.find (b), b.size (), a);
    bytes (root (dir) / "duplicate.ath", xml);
    loc::service service (root (dir));
    auto result= wait (service.request ({a, c}));
    QVERIFY (result.items[0].state == loc::status::conflict);
    QVERIFY (result.items[1].state == loc::status::unreadable);
    QCOMPARE (result.diagnostics[0].identity, a);
  }
};
QTEST_APPLESS_MAIN (TestNodeLocation)
#include "node_location_test.moc"
