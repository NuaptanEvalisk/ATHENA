/******************************************************************************
* MODULE     : file_chooser_test.cpp
* DESCRIPTION: Nonblocking file chooser completion and lifetime
* COPYRIGHT  : (C) 2026 ATHENA contributors
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* See the file LICENSE in the root directory.
******************************************************************************/

#include <QApplication>
#include <QDialog>
#include <QFileDialog>
#include <QImage>
#include <QLineEdit>
#include <QPointer>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest/QtTest>
#include "qt_chooser_widget.hpp"
#include "message.hpp"
#include "analyze.hpp"
#include "QTMFileDialog.hpp"
#ifdef USE_KF6
#include <KIOFileWidgets/KFileCustomDialog>
#endif

bool headless_mode= false;
bool is_headless () { return false; }

struct chooser_state {
  int calls= 0;
  int quits= 0;
  bool destroyed= false;
  string result;
  qt_chooser_widget_rep* chooser= nullptr;
};

static void
chosen (void* data, void*) {
  auto& state= *static_cast<chooser_state*> (data);
  state.calls++;
  state.result= get_string_input (widget (state.chooser));
}

static void
closed (void* data, void*) {
  static_cast<chooser_state*> (data)->quits++;
}

class tracked_chooser: public qt_chooser_widget_rep {
  chooser_state& state;
public:
  explicit tracked_chooser (chooser_state* data):
    qt_chooser_widget_rep (command (chosen, data), "generic", ""),
    state (*data) {
    state.chooser= this;
    win_title= "ATHENA chooser regression";
    directory= string (qgetenv ("HOME").constData ());
    quit= command (closed, &state);
  }
  ~tracked_chooser () override { state.destroyed= true; }
  void configure (bool save, const QString& kind, const QString& suffix) {
    prompt= save? "Save": "";
    type= from_qstring_utf8 (kind);
    defaultSuffix= suffix;
  }
};

static QDialog*
active_chooser () {
  for (QWidget* widget: QApplication::topLevelWidgets ())
    if (widget->isVisible () &&
        widget->windowTitle () == "ATHENA chooser regression")
      if (auto* dialog= qobject_cast<QDialog*> (widget)) return dialog;
  return nullptr;
}

static void
close_choosers () {
  for (QWidget* window: QApplication::topLevelWidgets ())
    if (window->windowTitle () == "ATHENA chooser regression")
      if (auto* dialog= qobject_cast<QDialog*> (window)) dialog->reject ();
  QCoreApplication::sendPostedEvents (nullptr, QEvent::DeferredDelete);
}

