/******************************************************************************
* MODULE     : compound_clipboard_v3_test.cpp
* DESCRIPTION: Versioned compound clipboard and pre-cut actor validation
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
******************************************************************************/
#include <QtTest/QtTest>
#include <QDir>
#include <QTemporaryDir>
#include "Qt/QTMApplication.hpp"
#include "ATHENA/Data/compound_document_edit.hpp"
#include "ATHENA/Data/new_buffer.hpp"
#include "ATHENA/Data/new_view.hpp"
#include "ATHENA/tm_window.hpp"
#include "ATHENA/server.hpp"
#include "buffer_actor.hpp"
#include "buffer_state.hpp"
#include "editor.hpp"
#include "boot.hpp"
#include "data_cache.hpp"
#include "gui.hpp"
#include "scheme.hpp"
#include "convert.hpp"
#include "math_tree_v3.hpp"
#include "node_metadata.hpp"
#include "Data/Convert/Xml/clipboard_xml.hpp"
#include <chrono>
#include <cstdlib>
#include <future>

bool headless_mode= false;
bool is_headless () { return false; }

namespace {
namespace avd= athena::avd;
namespace doc= athena::document;
namespace node= athena::node;
using version= doc::xml_format_version;

tree native_block () {
  tree result (MATH_BLOCK, athena::math_v3::scripts ("x", "", "", "i", "2", 'r'));
  node::metadata metadata;
  metadata.id= node::new_id ();
  metadata.properties.emplace ("math:block-numbered", node::property (true));
  metadata.properties.emplace ("fixture:rich", node::property (node::rich_text {
    compound ("strong", "retained")}));
  metadata.properties.emplace ("fixture:typed", node::property (node::property::list {
    node::property (true), node::property (std::int64_t (9)), node::property (1.5)}));
  node::set (result, metadata);
  return result;
}

std::string piece (tree content, version source, string mode= "text", string language= "english") {
  return doc::write_clipboard_xml (tuple ("texmacs", content, mode, language), source);
}

bool contains_content (const tree& source, const tree& wanted) {
  if (node::content_equal (source, wanted)) return true;
  if (is_compound (source))
    for (int i=0; i<N(source); ++i)
      if (contains_content (source[i], wanted)) return true;
  return false;
}
}

class CompoundClipboardV3Test: public QObject {
  Q_OBJECT
  std::vector<url> sources;

  avd::source_range source (tree body, version format) {
    const url name ("tmfs://aux/compound-clipboard-" * as_string (int (sources.size ())));
    const tree document (DOCUMENT, compound ("style", tuple ("generic")), compound ("body", body));
    if (!initialize_buffer_tree (name, document, format))
      throw std::runtime_error ("Cannot initialize compound clipboard fixture");
    sources.push_back (name);
    const auto view= concrete_view (get_passive_view (name));
    if (!view) throw std::runtime_error ("Cannot create compound clipboard source view");
    const auto actor= concrete_buffer (name)->actor;
    return {actor->id (), view->runtime_id, actor->source_epoch (), {}, {}};
  }

  bool on_owner (const avd::source_range& range, std::function<void()> callback) {
    const auto continuation= actor_continuation_registry::instance ().store (std::move (callback));
    return buffer_actor::invoke_on (range.actor, actor_command_kind::run_native_continuation,
      range.view, ATHENA_NO_BLOB, ATHENA_NO_BLOB, nullptr,
      SCHEME_CAPABILITY_BUFFER, continuation);
  }

  std::future<std::string> replace (const std::vector<avd::source_range>& ranges,
                                    const std::string& bytes) {
    auto result= std::make_shared<std::promise<std::string>> ();
    auto future= result->get_future ();
    avd::replace_ranges (ranges, bytes, [result] (std::string error) {
      result->set_value (std::move (error));
    });
    return future;
  }

private slots:
  void cleanup () {
    for (const auto& name: sources) remove_buffer (name);
    sources.clear ();
  }

