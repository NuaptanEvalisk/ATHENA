/******************************************************************************
* MODULE     : native_v3_properties_diff_test.cpp
* DESCRIPTION: Actor-owned property editing and versioned Qt revision comparison
* COPYRIGHT  : (C) 2026 ATHENA contributors
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
******************************************************************************/
#include <QtTest/QtTest>
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QElapsedTimer>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <cstdlib>
#include <functional>
#include <vector>

#include "Subsystems/Qt/QTMNodePropertiesDialog.hpp"
#include "Subsystems/Qt/QTMATHENADiff.hpp"
#include "Qt/QTMApplication.hpp"
#include "ATHENA/Data/enunciation_model.hpp"
#include "ATHENA/Data/new_buffer.hpp"
#include "ATHENA/Data/new_view.hpp"
#include "ATHENA/Data/new_window.hpp"
#include "ATHENA/server.hpp"
#include "ATHENA/tm_window.hpp"
#include "Editor/edit_main.hpp"
#include "Graphics/Gui/gui.hpp"
#include "Data/Convert/convert.hpp"
#include "analyze.hpp"
#include "boot.hpp"
#include "buffer_actor.hpp"
#include "buffer_state.hpp"
#include "data_cache.hpp"
#include "file.hpp"
#include "math_tree_v3.hpp"
#include "node_metadata.hpp"
#include "scheme.hpp"
#include "scheme_execution_context.hpp"
#include "sys_utils.hpp"

bool headless_mode= false;
bool is_headless () { return false; }

namespace {
namespace xml= athena::document;
namespace node= athena::node;

tree fixture_document (tree paragraph) {
  return tree (DOCUMENT, compound ("style", tuple ("generic")),
    compound ("body", tree (DOCUMENT, paragraph)));
}

tree statement (bool native) {
  tree source (athena::enunciation::label (), tree (DOCUMENT, "Preserved body"));
  node::metadata metadata;
  metadata.id= node::new_id ();
  metadata.properties["kind"]= node::property (std::string ("theorem"));
  metadata.properties["numbered"]= node::property (false);
  metadata.properties["name"]= node::property (node::rich_text {native ?
    athena::math_v3::scripts ("x", "", "", "i", "", 'r') : tree ("Original")});
  metadata.properties["test:integer"]= node::property (std::int64_t (9223372036854775807LL));
  metadata.properties["athena:artifact-bindings"]= node::property (node::property::dictionary {
    {"statement", node::property (node::new_id ())}});
  node::set (source, metadata);
  return source;
}

QDialog* propertyDialog () {
  for (auto* widget: QApplication::allWidgets ())
    if (widget->objectName () == "node-properties-dialog")
      return qobject_cast<QDialog*> (widget);
  return nullptr;
}

xml::native_xml_read_result snapshot (url name) {
  auto buffer= concrete_buffer (name);
  actor_command_record response;
  if (is_nil (buffer) || !buffer->actor->invoke (actor_command_kind::snapshot_document,
      ATHENA_NO_VIEW, ATHENA_NO_BLOB, ATHENA_NO_BLOB, &response))
    throw std::runtime_error ("Cannot snapshot test buffer");
  tree source= actor_tree_registry::instance ().take (response.payload0);
  switch (response.argument[0]) {
  case 1: return {xml::xml_format_version::v1, source};
  case 2: return {xml::xml_format_version::v2, source};
  case 3: return {xml::xml_format_version::v3, source};
  default: throw std::runtime_error ("Invalid snapshot storage version");
  }
}
} // namespace

class NativeV3PropertiesDiffTest: public QObject {
  Q_OBJECT
  url name= url_none ();
  tm_view view= nullptr;
  array<url> initial_buffers;
  array<url> initial_windows;

