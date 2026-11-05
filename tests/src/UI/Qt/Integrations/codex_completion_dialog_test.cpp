/******************************************************************************
* MODULE     : codex_completion_dialog_test.cpp
* DESCRIPTION: AI completion model dialog ownership and catalog loading
*******************************************************************************/

#include <QtTest/QtTest>

#include "QTMCodexCompletion.hpp"
#include "qt_utilities.hpp"

#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QPushButton>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTimer>

#include <atomic>
#include <thread>

bool headless_mode= false;
bool is_headless () { return false; }

namespace {

QDialog*
completion_dialog () {
  for (QWidget* widget: QApplication::topLevelWidgets ()) {
    auto* dialog= qobject_cast<QDialog*> (widget);
    if (dialog != nullptr && dialog->windowTitle () == "AI completion (custom)")
      return dialog;
  }
  return nullptr;
}

QString
write_fake_bridge (QTemporaryDir& directory) {
  QString path= directory.filePath ("fake-codex-bridge");
  QFile file (path);
  if (!file.open (QIODevice::WriteOnly | QIODevice::Text)) return QString ();
  file.write (
    "#!/bin/sh\n"
    "if [ \"$1\" = \"--list-models\" ]; then\n"
    "  printf '%s\\n' '[{\"model\":\"gpt-test\",\"displayName\":\"GPT Test\","
    "\"description\":\"Test model\",\"isDefault\":true,"
    "\"defaultReasoningEffort\":\"medium\","
    "\"supportedReasoningEfforts\":[{\"reasoningEffort\":\"low\","
    "\"description\":\"Low\"},{\"reasoningEffort\":\"medium\","
    "\"description\":\"Medium\"}],\"serviceTiers\":[]}]'\n"
    "  exit 0\n"
    "fi\n"
    "output=''\n"
    "while [ $# -gt 0 ]; do\n"
    "  if [ \"$1\" = \"--output\" ]; then shift; output=$1; fi\n"
    "  shift\n"
    "done\n"
    "[ -n \"$output\" ] || exit 2\n"
    "printf '%s' 'continued formula' > \"$output\"\n");
  file.close ();
  QFile::Permissions permissions= file.permissions ();
  permissions |= QFileDevice::ExeOwner | QFileDevice::ExeGroup |
                 QFileDevice::ExeOther;
  if (!file.setPermissions (permissions)) return QString ();
  return path;
}

} // namespace

class CodexCompletionDialogTest: public QObject {
  Q_OBJECT

private slots:
  void actorOriginLoadsModelsAndCancels ();
  void actorOriginLaunchesCompletionProcess ();
};

static void
mark_called (void* state, void*) {
  static_cast<std::atomic<bool>*> (state)->store (true,
                                                  std::memory_order_release);
}

void
CodexCompletionDialogTest::actorOriginLoadsModelsAndCancels () {
  QTemporaryDir directory;
  QVERIFY (directory.isValid ());
  QString bridge= write_fake_bridge (directory);
  QVERIFY (!bridge.isEmpty ());

  std::atomic<bool> finished {false};
  array<string> result;
  bool sawDialog= false;
  bool sawModels= false;

  QTimer interaction;
  interaction.setInterval (10);
  connect (&interaction, &QTimer::timeout, this, [&] {
    QDialog* dialog= completion_dialog ();
    if (dialog == nullptr) return;
    sawDialog= true;
    QCOMPARE (dialog->thread (), qApp->thread ());
    const auto combos= dialog->findChildren<QComboBox*> ();
    if (combos.isEmpty ()) return;
    QComboBox* model= combos.front ();
    if (model->findData (QStringLiteral ("gpt-test")) < 0) return;
    sawModels= true;
    auto* buttons= dialog->findChild<QDialogButtonBox*> ();
    QVERIFY (buttons != nullptr);
    QPushButton* cancel= buttons->button (QDialogButtonBox::Cancel);
    QVERIFY (cancel != nullptr);
    interaction.stop ();
    QTest::mouseClick (cancel, Qt::LeftButton);
  });
  interaction.start ();

  std::thread actor ([&] {
    result= qtm_codex_completion_options (
      from_qstring (bridge), from_qstring (directory.filePath ("codex-home")));
    finished.store (true, std::memory_order_release);
  });
  auto cleanup= qScopeGuard ([&] {
    if (!actor.joinable ()) return;
    if (QDialog* dialog= completion_dialog ())
      (void) QMetaObject::invokeMethod (
        dialog, &QDialog::reject, Qt::QueuedConnection);
    actor.join ();
  });

  QTRY_VERIFY_WITH_TIMEOUT (finished.load (std::memory_order_acquire), 5000);
  actor.join ();
  QVERIFY (sawDialog);
  QVERIFY (sawModels);
  QCOMPARE (N(result), 0);
  QVERIFY (completion_dialog () == nullptr);
}

void
CodexCompletionDialogTest::actorOriginLaunchesCompletionProcess () {
  QTemporaryDir directory;
  QVERIFY (directory.isValid ());
  QString bridge= write_fake_bridge (directory);
  QVERIFY (!bridge.isEmpty ());
  QString input= directory.filePath ("input.txt");
  QString output= directory.filePath ("output.txt");
  QFile prompt (input);
  QVERIFY (prompt.open (QIODevice::WriteOnly | QIODevice::Text));
  prompt.write ("Continue x");
  prompt.close ();

  std::atomic<bool> called {false};
  std::thread actor ([&] {
    array<string> images;
    qtm_codex_run_completion_async (
      from_qstring (bridge), from_qstring (directory.path ()),
      from_qstring (input), from_qstring (output), "gpt-test", "medium", "",
      "disabled", images, command (mark_called, &called));
  });
  actor.join ();

  QTRY_VERIFY_WITH_TIMEOUT (called.load (std::memory_order_acquire), 5000);
  QFile answer (output);
  QVERIFY (answer.open (QIODevice::ReadOnly | QIODevice::Text));
  QCOMPARE (answer.readAll (), QByteArray ("continued formula"));
}

int
main (int argc, char** argv) {
  qputenv ("QT_QPA_PLATFORM", "offscreen");
  QApplication app (argc, argv);
  app.setQuitOnLastWindowClosed (false);
  CodexCompletionDialogTest test;
  return QTest::qExec (&test, argc, argv);
}

#include "codex_completion_dialog_test.moc"
