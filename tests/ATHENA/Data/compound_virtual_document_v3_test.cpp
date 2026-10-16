/******************************************************************************
* MODULE     : compound_virtual_document_v3_test.cpp
* DESCRIPTION: AVD source-version, checkpoint and native counter integration
* COPYRIGHT  : (C) 2026 ATHENA contributors
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
******************************************************************************/
#include <QApplication>
#include <QCryptographicHash>
#include <QTemporaryDir>
#include <QtTest/QtTest>
#include <cstdlib>
#include "ATHENA/server.hpp"
#include "ATHENA/Data/compound_virtual_document.hpp"
#include "ATHENA/Data/interop_document_codec.hpp"
#include "Editor/edit_main.hpp"
#include "Typeset/Env/compound_counters.hpp"
#include "typesetter.hpp"
#include "boot.hpp"
#include "buffer_state.hpp"
#include "data_cache.hpp"
#include "gui.hpp"
#include "math_tree_v3.hpp"
#include "node_metadata.hpp"
#include "scheme.hpp"
#include "Xml/document_file_codec.hpp"
#include "Subsystems/Pdf/pdf_merge.hpp"
#include "Subsystems/Pdf/PDFWriter/InputFile.h"
#include "Subsystems/Pdf/PDFWriter/PDFParser.h"

bool headless_mode= true;
bool is_headless () { return true; }
static server_rep* test_server= nullptr;

namespace {
namespace avd= athena::avd;
namespace doc= athena::document;
namespace math= athena::math_v3;
namespace node= athena::node;

namespace avd_fixture {
tree grid (const char* kind, std::initializer_list<tree> rows) {
  tree result (MATH_GRID);
  for (const auto& row: rows) result << row;
  node::metadata meta;
  meta.properties.emplace ("math:grid-kind", node::property (std::string (kind)));
  node::set (result, meta);
  return result;
}
} // namespace avd_fixture

tree row (tree body= tree ("x")) { return tree (ROW, tree (CELL, body)); }

tree block (tree body, bool numbered) {
  tree result (MATH_BLOCK, body);
  node::metadata meta;
  meta.properties.emplace ("math:block-numbered", node::property (numbered));
  node::set (result, meta);
  return result;
}

tree counter (const tree& state) {
  for (int i= 0; i < N(state); ++i)
    if (state[i][0] == "test-equation-counter") return state[i][1];
  throw std::runtime_error ("Missing fixture equation counter");
}

class CounterEditor: public edit_main_rep {
public:
  CounterEditor (server_rep* server, buffer_document_state* buffer):
    editor_rep (server, buffer), edit_main_rep (server, buffer) {
    env->write_default_env ();
    env->write (FONT, "TeX Gyre Pagella");
    env->write (MODE, "text");
    env->write ("athena-radioactive-links-suppressed", "true");
    env->write ("test-equation-counter", "0");
    env->write ("counter-equation", tree (MACRO, "test-equation-counter"));
    env->write ("next-equation", tree (MACRO, tree (ASSIGN,
      "test-equation-counter", tree (PLUS, tree (VALUE, "test-equation-counter"), "1"))));
    env->write ("the-equation", tree (MACRO, tree (VALUE, "test-equation-counter")));
    env->write (THE_LABEL, "outside");
    env->write ("the-tags", tree (TUPLE));
    for (const char* key: {"font", "mode", "athena-radioactive-links-suppressed",
                          "test-equation-counter", "counter-equation",
                          "next-equation", "the-equation", "the-label", "the-tags"})
      init (key)= env->read (key);
    env->read_only= true;
    env->update ();
    env->read_env (pre);
  }
  void* derived_this () override { return this; }
  tree live_counter () { return env->read ("test-equation-counter"); }
  tree layout_counter (const tree& source) {
    env->patch_env (pre);
    env->update ();
    (void) typeset_as_concat (env, source, path (0));
    return live_counter ();
  }
  tree evaluate_in_test_environment (const tree& source) {
    return avd::evaluate_counter_state (env, source, tree (COLLECTION),
                                       doc::xml_format_version::v3);
  }
  bool native_execution_enabled () { return env->compound_counter_execution; }
  tree mode () { return env->read (MODE); }
  tree label () { return env->read (THE_LABEL); }
  tree binding (string key) {
    const tree value= env->local_ref[key];
    return is_func (value, TUPLE) && N(value) > 0 ? value[0] : tree (UNINIT);
  }
};
}

