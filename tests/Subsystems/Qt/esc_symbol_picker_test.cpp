/******************************************************************************
* MODULE     : esc_symbol_picker_test.cpp
* DESCRIPTION: ESC picker UI ownership and asynchronous actor dispatch
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
*******************************************************************************/

#include <QtTest/QtTest>
#include <QDialog>
#include <QLineEdit>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>

#include "QTMESCSymbolPicker.hpp"
#include "scheme_execution_context.hpp"

#include <atomic>
#include <thread>

bool headless_mode= true;
bool is_headless () { return true; }

namespace {
std::atomic<bool> affinity_warning {false};
QtMessageHandler previous_handler= nullptr;

void capture_message (QtMsgType type, const QMessageLogContext& context,
                      const QString& message) {
  if (message.contains ("different thread") ||
      message.contains ("another thread") ||
      message.contains ("event dispatcher has already been destroyed"))
    affinity_warning.store (true);
  if (previous_handler) previous_handler (type, context, message);
}

QDialog* active_picker () {
  auto* dialog= qobject_cast<QDialog*> (QApplication::activePopupWidget ());
  if (dialog && dialog->findChild<QLineEdit*> ()) return dialog;
  return nullptr;
}
}

class EscSymbolPickerTest: public QObject {
  Q_OBJECT
  QTemporaryDir home;

private slots:
  void initTestCase () {
    QVERIFY (home.isValid ());
    qputenv ("ATHENA_HOME_PATH", home.path ().toUtf8 ());
    previous_handler= qInstallMessageHandler (capture_message);
  }
  void cleanupTestCase () {
    qInstallMessageHandler (previous_handler);
  }
  void returnsSelectionOnUiThread () {
    bool selected= false;
    QTimer select;
    connect (&select, &QTimer::timeout, this, [&] {
      auto* picker= active_picker ();
      if (!picker) return;
      select.stop ();
      QCOMPARE (picker->thread (), qApp->thread ());
      auto* search= picker->findChild<QLineEdit*> ();
      search->setText ("a");
      selected= true;
      QTest::keyClick (search, Qt::Key_Escape);
    });
    select.start (10);
    QCOMPARE (escape_symbol_picker_dialog (), string ("alpha"));
    QVERIFY (selected);
    QVERIFY (!affinity_warning.load ());
  }
  void actorReturnsBeforeUiInteraction () {
    // The origin has already gone away: completion must release its payload.
    for (bool accept: {false, true}) {
      bool returned_empty= false;
      std::thread actor ([&] {
        SchemeExecutionContext context (
          nullptr, nullptr, nullptr, nullptr, 1234567, 7654321, 1,
          SCHEME_CAPABILITY_BUFFER | SCHEME_CAPABILITY_UI);
        SchemeExecutionScope scope (context);
        returned_empty= escape_symbol_picker_dialog () == "";
      });
      actor.join ();
      QVERIFY (returned_empty);

      bool finished= false;
      QTimer interact;
      connect (&interact, &QTimer::timeout, this, [&] {
        auto* picker= active_picker ();
        if (!picker) return;
        interact.stop ();
        QCOMPARE (picker->thread (), qApp->thread ());
        finished= true;
        if (accept)
          QTest::keyClick (picker->findChild<QLineEdit*> (), Qt::Key_Escape);
        else picker->reject ();
      });
      interact.start (10);
      QTRY_VERIFY_WITH_TIMEOUT (finished, 1000);
      QVERIFY (active_picker () == nullptr);
      QVERIFY (!affinity_warning.load ());
    }
  }
};

QTEST_MAIN (EscSymbolPickerTest)
#include "esc_symbol_picker_test.moc"