  void mergesExplicitVersionsWithoutLosingProperties () {
    const tree native= native_block ();
    const auto native_bytes= piece (native, version::v3);
    const tree single= avd::merge_clipboard_pieces ({native_bytes});
    QCOMPARE (*doc::clipboard_version (single), version::v3);
    QVERIFY (single[1] == native);
    QCOMPARE (doc::write_clipboard_xml (single), native_bytes);

    tree rich ("typed");
    node::set (rich, {node::new_id (), {{"fixture:flag", node::property (true)}}});
    const tree merged= avd::merge_clipboard_pieces ({piece ("plain", version::v1),
      piece (rich, version::v2), native_bytes});
    QCOMPARE (*doc::clipboard_version (merged), version::v3);
    QVERIFY (merged[1] == tree (DOCUMENT, "plain", rich, native));
    QVERIFY (doc::read_clipboard_xml (doc::write_clipboard_xml (merged)) == merged);

    const tree in_math= avd::merge_clipboard_pieces ({piece ("before", version::v1),
      piece (native, version::v3, "math", "std-math")});
    const tree scoped= in_math[1][1];
    QVERIFY (is_func (scoped, WITH, 3));
    QVERIFY (scoped[0] == "math-language");
    QVERIFY (scoped[1] == "std-math");
    QVERIFY (scoped[2] == native);
    QVERIFY (!is_compound (scoped[2], "math"));

    const tree language= avd::merge_clipboard_pieces ({piece ("one", version::v1),
      piece ("deux", version::v2, "text", "french")});
    QCOMPARE (*doc::clipboard_version (language), version::v2);
    QVERIFY (language[1][1] == tree (WITH, "language", "french", "deux"));
  }

  void rejectsLossyAndAmbiguousMerges () {
    const tree native= native_block ();
    const auto bytes= piece (native, version::v3);
    QVERIFY_EXCEPTION_THROWN (avd::merge_clipboard_pieces ({bytes, bytes}), doc::codec_exception);
    QVERIFY_EXCEPTION_THROWN (avd::merge_clipboard_pieces ({
      piece (tree (RSUP, "2"), version::v2), bytes}), doc::codec_exception);
    QVERIFY_EXCEPTION_THROWN (avd::merge_clipboard_pieces ({bytes, "<broken"}), doc::codec_exception);
    tree attributed (DOCUMENT, "paragraph", "second paragraph");
    node::set (attributed, {node::new_id (), {{"fixture:rich", node::property (
      node::rich_text {compound ("strong", "document metadata")})}}});
    const auto document= piece (attributed, version::v3);
    QVERIFY (avd::merge_clipboard_pieces ({document})[1] == attributed);
    const tree merged= avd::merge_clipboard_pieces ({document, piece ("next", version::v1)});
    QCOMPARE (*doc::clipboard_version (merged), version::v3);
    QVERIFY (merged[1] == tree (DOCUMENT, attributed, "next"));
    QVERIFY (node::equal_metadata (merged[1][0], attributed));
    QVERIFY (doc::read_clipboard_xml (doc::write_clipboard_xml (merged)) == merged);
    const tree move= tuple ("texmacs", native, "text", "english",
      tuple ("athena-move-v1", string (node::new_id ().c_str ())));
    const auto moving= doc::write_clipboard_xml (move, version::v3);
    QVERIFY (doc::clipboard_without_version (avd::merge_clipboard_pieces ({moving})) == move);
    QVERIFY_EXCEPTION_THROWN (avd::merge_clipboard_pieces ({moving, piece ("next", version::v1)}),
                              std::runtime_error);
  }

  void copiesActualActorStorageVersions () {
    std::vector<avd::source_range> ranges;
    for (const auto format: {version::v1, version::v2, version::v3})
      ranges.push_back (source (tree (DOCUMENT, "identical plain text"), format));
    using copied= std::pair<std::string, std::vector<std::string>>;
    auto result= std::make_shared<std::promise<copied>> ();
    auto future= result->get_future ();
    avd::copy_ranges (ranges, [result] (std::string error, avd::clipboard_copy copied) {
      result->set_value ({std::move (error), std::move (copied.pieces)});
    });
    QTRY_VERIFY_WITH_TIMEOUT (future.wait_for (std::chrono::seconds (0)) == std::future_status::ready, 10000);
    const auto copied_pieces= future.get ();
    QVERIFY2 (copied_pieces.first.empty (), copied_pieces.first.c_str ());
    QCOMPARE (copied_pieces.second.size (), std::size_t (3));
    for (std::size_t i=0; i<ranges.size (); ++i) {
      const auto format= i == 0 ? version::v1 : i == 1 ? version::v2 : version::v3;
      QCOMPARE (doc::inspect_xml_version (copied_pieces.second[i]), format);
      const tree envelope= doc::read_clipboard_xml (copied_pieces.second[i]);
      QCOMPARE (*doc::clipboard_version (envelope), format);
      QVERIFY (contains_content (envelope[1], tree ("identical plain text")));
    }
  }

