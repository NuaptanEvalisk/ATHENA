/******************************************************************************
* MODULE     : native_v3_input_widget_test.cpp
* DESCRIPTION: Explicit input-widget versions reach native keyboard editing
* COPYRIGHT  : (C) 2026 ATHENA contributors
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
******************************************************************************/
#include <QDir>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QtTest/QtTest>
#include <cstdlib>

#include "ATHENA/server.hpp"
#include "Qt/QTMApplication.hpp"
#include "ATHENA/tm_window.hpp"
#include "ATHENA/Data/new_view.hpp"
#include "ATHENA/Data/new_window.hpp"
#include "ATHENA/Data/source_document_creation.hpp"
#include "Editor/edit_main.hpp"
#include "Graphics/Gui/gui.hpp"
#include "Data/Convert/convert.hpp"
#include "buffer_actor.hpp"
#include "buffer_state.hpp"
#include "boot.hpp"
#include "data_cache.hpp"
#include "node_metadata.hpp"
#include "message.hpp"
#include "scheme.hpp"
#include "scheme_execution_context.hpp"
#include "sys_utils.hpp"

bool headless_mode= false;
bool is_headless () { return false; }

namespace {
using namespace athena::document;

tree document_with_math (const string& text) {
  return tree (DOCUMENT, compound ("style", tuple ("generic")),
    compound ("body", tree (DOCUMENT, compound ("math", text))));
}

bool contains_label (const tree& source, tree_label label) {
  if (is_atomic (source)) return false;
  if (L(source) == label) return true;
  for (int i=0; i<N(source); ++i)
    if (contains_label (source[i], label)) return true;
  return false;
}
} // namespace

class TestNativeV3InputWidget: public QObject {
  Q_OBJECT
private slots:
  void cleanup () {
    QVERIFY (is_nil (concrete_buffer (url ("tmfs://aux/input-widget-query"))));
    QVERIFY (is_nil (concrete_buffer (url ("tmfs://aux/input-widget-master"))));
  }

  void keyboardUsesCreationVersion_data () {
    QTest::addColumn<int> ("model");
    QTest::addColumn<bool> ("existing_v3");
    QTest::newRow ("legacy-three-argument-default") << -1 << false;
    QTest::newRow ("v2-target") << 1 << false;
    QTest::newRow ("v3-target") << 2 << false;
    QTest::newRow ("existing-v3-after-target-switch") << 1 << true;
  }

  void keyboardUsesCreationVersion () {
    QFETCH (int, model);
    QFETCH (bool, existing_v3);
    const string previous_brackets= get_preference ("automatic brackets", "mathematics");
    set_preference ("automatic brackets", "on");
    auto preference_cleanup= qScopeGuard ([&] {
      set_preference ("automatic brackets", previous_brackets);
    });
    const url master ("tmfs://aux/input-widget-master");
    const url query ("tmfs://aux/input-widget-query");
    create_buffer (master, document_with_math ("master"));
    const url window= new_window (false);
    window_set_view (window, get_passive_view (master), true);
    auto master_cleanup= qScopeGuard ([&] {
      delete_window (window);
      remove_buffer (master);
    });

    const auto requested_version= model < 0 ? xml_format_version::v1 :
                                            new_source_version_for_model (model);
    if (existing_v3)
      QVERIFY (initialize_buffer_tree (query, document_with_math ("retained"),
                                       xml_format_version::v3));
    const tree query_body (DOCUMENT, compound ("math", ""));
    const tree query_style= compound ("style", tuple ("generic"));
    widget input= model < 0 ? texmacs_input_widget (query_body, query_style, query) :
      texmacs_input_widget (query_body, query_style, query, requested_version);
    auto input_cleanup= qScopeGuard ([&] { send_destroy (input); });
    tm_buffer buffer= concrete_buffer (query);
    QVERIFY (!is_nil (buffer));
    const auto views= buffer_to_views (query);
    QCOMPARE (N(views), 1);
    tm_view view= concrete_view (views[0]);
    QVERIFY (view != nullptr);

    actor_command_record snapshot;
    QVERIFY (buffer->actor->invoke (actor_command_kind::snapshot_document,
      view->runtime_id, ATHENA_NO_BLOB, ATHENA_NO_BLOB, &snapshot));
    const tree before= actor_tree_registry::instance ().take (snapshot.payload0);
    const bool native= model == 2 || existing_v3;
    const std::uint64_t expected_version= native ? 3 : model < 0 ? 1 : 2;
    QCOMPARE (snapshot.argument[0], expected_version);
    if (existing_v3) {
      const tree body= extract (before, "body");
      QVERIFY (is_compound (body[0], "math", 1));
      QVERIFY (is_atomic (body[0][0]) && body[0][0]->label == "retained");
    }

    bool native_context= false, math_context= false;
    const auto prepare= actor_continuation_registry::instance ().store ([&] {
      auto* actor= current_scheme_execution_context ()->actor;
      auto* editor= actor->current_editor (view->runtime_id);
      editor->go_to_end (actor->current_state ()->root_path * path (0, 0));
      math_context= editor->get_env_string ("mode") == "math";
      auto* math= dynamic_cast<edit_math_rep*> (editor);
      native_context= math && math->native_math_editing ();
    });
    QVERIFY (buffer->actor->invoke (actor_command_kind::run_native_continuation,
      view->runtime_id, ATHENA_NO_BLOB, ATHENA_NO_BLOB, nullptr,
      SCHEME_CAPABILITY_BUFFER, prepare));
    QVERIFY (math_context);
    QCOMPARE (native_context, native);

    const auto key= actor_text_registry::instance ().store ("(");
    QVERIFY (buffer->actor->invoke (actor_command_kind::key_press,
      view->runtime_id, key, ATHENA_NO_BLOB, nullptr,
      SCHEME_CAPABILITY_BUFFER, std::uint64_t (texmacs_time ())));
    QVERIFY (buffer->actor->invoke (actor_command_kind::snapshot_document,
      view->runtime_id, ATHENA_NO_BLOB, ATHENA_NO_BLOB, &snapshot));
    const tree edited= actor_tree_registry::instance ().take (snapshot.payload0);
    QCOMPARE (snapshot.argument[0], expected_version);
    const tree body= extract (edited, "body");
    QCOMPARE (contains_label (body, DELIMITED), native);
    if (native) {
      QVERIFY (!contains_label (body, AROUND));
      QVERIFY (!contains_label (body, VAR_AROUND));
      QVERIFY (!contains_label (body, LEFT));
      QVERIFY (!contains_label (body, RIGHT));
      QVERIFY (read_xml_v3 (write_xml_v3 (edited)) == edited);
    }
    else QVERIFY (contains_label (body, AROUND) || contains_label (body, VAR_AROUND));

    // A later creator/target change cannot reinitialize this live query.
    QVERIFY (!initialize_buffer_tree (query, document_with_math ("replacement"),
      native ? xml_format_version::v2 : xml_format_version::v3));
    QVERIFY (get_buffer_tree (query) == edited);
  }
};

static void run_tests (int argc, char** argv) {
  init_system_state ();
  gui_open (argc, argv);
  int status;
  {
    server runtime;
    eval ("(begin (tm-define (notify-cursor-moved status) #f) "
          "(tm-define (like-emacs?) #f))");
    TestNativeV3InputWidget test;
    status= QTest::qExec (&test, argc, argv);
  }
  gui_close ();
  release_boot_lock ();
  std::exit (status);
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

#include "native_v3_input_widget_test.moc"