class CompoundVirtualDocumentV3Test: public QObject {
  Q_OBJECT
  buffer_document_state* buffer= nullptr;
  CounterEditor* editor= nullptr;

  void load_source (const tree& source,
                    doc::xml_format_version version= doc::xml_format_version::v3) {
    const auto decoded= doc::decode_native_xml_bytes (
      doc::encode_native_xml_bytes (source, version));
    buffer->storage_version= decoded.version;
    set_document (buffer->document, buffer->root_path, decoded.document);
  }

private slots:
  void init () {
    buffer= tm_new<buffer_document_state> (
      nullptr, "tmfs://avd-v3-test", "", "UTF-8", false, 0);
    swap_current_document_tree (&buffer->document);
    buffer->data->init ("no-zoom")= "true";
    buffer->data->init (ZOOM_FACTOR)= "1";
    set_document (buffer->document, buffer->root_path, tree (DOCUMENT, ""));
    editor= tm_new<CounterEditor> (test_server, buffer);
  }
  void cleanup () {
    tm_delete<editor_rep> (editor);
    swap_current_document_tree (nullptr);
    tm_delete (buffer);
  }

  void explicitVersionsAndLegacyBytes () {
    const tree plain (COLLECTION, tree (ASSOCIATE, "test-equation-counter", "7"));
    for (const auto version: {doc::xml_format_version::v1, doc::xml_format_version::v2,
                              doc::xml_format_version::v3}) {
      const auto bytes= avd::encode_value_fragment (plain, version);
      QCOMPARE (doc::inspect_xml_version (bytes.toStdString ()), version);
      QVERIFY (avd::decode_counter_fragment (bytes) == plain);
      load_source (tree (DOCUMENT, compound ("next-equation")), version);
      QCOMPARE (counter (editor->evaluate_compound_counters (tree (COLLECTION))), tree ("1"));
      QCOMPARE (editor->live_counter (), tree ("0"));
    }
    QCOMPARE (avd::encode_value_fragment (plain, doc::xml_format_version::v2).toStdString (),
      doc::write_xml_v2 (node::content_projection (plain), doc::xml_kind::fragment));
    QVERIFY_EXCEPTION_THROWN (avd::decode_counter_fragment (
      QByteArray::fromStdString (doc::write_xml_v3 (tree (DOCUMENT, "")))), std::exception);
    QVERIFY_EXCEPTION_THROWN (avd::decode_counter_fragment (
      avd::encode_value_fragment (tree (DOCUMENT, ""), doc::xml_format_version::v3)), std::exception);
  }

  void derivedNativeValuesRetainPropertiesNotUuids () {
    tree source= block (avd_fixture::grid ("align", {row ()}), true);
    math::set_row_tag (source[0][0], math::scripts ("x", "", "", "i", ""));
    auto meta= *node::get (source);
    meta.id= node::new_id ();
    node::set (source, meta);
    const auto before= doc::write_xml_v3 (source, doc::xml_kind::fragment);
    const auto bytes= avd::encode_value_fragment (source, doc::xml_format_version::v3);
    const auto decoded= doc::decode_native_xml_bytes (bytes.toStdString (), doc::xml_kind::fragment);
    QVERIFY (decoded.version == doc::xml_format_version::v3);
    QVERIFY (decoded.document == node::content_projection (source));
    QVERIFY (node::id (decoded.document).empty ());
    QVERIFY (math::get_row_tag (decoded.document[0][0]).has_value ());
    QCOMPARE (doc::write_xml_v3 (source, doc::xml_kind::fragment), before);
  }

