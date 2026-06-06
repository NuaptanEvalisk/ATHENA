/******************************************************************************
* MODULE     : vault_maintenance_pass_orphans.cpp
* DESCRIPTION: Vault maintenance orphan asset collection pass
* COPYRIGHT  : (C) 2026  Felix
******************************************************************************/

#include "ATHENA/Data/vault_maintenance_internal.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_set>
#include <vector>

namespace fs = std::filesystem;

static bool
collect_used_asset_refs_from_document (const fs::path& doc_path,
                                       std::unordered_set<std::string>& used) {
  std::string text;
  if (!read_file_bytes (doc_path, text)) {
    log_error ("failed to read document " + doc_path.string ());
    return false;
  }

  size_t cursor = 0;
  while (true) {
    size_t pos = text.find ("<image|", cursor);
    if (pos == std::string::npos) break;

    ImageRef ref;
    if (!parse_image_ref_at (text, pos, ref)) {
      cursor = pos + 1;
      continue;
    }

    std::string unescaped = tm_unescape_path (ref.raw_path);
    if (is_probably_local_path (unescaped)) {
      fs::path resolved = resolve_reference_path (doc_path, unescaped);
      if (is_image_extension (resolved)) used.insert (path_key (resolved));
    }
    cursor = ref.end;
  }

  return true;
}

static fs::path
next_orphan_directory (const fs::path& root) {
  fs::path candidate = root / "orphan";
  if (!fs::exists (candidate)) return candidate;
  for (int i=1; ; i++) {
    candidate = root / ("orphan (" + std::to_string (i) + ")");
    if (!fs::exists (candidate)) return candidate;
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
collect_orphan_assets (const fs::path& root, size_t& moved, fs::path& orphan_dir) {
  moved = 0;
  orphan_dir.clear ();

  std::vector<fs::path> docs = scan_ath_documents (root);
  log_info ("orphan assets: scanning " + std::to_string (docs.size ()) +
            " .ath files for asset references");

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
  std::vector<fs::path> orphans;
  for (const fs::path& asset : assets)
    if (used_assets.find (path_key (asset)) == used_assets.end ())
      orphans.push_back (asset);

  log_info ("orphan assets: found " + std::to_string (orphans.size ()) +
            " orphan asset(s)");
  if (orphans.empty ()) return true;

  orphan_dir = next_orphan_directory (root);
  std::error_code ec;
  fs::create_directories (orphan_dir, ec);
  if (ec) {
    log_error ("failed to create orphan asset directory " +
               orphan_dir.string () + ": " + ec.message ());
    return false;
  }

  std::ofstream manifest (orphan_dir / "orphans.lst",
                          std::ios::binary | std::ios::trunc);
  if (!manifest) {
    log_error ("failed to create orphan manifest " +
               (orphan_dir / "orphans.lst").string ());
    return false;
  }
  manifest << "Renamed orphan\tOriginal full path\n";

  for (size_t i=0; i<orphans.size (); i++) {
    fs::path source = orphans[i];
    std::string name = "orphan-" + std::to_string (i + 1) +
                       canonical_extension (source);
    fs::path target = orphan_dir / name;
    print_progress (i + 1, orphans.size (), "Collecting orphans",
                    source.filename ().string ());
    manifest << name << "\t" << path_key (source) << "\n";
    if (!move_or_copy_file (source, target)) {
      finish_progress ();
      log_error ("failed to move orphan asset " + source.string () +
                 " -> " + target.string ());
      return false;
    }
    moved++;
  }
  finish_progress ();
  manifest.close ();
  if (!manifest) {
    log_error ("failed to finalize orphan manifest " +
               (orphan_dir / "orphans.lst").string ());
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
                             ctx.summary.orphan_dir))
    return VaultMaintenancePassResult::success ();
  return VaultMaintenancePassResult::failure ("orphan asset collection failed");
}
