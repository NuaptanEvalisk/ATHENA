/******************************************************************************
* MODULE     : vault_maintenance_assets_test.cpp
* DESCRIPTION: Tests for structural Vault asset maintenance
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
*******************************************************************************/

#include <QtTest/QtTest>
#include <QTemporaryDir>

#include "ATHENA/Data/vault_file_references.hpp"
#include "ATHENA/Data/vault_maintenance_internal.hpp"
#include "ATHENA/Data/vault_maintenance_passes.hpp"
#include "ATHENA/Data/vaultfile_json.hpp"
#include "convert.hpp"

#include <filesystem>
#include <fstream>
#include <unordered_set>

bool headless_mode= true;
bool is_headless () { return true; }

namespace fs = std::filesystem;

namespace {

tree
reference_node (const std::string& kind, const std::string& path) {
  tree node (make_tree_label (kind.c_str ()));
  if (kind == "hlink" || kind == "cardlink")
    node << tree ("display") << tree (path.c_str ());
  else node << tree (path.c_str ());
  return node;
}

tree
document_with (const std::vector<tree>& nodes) {
  tree body (DOCUMENT);
  for (const tree& node: nodes) body << node;
  tree document (DOCUMENT);
  document << compound ("TeXmacs", "2.1.4")
           << compound ("style", tuple ("generic"))
           << compound ("body", body);
  return document;
}

bool
write_document (const fs::path& path, tree document) {
  string serialized= tree_to_texmacs (document);
  std::ofstream out (path, std::ios::binary);
  out.write (as_charp (serialized), N(serialized));
  return (bool) out;
}

tree
read_document (const fs::path& path) {
  std::ifstream in (path, std::ios::binary);
  std::string text ((std::istreambuf_iterator<char> (in)), {});
  return texmacs_document_to_tree (string (text.data (), (int) text.size ()));
}

void
touch (const fs::path& path, const std::string& contents= "asset") {
  std::ofstream out (path, std::ios::binary);
  out << contents;
}

} // namespace

class TestVaultMaintenanceAssets: public QObject {
  Q_OBJECT

private slots:
  void normalizesEveryStructuralAssetKind ();
  void preservesReferenceFormsAndDocumentTargets ();
  void protectsConfiguredVaultInfrastructure ();
  void reportsMissingLocalImagesOnly ();
  void orphanCollectionUsesStructuralReferences ();
  void parseFailureLeavesAssetsUntouched ();
};

void
TestVaultMaintenanceAssets::normalizesEveryStructuralAssetKind () {
  QTemporaryDir temporary;
  QVERIFY (temporary.isValid ());
  fs::path root (temporary.path ().toStdString ());
  fs::create_directories (root / "assets");

  const std::vector<std::pair<std::string, std::string>> cases= {
    {"image", "picture.png"}, {"cardlink", "report.pdf"},
    {"hlink", "notes.txt"}, {"cardlink", "extensionless"},
    {"include", "fragment.dat"},
    {"sound", "audio.bin"}, {"video", "movie.xyz"},
    {"animation", "motion.tar.gz"}};
  std::vector<tree> nodes;
  for (const auto& item: cases) {
    touch (root / "assets" / item.second);
    nodes.push_back (reference_node (item.first, "assets/" + item.second));
  }
  touch (root / "assets/unreferenced.pdf");
  // The same PDF reference appearing twice must produce one rename and two
  // structurally consistent updates.
  nodes.push_back (reference_node ("hlink", "assets/report.pdf"));
  QVERIFY (write_document (root / "Note.ath", document_with (nodes)));

  VaultMaintenanceContext context;
  context.root= root;
  VaultMaintenancePassResult result=
    vault_maintenance_pass_normalize_assets (context);
  QVERIFY2 (result.ok, result.message.c_str ());
  QCOMPARE (context.summary.asset_renames, cases.size () + 1);
  QCOMPARE (context.summary.asset_reference_updates, cases.size () + 1);

  tree rewritten= read_document (root / "Note.ath");
  QVERIFY (!is_func (rewritten, _ERROR));
  std::vector<AthenaVaultFileReference> references;
  athena_vault_collect_file_references (rewritten, root / "Note.ath",
                                        references);
  QCOMPARE (references.size (), cases.size () + 1);
  for (const AthenaVaultFileReference& reference: references) {
    QVERIFY (fs::exists (reference.resolved_path));
    QVERIFY (has_canonical_asset_name (reference.resolved_path));
    QVERIFY (reference.resolved_path.filename ().string ().rfind (
               "asset-", 0) == 0);
  }
  bool kept_compound_extension= false;
  for (const AthenaVaultFileReference& reference: references)
    if (ends_with (reference.resolved_path.filename ().string (), ".tar.gz"))
      kept_compound_extension= true;
  QVERIFY (kept_compound_extension);
  for (const auto& item: cases)
    QVERIFY (!fs::exists (root / "assets" / item.second));
  QVERIFY (!fs::exists (root / "assets/unreferenced.pdf"));

  VaultMaintenanceContext second_context;
  second_context.root= root;
  VaultMaintenancePassResult second=
    vault_maintenance_pass_normalize_assets (second_context);
  QVERIFY2 (second.ok, second.message.c_str ());
  QCOMPARE (second_context.summary.asset_renames, (size_t) 0);
  QCOMPARE (second_context.summary.asset_reference_updates, (size_t) 0);
}