  void incompatibleOrMalformedPasteDoesNotCutAnyParticipant () {
    const auto target= source (tree (DOCUMENT, "target"), version::v2);
    const auto peer= source (tree (DOCUMENT, native_block ()), version::v3);
    const tree first= get_buffer_tree (sources[0]);
    const tree second= get_buffer_tree (sources[1]);
    for (const auto& bytes: {piece (native_block (), version::v3), std::string ("<a v=\"3\"")}) {
      auto future= replace ({target, peer}, bytes);
      QTRY_VERIFY_WITH_TIMEOUT (future.wait_for (std::chrono::seconds (0)) == std::future_status::ready, 10000);
      QVERIFY (!future.get ().empty ());
      QVERIFY (get_buffer_tree (sources[0]) == first);
      QVERIFY (get_buffer_tree (sources[1]) == second);
      QCOMPARE (concrete_buffer (sources[0])->actor->source_epoch (), target.epoch);
      QCOMPARE (concrete_buffer (sources[1])->actor->source_epoch (), peer.epoch);
    }
  }

  void nativeGridCopyKeepsInheritedBlockNumbering () {
    tree cell (CELL, "x");
    node::set (cell, {node::new_id (), {{"math:cell-format", node::property (
      node::property::dictionary {{"align", node::property (std::string ("r"))}})}}});
    tree row (ROW, cell);
    athena::math_v3::set_row_tag (row, compound ("strong", "custom"));
    tree grid (MATH_GRID, row);
    node::set (grid, {node::new_id (), {{"math:grid-kind", node::property (std::string ("align"))}}});
    tree block (MATH_BLOCK, grid);
    node::set (block, {node::new_id (), {{"math:block-numbered", node::property (true)}}});
    auto range= source (tree (DOCUMENT, block), version::v3);
    // The selected object is the grid, not its enclosing MathBlock.
    range.first= {0, 0, 0}; range.last= {0, 0, 1};
    const tree snapshot= get_buffer_tree (sources[0]);
    const tree original= extract (snapshot, "body")[0];
    using copied= std::pair<std::string, std::vector<std::string>>;
    auto result= std::make_shared<std::promise<copied>> ();
    auto future= result->get_future ();
    avd::copy_ranges ({range}, [result] (std::string error, avd::clipboard_copy copied) {
      result->set_value ({std::move (error), std::move (copied.pieces)});
    });
    QTRY_VERIFY_WITH_TIMEOUT (future.wait_for (std::chrono::seconds (0)) == std::future_status::ready, 10000);
    const auto copied_pieces= future.get ();
    QVERIFY2 (copied_pieces.first.empty (), copied_pieces.first.c_str ());
    QCOMPARE (copied_pieces.second.size (), std::size_t (1));
    const tree envelope= doc::read_clipboard_xml (copied_pieces.second[0]);
    QCOMPARE (*doc::clipboard_version (envelope), version::v3);
    QVERIFY (envelope[2] == "text");
    QVERIFY (envelope[1] == original);
    QCOMPARE (athena::math_v3::get_row_numbering (envelope[1][0][0]),
              athena::math_v3::row_numbering::inherit);
    QVERIFY (std::get<bool> (node::get (envelope[1])->properties.at ("math:block-numbered").data));
    QVERIFY (get_buffer_tree (sources[0]) == snapshot);
    QCOMPARE (concrete_buffer (sources[0])->actor->source_epoch (), range.epoch);
    auto replacement= replace ({range}, piece ("replacement", version::v1));
    QTRY_VERIFY_WITH_TIMEOUT (replacement.wait_for (std::chrono::seconds (0)) == std::future_status::ready, 10000);
    const auto error= replacement.get ();
    QVERIFY2 (error.empty (), error.c_str ());
    const tree replaced= extract (get_buffer_tree (sources[0]), "body");
    QVERIFY (contains_content (replaced, tree ("replacement")));
    QVERIFY (!contains_content (replaced, original));
  }