  void environmentDigestAcceptsMixedStyleAndNativeValues () {
    // std-math's sum syntax contains BIG; evaluated environments are not v3
    // source documents and must not be fed to the strict source writer.
    tree environment (COLLECTION,
      tree (ASSOCIATE, "legacy-style", tree (MACRO, tree (BIG, "sum"))),
      tree (ASSOCIATE, "native-value", block ("x", true)));
    QVERIFY_EXCEPTION_THROWN (doc::write_xml_v3 (environment, doc::xml_kind::fragment), std::exception);
    const auto digest= avd::environment_revision (environment, doc::xml_format_version::v3);
    QCOMPARE (digest.size (), qsizetype (32));
    node::metadata meta;
    meta.id= node::new_id ();
    node::set (environment, meta);
    QCOMPARE (avd::environment_revision (environment, doc::xml_format_version::v3), digest);
    meta.properties.emplace ("fixture:rich", node::property (node::rich_text {block ("y", false)}));
    node::set (environment, meta);
    QVERIFY (avd::environment_revision (environment, doc::xml_format_version::v3) != digest);
    const auto with_rich= avd::environment_revision (environment, doc::xml_format_version::v3);
    meta.properties["fixture:rich"]= node::property (node::rich_text {block ("y", true)});
    node::set (environment, meta);
    QVERIFY (avd::environment_revision (environment, doc::xml_format_version::v3) != with_rich);
    const tree legacy (COLLECTION, tree (ASSOCIATE, "legacy-style", tree (MACRO, tree (BIG, "sum"))));
    QCOMPARE (avd::environment_revision (legacy, doc::xml_format_version::v2),
      QCryptographicHash::hash (QByteArray::fromStdString (doc::write_xml_v2 (
        node::content_projection (legacy), doc::xml_kind::fragment)), QCryptographicHash::Sha256));
    // Also exercise the actual style environment exported by the editor.
    QCOMPARE (avd::environment_revision (editor->compound_counter_environment (),
      doc::xml_format_version::v3).size (), qsizetype (32));
  }

  void environmentDigestUsesExistingTypedDocumentModel () {
    tree environment (COLLECTION,
      tree (ASSOCIATE, "legacy-style", tree (MACRO, tree (BIG, "sum"))),
      tree (ASSOCIATE, "native-value", block ("x", true)),
      tree (ASSOCIATE, "binary", tree (RAW_DATA, tree (string ("\0\xff", 2)))));
    node::metadata meta;
    meta.id= node::new_id ();
    meta.properties.emplace ("fixture:values", node::property (node::property::list {
      node::property (std::string ("1")), node::property (std::int64_t (1)),
      node::property (true), node::property (1.25),
      node::property (node::reference {meta.id}),
      node::property (node::property::dictionary {
        {"rich", node::property (node::rich_text {block ("y", false)})}})}));
    node::set (environment, meta);
    // Evaluated values may reuse source objects. Projection must remove every
    // identity before the shared model codec checks global UUID uniqueness.
    node::set (environment[0], meta);
    const auto projected= node::content_projection (environment);
    const auto model= athena::interop::document_node_to_value_v3 (projected);
    QVERIFY (athena::interop::document_node_from_value_v3 (model) == projected);
    const auto expected= QCryptographicHash::hash (
      QByteArrayLiteral ("ATHENA AVD environment document-model 3\n") +
      QByteArray::fromStdString (model.dump ()), QCryptographicHash::Sha256);
    QCOMPARE (avd::environment_revision (environment, doc::xml_format_version::v3), expected);
    QCOMPARE (node::id (environment), meta.id);
    QCOMPARE (node::id (environment[0]), meta.id);
  }

