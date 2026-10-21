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
#include "ATHENA/Data/enunciation_model.hpp"
#include "Subsystems/Qt/QTMEnunciationSlots.hpp"
#include "Subsystems/Qt/QTMWidget.hpp"
#include "Subsystems/Qt/qt_actor_widget.hpp"
#include "Subsystems/Qt/qt_widget.hpp"

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
  void structuredTitleSlots () {
    namespace en= athena::enunciation;
    namespace node= athena::node;
    const url master ("tmfs://aux/input-widget-master");
    tree statement (en::label (), tree (DOCUMENT, "A statement body."));
    node::metadata metadata;
    metadata.id= node::new_id ();
    metadata.properties["kind"]= node::property (std::string ("theorem"));
    metadata.properties["numbered"]= node::property (true);
    metadata.properties["name"]= node::property (node::rich_text {tree ("AlphaBeta")});
    node::set (statement, metadata);
    tree document (DOCUMENT, compound ("style", tuple ("generic")),
      compound ("body", tree (DOCUMENT, statement, "Outside.")));
    QVERIFY (initialize_buffer_tree (master, document, xml_format_version::v3));
    const url window= new_window (true);
    const url view_url= get_passive_view (master);
    window_set_view (window, view_url, true);
    const tm_view view= concrete_view (view_url);
    const auto owner= view->buf->actor;
    auto cleanup= qScopeGuard ([&] {
      const auto close= actor_continuation_registry::instance ().store ([] {
        current_scheme_execution_context ()->editor->open_enunciation_slot ("", 0);
      });
      owner->invoke (actor_command_kind::run_native_continuation, view->runtime_id,
        ATHENA_NO_BLOB, ATHENA_NO_BLOB, nullptr, SCHEME_CAPABILITY_BUFFER, close);
      qtm_enunciation_slot (nullptr, owner->id (), view->runtime_id, "");
      const bool drained= QTest::qWaitFor ([] {
        const auto buffers= get_all_buffers ();
        for (int i=0; i<N(buffers); ++i)
          if (starts (as_string (buffers[i]), "tmfs://aux/enunciation-slot-")) return false;
        return true;
      }, 5000);
      delete_window (window);
      remove_buffer (master);
      QVERIFY (drained);
    });
    const auto command= [&] (const std::function<void (editor_rep&)>& action) {
      const auto token= actor_continuation_registry::instance ().store ([action] {
        action (*current_scheme_execution_context ()->editor);
      });
      return owner->invoke (actor_command_kind::run_native_continuation,
        view->runtime_id, ATHENA_NO_BLOB, ATHENA_NO_BLOB, nullptr,
        SCHEME_CAPABILITY_BUFFER, token);
    };
    bool opened= false;
    QVERIFY (command ([&] (editor_rep& editor) {
      editor.go_to_end (editor.the_buffer_path () * path (0, 0, 0));
      opened= editor.open_enunciation_slot ("name", 0);
    }));
    QVERIFY (opened);
    const auto active_input= [] () -> QTMWidget* {
      for (QWidget* widget: QApplication::allWidgets ())
        if (widget->objectName () == "enunciation-inline-slot" && widget->isVisible () && widget->isEnabled ())
          return widget->findChild<QTMWidget*> ();
      return nullptr;
    };
    QTRY_VERIFY_WITH_TIMEOUT (active_input () != nullptr, 10000);
    QTRY_VERIFY (active_input ()->width () > 20 && active_input ()->height () > 15);
    QCOMPARE (active_input ()->horizontalScrollBarPolicy (), Qt::ScrollBarAlwaysOff);
    QCOMPARE (active_input ()->verticalScrollBarPolicy (), Qt::ScrollBarAlwaysOff);
    const auto input_command= [&] (const std::function<void (editor_rep&)>& action) {
      QTMWidget* input= active_input ();
      if (!input) return false;
      auto* proxy= dynamic_cast<qt_actor_widget_rep*> (input->tm_widget ());
      if (!proxy) return false;
      tm_view child= concrete_runtime_view (proxy->view_id ());
      if (!child) return false;
      const auto token= actor_continuation_registry::instance ().store ([action] {
        action (*current_scheme_execution_context ()->editor);
      });
      return child->buf->actor->invoke (actor_command_kind::run_native_continuation,
        child->runtime_id, ATHENA_NO_BLOB, ATHENA_NO_BLOB, nullptr,
        SCHEME_CAPABILITY_BUFFER, token);
    };
    auto snapshot= [&] () {
      actor_command_record reply;
      if (!owner->invoke (actor_command_kind::snapshot_body, view->runtime_id,
            ATHENA_NO_BLOB, ATHENA_NO_BLOB, &reply)) return tree (UNINIT);
      return actor_tree_registry::instance ().take (reply.payload0);
    };
    const tree original_body= snapshot ()[0][0];
    bool suppress_links= false;
    QVERIFY (input_command ([&] (editor_rep& editor) {
      suppress_links= editor.get_init_string ("athena-radioactive-links-suppressed") == "true";
    }));
    QVERIFY (suppress_links);
    QVERIFY (input_command ([] (editor_rep& editor) {
      editor.go_to (editor.the_buffer_path () * path (0, 5));
    }));
    QPointer<QTMWidget> previous_input= active_input ();
    QTest::keyClick (active_input (), Qt::Key_Comma, Qt::ControlModifier);
    QTRY_COMPARE_WITH_TIMEOUT (en::names (snapshot ()[0]).size (), size_t (2), 10000);
    QCOMPARE (en::names (snapshot ()[0])[0], tree ("Alpha"));
    QCOMPARE (en::names (snapshot ()[0])[1], tree ("Beta"));
    QTRY_VERIFY (active_input () && active_input () != previous_input);
    QTest::qWait (100);
    QVERIFY (input_command ([] (editor_rep& editor) {
      editor.go_to_end (editor.the_buffer_path ());
    }));
    const int before_comma_width= active_input ()->width ();
    QTest::keyClick (active_input (), Qt::Key_Comma);
    QTRY_COMPARE_WITH_TIMEOUT (en::names (snapshot ()[0])[1], tree ("Beta,"), 10000);
    QCOMPARE (en::names (snapshot ()[0]).size (), size_t (2));
    QTRY_VERIFY (active_input ()->width () > before_comma_width);
    const int short_width= active_input ()->width ();
    QPointer<QTMWidget> growing_input= active_input ();
    SI line_y= 0, previous_x= 0;
    QVERIFY (input_command ([&] (editor_rep& editor) {
      previous_x= editor.get_cursor_x (); line_y= editor.get_cursor_y ();
    }));
    for (char key: std::string (" extended alias")) {
      QTest::keyClicks (active_input (), QString (QChar (key)));
      QTest::qWait (20);
      SI x= 0, y= 0;
      QVERIFY (input_command ([&] (editor_rep& editor) {
        x= editor.get_cursor_x (); y= editor.get_cursor_y ();
      }));
      QCOMPARE (y, line_y);
      QVERIFY (x >= previous_x);
      previous_x= x;
      QCOMPARE (active_input ()->origin (), QPoint ());
    }
    QTRY_COMPARE (en::names (snapshot ()[0])[1], tree ("Beta, extended alias"));
    QTRY_VERIFY (active_input ()->width () > short_width + 40);
    QCOMPARE (active_input (), growing_input.data ());
    for (int i=0; i<15; ++i) {
      QTest::keyClick (active_input (), Qt::Key_Backspace);
      QTest::qWait (20);
      SI x= 0, y= 0;
      QVERIFY (input_command ([&] (editor_rep& editor) {
        x= editor.get_cursor_x (); y= editor.get_cursor_y ();
      }));
      QCOMPARE (y, line_y);
      QVERIFY (x <= previous_x);
      previous_x= x;
      QCOMPARE (active_input ()->origin (), QPoint ());
    }
    QTRY_COMPARE (en::names (snapshot ()[0])[1], tree ("Beta,"));
    QTRY_COMPARE (active_input ()->width (), short_width);
    previous_input= active_input ();
    QVERIFY (input_command ([] (editor_rep& editor) {
      editor.go_to_end (editor.the_buffer_path ());
      editor.handle_keypress ("C-,", texmacs_time ());
    }));
    QTRY_COMPARE_WITH_TIMEOUT (en::names (snapshot ()[0]).size (), size_t (3), 10000);
    QCOMPARE (en::names (snapshot ()[0])[2], tree (""));
    QTest::qWait (100);
    QTRY_VERIFY (active_input () && active_input () != previous_input);
    previous_input= active_input ();
    QVERIFY (input_command ([] (editor_rep& editor) {
      editor.handle_keypress ("backspace", texmacs_time ());
    }));
    QTRY_COMPARE_WITH_TIMEOUT (en::names (snapshot ()[0]).size (), size_t (2), 10000);
    QTRY_VERIFY (active_input () && active_input () != previous_input);
    QCOMPARE (snapshot ()[0][0], original_body);
    {
      QTMWidget* input= active_input ();
      QVERIFY (input);
      QVERIFY (input->surface ()->width () > 20);
      const auto drawn= [&] {
        const QImage image= input->grab ().toImage ();
        int ink= 0, background= 0;
        for (int y=2; y<image.height ()-2; ++y)
          for (int x=2; x<image.width ()-2; ++x) {
            const QColor pixel= image.pixelColor (x,y);
            if (pixel.red () < 80 && pixel.green () < 80 && pixel.blue () < 80) ++ink;
            if (pixel == QColor ("#f4e9fa")) ++background;
          }
        return ink > 10 && background > 100;
      };
      QTRY_VERIFY (drawn ());
      QCOMPARE (QApplication::focusWidget (), input);
      if (!qEnvironmentVariableIsEmpty ("ATHENA_TEST_SCREENSHOT"))
        QVERIFY (input->window ()->grab ().save (qEnvironmentVariable ("ATHENA_TEST_SCREENSHOT")));
    }
    previous_input= active_input ();
    QTest::keyClick (active_input (), Qt::Key_Tab);
    QTRY_VERIFY (active_input () && active_input () != previous_input);
    QTest::keyClicks (active_input (), "1890");
    QTRY_COMPARE_WITH_TIMEOUT (en::title_fields (snapshot ()[0], "year"),
      std::vector<tree> {tree ("1890")}, 10000);
    previous_input= active_input ();
    QTest::keyClick (active_input (), Qt::Key_Tab);
    QTRY_VERIFY (active_input () && active_input () != previous_input);
    QTest::keyClick (active_input (), Qt::Key_Backspace);
    QTest::qWait (100);
    QVERIFY (active_input ());
    QTest::keyClicks (active_input (), "Gauss");
    QTRY_COMPARE_WITH_TIMEOUT (en::title_fields (snapshot ()[0], "attribution"),
      std::vector<tree> {tree ("Gauss")}, 10000);
    previous_input= active_input ();
    QVERIFY (input_command ([] (editor_rep& editor) {
      editor.go_to_end (editor.the_buffer_path ());
      editor.insert_tree (compound ("strong", compound ("math", "xy")));
      editor.go_to (editor.the_buffer_path () * path (0, 1, 0, 0) * 1);
      editor.handle_keypress ("C-,", texmacs_time ());
    }));
    QTRY_COMPARE_WITH_TIMEOUT (en::title_fields (snapshot ()[0], "attribution").size (),
      size_t (2), 10000);
    QCOMPARE (en::title_fields (snapshot ()[0], "attribution")[0],
      tree (CONCAT, "Gauss", compound ("strong", compound ("math", "x"))));
    QCOMPARE (en::title_fields (snapshot ()[0], "attribution")[1],
      compound ("strong", compound ("math", "y")));
    QCOMPARE (snapshot ()[0][0], original_body);
    QTRY_VERIFY (active_input () && active_input () != previous_input);

    QTMWidget* source_canvas= nullptr;
    for (QWidget* candidate: QApplication::allWidgets ()) {
      auto* canvas= qobject_cast<QTMWidget*> (candidate);
      if (!canvas) continue;
      auto* proxy= dynamic_cast<qt_actor_widget_rep*> (canvas->tm_widget ());
      if (proxy && proxy->view_id () == view->runtime_id) source_canvas= canvas;
    }
    QVERIFY (source_canvas);
    const QPoint click= source_canvas->presentationSurface ()->mapFromGlobal (
      active_input ()->mapToGlobal (active_input ()->rect ().center ()));
    QVERIFY (command ([] (editor_rep& editor) { editor.open_enunciation_slot ("", 0); }));
    QTRY_VERIFY (!active_input ());
    QTest::mouseClick (source_canvas->presentationSurface (), Qt::LeftButton, Qt::NoModifier, click);
    QTRY_VERIFY (active_input ());

    QVERIFY (command ([] (editor_rep& editor) {
      editor.open_enunciation_slot ("", 0);
      editor.undo (0);
    }));
    QTRY_VERIFY (!active_input ());
    QTRY_COMPARE (en::title_fields (snapshot ()[0], "attribution").size (), size_t (1));
    QCOMPARE (snapshot ()[0][0], original_body);
  }

  void cleanup () {
    const auto buffers= get_all_buffers ();
    for (int i=0; i<N(buffers); ++i)
      QVERIFY (!starts (as_string (buffers[i]), "tmfs://aux/enunciation-slot-"));
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