class FileChooserTest: public QObject {
  Q_OBJECT
private slots:
  void returnsBeforeCompletion_data () {
    QTest::addColumn<bool> ("fallback");
    QTest::newRow ("platform") << false;
    QTest::newRow ("qfiledialog") << true;
  }
  void returnsBeforeCompletion () {
    QFETCH (bool, fallback);
    chooser_state state;
    auto cleanup= qScopeGuard (close_choosers);
    widget owner= tm_new<tracked_chooser> (&state);
    send_keyboard_focus (get_file (owner), false);
    QVERIFY (!active_chooser ());
    set_visibility (owner, true);
    bool returned= false, nested= false;
    QTimer watchdog;
    QObject::connect (&watchdog, &QTimer::timeout, [&] {
      if (auto* dialog= active_chooser ()) {
        nested= !returned;
        watchdog.stop ();
        dialog->reject ();
      }
    });
    watchdog.start (100);
    if (fallback) state.chooser->perform_dialog_with_qfiledialog ();
    else send_keyboard_focus (get_file (owner));
    returned= true;
    QTRY_COMPARE (state.calls, 1);
    QCOMPARE (state.quits, 1);
    QCOMPARE (state.result, string ("#f"));
    QVERIFY2 (!nested, "Chooser ran a nested event loop before returning");
  }
  void retainsUntilCompletion_data () { returnsBeforeCompletion_data (); }
  void retainsUntilCompletion () {
    QFETCH (bool, fallback);
    chooser_state state;
    auto cleanup= qScopeGuard (close_choosers);
    widget owner= tm_new<tracked_chooser> (&state);
    if (fallback) state.chooser->perform_dialog_with_qfiledialog ();
    else state.chooser->perform_dialog ();
    QPointer<QDialog> dialog= active_chooser ();
    QVERIFY (!dialog.isNull ());
    owner= widget ();
    QVERIFY (!state.destroyed);
    dialog->reject ();
    QCOMPARE (state.calls, 1);
    QCOMPARE (state.quits, 1);
    QCoreApplication::sendPostedEvents (nullptr, QEvent::DeferredDelete);
    QVERIFY (dialog.isNull ());
    QVERIFY (state.destroyed);
  }
  void completesOnce_data () { returnsBeforeCompletion_data (); }
  void completesOnce () {
    QFETCH (bool, fallback);
    chooser_state state;
    auto cleanup= qScopeGuard (close_choosers);
    widget owner= tm_new<tracked_chooser> (&state);
    auto open= [&] {
      if (fallback) state.chooser->perform_dialog_with_qfiledialog ();
      else state.chooser->perform_dialog ();
    };
    open ();
    QPointer<QDialog> dialog= active_chooser ();
    QVERIFY (dialog);
    QCOMPARE (dialog->windowModality (), Qt::ApplicationModal);
    open ();
    QCOMPARE (active_chooser (), dialog.data ());
    set_visibility (owner, false);
    QCOMPARE (state.calls, 1);
    QCOMPARE (state.quits, 1);
    dialog->reject ();
    dialog->done (QDialog::Accepted);
    QCOMPARE (state.calls, 1);
    QCOMPARE (state.quits, 1);
    QCOMPARE (state.result, string ("#f"));
  }
  void acceptsSelection_data () {
    QTest::addColumn<bool> ("fallback");
    QTest::addColumn<QString> ("kind");
    QTest::addColumn<bool> ("save");
    for (bool fallback: {false, true}) {
      QByteArray prefix= fallback? "qt-": "kde-";
      QTest::newRow ((prefix+"open").constData ()) << fallback << "generic" << false;
      QTest::newRow ((prefix+"save-suffix").constData ()) << fallback << "generic" << true;
      QTest::newRow ((prefix+"directory").constData ()) << fallback << "directory" << false;
      QTest::newRow ((prefix+"image").constData ()) << fallback << "image" << false;
    }
  }
  void acceptsSelection () {
    QFETCH (bool, fallback);
    QFETCH (QString, kind);
    QFETCH (bool, save);
    QTemporaryDir source;
    QVERIFY (source.isValid ());
    QString selected= kind == "directory"? source.path ():
      source.filePath (save? "new document": kind == "image"? "image.png": "input.txt");
    if (kind == "image") {
      QImage image (10, 20, QImage::Format_RGB32);
      image.fill (Qt::green);
      QVERIFY (image.save (selected));
    }
    else if (!save && kind != "directory") {
      QFile input (selected);
      QVERIFY (input.open (QIODevice::WriteOnly));
      input.write ("input\n");
    }
    chooser_state state;
    auto cleanup= qScopeGuard (close_choosers);
    auto* chooser= tm_new<tracked_chooser> (&state);
    widget owner= chooser;
    chooser->configure (save, kind, save? "ath": "");
    if (fallback) chooser->perform_dialog_with_qfiledialog ();
    else chooser->perform_dialog ();
    QPointer<QDialog> dialog= active_chooser ();
    QVERIFY (dialog);
    auto* preview= dialog->findChild<QTMImagePreview*> ();
    if (kind == "image") {
      QVERIFY (preview);
      preview->wid->setText ("30pt");
      preview->hei->setText ("60pt");
      preview->xps->setText ("1pt");
      preview->yps->setText ("2pt");
    }
#ifdef USE_KF6
    if (auto* kde= qobject_cast<KFileCustomDialog*> (dialog.data ())) {
      kde->fileWidget ()->setSelectedUrl (QUrl::fromLocalFile (selected));
      kde->fileWidget ()->slotOk ();
    }
    else
#endif
    {
      QFileDialog* qt= qobject_cast<QFileDialog*> (dialog.data ());
      if (!qt) qt= dialog->findChild<QFileDialog*> ();
      QVERIFY (qt);
      qt->selectFile (selected);
      QVERIFY (QMetaObject::invokeMethod (qt, "accept", Qt::DirectConnection));
    }
    QTRY_COMPARE (state.calls, 1);
    QCOMPARE (state.quits, 1);
    string expected= "(system->url " *
      scm_quote (from_qstring_utf8 (selected + (save? ".ath": ""))) * ")";
    if (kind == "image")
      expected= "(list " * expected * " \"30pt\" \"60pt\" \"1pt\" \"2pt\")";
    QCOMPARE (utf8_to_qstring (state.result), utf8_to_qstring (expected));
    QCoreApplication::sendPostedEvents (nullptr, QEvent::DeferredDelete);
    QVERIFY (dialog.isNull ());
  }
};

int main (int argc, char** argv) {
  QTemporaryDir home;
  if (!home.isValid ()) return 1;
  qputenv ("HOME", home.path ().toUtf8 ());
  qputenv ("ATHENA_HOME_PATH", home.path ().toUtf8 ());
  qputenv ("XDG_CONFIG_HOME", home.filePath ("config").toUtf8 ());
  qputenv ("XDG_CACHE_HOME", home.filePath ("cache").toUtf8 ());
  qputenv ("XDG_DATA_HOME", home.filePath ("data").toUtf8 ());
  QApplication app (argc, argv);
  app.setQuitOnLastWindowClosed (false);
  FileChooserTest test;
  return QTest::qExec (&test, argc, argv);
}

#include "file_chooser_test.moc"