  bool run (std::function<void (editor_rep*, buffer_document_state*)> action) {
    auto buffer= concrete_buffer (name);
    const auto continuation= actor_continuation_registry::instance ().store ([&, action] {
      auto* actor= current_scheme_execution_context ()->actor;
      action (actor->current_editor (view->runtime_id), actor->current_state ());
    });
    const bool result= buffer->actor->invoke (actor_command_kind::run_native_continuation,
      view->runtime_id, ATHENA_NO_BLOB, ATHENA_NO_BLOB, nullptr,
      SCHEME_CAPABILITY_BUFFER, continuation);
    if (!result) actor_continuation_registry::instance ().discard (continuation);
    return result;
  }
  bool setup (xml::xml_format_version version) {
    name= url_scratch ("native_properties_", ".ath");
    if (!initialize_buffer_tree (name, fixture_document (statement (version == xml::xml_format_version::v3)),
                                 version)) return false;
    const url window= new_window (false);
    window_set_view (window, get_passive_view (name), true);
    view= concrete_view (window_to_view (window));
    return view != nullptr;
  }
  bool showProperties () {
    bool shown= false;
    return run ([&] (editor_rep* editor, buffer_document_state*) {
      shown= node_properties_show (editor->the_buffer ()[0]);
    }) && shown;
  }
private slots:
  void init () {
    initial_buffers= get_all_buffers ();
    initial_windows= windows_list ();
  }
  void cleanup () {
    std::vector<QPointer<QDialog>> dialogs;
    for (auto* widget: QApplication::topLevelWidgets ())
      if (auto* dialog= qobject_cast<QDialog*> (widget)) dialogs.emplace_back (dialog);
    for (auto dialog: dialogs) if (dialog) delete dialog.data ();
    QCoreApplication::processEvents ();
    const auto windows= windows_list ();
    for (int i=0; i<N(windows); ++i) {
      bool existing= false;
      for (int j=0; j<N(initial_windows); ++j) existing |= windows[i] == initial_windows[j];
      if (!existing) delete_window (windows[i]);
    }
    const auto buffers= get_all_buffers ();
    for (int i=0; i<N(buffers); ++i) {
      bool existing= false;
      for (int j=0; j<N(initial_buffers); ++j) existing |= buffers[i] == initial_buffers[j];
      if (!existing) remove_buffer (buffers[i]);
    }
    view= nullptr;
    name= url_none ();
  }
  void propertyApplyAndUndo_data () {
    QTest::addColumn<bool> ("native");
    QTest::newRow ("v2") << false;
    QTest::newRow ("v3") << true;
  }
  void propertyApplyAndUndo () {
    QFETCH (bool, native);
    const auto version= native ? xml::xml_format_version::v3 : xml::xml_format_version::v2;
    QVERIFY (setup (version));
    const tree before= extract (snapshot (name).document, "body");
    QVERIFY (showProperties ());
    QTRY_VERIFY (propertyDialog () != nullptr);
    QPointer<QDialog> dialog= propertyDialog ();
    dialog->findChild<QLineEdit*> ("enunciation-year")->setText ("19XX");
    dialog->findChild<QDialogButtonBox*> ()->button (QDialogButtonBox::Ok)->click ();
    QTRY_VERIFY (dialog.isNull ());
    const auto edited= snapshot (name);
    QCOMPARE (edited.version, version);
    const tree after= extract (edited.document, "body");
    QCOMPARE (after[0][0], before[0][0]);
    QCOMPARE (node::id (after[0]), node::id (before[0]));
    for (const char* key: {"name", "test:integer", "athena:artifact-bindings"})
      QVERIFY (node::equal (node::get (before[0])->properties.at (key),
                            node::get (after[0])->properties.at (key)));
    QCOMPARE (std::get<std::string> (node::get (after[0])->properties.at ("year").data),
              std::string ("19XX"));
    QVERIFY (run ([] (editor_rep* editor, buffer_document_state*) { editor->undo (0); }));
    QCOMPARE (extract (snapshot (name).document, "body"), before);
    QVERIFY (run ([] (editor_rep* editor, buffer_document_state*) { editor->redo (0); }));
    QCOMPARE (extract (snapshot (name).document, "body"), after);
  }
  void rejectsStaleVersionAndLease_data () {
    QTest::addColumn<bool> ("version_change");
    QTest::newRow ("version-change") << true;
    QTest::newRow ("removed-node") << false;
  }
  void rejectsStaleVersionAndLease () {
    QFETCH (bool, version_change);
    QVERIFY (setup (xml::xml_format_version::v3));
    QVERIFY (showProperties ());
    QTRY_VERIFY (propertyDialog () != nullptr);
    auto* dialog= propertyDialog ();
    QVERIFY (run ([&] (editor_rep* editor, buffer_document_state* state) {
      if (version_change) state->storage_version= xml::xml_format_version::v2;
      else {
        tree scope= editor->the_buffer ();
        editor->before_menu_action ();
        ::apply (scope, mod_assign (path (0), tree ("Replacement")));
        editor->after_menu_action ();
      }
    }));
    const tree before= extract (snapshot (name).document, "body");
    dialog->findChild<QLineEdit*> ("enunciation-year")->setText ("19XX");
    dialog->findChild<QDialogButtonBox*> ()->button (QDialogButtonBox::Ok)->click ();
    auto* error= dialog->findChild<QLabel*> ("node-properties-error");
    QTRY_VERIFY (!error->text ().isEmpty ());
    if (version_change) QVERIFY (error->text ().contains ("storage version"));
    QCOMPARE (extract (snapshot (name).document, "body"), before);
    QVERIFY (dialog->findChild<QDialogButtonBox*> ()->isEnabled ());
  }
  void structuredInputUsesSourceVersion_data () { propertyApplyAndUndo_data (); }
  void structuredInputUsesSourceVersion () {
    QFETCH (bool, native);
    const auto version= native ? xml::xml_format_version::v3 : xml::xml_format_version::v2;
    QVERIFY (setup (version));
    QVERIFY (showProperties ());
    QTRY_VERIFY (propertyDialog () != nullptr);
    auto* field= propertyDialog ()->findChild<QWidget*> ("enunciation-name");
    QVERIFY (field);
    auto* button= field->findChild<QToolButton*> ();
    QVERIFY (button);
    bool found= false, correct_version= false, timed_out= false;
    QElapsedTimer deadline;
    deadline.start ();
    QTimer poll;
    poll.setInterval (10);
    connect (&poll, &QTimer::timeout, [&] {
      if (deadline.elapsed () >= 5000) {
        timed_out= true;
        // Unwind even a hidden or unexpected nested dialog. Keep polling until
        // click() returns, since initialization may not have entered exec yet.
        for (auto* dialog: field->findChildren<QDialog*> ())
          dialog->done (QDialog::Rejected);
        return;
      }
      auto* modal= qobject_cast<QDialog*> (QApplication::activeModalWidget ());
      if (!modal || modal->windowTitle () != "Structured text") return;
      poll.stop ();
      try {
        const auto buffers= get_all_buffers ();
        for (int i=0; i<N(buffers); ++i)
          if (starts (as_string (buffers[i]), "tmfs://aux/node-property-")) {
            found= true;
            correct_version= snapshot (buffers[i]).version == version;
          }
      }
      catch (const std::exception&) { correct_version= false; }
      modal->reject ();
    });
    poll.start ();
    button->click ();
    poll.stop ();
    QVERIFY2 (!timed_out, "Structured-text modal did not become available within five seconds");
    QVERIFY (found);
    QVERIFY (correct_version);
  }
  void mixedVersionNativeDiff () {
    QVERIFY (setup (xml::xml_format_version::v2));
    tree left= fixture_document ("old plain source");
    tree right= fixture_document (athena::math_v3::delimited ("(", ")", 'a', "x"));
    node::metadata body_metadata;
    body_metadata.id= node::new_id ();
    node::set (right[1][0], body_metadata);
    node::metadata delimiter_metadata;
    delimiter_metadata.id= node::new_id ();
    delimiter_metadata.properties["test:tag"]= node::property (std::string ("native"));
    node::set (right[1][0][0], delimiter_metadata);
    const auto before= get_all_buffers ();
    athena_diff_show_snapshots (xml::write_xml (left), xml::write_xml_v3 (right),
      url ("tmfs://diff-left"), url ("tmfs://diff-right"), "Left v1", "Right v3");
    bool found_left= false, found_right= false;
    const auto after= get_all_buffers ();
    for (int i=0; i<N(after); ++i) {
      bool existing= false;
      for (int j=0; j<N(before); ++j) existing |= after[i] == before[j];
      if (existing) continue;
      const auto result= snapshot (after[i]);
      const tree body= extract (result.document, "body");
      if (body == extract (left, "body")) {
        QCOMPARE (result.version, xml::xml_format_version::v1);
        found_left= true;
      }
      if (body == extract (right, "body")) {
        QCOMPARE (result.version, xml::xml_format_version::v3);
        found_right= true;
      }
      bool read_only= false;
      auto buffer= concrete_buffer (after[i]);
      const auto continuation= actor_continuation_registry::instance ().store ([&] {
        read_only= current_scheme_execution_context ()->actor->current_state ()->read_only;
      });
      QVERIFY (buffer->actor->invoke (actor_command_kind::run_native_continuation,
        ATHENA_NO_VIEW, ATHENA_NO_BLOB, ATHENA_NO_BLOB, nullptr,
        SCHEME_CAPABILITY_BUFFER, continuation));
      QVERIFY (read_only);
    }
    QVERIFY (found_left && found_right);
  }
  void nativeDiffMetadataInspection () {
    QVERIFY (setup (xml::xml_format_version::v2));
    tree source= athena::math_v3::delimited ("(", ")", 'a', "x");
    node::metadata metadata;
    metadata.id= node::new_id ();
    metadata.properties["test:tag"]= node::property (std::string ("before"));
    node::set (source, metadata);
    const tree left= fixture_document (source);
    tree right= copy (left);
    node::edit (right[1][0][0])->properties["test:tag"]= node::property (std::string ("after"));
    const tree original_left= copy (left), original_right= copy (right);
    athena_diff_show_snapshots (
      xml::native_xml_read_result {xml::xml_format_version::v3, left},
      xml::native_xml_read_result {xml::xml_format_version::v3, right},
      url ("tmfs://diff-left"), url ("tmfs://diff-right"), "Left", "Right");
    QCOMPARE (left, original_left);
    QCOMPARE (right, original_right);
    QDialog* differences= nullptr;
    for (auto* widget: QApplication::topLevelWidgets ())
      if (widget->windowTitle () == "Revision property differences")
        differences= qobject_cast<QDialog*> (widget);
    QVERIFY (differences);
    auto* entries= differences->findChild<QTreeWidget*> ();
    QVERIFY (entries && entries->topLevelItemCount () == 1);
    entries->setCurrentItem (entries->topLevelItem (0));
    QPushButton* inspect= nullptr;
    for (auto* button: differences->findChildren<QPushButton*> ())
      if (button->text () == "Inspect both nodes") inspect= button;
    QVERIFY (inspect && inspect->isEnabled ());
    inspect->click ();
    int inspected= 0;
    for (auto* dialog: differences->findChildren<QDialog*> ()) {
      if (dialog->objectName () != "node-properties-dialog") continue;
      ++inspected;
      QCOMPARE (dialog->findChild<QLineEdit*> ("node-uuid")->text (),
                QString::fromStdString (node::id (source)));
      QVERIFY (!dialog->findChild<QDialogButtonBox*> ()->button (QDialogButtonBox::Ok));
      const auto* properties= dialog->findChild<QTreeWidget*> ("node-property-inspector");
      QCOMPARE (properties->topLevelItemCount (), 1);
      QCOMPARE (properties->topLevelItem (0)->text (2),
        dialog->windowTitle () == "Left revision properties" ? QString ("before") : QString ("after"));
    }
    QCOMPARE (inspected, 2);
    QCOMPARE (left, original_left);
    QCOMPARE (right, original_right);
  }
  void invalidNativeDiffDoesNotOpenBuffers () {
    const auto before= get_all_buffers ();
    const auto bytes= xml::write_xml_v3 (fixture_document ("plain v3"));
    QVERIFY_THROWS_EXCEPTION (std::exception, athena_diff_show_snapshots (
      bytes, std::string ("<not-native/>"), url ("tmfs://left"), url ("tmfs://right"), "Left", "Right"));
    QCOMPARE (N(get_all_buffers ()), N(before));
  }
};

static void run_tests (int argc, char** argv) {
  init_system_state ();
  gui_open (argc, argv);
  int status;
  {
    server runtime;
    eval ("(begin (tm-define (notify-cursor-moved status) #f) (tm-define (like-emacs?) #f))");
    NativeV3PropertiesDiffTest test;
    status= QTest::qExec (&test, argc, argv);
  }
  gui_close ();
  release_boot_lock ();
  std::exit (status);
}

int main (int argc, char** argv) {
  qputenv ("GUILE_AUTO_COMPILE", "0");
  qputenv ("QT_QPA_PLATFORM", "offscreen");
  QTMApplication application (argc, argv);
  QTemporaryDir profile;
  if (!profile.isValid () || !QDir (profile.path ()).mkpath ("fonts")) return 1;
  qputenv ("ATHENA_HOME_PATH", profile.path ().toUtf8 ());
  cache_initialize ();
  reset_document_tree (current_document_tree ());
  init_athena ();
  start_scheme (argc, argv, run_tests);
  return 1;
}

#include "native_v3_properties_diff_test.moc"
