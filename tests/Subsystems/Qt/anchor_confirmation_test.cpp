/******************************************************************************
* MODULE     : anchor_confirmation_test.cpp
* DESCRIPTION: GUI ownership and asynchronous anchor confirmation lifetime
* COPYRIGHT  : (C) 2026 ATHENA contributors
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* See the file LICENSE in the root directory.
******************************************************************************/

#include "QTMAnchorConfirmation.hpp"
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QListWidget>
#include <QPointer>
#include <QPushButton>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QThread>
#include <QtTest/QtTest>
#include <memory>
#include <thread>

bool headless_mode= false;
bool is_headless () { return false; }

static QDialog*
confirmation () {
  for (QWidget* window: QApplication::topLevelWidgets ())
    if (window->objectName () == "athena-anchor-confirmation")
      return qobject_cast<QDialog*> (window);
  return nullptr;
}

class AnchorConfirmationTest: public QObject {
  Q_OBJECT
private slots:
  void completesOnGui_data () {
    QTest::addColumn<QString> ("action");
    QTest::newRow ("apply") << "apply";
    QTest::newRow ("cancel") << "cancel";
    QTest::newRow ("escape") << "escape";
    QTest::newRow ("close") << "close";
  }
  void destructionCompletesOnce_data () {
    QTest::addColumn<bool> ("destroy_parent");
    QTest::newRow ("dialog-destroyed") << false;
    QTest::newRow ("parent-destroyed") << true;
  }
  void destructionCompletesOnce () {
    QFETCH (bool, destroy_parent);
    QPointer<QWidget> parent= new QWidget;
    parent->show ();
    QApplication::setActiveWindow (parent);
    int calls= 0;
    bool accepted= true;
    QThread* callback_thread= nullptr;
    auto lifetime= std::make_shared<int> (7);
    std::weak_ptr<int> weak= lifetime;
    auto cleanup= qScopeGuard ([&] {
      if (auto* dialog= confirmation ()) delete dialog;
      delete parent.data ();
    });
    qt_anchor_enunciations_confirm ("1", "0", "0", "wrap definition",
      [&, lifetime] (bool result) {
        ++calls;
        accepted= result;
        callback_thread= QThread::currentThread ();
      });
    lifetime.reset ();
    QTRY_VERIFY (confirmation ());
    QPointer<QDialog> dialog= confirmation ();
    QCOMPARE (dialog->parentWidget (), parent.data ());
    if (destroy_parent) delete parent.data ();
    else delete dialog.data ();
    QVERIFY (dialog.isNull ());
    QCOMPARE (calls, 1);
    QVERIFY (!accepted);
    QCOMPARE (callback_thread, qApp->thread ());
    QVERIFY (weak.expired ());
    QCoreApplication::sendPostedEvents (nullptr, QEvent::DeferredDelete);
    QCOMPARE (calls, 1);
  }
  void completesOnGui () {
    QFETCH (QString, action);
    int calls= 0;
    bool accepted= false;
    QThread* callback_thread= nullptr;
    auto lifetime= std::make_shared<int> (7);
    std::weak_ptr<int> weak= lifetime;
    auto cleanup= qScopeGuard ([] {
      if (auto* dialog= confirmation ()) dialog->reject ();
      QCoreApplication::sendPostedEvents (nullptr, QEvent::DeferredDelete);
    });
    std::thread worker ([&, lifetime] {
      qt_anchor_enunciations_confirm (
        "2", "1", "3", "wrap example<<<ATHENA-ANCHOR-ACTION>>>anchor heading: Test",
        [&, lifetime] (bool result) {
          calls++;
          accepted= result;
          callback_thread= QThread::currentThread ();
        });
    });
    worker.join ();
    QCOMPARE (calls, 0);
    QVERIFY (!confirmation ());
    lifetime.reset ();
    QVERIFY (!weak.expired ());
    QTRY_VERIFY (confirmation ());
    QPointer<QDialog> dialog= confirmation ();
    QCOMPARE (dialog->thread (), qApp->thread ());
    QCOMPARE (dialog->windowModality (), Qt::ApplicationModal);
    for (QObject* child: dialog->findChildren<QObject*> ())
      QCOMPARE (child->thread (), qApp->thread ());
    auto* list= dialog->findChild<QListWidget*> ();
    QVERIFY (list);
    QCOMPARE (list->count (), 2);
    auto* buttons= dialog->findChild<QDialogButtonBox*> ();
    QVERIFY (buttons);
    if (action == "apply") {
      for (QAbstractButton* button: buttons->buttons ())
        if (buttons->buttonRole (button) == QDialogButtonBox::AcceptRole)
          QTest::mouseClick (button, Qt::LeftButton);
    }
    else if (action == "cancel")
      QTest::mouseClick (buttons->button (QDialogButtonBox::Cancel), Qt::LeftButton);
    else if (action == "escape") QTest::keyClick (dialog, Qt::Key_Escape);
    else dialog->close ();
    QCOMPARE (calls, 1);
    QCOMPARE (accepted, action == "apply");
    QCOMPARE (callback_thread, qApp->thread ());
    dialog->reject ();
    QCOMPARE (calls, 1);
    QCoreApplication::sendPostedEvents (nullptr, QEvent::DeferredDelete);
    QVERIFY (dialog.isNull ());
    QVERIFY (weak.expired ());
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
  AnchorConfirmationTest test;
  return QTest::qExec (&test, argc, argv);
}

#include "anchor_confirmation_test.moc"
