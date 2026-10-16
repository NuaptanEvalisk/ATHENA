/******************************************************************************
* MODULE     : native_v3_search_query_widget_test.cpp
* DESCRIPTION: Global search query version and content across Vault lifecycles
* COPYRIGHT  : (C) 2026 ATHENA contributors
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
******************************************************************************/
#include <QtTest/QtTest>
#include <QDir>
#include <QScopeGuard>
#include <QTemporaryDir>

#include "Qt/QTMApplication.hpp"
#include "Qt/QTMGlobalSearch.hpp"
#include "Qt/QTMVaultSearchWorker.hpp"
#include "ATHENA/Data/new_buffer.hpp"
#include "ATHENA/Data/new_view.hpp"
#include "ATHENA/Data/new_window.hpp"
#include "ATHENA/Data/vault.hpp"
#include "ATHENA/Data/vaultfile_json.hpp"
#include "ATHENA/tm_window.hpp"
#include "boot.hpp"
#include "Data/Convert/convert.hpp"
#include "data_cache.hpp"
#include "math_tree_v3.hpp"
#include "node_metadata.hpp"
#include "scheme.hpp"
#include "sys_utils.hpp"

#include <cstdlib>
#include <filesystem>

bool headless_mode= false;
bool is_headless () { return false; }

namespace {
using namespace athena::document;

tree source_document (tree content) {
  return tree (DOCUMENT, compound ("style", tuple ("generic")),
               compound ("body", tree (DOCUMENT, content)));
}

}

class NativeV3SearchQueryWidgetTest: public QObject {
  Q_OBJECT
  url master= url ("tmfs://aux/search-query-test-master");
  url window= url_none ();

private slots:
  void init () {
    QVERIFY (!vault_active ());
    QVERIFY (initialize_buffer_tree (master, source_document (""), xml_format_version::v2));
    window= new_window (false);
    window_set_view (window, get_passive_view (master), true);
    QVERIFY (has_current_view ());
  }

  void cleanup () {
    if (vault_active ()) vault_close ();
    if (!is_none (window)) delete_window (window);
    window= url_none ();
    remove_buffer (master);
  }

  void globalSearchPreservesExistingQueryAcrossVaultSwitch () {
    QTemporaryDir first, second;
    QVERIFY (first.isValid ());
    QVERIFY (second.isValid ());
    for (const auto& root: {first.path (), second.path ()}) {
      AthenaVaultfileInfo info;
      info.node_model_version= 1;
      std::string error;
      QVERIFY2 (athena_vaultfile_write (std::filesystem::path (root.toStdString ()),
                                      info, error), error.c_str ());
    }
    QCOMPARE (vault_load (url_system (from_qstring (first.path ())),
      "First query fixture", "map.sqlite", "namespaces.sqlite"), string (""));
    const url query ("tmfs://aux/global-search");
    const tree scripts= athena::math_v3::scripts ("x", "", "", "i", "", 'r');
    QVERIFY (initialize_buffer_tree (query, source_document (compound ("math", scripts)),
                                    xml_format_version::v3));
    auto cleanup_query= qScopeGuard ([&] {
      if (!is_nil (concrete_buffer (query))) remove_buffer (query);
    });
    const auto original= vault_search_buffer_snapshot (query);
    const auto bytes= encode_native_xml_bytes (original.document, original.version, xml_kind::fragment);
    {
      QTMGlobalSearch search;
      const auto opened= vault_search_buffer_snapshot (query);
      QCOMPARE (opened.version, xml_format_version::v3);
      QCOMPARE (encode_native_xml_bytes (opened.document, opened.version, xml_kind::fragment), bytes);
      vault_close ();
      QCOMPARE (vault_load (url_system (from_qstring (second.path ())),
        "Second query fixture", "map.sqlite", "namespaces.sqlite"), string (""));
      search.refreshNamespaces ();
      search.refreshSearchOptions ();
      const auto retained= vault_search_buffer_snapshot (query);
      QCOMPARE (retained.version, xml_format_version::v3);
      QCOMPARE (encode_native_xml_bytes (retained.document, retained.version, xml_kind::fragment), bytes);
    }
    QVERIFY (is_nil (concrete_buffer (query)));
    {
      QTMGlobalSearch fresh;
      const auto created= vault_search_buffer_snapshot (query);
      QCOMPARE (created.version, xml_format_version::v2);
      // Explicit v2 initialization assigns IDs even to an empty paragraph.
      const tree live_body= extract (get_buffer_tree (query), "body");
      QVERIFY (is_func (live_body, DOCUMENT, 1));
      QVERIFY (is_atomic (live_body[0]));
      QVERIFY (live_body[0]->label == "");
      QVERIFY (athena::node::valid_id (athena::node::id (live_body)));
      QVERIFY (athena::node::valid_id (athena::node::id (live_body[0])));
      QVERIFY (athena::node::id (live_body) != athena::node::id (live_body[0]));
      QVERIFY (created.document == live_body);
    }
    QVERIFY (is_nil (concrete_buffer (query)));
  }
};

static void run_tests (int argc, char** argv) {
  init_system_state ();
  gui_open (argc, argv);
  int result;
  {
    server runtime;
    NativeV3SearchQueryWidgetTest test;
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
  qputenv ("ATHENA_HOME_PATH", profile.path ().toUtf8 ());
  cache_initialize ();
  reset_document_tree (current_document_tree ());
  init_athena ();
  start_scheme (argc, argv, run_tests);
  return 1;
}

#include "native_v3_search_query_widget_test.moc"
