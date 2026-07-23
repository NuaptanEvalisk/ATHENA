/******************************************************************************
* MODULE     : vault_maintenance_persons_test.cpp
* DESCRIPTION: Tests for semantic person normalization during maintenance
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
*******************************************************************************/

#include <QtTest/QtTest>
#include <QTemporaryDir>

#include "ATHENA/Data/person_names.hpp"
#include "ATHENA/Data/vault_maintenance_internal.hpp"
#include "ATHENA/Data/vault_maintenance_passes.hpp"
#include "convert.hpp"
#include "drd_std.hpp"

#include <filesystem>
#include <fstream>

bool headless_mode= true;
bool is_headless () { return true; }

namespace fs = std::filesystem;

namespace {

tree
person_test_document (tree body) {
  tree document (DOCUMENT);
  document << compound ("TeXmacs", "2.1.4")
           << compound ("style", tuple ("generic"))
           << compound ("body", body);
  return document;
}

bool
write_person_test_document (const fs::path& path, tree document) {
  string serialized= tree_to_texmacs (document);
  std::ofstream out (path, std::ios::binary);
  out.write (as_charp (serialized), N(serialized));
  return (bool) out;
}

tree
read_person_test_document (const fs::path& path) {
  std::ifstream in (path, std::ios::binary);
  std::string text ((std::istreambuf_iterator<char> (in)), {});
  return texmacs_document_to_tree (string (text.data (), (int) text.size ()));
}

} // namespace

class TestVaultMaintenancePersons: public QObject {
  Q_OBJECT

private slots:
  void initTestCase ();
  void normalizesKnownAndVaultTaggedNames ();
};

void
TestVaultMaintenancePersons::initTestCase () {
  init_std_drd ();
}

void
TestVaultMaintenancePersons::normalizesKnownAndVaultTaggedNames () {
  QTemporaryDir temporary;
  QVERIFY (temporary.isValid ());
  fs::path root (temporary.path ().toStdString ());

  QVERIFY (write_person_test_document (
    root / "Tagged.ath",
    person_test_document (
      tree (DOCUMENT, compound ("person", "Zyxwvu Exampleperson")))));
  QVERIFY (write_person_test_document (
    root / "Plain.ath",
    person_test_document (
      tree (DOCUMENT, "Emmy Noether and Zyxwvu Exampleperson"))));

  tree before= read_person_test_document (root / "Plain.ath");
  QVERIFY2 (!is_func (before, _ERROR),
            tm_to_std (tree_to_scheme (before)).c_str ());
  tree body= extract (before, "body");
  int direct_wrapped= 0;
  tree normalized_body=
    athena_normalize_person_names (body, direct_wrapped);
  QVERIFY2 (normalized_body != body,
            tm_to_std (tree_to_scheme (before)).c_str ());
  QCOMPARE (direct_wrapped, 1);

  VaultMaintenanceContext context;
  context.root= root;
  VaultMaintenancePassResult result=
    vault_maintenance_pass_normalize_person_names (context);
  QVERIFY2 (result.ok, result.message.c_str ());
  QCOMPARE (context.summary.person_files_scanned, (size_t) 2);
  QCOMPARE (context.summary.person_files_changed, (size_t) 1);
  QCOMPARE (context.summary.person_names_wrapped, (size_t) 2);

  tree rewritten= read_person_test_document (root / "Plain.ath");
  std::vector<string> names= athena_collect_person_names (rewritten);
  QCOMPARE (names.size (), (size_t) 2);
  QCOMPARE (names[0], string ("Emmy Noether"));
  QCOMPARE (names[1], string ("Zyxwvu Exampleperson"));

  VaultMaintenanceContext second_context;
  second_context.root= root;
  VaultMaintenancePassResult second=
    vault_maintenance_pass_normalize_person_names (second_context);
  QVERIFY2 (second.ok, second.message.c_str ());
  QCOMPARE (second_context.summary.person_files_changed, (size_t) 0);
  QCOMPARE (second_context.summary.person_names_wrapped, (size_t) 0);
}

QTEST_MAIN (TestVaultMaintenancePersons)
#include "vault_maintenance_persons_test.moc"
