/******************************************************************************
* MODULE     : vault_backup_test.cpp
* DESCRIPTION: Tests for stable pre-save Vault histories
* COPYRIGHT  : (C) 2026  Felix
*******************************************************************************/

#include <QtTest/QtTest>
#include <QTemporaryDir>

#include "ATHENA/Data/vault_backup.hpp"
#include "ATHENA/Data/vault.hpp"

#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>
#include <zstd.h>

namespace fs = std::filesystem;

static bool
write_file (const fs::path& path, const std::string& contents) {
  std::ofstream out (path, std::ios::binary | std::ios::trunc);
  out.write (contents.data (), (std::streamsize) contents.size ());
  return out.good ();
}

static bool
decompress_file (const fs::path& path, std::string& contents) {
  std::ifstream in (path, std::ios::binary);
  if (!in) return false;
  ZSTD_DCtx* context= ZSTD_createDCtx ();
  if (context == nullptr) return false;
  std::vector<char> input (ZSTD_DStreamInSize ());
  std::vector<char> output (ZSTD_DStreamOutSize ());
  size_t remaining= 1;
  bool ok= true;
  contents.clear ();
  while (ok && in && remaining != 0) {
    in.read (input.data (), (std::streamsize) input.size ());
    ZSTD_inBuffer source= {input.data (), (size_t) in.gcount (), 0};
    if (source.size == 0) break;
    while (source.pos < source.size) {
      ZSTD_outBuffer target= {output.data (), output.size (), 0};
      remaining= ZSTD_decompressStream (context, &target, &source);
      if (ZSTD_isError (remaining)) {
        ok= false;
        break;
      }
      contents.append (output.data (), target.pos);
    }
  }
  ZSTD_freeDCtx (context);
  return ok && remaining == 0;
}

class TestVaultBackup: public QObject {
  Q_OBJECT

private slots:
  void publishesCompleteDistinctPreSaveHistories ();
};

void
TestVaultBackup::publishesCompleteDistinctPreSaveHistories () {
  QTemporaryDir temporary;
  QVERIFY (temporary.isValid ());
  fs::path root= temporary.path ().toStdString ();
  fs::path document= root / "Document.ath";

  is_vault_active= true;
  current_vault.root= url_system (string (root.string ().c_str ()));
  QVERIFY (write_file (document, "first persisted version"));
  QVERIFY (vault_backup_pre_save (
    url_system (string (document.string ().c_str ()))));
  QVERIFY (write_file (document, "second persisted version"));
  QVERIFY (vault_backup_pre_save (
    url_system (string (document.string ().c_str ()))));
  is_vault_active= false;
  current_vault= vault_info ();

  std::set<std::string> histories;
  size_t partials= 0;
  fs::path history_root= root / ".backup" / "manual-save";
  for (const fs::directory_entry& entry:
       fs::recursive_directory_iterator (history_root)) {
    if (!entry.is_regular_file ()) continue;
    if (entry.path ().extension () == ".partial") partials++;
    if (entry.path ().extension () != ".zst") continue;
    std::string contents;
    QVERIFY (decompress_file (entry.path (), contents));
    histories.insert (contents);
  }

  QCOMPARE (partials, (size_t) 0);
  QCOMPARE (histories.size (), (size_t) 2);
  QVERIFY (histories.count ("first persisted version") == 1);
  QVERIFY (histories.count ("second persisted version") == 1);
}

QTEST_MAIN (TestVaultBackup)
#include "vault_backup_test.moc"