void
TestVaultMaintenanceAssets::preservesReferenceFormsAndDocumentTargets () {
  QTemporaryDir temporary;
  QVERIFY (temporary.isValid ());
  fs::path root (temporary.path ().toStdString ());
  fs::create_directories (root / "assets");
  fs::path asset= root / "assets/data.txt";
  touch (asset);
  const std::string figure=
    "figure-33333333-3333-4333-8333-333333333333.png";
  touch (root / "assets" / figure);
  QVERIFY (write_document (root / "Other.ath", document_with ({})));
  QVERIFY (write_document (
    root / "Note.ath",
    document_with ({
      reference_node ("hlink", "./assets/data.txt"),
      reference_node ("hlink", asset.generic_string ()),
      reference_node ("hlink", "file://" + asset.generic_string ()),
      reference_node ("image", "assets/" + figure),
      reference_node ("cardlink", "Other.ath"),
      reference_node ("hlink", "https://example.com/data.txt")})));

  VaultMaintenanceContext context;
  context.root= root;
  VaultMaintenancePassResult result=
    vault_maintenance_pass_normalize_assets (context);
  QVERIFY2 (result.ok, result.message.c_str ());
  QCOMPARE (context.summary.asset_renames, (size_t) 1);
  QCOMPARE (context.summary.asset_reference_updates, (size_t) 3);
  QVERIFY (fs::exists (root / "Other.ath"));

  std::vector<AthenaVaultFileReference> references;
  athena_vault_collect_file_references (
    read_document (root / "Note.ath"), root / "Note.ath", references);
  QCOMPARE (references.size (), (size_t) 5);
  QVERIFY (references[0].value.rfind ("./assets/asset-", 0) == 0);
  QVERIFY (fs::path (references[1].value).is_absolute ());
  QVERIFY (references[2].value.rfind ("file://", 0) == 0);
  QCOMPARE (references[3].value, std::string ("assets/") + figure);
  QCOMPARE (references[4].value, std::string ("Other.ath"));
  QVERIFY (references[0].resolved_path == references[1].resolved_path);
  QVERIFY (references[1].resolved_path == references[2].resolved_path);
  QVERIFY (fs::exists (root / "assets" / figure));
}

void
TestVaultMaintenanceAssets::protectsConfiguredVaultInfrastructure () {
  QTemporaryDir temporary;
  QVERIFY (temporary.isValid ());
  fs::path root (temporary.path ().toStdString ());
  fs::create_directories (root / "state");
  AthenaVaultfileInfo info;
  info.map_path=
    "state/asset-44444444-4444-4444-8444-444444444444.db";
  info.preferences_path= "state/preferences.txt";
  std::string error;
  QVERIFY2 (athena_vaultfile_write (root, info, error), error.c_str ());
  touch (root / info.map_path);
  touch (root / info.preferences_path);
  QVERIFY (write_document (
    root / "Note.ath",
    document_with ({reference_node ("cardlink", info.preferences_path)})));

  VaultMaintenanceContext context;
  context.root= root;
  VaultMaintenancePassResult normalize=
    vault_maintenance_pass_normalize_assets (context);
  QVERIFY2 (normalize.ok, normalize.message.c_str ());
  QCOMPARE (context.summary.asset_renames, (size_t) 0);
  QVERIFY (fs::exists (root / info.preferences_path));

  context.summary.orphan_collection_enabled= true;
  VaultMaintenancePassResult orphans=
    vault_maintenance_pass_collect_orphans (context);
  QVERIFY2 (orphans.ok, orphans.message.c_str ());
  QCOMPARE (context.summary.orphan_assets_collected, (size_t) 0);
  QVERIFY (fs::exists (root / info.map_path));
}