  void directRowPoliciesMatchLayout () {
    for (const char* kind: {"eqnarray", "align", "stack"}) {
      for (bool numbered: {false, true}) {
        tree source= avd_fixture::grid (kind, {row (), row (), row (), row (), row (), row ()});
        math::set_row_numbering (source[1], math::row_numbering::numbered);
        math::set_row_numbering (source[2], math::row_numbering::unnumbered);
        math::set_row_tag (source[3], "");
        math::set_row_numbering (source[3], math::row_numbering::numbered);
        math::set_row_tag (source[4], math::scripts ("x", "", "", "i", ""));
        math::set_row_numbering (source[4], math::row_numbering::unnumbered);
        source= block (source, numbered);
        load_source (tree (DOCUMENT, source));
        const auto result= editor->evaluate_compound_counters (tree (COLLECTION));
        QCOMPARE (counter (result), tree (numbered ? "3" : "1"));
        QCOMPARE (editor->layout_counter (source), counter (result));
      }
    }
  }

  void wholeBlocksAndNestedGridsMatchLayout () {
    for (const char* kind: {"matrix", "cases", "generic"}) {
      tree source= block (avd_fixture::grid (kind, {row (), row ()}), true);
      load_source (tree (DOCUMENT, source));
      QCOMPARE (counter (editor->evaluate_compound_counters (tree (COLLECTION))), tree ("1"));
      QCOMPARE (editor->layout_counter (source), tree ("1"));
    }
    tree nested= avd_fixture::grid ("align", {row (), row ()});
    tree source= block (avd_fixture::grid ("stack", {row (nested), row ()}), true);
    load_source (tree (DOCUMENT, source));
    QCOMPARE (counter (editor->evaluate_compound_counters (tree (COLLECTION))), tree ("2"));
    QCOMPARE (editor->layout_counter (source), tree ("2"));
    source= block (tree (CONCAT, nested), true);
    load_source (tree (DOCUMENT, source));
    QCOMPARE (counter (editor->evaluate_compound_counters (tree (COLLECTION))), tree ("1"));
    QCOMPARE (editor->layout_counter (source), tree ("1"));
    source= avd_fixture::grid ("matrix", {row (block (nested, true))});
    load_source (tree (DOCUMENT, source));
    QCOMPARE (counter (editor->evaluate_compound_counters (tree (COLLECTION))), tree ("2"));
    QCOMPARE (editor->layout_counter (source), tree ("2"));
  }

  void ordinaryTableRowsHaveNoEquationPolicy () {
    tree source (TABLE, row (), row ());
    math::set_row_numbering (source[0], math::row_numbering::numbered);
    math::set_row_tag (source[1], "T");
    load_source (tree (DOCUMENT, source));
    QCOMPARE (counter (editor->evaluate_compound_counters (tree (COLLECTION))), tree ("0"));
  }

  void macrosAndConditionalsExecuteNativeBlocksOnce () {
    tree equation= block (avd_fixture::grid ("align", {row (), row ()}), true);
    tree source (DOCUMENT,
      tree (ASSIGN, "fixture-equation", tree (MACRO, equation)),
      tree (IF, "false", compound ("fixture-equation"), tree ("")),
      tree (WITH, "fixture-enabled", "true", tree (IF,
        tree (VALUE, "fixture-enabled"), compound ("fixture-equation"), tree (""))));
    load_source (source);
    const auto before= doc::write_xml_v3 (subtree (current_document_tree (), buffer->root_path));
    for (int i= 0; i < 2; ++i)
      QCOMPARE (counter (editor->evaluate_compound_counters (tree (COLLECTION))), tree ("2"));
    QCOMPARE (editor->live_counter (), tree ("0"));
    QCOMPARE (doc::write_xml_v3 (subtree (current_document_tree (), buffer->root_path)), before);
  }

