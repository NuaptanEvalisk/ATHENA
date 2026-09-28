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
#include "ATHENA/Data/node_reference.hpp"
#include "ATHENA/Data/node_reference_export.hpp"
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
athena::node_reference::prepared_snapshot wait (const athena::node_reference::export_preparation& job) {
  const auto until= std::chrono::steady_clock::now () + std::chrono::seconds (10);
  for (;;) {
    if (auto result= job.read ()) return result;
    if (std::chrono::steady_clock::now () >= until) throw std::runtime_error ("Export preparation timed out");
    std::this_thread::sleep_for (std::chrono::milliseconds (10));
  }
}
tree reference (const std::string& id) { return tree (TRANSCLUDE, tree (TUPLE, string (id.c_str ()))); }
}

class TestNodeLocation: public QObject {
  Q_OBJECT
private slots:
  void initTestCase () {
    make_tree_label (DOCUMENT, "document");
    make_tree_label (CONCAT, "concat");
    make_tree_label (WITH, "with");
    make_tree_label (TUPLE, "tuple");
    make_tree_label (HLINK, "hlink");
    make_tree_label (IMAGE, "image");
    make_tree_label (TRANSCLUDE, "transclude");
    make_tree_label (COLLECTION, "collection");
    make_tree_label (ASSOCIATE, "associate");
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
  void contentHandoffAndCompletion () {
    QTemporaryDir dir;
    tree target= identified (tree ("Target"), a);
    document (root (dir) / "doc.ath", tree (DOCUMENT, target));
    std::mutex lock;
    std::condition_variable ready;
    loc::snapshot received;
    loc::service service (root (dir));
    auto query= service.request ({a, b}, {}, true, [&] (loc::snapshot result) {
      { std::lock_guard<std::mutex> guard (lock); received= result; }
      ready.notify_all ();
    });
    {
      std::unique_lock<std::mutex> guard (lock);
      QVERIFY (ready.wait_for (guard, std::chrono::seconds (10), [&] { return bool (received); }));
    }
    QVERIFY (received == query->read ());
    QVERIFY (received->items[0].state == loc::status::resolved);
    QVERIFY (received->items[1].state == loc::status::missing);
    QVERIFY (athena::document::read_xml_v2 (received->items[0].fragment_xml,
      athena::document::xml_kind::fragment) == target);
    QVERIFY (received->watched_paths && !received->watched_paths->empty ());
    QCOMPARE (received->items[0].source_directory, root (dir).string ());
  }
  void canonicalPresentationPreservesOrderedMissingItemsAndIndependentAncestry () {
    namespace ref= athena::node_reference;
    tree transclusion (TRANSCLUDE, tree (TUPLE, string (b.c_str ()), string (a.c_str ()), string (b.c_str ())));
    QVERIFY (ref::canonical (transclusion));
    auto ids= ref::targets (transclusion);
    QVERIFY (ids == std::vector<std::string> ({b, a}));
    auto result= std::make_shared<loc::result> ();
    result->state= loc::status::missing;
    result->ancestry= {c};
    loc::item missing; missing.id= b; missing.state= loc::status::missing;
    loc::item resolved; resolved.id= a; resolved.state= loc::status::resolved;
    resolved.fragment_xml= athena::document::write_xml_v2 (identified (tree ("Text"), a),
      athena::document::xml_kind::fragment);
    result->items= {missing, resolved};
    tree display= ref::display ({result, 1});
    QCOMPARE (N(display), 2);
    QVERIFY (is_func (display[0], WITH));
    QVERIFY (ref::ancestry (display[1][1]) == std::vector<std::string> ({c, a}));
    QVERIFY (loc::collect (display).empty ());
    QVERIFY (display[1][2][1][0] == "Text");
    auto overlap= std::make_shared<loc::result> (*result);
    overlap->state= loc::status::overlap;
    QVERIFY (N(ref::display ({overlap, 2})) == 1);
  }
  void nativeTargetSyntaxDoesNotConsumeLegacyHints () {
    namespace ref= athena::node_reference;
    const auto url= string (("tmfs://wikilink/" + a).c_str ());
    QCOMPARE (ref::target_id (url), a);
    QCOMPARE (ref::target_id (string (("tmfs://transclude/" + a + "/context").c_str ())), a);
    QVERIFY (ref::target_id (url * "/file/anchor").empty ());
    QVERIFY (ref::target_id (url * "?recover=x").empty ());
    QVERIFY (ref::target_id ("tmfs://wikilink/not-a-uuid").empty ());
    QVERIFY (ref::target_id ("https://example.com/").empty ());
  }
  void previewRetainsSourceContextWithoutImportingSourceIdentities () {
    namespace ref= athena::node_reference;
    QTemporaryDir dir;
    tree preamble= compound ("hide-preamble", tree (DOCUMENT, "Custom definition"));
    tree style= compound ("style", tree (TUPLE, "generic"));
    tree initial= compound ("initial", tree (COLLECTION, tree (ASSOCIATE, "font-base-size", "17")));
    tree source= identified (tree (DOCUMENT, style, initial,
      compound ("body", tree (DOCUMENT, preamble, "Before context",
        identified (tree ("Selected text"), b), "After context"))), a);
    document (root (dir) / "doc.ath", source);
    loc::service service (root (dir));
    auto child= service.request ({b}, {c}, true);
    QVERIFY (wait (child).state == loc::status::resolved);
    url location;
    tree preview= ref::preview_document ({child->read (), 1}, location);
    QVERIFY (preview[0] == style);
    QVERIFY (preview[1] == initial);
    QVERIFY (preview[2][0][0] == preamble);
    QVERIFY (ref::ancestry (preview[2][0][1][1]) == std::vector<std::string> ({c, b}));
    QVERIFY (preview[2][0][1][2][1][0] == "Selected text");
    QVERIFY (loc::collect (preview).empty ());
    QVERIFY (!is_none (location));
    QCOMPARE (child->read ()->items[0].source_url, (root (dir) / "doc.ath").string ());

    tree nearby= ref::preview_context_document ({child->read (), 1}, location);
    QVERIFY (nearby[0] == style);
    QVERIFY (nearby[1] == initial);
    QVERIFY (nearby[2][0][0] == preamble);
    QVERIFY (is_func (nearby[2][0][1], WITH, 3));
    QVERIFY (ref::ancestry (nearby[2][0][1][1]) == std::vector<std::string> ({c, b}));
    tree context= nearby[2][0][1][2];
    QCOMPARE (N(context), 3);
    QCOMPARE (context[0], tree ("Before context"));
    QVERIFY (is_func (context[1], WITH, 3));
    QCOMPARE (context[2], tree ("After context"));
    QVERIFY (context[0] != tree ("Selected text") && context[2] != tree ("Selected text"));

    auto whole= service.request ({a}, {}, true);
    QVERIFY (wait (whole).state == loc::status::resolved);
    preview= ref::preview_document ({whole->read (), 2}, location);
    QCOMPARE (N(preview[2][0]), 1); // The selected root already owns the preamble.
    auto content= preview[2][0][0][2][1];
    QCOMPARE (N(content), 4);
    QVERIFY (content[0] == preamble);
    QVERIFY (content[1] == "Before context");
    QVERIFY (content[2] == "Selected text");
    QVERIFY (content[3] == "After context");
    QVERIFY (loc::collect (preview).empty ());
    QVERIFY (source[2][0][2] == identified (tree ("Selected text"), b));
  }
  void previewPendingAndMissingKeepAnExplicitBody () {
    namespace ref= athena::node_reference;
    url source;
    tree pending= ref::preview_document ({}, source);
    QVERIFY (is_none (source));
    QVERIFY (is_compound (pending[1], "body", 1));
    QVERIFY (pending[1][0][0] == "Locating referenced nodes...");
    auto result= std::make_shared<loc::result> ();
    result->state= loc::status::missing;
    loc::item missing; missing.id= a; missing.state= loc::status::missing;
    result->items.push_back (missing);
    auto absent= ref::preview_document ({result, 1}, source);
    QVERIFY (is_none (source));
    QVERIFY (is_func (absent[1][0][0], WITH));
  }
  void exportPreparationClosesDependenciesAndPreservesFailureStates () {
    namespace ref= athena::node_reference;
    QTemporaryDir dir;
    document (root (dir) / "a.ath", identified (tree (DOCUMENT, reference (b)), a));
    document (root (dir) / "b.ath", identified (tree (DOCUMENT, reference (a)), b));
    tree source (DOCUMENT, reference (a));
    node::metadata metadata;
    metadata.properties["example:rich"]= node::property (node::rich_text {reference (c)});
    node::set (source, metadata);
    auto seeds= ref::export_selections (source);
    QCOMPARE (seeds.size (), std::size_t (2));
    auto service= std::make_shared<loc::service> (root (dir));
    ref::export_preparation job (service, seeds);
    auto prepared= wait (job);
    QVERIFY2 (prepared->error.empty (), prepared->error.c_str ());
    QVERIFY (!prepared->cancelled);
    QCOMPARE (prepared->selections.size (), std::size_t (4));
    QVERIFY (prepared->selections.at ({{a}, {}})->state == loc::status::resolved);
    QVERIFY (prepared->selections.at ({{b}, {a}})->state == loc::status::resolved);
    QVERIFY (prepared->selections.at ({{a}, {a, b}})->items[0].state == loc::status::cycle);
    QVERIFY (prepared->selections.at ({{c}, {}})->items[0].state == loc::status::missing);
    ref::export_reference_scope scope (prepared);
    QVERIFY (ref::export_reference_view ({{b}, {a}})->snapshot != nullptr);
    scope.require_ready ();
  }
  void exportSourceRevisionIncludesMetadataAndStructure () {
    namespace ref= athena::node_reference;
    tree source= identified (tree (DOCUMENT, "unchanged text"), a);
    const auto revision= ref::export_source_revision (source);
    QCOMPARE (revision.size (), std::size_t (64));
    QCOMPARE (ref::export_source_revision (copy (source)), revision);
    auto roundtrip= athena::document::read_xml_v2 (
      athena::document::write_xml_v2 (source));
    QCOMPARE (ref::export_source_revision (roundtrip), revision);
    tree changed= copy (source);
    node::set (changed, {b, {}});
    QVERIFY (ref::export_source_revision (changed) != revision);
    changed= copy (source);
    node::set (changed, {a, {{"example:flag", node::property (true)}}});
    QVERIFY (ref::export_source_revision (changed) != revision);
    changed= copy (source); changed[0]= tree (CONCAT, "unchanged text");
    QVERIFY (ref::export_source_revision (changed) != revision);
    changed= copy (source); changed[0]= "changed text";
    QVERIFY (ref::export_source_revision (changed) != revision);
  }
  void exportScopesNeverSubstituteInteractiveOrPendingState () {
    namespace ref= athena::node_reference;
    QVERIFY (!ref::export_reference_view ({{a}, {}}));
    auto prepared= std::make_shared<ref::prepared_references> ();
    auto resolved= std::make_shared<loc::result> ();
    resolved->state= loc::status::resolved;
    prepared->selections[{{a}, {}}]= resolved;
    {
      ref::export_reference_scope outer (prepared);
      {
        ref::export_reference_scope inner;
        QVERIFY (ref::export_reference_view ({{a}, {}})->snapshot == resolved);
        inner.require_ready ();
        QVERIFY (!ref::export_reference_view ({{b}, {}})->snapshot);
        QCOMPARE (inner.missing ().size (), std::size_t (1));
        QVERIFY_EXCEPTION_THROWN (inner.require_ready (), std::runtime_error);
      }
      QCOMPARE (outer.missing ().size (), std::size_t (1));
      QVERIFY_EXCEPTION_THROWN (outer.require_ready (), std::runtime_error);
    }
    QVERIFY (!ref::export_reference_view ({{a}, {}}));
    {
      ref::export_reference_scope empty;
      QVERIFY (!ref::export_reference_view ({{a}, {}})->snapshot);
      QVERIFY_EXCEPTION_THROWN (empty.require_ready (), std::runtime_error);
    }
    auto failed= std::make_shared<ref::prepared_references> ();
    failed->error= "Preparation failed";
    QVERIFY_EXCEPTION_THROWN (ref::export_reference_scope {failed}, std::runtime_error);
    QVERIFY (!ref::export_reference_view ({{a}, {}}));
  }
  void exportPreparationBudgetsAndCancellation () {
    namespace ref= athena::node_reference;
    QTemporaryDir dir;
    document (root (dir) / "a.ath", identified (tree (DOCUMENT, reference (b)), a));
    auto service= std::make_shared<loc::service> (root (dir));
    ref::export_preparation limited (service, {{{a}, {}}}, {}, {1, 1024*1024});
    QVERIFY (!wait (limited)->error.empty ());
    ref::export_preparation tiny (service, {{{a}, {}}}, {}, {4096, 1});
    QVERIFY (!wait (tiny)->error.empty ());
    ref::export_preparation no_refs ({}, {});
    QVERIFY (wait (no_refs)->error.empty ());
    std::atomic<bool> release {false};
    std::atomic<unsigned> calls {0};
    auto paused= std::make_shared<loc::service> (root (dir), [&] (const std::atomic<bool>& stop) {
      while (!release && !stop) std::this_thread::sleep_for (std::chrono::milliseconds (1));
      return std::vector<loc::live_source> {};
    });
    ref::export_preparation cancelled (paused, {{{a}, {}}}, [&] (ref::prepared_snapshot) { ++calls; });
    cancelled.cancel ();
    QVERIFY (cancelled.read ()->cancelled);
    QCOMPARE (calls.load (), 1U);
    cancelled.cancel ();
    QCOMPARE (calls.load (), 1U);
    release= true;
  }
  void exportPreparationRejectsMixedRevisionsOfOneSource () {
    namespace ref= athena::node_reference;
    QTemporaryDir dir;
    const auto folder= root (dir);
    auto body= [] (string text) {
      return tree (DOCUMENT, identified (tree (DOCUMENT, reference (b)), a), identified (tree (text), b));
    };
    document (folder / "doc.ath", body ("before"));
    auto service= std::make_shared<loc::service> (folder, loc::live_provider {},
      [folder, body] (const loc::item& target, const std::atomic<bool>&) {
        auto payload= loc::read_disk_content (folder, target);
        if (target.id == a) document (folder / "doc.ath", body ("Changed after first capture"));
        return payload;
      });
    ref::export_preparation job (service, {{{a}, {}}});
    auto prepared= wait (job);
    QVERIFY (prepared->error.find ("Source changed") != std::string::npos);
  }
  void completionCanReleaseTheLastLocatorOwner () {
    QTemporaryDir dir;
    document (root (dir) / "doc.ath", identified (tree (DOCUMENT, "Text"), a));
    std::atomic<bool> admitted {false};
    auto service= std::make_shared<loc::service> (root (dir), [&] (const std::atomic<bool>& stop) {
      while (!admitted && !stop) std::this_thread::sleep_for (std::chrono::milliseconds (1));
      return std::vector<loc::live_source> {};
    });
    std::mutex lock;
    std::condition_variable ready;
    bool released= false;
    auto query= service->request ({a}, {}, true, [&] (loc::snapshot) {
      service.reset ();
      { std::lock_guard<std::mutex> guard (lock); released= true; }
      ready.notify_all ();
    });
    admitted= true;
    std::unique_lock<std::mutex> guard (lock);
    QVERIFY (ready.wait_for (guard, std::chrono::seconds (10), [&] { return released; }));
    QVERIFY (query->read ()->state == loc::status::resolved);
  }
};
QTEST_APPLESS_MAIN (TestNodeLocation)
#include "node_location_test.moc"