void
TestVaultMaintenanceAssets::reportsMissingLocalImagesOnly () {
  QTemporaryDir temporary;
  QVERIFY (temporary.isValid ());
  fs::path root (temporary.path ().toStdString ());
  fs::create_directories (root / "assets");
  touch (root / "assets/present.png");
  QVERIFY (write_document (
    root / "Note.ath",
    document_with ({
      reference_node ("image", "assets/present.png"),
      reference_node ("image", "assets/missing.png"),
      reference_node ("image", "assets/missing.png"),
      reference_node ("image", "https://example.com/remote.png"),
      reference_node ("hlink", "assets/missing-link.png")})));

  VaultMaintenanceContext context;
  context.root= root;
  VaultMaintenancePassResult result=
    vault_maintenance_pass_scan_missing_images (context);
  QVERIFY2 (result.ok, result.message.c_str ());
  QVERIFY (context.summary.missing_image_scan_enabled);
  QCOMPARE (context.summary.missing_image_files_scanned, (size_t) 1);
  QCOMPARE (context.summary.local_image_references_scanned, (size_t) 3);
  QCOMPARE (context.summary.missing_images.size (), (size_t) 1);
  QCOMPARE (context.summary.missing_images[0].document_path,
            root / "Note.ath");
  QCOMPARE (context.summary.missing_images[0].reference,
            std::string ("assets/missing.png"));
  QCOMPARE (context.summary.missing_images[0].resolved_path,
            athena_vault_normalized_path (root / "assets/missing.png"));
  QCOMPARE (context.warnings.size (), (size_t) 1);
}

void
TestVaultMaintenanceAssets::orphanCollectionUsesStructuralReferences () {
  QTemporaryDir temporary;
  QVERIFY (temporary.isValid ());
  fs::path root (temporary.path ().toStdString ());
  const std::string kept_name=
    "asset-11111111-1111-4111-8111-111111111111.txt";
  const std::string orphan_name=
    "asset-22222222-2222-4222-8222-222222222222.dat";
  touch (root / kept_name);
  touch (root / orphan_name);
  touch (root / "legacy.png");
  touch (root / "ordinary-unmanaged.txt");
  QVERIFY (write_document (
    root / "Note.ath",
    document_with ({reference_node ("cardlink", kept_name)})));

  VaultMaintenanceContext context;
  context.root= root;
  context.summary.orphan_collection_enabled= true;
  VaultMaintenancePassResult result=
    vault_maintenance_pass_collect_orphans (context);
  QVERIFY2 (result.ok, result.message.c_str ());
  QCOMPARE (context.summary.orphan_assets_collected, (size_t) 2);
  QVERIFY (fs::exists (root / kept_name));
  QVERIFY (fs::exists (root / "ordinary-unmanaged.txt"));
  QVERIFY (!fs::exists (root / orphan_name));
  QVERIFY (!fs::exists (root / "legacy.png"));
  QVERIFY (fs::exists (context.summary.orphan_dir / "orphans.lst"));
}

void
TestVaultMaintenanceAssets::parseFailureLeavesAssetsUntouched () {
  QTemporaryDir temporary;
  QVERIFY (temporary.isValid ());
  fs::path root (temporary.path ().toStdString ());
  touch (root / "report.pdf");
  QVERIFY (write_document (
    root / "A.ath",
    document_with ({reference_node ("cardlink", "report.pdf")})));
  touch (root / "Z.ath", "not an ATHENA document");

  VaultMaintenanceContext context;
  context.root= root;
  VaultMaintenancePassResult result=
    vault_maintenance_pass_normalize_assets (context);
  QVERIFY (!result.ok);
  QVERIFY (fs::exists (root / "report.pdf"));
  size_t canonical_count= 0;
  for (const fs::directory_entry& entry: fs::directory_iterator (root))
    if (has_canonical_asset_name (entry.path ())) ++canonical_count;
  QCOMPARE (canonical_count, (size_t) 0);
}

QTEST_MAIN (TestVaultMaintenanceAssets)
#include "vault_maintenance_assets_test.moc"