  void checkpointsPersistAndContinueAcrossMembers () {
    load_source (tree (DOCUMENT, block (avd_fixture::grid ("align", {row (), row ()}), true)));
    const auto first= editor->evaluate_compound_counters (tree (COLLECTION));
    avd::counter_cache cache ({{"a.ath", "a.ath", "content-a", "env-a"},
                              {"b.ath", "b.ath", "content-b", "env-b"}}, "native-test-contract");
    const auto bytes= avd::encode_value_fragment (first, buffer->storage_version);
    QVERIFY (cache.publish (0, cache.prefix_after (0), bytes));
    QTemporaryDir directory;
    QVERIFY (directory.isValid ());
    avd::descriptor descriptor;
    descriptor.namespace_uuid= "11111111-1111-4111-8111-111111111111";
    descriptor.vault_directory= ".";
    descriptor.checkpoints= cache.persistent_checkpoints ();
    const auto filename= directory.filePath ("selection.avd");
    const auto revision= avd::save_descriptor (filename, descriptor, {});
    const auto saved= avd::read_descriptor (filename);
    QCOMPARE (saved.revision, revision);
    QCOMPARE (saved.value.checkpoints.size (), std::size_t (1));
    QCOMPARE (saved.value.checkpoints[0].counters_xml, bytes);
    avd::counter_cache stale ({{"a.ath", "a.ath", "content-a", "env-a"}},
                             "old-test-contract", saved.value.checkpoints);
    QVERIFY (!stale.before (1));
    load_source (tree (DOCUMENT, block ("y", true)));
    const auto incoming= avd::decode_counter_fragment (saved.value.checkpoints[0].counters_xml);
    const auto second= editor->evaluate_compound_counters (incoming);
    QCOMPARE (counter (second), tree ("3"));
    const auto outgoing= avd::encode_value_fragment (second, buffer->storage_version);
    QVERIFY (cache.publish (1, cache.prefix_after (1), outgoing));
    QVERIFY (doc::inspect_xml_version (outgoing.toStdString ()) == doc::xml_format_version::v3);
    QCOMPARE (editor->live_counter (), tree ("0"));
  }

  void richTagsBindWithoutAdvancing () {
    tree source= avd_fixture::grid ("stack", {row (tree (LABEL, "custom")), row (tree (LABEL, "automatic"))});
    const tree tag= math::scripts ("x", "", "", "i", "");
    math::set_row_tag (source[0], tag);
    auto meta= node::metadata ();
    meta.id= node::new_id ();
    node::set (source[1], meta);
    const auto result= editor->evaluate_in_test_environment (block (source, true));
    QCOMPARE (counter (result), tree ("1"));
    QVERIFY (editor->binding ("custom") == tag);
    QCOMPARE (editor->binding ("automatic"), tree ("1"));
    const auto key= "athena-node-" + meta.id;
    QCOMPARE (editor->binding (string (key.data (), int (key.size ()))), tree ("1"));
    QCOMPARE (editor->mode (), tree ("text"));
    QCOMPARE (editor->label (), tree ("outside"));
    QVERIFY (!editor->native_execution_enabled ());
  }

