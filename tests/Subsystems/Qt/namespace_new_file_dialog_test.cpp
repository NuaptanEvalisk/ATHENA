/******************************************************************************
* MODULE     : namespace_new_file_dialog_test.cpp
* DESCRIPTION: namespace wizard actor/Qt ownership regressions
*******************************************************************************/

#include <QtTest/QtTest>

#include "QTMNamespaceNewFile.hpp"

#include <QApplication>
#include <QDialog>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>

#include <atomic>
#include <thread>

namespace {

QDialog*
namespace_wizard () {
  for (QWidget* widget: QApplication::topLevelWidgets ()) {
    auto* dialog= qobject_cast<QDialog*> (widget);
    if (dialog != nullptr && dialog->windowTitle () == "New within namespace")
      return dialog;
  }
  return nullptr;
}

} // namespace

class NamespaceNewFileDialogTest: public QObject {
  Q_OBJECT

private slots:
  void actorOriginEscapeCancelsWithoutBlocking ();
  void actorOriginOptionalInitializerCompletesAsync ();
};

void
NamespaceNewFileDialogTest::actorOriginEscapeCancelsWithoutBlocking () {
  std::atomic<bool> callerReturned {false};
  std::atomic<bool> completed {false};
  string result= "pending";
  bool sawWizard= false;

  QTimer interaction;
  interaction.setInterval (10);
  connect (&interaction, &QTimer::timeout, this, [&] {
    QDialog* wizard= namespace_wizard ();
    if (wizard == nullptr) return;
    sawWizard= true;
    QCOMPARE (wizard->thread (), qApp->thread ());
    interaction.stop ();
    QTest::keyClick (wizard, Qt::Key_Escape);
  });
  interaction.start ();

  std::thread actor ([&] {
    namespace_new_file_wizard_async ([&] (string value) {
      result= std::move (value);
      completed.store (true, std::memory_order_release);
    });
    callerReturned.store (true, std::memory_order_release);
  });

  QTRY_VERIFY_WITH_TIMEOUT (
    callerReturned.load (std::memory_order_acquire), 1000);
  actor.join ();

  QTRY_VERIFY_WITH_TIMEOUT (completed.load (std::memory_order_acquire), 2000);
  QVERIFY (sawWizard);
  QCOMPARE (result, string (""));
  QTRY_VERIFY_WITH_TIMEOUT (namespace_wizard () == nullptr, 1000);
}

void
NamespaceNewFileDialogTest::actorOriginOptionalInitializerCompletesAsync () {
  QTemporaryDir directory;
  QVERIFY (directory.isValid ());
  QString target= directory.filePath ("plain.ath");
  std::atomic<bool> callerReturned {false};
  std::atomic<bool> completed {false};
  bool success= false;
  string error;

  std::thread actor ([&] {
    namespace_create_file_with_optional_initializer_async (
      string (target.toUtf8 ().constData ()),
      [&] (bool ok, string message) {
        success= ok;
        error= std::move (message);
        completed.store (true, std::memory_order_release);
      });
    callerReturned.store (true, std::memory_order_release);
  });

  QTRY_VERIFY_WITH_TIMEOUT (
    callerReturned.load (std::memory_order_acquire), 1000);
  actor.join ();
  QTRY_VERIFY_WITH_TIMEOUT (completed.load (std::memory_order_acquire), 2000);
  QVERIFY2 (success, as_charp (error));
  QVERIFY (QFileInfo::exists (target));
}

int
main (int argc, char** argv) {
  qputenv ("QT_QPA_PLATFORM", "offscreen");
  QApplication app (argc, argv);
  app.setQuitOnLastWindowClosed (false);
  NamespaceNewFileDialogTest test;
  return QTest::qExec (&test, argc, argv);
}

#include "namespace_new_file_dialog_test.moc"
