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
#include <QMessageBox>
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
  void questionUsesNativeLabelsWithoutRedundantCancel ();
  void asynchronousForm_data ();
  void asynchronousForm ();
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

void
NativeInteractiveDialogTest::questionUsesNativeLabelsWithoutRedundantCancel () {
  array<string> result;
  auto* worker= QThread::create ([&] {
    QTMInteractiveField field;
    field.prompt= "Proceed?";
    field.type= "question";
    field.proposals << string ("yes") << string ("no");
    result= qtm_interactive_dialog ("Ignored for native questions", {field});
  });
  worker->start ();
  QTRY_VERIFY (QApplication::activeModalWidget () != nullptr);
  auto* box= qobject_cast<QMessageBox*> (QApplication::activeModalWidget ());
  QVERIFY (box != nullptr);
  QCOMPARE (box->windowTitle (), QString ("Question"));
  QStringList labels;
  QPushButton* yes= nullptr;
  for (QPushButton* button: box->findChildren<QPushButton*> ()) {
    labels << button->text ();
    if (button->text () == "Yes") yes= button;
  }
  QVERIFY (labels.contains ("Yes"));
  QVERIFY (labels.contains ("No"));
  QVERIFY (!labels.contains ("yes"));
  QVERIFY (!labels.contains ("no"));
  QVERIFY (!labels.contains ("Cancel"));
  QVERIFY (yes != nullptr);
  yes->click ();
  QTRY_VERIFY_WITH_TIMEOUT (worker->isFinished (), 4000);
  worker->wait ();
  delete worker;
  QCOMPARE (N(result), 1);
  QCOMPARE (result[0], string ("yes"));
}

void
NativeInteractiveDialogTest::asynchronousForm_data () {
  QTest::addColumn<bool> ("accept");
  QTest::newRow ("accept") << true;
  QTest::newRow ("cancel") << false;
}

void
NativeInteractiveDialogTest::asynchronousForm () {
  QFETCH (bool, accept);
  affinityWarning.store (false);
  int completions= 0;
  std::vector<std::string> result;
  auto* worker= QThread::create ([&] {
    QTMInteractiveField field;
    field.prompt= "Value:";
    field.type= "string";
    field.proposals << string ("initial");
    qtm_interactive_form_async ("Async formatting", {field},
      [&] (std::vector<std::string> answers) {
        QCOMPARE (QThread::currentThread (), qApp->thread ());
        ++completions;
        result= std::move (answers);
      });
  });
  worker->start ();
  // No GUI event processing: the caller must return before the form is shown.
  bool returned= worker->wait (1000);
  if (!returned) {
    QTRY_VERIFY_WITH_TIMEOUT (worker->isFinished (), 4000);
    worker->wait ();
  }
  delete worker;
  QVERIFY (returned);
  QTRY_VERIFY (QApplication::activeModalWidget () != nullptr);
  auto* dialog= qobject_cast<QDialog*> (QApplication::activeModalWidget ());
  QVERIFY (dialog != nullptr);
  QCOMPARE (dialog->windowTitle (), QString ("Async formatting"));
  QCOMPARE (dialog->thread (), qApp->thread ());
  auto* combo= dialog->findChild<QComboBox*> ();
  QVERIFY (combo != nullptr);
  QCOMPARE (combo->currentText (), QString ("initial"));
  combo->setEditText (QString::fromUtf8 ("caf\xc3\xa9"));
  auto* buttons= dialog->findChild<QDialogButtonBox*> ();
  QVERIFY (buttons != nullptr);
  buttons->button (accept ? QDialogButtonBox::Ok : QDialogButtonBox::Cancel)->click ();
  QCOMPARE (completions, 1);
  QCOMPARE (result.size (), accept ? size_t (1) : size_t (0));
  if (accept) QCOMPARE (result[0], std::string ("caf\xc3\xa9"));
  QVERIFY (!affinityWarning.load ());
}

QTEST_MAIN (NativeInteractiveDialogTest)
#include "native_interactive_dialog_test.moc"
