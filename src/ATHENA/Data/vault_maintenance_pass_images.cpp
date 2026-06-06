/******************************************************************************
* MODULE     : vault_maintenance_pass_images.cpp
* DESCRIPTION: Vault maintenance image normalization pass
* COPYRIGHT  : (C) 2026  Felix
******************************************************************************/

#include "ATHENA/Data/vault_maintenance_internal.hpp"

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;

static std::vector<RenamePlan>
build_rename_plan (const std::vector<fs::path>& images) {
  std::vector<RenamePlan> plans;
  std::unordered_set<std::string> reserved;
  plans.reserve (images.size ());

  for (const fs::path& old_path : images) {
    fs::path target;
    do {
      std::string name = "figure-" + generate_uuid_v4 () +
                         canonical_extension (old_path);
      target = old_path.parent_path () / name;
    } while (fs::exists (target) ||
             reserved.find (path_key (target)) != reserved.end ());

    RenamePlan plan;
    plan.old_path = old_path;
    plan.new_path = target;
    plan.old_key = path_key (old_path);
    plan.old_stem = old_path.stem ().string ();
    plan.old_name = old_path.filename ().string ();
    plans.push_back (plan);
    reserved.insert (path_key (target));
  }

  return plans;
}

static bool
rename_images (const std::vector<RenamePlan>& plans) {
  for (size_t i=0; i<plans.size (); i++) {
    const RenamePlan& plan = plans[i];
    print_progress (i + 1, plans.size (), "Renaming", plan.old_name);
    std::error_code ec;
    fs::rename (plan.old_path, plan.new_path, ec);
    if (ec) {
      finish_progress ();
      log_error ("failed to rename " + plan.old_path.string () + " -> " +
                 plan.new_path.string () + ": " + ec.message ());
      return false;
    }
  }
  finish_progress ();
  return true;
}

static bool
rewrite_document_image_refs (
  const fs::path& doc_path,
  const std::unordered_map<std::string, std::vector<size_t>>& plans_by_stem,
  const std::unordered_map<std::string, fs::path>& rename_path_by_old_path,
  size_t& replacements) {
  std::string text;
  if (!read_file_bytes (doc_path, text)) {
    log_error ("failed to read document " + doc_path.string ());
    return false;
  }

  std::string out;
  out.reserve (text.size ());
  size_t cursor = 0;
  bool changed = false;
  size_t document_replacements = 0;

  while (true) {
    size_t pos = text.find ("<image|", cursor);
    if (pos == std::string::npos) break;

    ImageRef ref;
    if (!parse_image_ref_at (text, pos, ref)) {
      cursor = pos + 1;
      continue;
    }

    std::string unescaped = tm_unescape_path (ref.raw_path);
    std::string stem = stem_from_reference (unescaped);
    auto hit = plans_by_stem.find (stem);
    if (hit == plans_by_stem.end () || !is_probably_local_path (unescaped)) {
      cursor = ref.end;
      continue;
    }

    std::string ref_key = path_key (resolve_reference_path (doc_path, unescaped));
    auto key_hit = rename_path_by_old_path.find (ref_key);
    if (key_hit == rename_path_by_old_path.end ()) {
      cursor = ref.end;
      continue;
    }

    std::string new_ref =
      reference_for_replacement (doc_path, key_hit->second, unescaped);
    out.append (text, cursor, ref.begin - cursor);
    out += tm_escape_path (new_ref);
    cursor = ref.end;
    changed = true;
    replacements++;
    document_replacements++;
  }

  if (!changed) return true;
  out.append (text, cursor, std::string::npos);
  if (!write_file_bytes (doc_path, out)) {
    log_error ("failed to write document " + doc_path.string ());
    return false;
  }
  log_info ("updated " + std::to_string (document_replacements) +
            " image references in " + doc_path.string ());
  return true;
}

static bool
rewrite_documents (const fs::path& root, const std::vector<RenamePlan>& plans,
                   size_t& replacements) {
  std::unordered_map<std::string, std::vector<size_t>> plans_by_stem;
  std::unordered_map<std::string, fs::path> rename_path_by_old_path;
  for (size_t i=0; i<plans.size (); i++)
    plans_by_stem[plans[i].old_stem].push_back (i);
  for (size_t i=0; i<plans.size (); i++)
    rename_path_by_old_path[plans[i].old_key] = plans[i].new_path;

  std::vector<fs::path> docs = scan_documents (root);
  log_info ("scanning " + std::to_string (docs.size ()) +
            " document files for image references");

  replacements = 0;
  for (size_t i=0; i<docs.size (); i++) {
    print_progress (i + 1, docs.size (), "Updating references",
                    docs[i].filename ().string ());
    if (!rewrite_document_image_refs (docs[i], plans_by_stem,
                                      rename_path_by_old_path,
                                      replacements)) {
      finish_progress ();
      return false;
    }
  }
  finish_progress ();
  return true;
}


VaultMaintenancePassResult
vault_maintenance_pass_normalize_images (VaultMaintenanceContext& ctx) {
  std::vector<fs::path> images = scan_noncanonical_images (ctx.root);
  log_info ("found " + std::to_string (images.size ()) +
            " non-canonical image files");

  if (images.empty ()) {
    log_info ("no image normalization needed");
    return VaultMaintenancePassResult::success ();
  }

  std::vector<RenamePlan> plans = build_rename_plan (images);
  log_info ("planned " + std::to_string (plans.size ()) + " image renames");

  if (!rename_images (plans))
    return VaultMaintenancePassResult::failure ("image rename failed");

  size_t replacements = 0;
  if (!rewrite_documents (ctx.root, plans, replacements))
    return VaultMaintenancePassResult::failure ("image reference rewrite failed");
  ctx.summary.image_renames = plans.size ();
  ctx.summary.image_reference_updates = replacements;
  log_info ("updated " + std::to_string (replacements) + " image references");
  return VaultMaintenancePassResult::success ();
}
