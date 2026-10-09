/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
// Standalone adapter test: link hodarium_resources, confined_filesystem,
// document_history_store (+ its existing delta dependencies) and athena_hodarium.
// The two UI-publication stubs below are intentionally local to this executable.
#include "ATHENA/Data/hodarium_resources.hpp"
#include "ATHENA/Data/background_workers.hpp"
#include <QCryptographicHash>
#include <QTemporaryDir>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <mutex>

bool headless_mode= true;
void athena_spdlog_error (const std::string& message) { std::cerr << message << '\n'; }
std::uint64_t published_buffer_actor_id (const std::string&) { return 0; }
std::recursive_mutex& document_publication_mutex () {
  static std::recursive_mutex value; return value;
}
// File History's only XML-upgrade dependency is the standard SHA-256 primitive.
// Supply that primitive here without linking the editor/Guile document loader.
namespace athena::document {
std::string storage_bytes_fingerprint (std::string_view bytes) {
  return QCryptographicHash::hash (QByteArrayView (bytes.data (), qsizetype (bytes.size ())),
    QCryptographicHash::Sha256).toHex ().toStdString ();
}
}
namespace {
using namespace athena::hodarium;
namespace fs= std::filesystem;
void require (bool condition, const char* message) {
  if (!condition) throw std::runtime_error (message);
}
template<typename F> void rejects (F f) {
  try { f (); } catch (const std::exception&) { return; }
  throw std::runtime_error ("Invalid resource operation was accepted");
}
void write (const fs::path& path, const std::string& bytes) {
  std::ofstream out (path, std::ios::binary);
  out.write (bytes.data (), bytes.size ());
  require (bool (out), "Cannot write temporary resource");
}
void check_watch (const fs::path& root) {
  fs::create_directory (root);
  write (root / "source.ath", "original");
  athena::background::source_watch sources;
  sources.directory (root);
  auto before= sources.revision ();
  require (sources.revision () == before, "Polling changed a stable source revision");
  write (root / "rag.sqlite-wal", "irrelevant");
  require (sources.revision () == before, "Database sidecar invalidated source watch");
  write (root / "source.ath", "in-place change");
  auto changed= sources.revision ();
  require (changed != before && sources.revision () == changed, "In-place source write not observed stably");
  athena::background::source_watch resources ([] (const fs::path& path) {
    return resource_path_kind (path.generic_string ()).has_value ();
  });
  resources.directory (root);
  before= resources.revision ();
  write (root / "new.pdf", "attachment");
  changed= resources.revision ();
  require (changed != before && resources.revision () == changed, "Custom resource creation was not observed");
  fs::remove (root / "new.pdf");
  auto removed= resources.revision ();
  require (removed != changed && resources.revision () == removed, "Deletion revision is not stable");
}
std::string hash (const std::string& bytes) {
  return QCryptographicHash::hash (QByteArray::fromStdString (bytes),
    QCryptographicHash::Sha256).toHex ().toStdString ();
}
void exercise () {
  QTemporaryDir temporary;
  require (temporary.isValid (), "Cannot create isolated test directory");
  const fs::path base (temporary.path ().toStdString ());
  check_watch (base / "watching");
  const auto left= base / "left", right= base / "right";
  fs::create_directories (left / "Courses"); fs::create_directories (right);
  fs::create_directories (left / ".athena");
  write (left / ".athena" / "hidden.pdf", "internal");
  write (left / "artifacts.db-journal", "temporary");
  write (left / "Courses" / "scan.pdf", std::string ("opaque attachment\0bytes", 23));
  const std::string namespace_id= "12345678-1234-4234-8234-123456789abc";
  auto avd= nlohmann::json{{"format", "athena-avd"}, {"version", 1},
    {"namespace", namespace_id}, {"vault", left.string ()},
    {"counter_cache", {{"version", 1}, {"entries", nlohmann::json::array ()}}}};
  const auto original= avd.dump (2);
  write (left / "Courses" / "Lectures.avd", original);
  const auto projected= resource_payload (left, "Courses/Lectures.avd", original);
  require (projected.find (left.string ()) == std::string::npos &&
    projected.find ("counter_cache") == std::string::npos, "AVD leaked local state");
  require (nlohmann::json::parse (projected).at ("vault") == "..", "AVD is not portable");
  avd["vault"]= right.string ();
  rejects ([&] { resource_payload (left, "Courses/Lectures.avd", avd.dump ()); });
  rejects ([&] { resource_payload (left, "Courses/Lectures.avd", "{\"format\":1,\"format\":2}"); });
  require (!resource_path_kind (".backup/x.pdf") && !resource_path_kind ("Courses/../x.pdf") &&
    !resource_path_kind ("Vaultfile.json") && !resource_path_kind ("rag.sqlite-wal"),
    "Resource path policy includes internal state");
  const resource_attachment_paths declared {"Materials/book.epub", "Materials/scan.djvu",
    "Materials/paper.docx", "Materials/no-extension", ".athena/private", "Materials/id_ed25519",
    "Materials/keys.pem", "Materials/artifacts.db-wal", "Courses/unsafe.ath", "../outside"};
  require (!resource_path_kind ("Materials/book.epub") &&
    resource_path_kind ("Materials/book.epub", declared) == resource_kind::attachment &&
    resource_path_kind ("Materials/no-extension", declared) == resource_kind::attachment &&
    !resource_path_kind ("Materials/unregistered.docx", declared), "Material attachment authorization failed");
  for (const auto& forbidden: {".athena/private", "Materials/id_ed25519", "Materials/keys.pem",
      "Materials/artifacts.db-wal", "Courses/unsafe.ath", "../outside"})
    require (!resource_path_kind (forbidden, declared), "Material registration authorized protected data");
  const std::atomic<bool> cancelled {false};
  auto inventory= inventory_resources (left, cancelled);
  require (inventory.complete && inventory.files.size () == 2, "Unexpected resource inventory");
  revision_store source (base / "source.sqlite"), target (base / "target.sqlite");
  const std::string vault= "test-vault", member= "test-member";
  auto avd_file= *std::find_if (inventory.files.begin (), inventory.files.end (),
    [] (const auto& file) { return file.kind == resource_kind::avd; });
  auto first_id= capture_resource (left, source, vault, member, avd_file);
  require (first_id.has_value (), "Initial AVD capture failed");
  auto first= *source.get (*first_id);
  const auto clone= base / "clone";
  fs::create_directories (clone / "Courses");
  write (clone / "Courses" / "Lectures.avd", projected);
  revision_store imported (base / "imported.sqlite");
  auto clone_inventory= inventory_resources (clone, cancelled);
  auto clone_id= capture_resource (clone, imported, vault, "another-device", clone_inventory.files.front ());
  require (clone_id == first_id, "Identical independently bound replicas diverged at genesis");
  require (first.object == initial_resource_identity (vault, avd_file.path) &&
    first.object != initial_resource_identity ("another-vault", avd_file.path),
    "Resource genesis identity has incorrect Vault scope");
  require (capture_resource (left, source, vault, member, avd_file, first) == first_id,
    "Unchanged resource created a revision");
  require (applied_resources (source, vault).at (avd_file.path).object == first.object,
    "Resource identity not reconstructed from journal");
  athena::history::document_history_store history;
  std::string error;
  require (history.open (right, error), "Cannot open isolated history");
  auto permitted= [] (resource_kind, const std::string&) { return true; };
  target.receive (first);
  require (apply_closed_resource (right, target, history, first.id, permitted), "Initial AVD apply failed");
  // A local disposable cache must not become a source edit or block the next
  // definition update. History nevertheless retains those exact original bytes.
  auto local= nlohmann::json::parse (projected);
  local["counter_cache"]= {{"version", 99}};
  const auto cached= local.dump (2);
  write (right / avd_file.path, cached);
  auto changed= first;
  changed.parents= {first.id};
  local.erase ("counter_cache");
  local["namespace"]= "22345678-1234-4234-8234-123456789abc";
  changed.payload= local.dump () + "\n"; changed= seal_revision (changed);
  write (clone / "Courses" / "Lectures.avd", changed.payload);
  revision_store independent (":memory:");
  auto differing= inventory_resources (clone, cancelled);
  auto different_id= capture_resource (clone, independent, vault, "third-device", differing.files.front ());
  require (different_id != first_id && independent.get (*different_id)->object == first.object,
    "Different initial replicas did not become branches of the same resource");
  validate_resource_revision (right, changed);
  target.receive (changed);
  auto lease= hold_resource_view (right / avd_file.path);
  require (!apply_closed_resource (right, target, history, changed.id, permitted),
    "Resource replacement ignored a live AVD view");
  lease.reset ();
  require (apply_closed_resource (right, target, history, changed.id, permitted), "AVD update failed");
  std::vector<athena::history::version_entry> versions;
  require (history.list (avd_file.path, versions, error) && versions.size () == 1 &&
    versions.front ().protected_snapshot, "Destructive resource apply lacks protected history");
  std::string restored;
  require (history.reconstruct (versions.front ().id, restored, error) && restored == cached,
    "History lost raw AVD cache bytes");
  auto renamed= changed;
  renamed.relative_path= "Courses/Renamed.avd"; renamed.parents= {changed.id};
  renamed= seal_revision (renamed); target.receive (renamed);
  // Recovery after the new destination has been durably installed but before
  // removal of the old path, using the real journal intent/history contract.
  apply_intent intent;
  intent.operation= "hodarium-resource-" + renamed.id;
  intent.revision_id= renamed.id; intent.expected_revision= changed.id;
  intent.source_path= changed.relative_path; intent.source_fingerprint= hash (changed.payload);
  require (history.protect (changed.relative_path, changed.payload, intent.operation,
    intent.protected_history_version, error), "Cannot protect rename preimage");
  target.prepare_apply (intent);
  athena::filesystem::confined_root disk (right);
  disk.create (renamed.relative_path, renamed.payload);
  require (apply_closed_resource (right, target, history, renamed.id, permitted), "Interrupted rename did not resume");
  require (!fs::exists (right / changed.relative_path) && fs::exists (right / renamed.relative_path),
    "Rename left duplicate files");
  auto tombstone= renamed; tombstone.parents= {renamed.id}; tombstone.payload.clear ();
  tombstone.deleted= true; tombstone= seal_revision (tombstone); target.receive (tombstone);
  require (!apply_closed_resource (right, target, history, tombstone.id, {}), "Unauthorized delete applied");
  require (apply_closed_resource (right, target, history, tombstone.id, permitted), "Resource deletion failed");
  require (!fs::exists (right / renamed.relative_path), "Deleted resource still exists");
  fs::create_directory (left / "Materials");
  write (left / "Materials" / "no-extension", std::string ("PK\0opaque", 9));
  auto registered_inventory= inventory_resources (left, cancelled, 100000, nullptr, declared);
  const auto registered_file= *std::find_if (registered_inventory.files.begin (), registered_inventory.files.end (),
    [] (const auto& file) { return file.path == "Materials/no-extension"; });
  auto registered_id= capture_resource (left, source, vault, member, registered_file, {}, {}, declared);
  const auto registered= *source.get (*registered_id);
  target.receive (registered);
  rejects ([&] { validate_resource_revision (right, registered); });
  validate_resource_revision (right, registered, declared);
  require (apply_closed_resource (right, target, history, registered.id, permitted, declared),
    "Explicitly registered extensionless Material did not apply");
  fs::create_symlink (right, left / "Escape");
  auto unsafe= inventory_resources (left, cancelled);
  require (!unsafe.complete && !unsafe.errors.empty (), "Inventory followed a symlink");
}
}
int main () {
  try { exercise (); std::cout << "Hodarium resources passed\n"; return 0; }
  catch (const std::exception& e) { std::cerr << e.what () << '\n'; return 1; }
}
