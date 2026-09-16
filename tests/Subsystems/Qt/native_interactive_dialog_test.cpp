/******************************************************************************
* MODULE     : native_interactive_dialog_test.cpp
* DESCRIPTION: Native interactive form GUI ownership and value return
*******************************************************************************/

#include "Subsystems/Qt/QTMNativeDialogs.hpp"

#include <QtTest/QtTest>

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLineEdit>
#include <QPushButton>
#include <QThread>
#include <QTimer>

#include <atomic>

namespace {

std::atomic<bool> affinityWarning {false};
QtMessageHandler previousHandler= nullptr;

void
captureMessage (QtMsgType type, const QMessageLogContext& context,
                const QString& message) {
  if (message.contains ("another thread") ||
      message.contains ("different thread") ||
      message.contains ("Cannot create children for a parent"))
    affinityWarning.store (true);
  if (previousHandler) previousHandler (type, context, message);
}

class InteractiveWorker: public QThread {
public:
  array<string> result;

protected:
  void run () override {
    std::vector<QTMInteractiveField> fields;
    QTMInteractiveField name;
    name.prompt= "Name:";
    name.type= "string";
    name.proposals << string ("Ada") << string ("Grace");
    fields.push_back (name);

    QTMInteractiveField password;
    password.prompt= "Secret:";
    password.type= "password";
    password.proposals << string ("must-not-prefill");
    fields.push_back (password);

    result= qtm_interactive_dialog ("Interactive command", fields);
  }
};

} // namespace

class NativeInteractiveDialogTest: public QObject {
  Q_OBJECT

private slots:
  void initTestCase ();
  void cleanupTestCase ();
  void nativeFormRunsOnGuiThreadAndReturnsValues ();
};

void
NativeInteractiveDialogTest::initTestCase () {
  previousHandler= qInstallMessageHandler (captureMessage);
}

void
NativeInteractiveDialogTest::cleanupTestCase () {
  qInstallMessageHandler (previousHandler);
}

void
NativeInteractiveDialogTest::nativeFormRunsOnGuiThreadAndReturnsValues () {
  affinityWarning.store (false);
  InteractiveWorker worker;
  bool sawDialog= false;

  QTimer responder;
  connect (&responder, &QTimer::timeout, this, [&] {
    auto* dialog= qobject_cast<QDialog*> (QApplication::activeModalWidget ());
    if (dialog == nullptr || dialog->windowTitle () != "Interactive command")
      return;

    responder.stop ();
    sawDialog= true;
    QCOMPARE (dialog->thread (), qApp->thread ());

    auto combos= dialog->findChildren<QComboBox*> ();
    QCOMPARE (combos.size (), 1);
    combos[0]->setEditText ("Grace Hopper");

    QLineEdit* password= nullptr;
    for (QLineEdit* edit: dialog->findChildren<QLineEdit*> ())
      if (edit->echoMode () == QLineEdit::Password) password= edit;
    QVERIFY (password != nullptr);
    QVERIFY (password->text ().isEmpty ());
    password->setText ("compiler");

    auto* buttons= dialog->findChild<QDialogButtonBox*> ();
    QVERIFY (buttons != nullptr);
    buttons->button (QDialogButtonBox::Ok)->click ();
  });
  responder.start (10);

  worker.start ();
  QTRY_VERIFY_WITH_TIMEOUT (worker.isFinished (), 4000);
  responder.stop ();
  worker.wait ();

  QVERIFY (sawDialog);
  QCOMPARE (N(worker.result), 2);
  QCOMPARE (worker.result[0], string ("Grace Hopper"));
  QCOMPARE (worker.result[1], string ("compiler"));
  QVERIFY (!affinityWarning.load ());
}

QTEST_MAIN (NativeInteractiveDialogTest)
#include "native_interactive_dialog_test.moc"