  void compoundPdfMemberExportAndMerge () {
    QTemporaryDir directory;
    QVERIFY (directory.isValid ());
    std::vector<athena::pdf::merge_member> members;
    QByteArray incoming;
    int next_page= -1;
    for (int i= 0; i < 2; ++i) {
      tree source= avd_fixture::grid ("align", {row (tree (LABEL, i == 0 ? "first" : "last")),
                                   row (tree (LABEL, i == 0 ? "custom" : "empty"))});
      math::set_row_tag (source[1], i == 0 ? tree ("Case A") : tree (""));
      load_source (tree (DOCUMENT, block (source, true)));
      const auto before= doc::write_xml_v3 (subtree (current_document_tree (), buffer->root_path));
      const tree state= incoming.isEmpty () ? tree (COLLECTION) : avd::decode_counter_fragment (incoming);
      editor->set_compound_counters (state);
      // Same counter handoff and native member-export entry points as the AVD
      // PDF coordinator, without a dialog, active Vault or asynchronous UI.
      incoming= avd::encode_value_fragment (editor->evaluate_compound_counters (state),
                                            buffer->storage_version);
      athena::pdf::merge_member member;
      member.filename= directory.filePath (QString::number (i) + ".pdf").toStdString ();
      next_page= editor->print_doc_numbered (
        url_system (string (member.filename.data (), int (member.filename.size ()))), next_page,
        [&] (string label, int page, double x, double y) {
          if (page < 0) throw std::runtime_error ("Invalid exported page");
          member.destinations.push_back ({{label.data (), std::size_t (N(label))},
            static_cast<unsigned long> (page), x, y});
        }, true);
      QCOMPARE (editor->live_counter (), tree (i == 0 ? "1" : "2"));
      QCOMPARE (counter (avd::decode_counter_fragment (incoming)), editor->live_counter ());
      QCOMPARE (editor->binding (i == 0 ? "first" : "last"), tree (i == 0 ? "1" : "2"));
      QCOMPARE (editor->binding (i == 0 ? "custom" : "empty"), tree (i == 0 ? "Case A" : ""));
      QVERIFY (!member.destinations.empty ());
      QCOMPARE (doc::write_xml_v3 (subtree (current_document_tree (), buffer->root_path)), before);
      members.push_back (std::move (member));
    }
    const auto output= directory.filePath ("compound.pdf").toStdString ();
    athena::pdf::merge_documents (members, output, [] (const std::string& link) { return link; });
    InputFile file;
    QVERIFY (file.OpenFile (output) == PDFHummus::eSuccess);
    PDFParser parser;
    QVERIFY (parser.StartPDFParsing (file.GetInputStream ()) == PDFHummus::eSuccess);
    QCOMPARE (parser.GetPagesCount (), 2UL);
  }

  void invalidContextsRestoreExecutionMode () {
    tree invalid= avd_fixture::grid ("align", {row ()});
    math::set_row_numbering (invalid[0], math::row_numbering::numbered);
    for (const tree& source: {invalid, block (avd_fixture::grid ("stack", {row (invalid)}), false)}) {
      QVERIFY_EXCEPTION_THROWN (editor->evaluate_in_test_environment (source), std::invalid_argument);
      QVERIFY (!editor->native_execution_enabled ());
      QCOMPARE (editor->mode (), tree ("text"));
      QCOMPARE (editor->label (), tree ("outside"));
      QCOMPARE (editor->live_counter (), tree ("0"));
    }
    tree bad_tag= avd_fixture::grid ("stack", {row ()});
    math::set_row_tag (bad_tag[0], block ("x", true));
    QVERIFY_EXCEPTION_THROWN (editor->evaluate_in_test_environment (block (bad_tag, true)), std::invalid_argument);
    QVERIFY (!editor->native_execution_enabled ());
    for (const char* kind: {"matrix", "cases", "generic"}) {
      tree source= avd_fixture::grid (kind, {row ()});
      math::set_row_tag (source[0], "");
      QVERIFY_EXCEPTION_THROWN (editor->evaluate_in_test_environment (block (source, true)), std::invalid_argument);
      QCOMPARE (editor->live_counter (), tree ("0"));
    }
  }
};

static void run_tests (int argc, char** argv) {
  init_system_state ();
  gui_open (argc, argv);
  int result;
  {
    server sv;
    test_server= sv->get_server ();
    CompoundVirtualDocumentV3Test test;
    result= QTest::qExec (&test, argc, argv);
    test_server= nullptr;
  }
  std::_Exit (result);
}

int main (int argc, char** argv) {
  qputenv ("QT_QPA_PLATFORM", "offscreen");
  QApplication app (argc, argv);
  QTemporaryDir profile;
  if (!profile.isValid ()) return 1;
  qputenv ("ATHENA_HOME_PATH", profile.path ().toUtf8 ());
  cache_initialize ();
  reset_document_tree (current_document_tree ());
  init_athena ();
  start_scheme (argc, argv, run_tests);
  return 1;
}

#include "compound_virtual_document_v3_test.moc"
