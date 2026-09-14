/******************************************************************************
* MODULE     : vault_maintenance_dialog_test.cpp
* DESCRIPTION: GUI ownership and cancellation of vault maintenance setup
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include <QApplication>
#include <QDialog>
#include <QTemporaryDir>
#include <QTableWidget>
#include <QTimer>
#include <QtTest/QtTest>
#include <thread>
#include "QTMVaultMaintenanceDialog.hpp"
#include "ATHENA/Data/vaultfile_json.hpp"
#include "boot.hpp"
#include "data_cache.hpp"
#include "gui.hpp"
#include "scheme.hpp"
#include "server.hpp"

bool headless_mode= true;
bool is_headless () { return true; }

class VaultMaintenanceDialogTest: public QObject {
  Q_OBJECT
private slots:
  void dispatchLeavesCallerBeforeInvokingScheme () {
    eval ("(begin (define maintenance-calls 0)"
          "(define (vault-maintenance-interactive)"
          "  (set! maintenance-calls (+ maintenance-calls 1))))");
    // Exercise the generated binding as well as the native worker entry.
    call ("vault-maintenance-start");
    QCOMPARE (as_int (eval ("maintenance-calls")), 0);
    QTRY_COMPARE (as_int (eval ("maintenance-calls")), 1);

    std::thread worker ([] { qtm_vault_maintenance_start (); });
    worker.join ();
    QCOMPARE (as_int (eval ("maintenance-calls")), 1);
    QTRY_COMPARE (as_int (eval ("maintenance-calls")), 2);
  }

  void cancelSetupInTemporaryVault () {
    QTemporaryDir vault;
    QVERIFY (vault.isValid ());
    std::string error;
    QVERIFY2 (athena_vaultfile_write (vault.path ().toStdString (),
      AthenaVaultfileInfo {}, error), error.c_str ());
    bool shown= false;
    bool gui_owned= false;
    QTimer::singleShot (0, [&] {
      auto* dialog= qobject_cast<QDialog*> (QApplication::activeModalWidget ());
      if (dialog != nullptr) {
        auto* table= dialog->findChild<QTableWidget*> ();
        shown= dialog->windowTitle () == "Maintenance Setup" &&
               table != nullptr && table->rowCount () > 0;
        gui_owned= dialog->thread () == qApp->thread ();
        dialog->reject ();
      }
    });
    tree result= as_tree (call ("vault-maintenance-setup",
      url_system (string (vault.path ().toUtf8 ().constData ()))));
    QVERIFY (shown);
    QVERIFY (gui_owned);
    QCOMPARE (result, tree (UNINIT));
  }
};

static void
run_tests (int argc, char** argv) {
  init_system_state ();
  gui_open (argc, argv);
  int result;
  {
    server sv;
    VaultMaintenanceDialogTest test;
    result= QTest::qExec (&test, argc, argv);
  }
  gui_close ();
  release_boot_lock ();
  std::exit (result);
}

int main (int argc, char** argv) {
  QApplication app (argc, argv);
  QTemporaryDir profile;
  if (!profile.isValid ()) return 1;
  qputenv ("ATHENA_HOME_PATH", profile.path ().toUtf8 ());
  cache_initialize ();
  init_athena ();
  start_scheme (argc, argv, run_tests);
  return 1;
}

#include "vault_maintenance_dialog_test.moc"
