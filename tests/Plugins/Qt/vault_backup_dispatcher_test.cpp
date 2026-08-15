/******************************************************************************
* MODULE     : vault_backup_dispatcher_test.cpp
* DESCRIPTION: Tests for vault backup dispatcher configuration and mirroring
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
******************************************************************************/

#include <QtTest/QtTest>
#include <QFile>
#include <QStandardPaths>
#include <QTemporaryDir>

#include "ATHENA/Data/vault_backup_dispatcher.hpp"
#include "ATHENA/Data/vault_maintenance_passes.hpp"
#include "ATHENA/Data/vaultfile_json.hpp"

#include <filesystem>
#include <fstream>

bool headless_mode= true;
bool is_headless () { return true; }

namespace fs= std::filesystem;

namespace {

void
write_text (const fs::path& path, const std::string& text) {
  fs::create_directories (path.parent_path ());
  std::ofstream out (path, std::ios::binary | std::ios::trunc);
  out << text;
}

std::string
read_text (const fs::path& path) {
  std::ifstream in (path, std::ios::binary);
  return std::string ((std::istreambuf_iterator<char> (in)), {});
}

} // namespace

class TestVaultBackupDispatcher: public QObject {
  Q_OBJECT

private slots:
  void vaultfileRoundTrip ();
  void mirrorsIncrementallyAndHonorsExclusions ();
  void rejectsOverlappingLocalDestinations ();
  void maintenanceRunsOnlyMatchingDispatchers ();
};

void
TestVaultBackupDispatcher::vaultfileRoundTrip () {
  QTemporaryDir temporary;
  QVERIFY (temporary.isValid ());
  fs::path root (temporary.path ().toStdString ());

  AthenaVaultfileInfo info;
  info.name= "Dispatch test";
  info.backup_dispatchers= {
    {"/backup/one", "realtime"},
    {"host:/srv/athena", "maintenance"},
    {"rsync://host/module/vault", "idle"}};
  std::string error;
  QVERIFY2 (athena_vaultfile_write (root, info, error), error.c_str ());

  AthenaVaultfileInfo loaded;
  QVERIFY2 (athena_vaultfile_read (root, loaded, error), error.c_str ());
  QCOMPARE (loaded.backup_dispatchers.size (), (size_t) 3);
  QCOMPARE (loaded.backup_dispatchers[0].destination, std::string ("/backup/one"));
  QCOMPARE (loaded.backup_dispatchers[0].trigger, std::string ("realtime"));
  QCOMPARE (loaded.backup_dispatchers[1].trigger, std::string ("maintenance"));
  QCOMPARE (loaded.backup_dispatchers[2].trigger, std::string ("idle"));
}

void
TestVaultBackupDispatcher::mirrorsIncrementallyAndHonorsExclusions () {
  if (QStandardPaths::findExecutable ("rsync").isEmpty ())
    QSKIP ("rsync is not installed");
  QTemporaryDir temporary;
  QVERIFY (temporary.isValid ());
  fs::path base (temporary.path ().toStdString ());
  fs::path root= base / "vault";
  fs::path destination= base / "mirror";
  fs::create_directories (root);
  write_text (root / "Note.ath", "first");
  write_text (root / ".backup/full.tar.zst", "excluded");
  write_text (root / ".athena/rag-backup-123/patch.sqlite", "excluded");
  write_text (root / ".athena/kept.json", "kept");
  write_text (destination / "stale.txt", "stale");

  std::string error;
  QVERIFY2 (athena_backup_dispatch_run (
              root, destination.string (), error), error.c_str ());
  QCOMPARE (read_text (destination / "Note.ath"), std::string ("first"));
  QCOMPARE (read_text (destination / ".athena/kept.json"),
            std::string ("kept"));
  QVERIFY (!fs::exists (destination / ".backup"));
  QVERIFY (!fs::exists (destination / ".athena/rag-backup-123"));
  QVERIFY (!fs::exists (destination / "stale.txt"));

  write_text (root / "Note.ath", "second");
  write_text (root / "new.txt", "new");
  QVERIFY2 (athena_backup_dispatch_run (
              root, destination.string (), error), error.c_str ());
  QCOMPARE (read_text (destination / "Note.ath"), std::string ("second"));
  QCOMPARE (read_text (destination / "new.txt"), std::string ("new"));
}

void
TestVaultBackupDispatcher::rejectsOverlappingLocalDestinations () {
  QTemporaryDir temporary;
  QVERIFY (temporary.isValid ());
  fs::path base (temporary.path ().toStdString ());
  fs::path root= base / "vault";
  fs::create_directories (root / "inside");
  AthenaBackupDispatchCommand command;
  std::string error;
  QVERIFY (!athena_backup_dispatch_prepare (
    root, root.string (), command, error));
  QVERIFY (!athena_backup_dispatch_prepare (
    root, (root / "inside").string (), command, error));
  QVERIFY (!athena_backup_dispatch_prepare (
    root, base.string (), command, error));
  QVERIFY (!athena_backup_dispatch_prepare (
    root, "relative-backup", command, error));
  QVERIFY (QString::fromStdString (error).contains ("absolute path"));
}

void
TestVaultBackupDispatcher::maintenanceRunsOnlyMatchingDispatchers () {
  if (QStandardPaths::findExecutable ("rsync").isEmpty ())
    QSKIP ("rsync is not installed");
  QTemporaryDir temporary;
  QVERIFY (temporary.isValid ());
  fs::path base (temporary.path ().toStdString ());
  fs::path root= base / "vault";
  fs::path maintenance_destination= base / "maintenance-mirror";
  fs::path realtime_destination= base / "realtime-mirror";
  fs::create_directories (root);
  write_text (root / "Note.ath", "content");

  AthenaVaultfileInfo info;
  info.backup_dispatchers= {
    {maintenance_destination.string (), "maintenance"},
    {realtime_destination.string (), "realtime"}};
  std::string error;
  QVERIFY2 (athena_vaultfile_write (root, info, error), error.c_str ());

  VaultMaintenanceContext context;
  context.root= root;
  VaultMaintenancePassResult result=
    vault_maintenance_pass_dispatch_backups (context);
  QVERIFY2 (result.ok, result.message.c_str ());
  QCOMPARE (context.summary.backup_dispatchers_run, (size_t) 1);
  QCOMPARE (read_text (maintenance_destination / "Note.ath"),
            std::string ("content"));
  QVERIFY (!fs::exists (realtime_destination));
}

QTEST_MAIN (TestVaultBackupDispatcher)
#include "vault_backup_dispatcher_test.moc"
