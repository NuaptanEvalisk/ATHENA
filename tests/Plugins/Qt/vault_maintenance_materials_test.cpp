/******************************************************************************
* MODULE     : vault_maintenance_materials_test.cpp
* DESCRIPTION: Tests for Material attachment Vault maintenance
* COPYRIGHT  : (C) 2026  Felix
*******************************************************************************/

#include <QtTest/QtTest>

#include "ATHENA/Data/materials.hpp"
#include "ATHENA/Data/vault_maintenance_passes.hpp"
#include "ATHENA/Data/vaultfile_json.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace {

bool
write_bytes (const fs::path& path, const std::string& bytes) {
  std::ofstream out (path, std::ios::binary | std::ios::trunc);
  out.write (bytes.data (), (std::streamsize) bytes.size ());
  return (bool) out;
}

std::string
read_bytes (const fs::path& path) {
  std::ifstream in (path, std::ios::binary);
  return std::string ((std::istreambuf_iterator<char> (in)), {});
}

MaterialRecord
material (const std::string& title, const std::string& family) {
  MaterialRecord result;
  result.item_type= "book";
  result.fields= {{"title", title, "", 0}, {"date", "2026", "", 0}};
  result.creators= {{"author", "", family, "", "", 0}};
  return result;
}

} // namespace

class VaultMaintenanceMaterialsTest: public QObject {
  Q_OBJECT

private slots:
  void skipsVaultWithoutMaterialsDatabase ();
  void canonicalizesPurgesAndReportsMissingAttachments ();
};

void
VaultMaintenanceMaterialsTest::skipsVaultWithoutMaterialsDatabase () {
  QTemporaryDir temporary;
  QVERIFY (temporary.isValid ());
  VaultMaintenanceContext context;
  context.root= fs::u8path (temporary.path ().toStdString ());
  AthenaVaultfileInfo info;
  std::string error;
  QVERIFY2 (athena_vaultfile_write (context.root, info, error), error.c_str ());

  VaultMaintenancePassResult result=
    vault_maintenance_pass_maintain_materials (context);
  QVERIFY2 (result.ok, result.message.c_str ());
  QCOMPARE (result.message, std::string ("no Materials database"));
  QVERIFY (!context.summary.materials_database_present);
  QVERIFY (!fs::exists (context.root / "materials.sqlite"));
}

void
VaultMaintenanceMaterialsTest::canonicalizesPurgesAndReportsMissingAttachments () {
  QTemporaryDir temporary;
  QVERIFY (temporary.isValid ());
  fs::path root= fs::u8path (temporary.path ().toStdString ());
  AthenaVaultfileInfo info;
  std::string error;
  QVERIFY2 (athena_vaultfile_write (root, info, error), error.c_str ());

  MaterialsStore store;
  QVERIFY2 (store.open (root, info, error), error.c_str ());

  MaterialRecord renamed= material ("Old title", "Old author");
  QVERIFY2 (store.create (renamed, error), error.c_str ());
  fs::path renamed_source= root / "renamed-source.pdf";
  QVERIFY (write_bytes (renamed_source, "%PDF renamed\n"));
  MaterialImportResult renamed_import;
  QVERIFY2 (store.import_file (renamed.uuid, renamed_source, "document", true,
                               renamed_import, error), error.c_str ());
  std::optional<MaterialRecord> renamed_record= store.get (renamed.uuid, error);
  QVERIFY2 (renamed_record.has_value (), error.c_str ());
  auto title= std::find_if (
    renamed_record->fields.begin (), renamed_record->fields.end (),
    [] (const MaterialField& field) { return field.name == "title"; });
  QVERIFY (title != renamed_record->fields.end ());
  title->value= "Canonical title";
  renamed_record->creators[0].family= "Canonical author";
  QVERIFY2 (store.update (*renamed_record, renamed_record->revision, error),
            error.c_str ());

  MaterialRecord missing= material ("Missing", "Attachment");
  QVERIFY2 (store.create (missing, error), error.c_str ());
  fs::path missing_source= root / "missing-source.pdf";
  QVERIFY (write_bytes (missing_source, "%PDF missing\n"));
  MaterialImportResult missing_import;
  QVERIFY2 (store.import_file (missing.uuid, missing_source, "document", true,
                               missing_import, error), error.c_str ());
  fs::path missing_path= root / missing_import.attachment.stored_path;
  QVERIFY (fs::remove (missing_path));

  fs::path orphan= store.materials_directory () / "unreferenced.bin";
  QVERIFY (write_bytes (orphan, "unreferenced"));
  store.close ();

  VaultMaintenanceContext context;
  context.root= root;
  VaultMaintenancePassResult result=
    vault_maintenance_pass_maintain_materials (context);
  QVERIFY2 (result.ok, result.message.c_str ());
  QVERIFY (context.summary.materials_database_present);
  QCOMPARE (context.summary.material_attachments_renamed, (size_t) 1);
  QCOMPARE (context.summary.material_attachments_unchanged, (size_t) 0);
  QCOMPARE (context.summary.material_attachments_missing, (size_t) 1);
  QCOMPARE (context.summary.material_files_purged, (size_t) 1);
  QCOMPARE (context.summary.missing_material_attachments.size (), (size_t) 1);
  QCOMPARE (context.summary.missing_material_attachments[0], missing_path);
  QVERIFY (!fs::exists (orphan));
  QCOMPARE (context.warnings.size (), (size_t) 1);

  context.vault_name= "Test Vault";
  context.summary.generate_summary_page= true;
  context.summary.summary_dir= "maintenance";
  QVERIFY (vault_maintenance_write_summary_page (context, true));
  QVERIFY (fs::is_regular_file (context.summary.summary_file));
  const std::string summary= read_bytes (context.summary.summary_file);
  QVERIFY (summary.find ("renamed 1 attachment(s), unchanged 0, missing 1, "
                         "purged 1 unreferenced file(s)") !=
           std::string::npos);
  QVERIFY (summary.find ("Missing Material Attachments") != std::string::npos);
  QVERIFY (summary.find (missing_path.generic_string ()) != std::string::npos);

  QVERIFY2 (store.open (root, info, error), error.c_str ());
  std::vector<MaterialAttachment> attachments=
    store.attachments (renamed.uuid, error);
  QVERIFY2 (error.empty (), error.c_str ());
  QCOMPARE (attachments.size (), (size_t) 1);
  QCOMPARE (attachments[0].canonical_name,
            std::string ("Canonical author - 2026 - Canonical title.pdf"));
  QVERIFY (fs::exists (root / attachments[0].stored_path));
}

QTEST_MAIN (VaultMaintenanceMaterialsTest)
#include "vault_maintenance_materials_test.moc"