  void modeAndReadOnlyFailurePrecedeEveryCut () {
    auto target= source (tree (DOCUMENT, compound ("math", "target")), version::v3);
    target.first= {0, 0, 1}; target.last= {0, 0, 5};
    const auto peer= source (tree (DOCUMENT, "peer"), version::v3);
    const tree first= get_buffer_tree (sources[0]);
    const tree second= get_buffer_tree (sources[1]);
    auto future= replace ({target, peer}, piece ("text", version::v1));
    QTRY_VERIFY_WITH_TIMEOUT (future.wait_for (std::chrono::seconds (0)) == std::future_status::ready, 10000);
    QVERIFY (future.get ().find ("text into a formula") != std::string::npos);
    QVERIFY (get_buffer_tree (sources[0]) == first);
    QVERIFY (get_buffer_tree (sources[1]) == second);
    QCOMPARE (concrete_buffer (sources[0])->actor->source_epoch (), target.epoch);
    QCOMPARE (concrete_buffer (sources[1])->actor->source_epoch (), peer.epoch);

    QVERIFY (on_owner (peer, [] { current_scheme_execution_context ()->actor->current_state ()->read_only= true; }));
    future= replace ({target, peer}, piece ("x", version::v1, "math", "std-math"));
    QTRY_VERIFY_WITH_TIMEOUT (future.wait_for (std::chrono::seconds (0)) == std::future_status::ready, 10000);
    QVERIFY (future.get ().find ("read-only") != std::string::npos);
    QVERIFY (get_buffer_tree (sources[0]) == first);
    QVERIFY (get_buffer_tree (sources[1]) == second);
    QCOMPARE (concrete_buffer (sources[0])->actor->source_epoch (), target.epoch);
    QCOMPARE (concrete_buffer (sources[1])->actor->source_epoch (), peer.epoch);
  }

  void compatibleReplacementUsesFirstActorVersion () {
    const auto target= source (tree (DOCUMENT, "target"), version::v3);
    const auto peer= source (tree (DOCUMENT, "peer"), version::v2);
    const tree native= native_block ();
    auto future= replace ({target, peer}, piece (native, version::v3));
    QTRY_VERIFY_WITH_TIMEOUT (future.wait_for (std::chrono::seconds (0)) == std::future_status::ready, 10000);
    const auto error= future.get ();
    QVERIFY2 (error.empty (), error.c_str ());
    QVERIFY (contains_content (extract (get_buffer_tree (sources[0]), "body"), native));
    QVERIFY (!contains_content (extract (get_buffer_tree (sources[1]), "body"), tree ("peer")));
    for (int i=0; i<2; ++i) {
      version actual= version::v1;
      QVERIFY (on_owner (i == 0 ? target : peer, [&] {
        actual= current_scheme_execution_context ()->actor->current_state ()->storage_version;
      }));
      QCOMPARE (actual, i == 0 ? version::v3 : version::v2);
    }
  }

  void sharedPlainClipboardCanReplaceLegacyAndNativeSources () {
    const auto target= source (tree (DOCUMENT, "target"), version::v3);
    const auto peer= source (tree (DOCUMENT, "peer"), version::v1);
    // The recorded origin is v1; accepting shared plain content is not migration.
    auto future= replace ({target, peer}, piece ("replacement", version::v1));
    QTRY_VERIFY_WITH_TIMEOUT (future.wait_for (std::chrono::seconds (0)) == std::future_status::ready, 10000);
    const auto error= future.get ();
    QVERIFY2 (error.empty (), error.c_str ());
    QVERIFY (contains_content (extract (get_buffer_tree (sources[0]), "body"), tree ("replacement")));
    QVERIFY (!contains_content (extract (get_buffer_tree (sources[1]), "body"), tree ("peer")));
  }
};

static void run_tests (int argc, char** argv) {
  init_system_state ();
  gui_open (argc, argv);
  int result;
  {
    server runtime;
    CompoundClipboardV3Test test;
    result= QTest::qExec (&test, argc, argv);
  }
  gui_close ();
  release_boot_lock ();
  std::exit (result);
}

int main (int argc, char** argv) {
  qputenv ("QT_QPA_PLATFORM", "offscreen");
  qputenv ("GUILE_AUTO_COMPILE", "0");
  QTMApplication app (argc, argv);
  QTemporaryDir profile;
  if (!profile.isValid () || !QDir (profile.path ()).mkpath ("fonts")) return 1;
  qputenv ("HOME", profile.path ().toUtf8 ());
  qputenv ("ATHENA_HOME_PATH", profile.path ().toUtf8 ());
  cache_initialize ();
  reset_document_tree (current_document_tree ());
  init_athena ();
  start_scheme (argc, argv, run_tests);
  return 1;
}

#include "compound_clipboard_v3_test.moc"
