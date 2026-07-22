/******************************************************************************
* MODULE     : vault_maintenance_pass_orphans.cpp
* DESCRIPTION: Vault maintenance orphan asset collection pass
* COPYRIGHT  : (C) 2026  Felix
******************************************************************************/

#include "ATHENA/Data/vault_maintenance_internal.hpp"
#include "ATHENA/Data/vault_file_references.hpp"

#include "convert.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_set>
#include <vector>

namespace fs = std::filesystem;

static const char* orphan_manifest_name = "orphans.lst";

static bool
collect_used_asset_refs_from_document (const fs::path& doc_path,
                                       std::unordered_set<std::string>& used) {
  std::string text;
  if (!read_file_bytes (doc_path, text)) {
    log_error ("failed to read document " + doc_path.string ());
    return false;
  }

  tree document;
  try { document= texmacs_document_to_tree (std_to_tm (text)); }
  catch (...) { document= tree (_ERROR, "parse failed"); }
  if (is_func (document, _ERROR)) {
    log_error ("failed to parse document while collecting used assets: " +
               doc_path.string ());
    return false;
  }

  std::vector<AthenaVaultFileReference> references;
  athena_vault_collect_file_references (document, doc_path, references);
  for (const AthenaVaultFileReference& reference: references)
    used.insert (reference.resolved_path.generic_string ());

  return true;
}

static bool
existing_orphan_collection (const fs::path& dir) {
  std::error_code ec;
  return fs::is_directory (dir, ec) &&
         fs::is_regular_file (dir / orphan_manifest_name, ec);
}

static fs::path
choose_orphan_directory (const fs::path& root, bool& append) {
  fs::path candidate = root / "orphan";
  append = false;
  if (existing_orphan_collection (candidate)) {
    append = true;
    return candidate;
  }
  if (!fs::exists (candidate)) return candidate;
  for (int i=1; ; i++) {
    candidate = root / ("orphan (" + std::to_string (i) + ")");
    if (existing_orphan_collection (candidate)) {
      append = true;
      return candidate;
    }
    if (!fs::exists (candidate)) return candidate;
  }
}

static std::string
next_orphan_name (const fs::path& orphan_dir, size_t& index,
                  const fs::path& source) {
  std::string ext = canonical_extension (source);
  while (true) {
    std::string name = "orphan-" + std::to_string (index++) + ext;
    if (!fs::exists (orphan_dir / name)) return name;
  }
}

static bool
move_or_copy_file (const fs::path& from, const fs::path& to) {
  std::error_code ec;
  fs::rename (from, to, ec);
  if (!ec) return true;

  ec.clear ();
  fs::copy_file (from, to, fs::copy_options::none, ec);
  if (ec) return false;
  fs::remove (from, ec);
  return !ec;
}

static bool
collect_orphan_assets (const fs::path& root, size_t& moved,
                       fs::path& orphan_dir,
                       std::vector<VaultMaintenanceCollectedOrphan>& collected) {
  moved = 0;
  orphan_dir.clear ();
  collected.clear ();

  std::vector<fs::path> docs = scan_documents (root);
  log_info ("orphan assets: scanning " + std::to_string (docs.size ()) +
            " document files for structural asset references");

  std::unordered_set<std::string> used_assets;
  for (size_t i=0; i<docs.size (); i++) {
    print_progress (i + 1, docs.size (), "Scanning asset references",
                    docs[i].filename ().string ());
    if (!collect_used_asset_refs_from_document (docs[i], used_assets)) {
      finish_progress ();
      return false;
    }
  }
  finish_progress ();

  std::vector<fs::path> assets = scan_asset_files (root);
  std::unordered_set<std::string> infrastructure;
  std::string infrastructure_error;
  if (!collect_vault_infrastructure_paths (
        root, infrastructure, infrastructure_error)) {
    log_error ("could not read Vaultfile infrastructure paths: " +
               infrastructure_error);
    return false;
  }
  std::vector<fs::path> orphans;
  for (const fs::path& asset : assets)
    if (infrastructure.find (path_key (asset)) == infrastructure.end () &&
        used_assets.find (path_key (asset)) == used_assets.end ())
      orphans.push_back (asset);

  log_info ("orphan assets: found " + std::to_string (orphans.size ()) +
            " orphan asset(s)");
  if (orphans.empty ()) return true;

  bool append_manifest = false;
  orphan_dir = choose_orphan_directory (root, append_manifest);
  std::error_code ec;
  fs::create_directories (orphan_dir, ec);
  if (ec) {
    log_error ("failed to create orphan asset directory " +
               orphan_dir.string () + ": " + ec.message ());
    return false;
  }

  fs::path manifest_path = orphan_dir / orphan_manifest_name;
  bool write_header = !append_manifest ||
                      !fs::exists (manifest_path) ||
                      fs::file_size (manifest_path, ec) == 0;
  ec.clear ();
  std::ios::openmode mode = std::ios::binary |
                            (append_manifest ? std::ios::app : std::ios::trunc);
  std::ofstream manifest (manifest_path, mode);
  if (!manifest) {
    log_error ("failed to open orphan manifest " + manifest_path.string ());
    return false;
  }
  if (write_header) manifest << "Renamed orphan\tOriginal full path\n";

  size_t orphan_index = 1;
  for (size_t i=0; i<orphans.size (); i++) {
    fs::path source = orphans[i];
    std::string name = next_orphan_name (orphan_dir, orphan_index, source);
    fs::path target = orphan_dir / name;
    print_progress (i + 1, orphans.size (), "Collecting orphans",
                    source.filename ().string ());
    if (!move_or_copy_file (source, target)) {
      finish_progress ();
      log_error ("failed to move orphan asset " + source.string () +
                 " -> " + target.string ());
      return false;
    }
    manifest << name << "\t" << path_key (source) << "\n";
    collected.push_back ({target, source});
    moved++;
  }
  finish_progress ();
  manifest.close ();
  if (!manifest) {
    log_error ("failed to finalize orphan manifest " + manifest_path.string ());
    return false;
  }

  log_info ("orphan assets: collected " + std::to_string (moved) +
            " asset(s) into " + orphan_dir.string ());
  return true;
}


VaultMaintenancePassResult
vault_maintenance_pass_collect_orphans (VaultMaintenanceContext& ctx) {
  if (!ctx.summary.orphan_collection_enabled) {
    log_info ("orphan assets: collection disabled");
    return VaultMaintenancePassResult::success ();
  }
  if (collect_orphan_assets (ctx.root, ctx.summary.orphan_assets_collected,
                             ctx.summary.orphan_dir,
                             ctx.summary.collected_orphans))
    return VaultMaintenancePassResult::success ();
  return VaultMaintenancePassResult::failure ("orphan asset collection failed");
}
