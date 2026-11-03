/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#pragma once
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

// Notify local observers only after commit. Desktop observers invalidate the
// live artifact cache; headless callers require no editor services.
bool athena_artifacts_apply_path_rename (
  const std::filesystem::path& vault_root, const std::string& old_path,
  const std::string& new_path, bool is_directory, std::string& error);

// Native artifact-owner boundary. The source UUID and role are authoritative
// document bindings; this record contains only a reusable model result. Its
// model/input hash is recomputed by the existing extractor before consumption.
struct HodariumArtifactRangeResult {
  std::string artifact_uuid, source_uuid, source_role;
  std::string input_hash, structure_hash, content_hash;
  std::vector<int> offsets;
};
namespace athena::hodarium {
struct logical_database_object;
logical_database_object encode_artifact_range_result (const HodariumArtifactRangeResult&);
}
bool athena_artifacts_export_range_results (
  const std::filesystem::path& root, const std::string& after_uuid,
  unsigned limit, std::vector<HodariumArtifactRangeResult>& results,
  std::string& error);
bool athena_artifacts_import_range_result (
  const std::filesystem::path& root, const HodariumArtifactRangeResult& result,
  bool& applicable, bool& changed, std::string& error,
  const std::function<bool()>& permitted= {});
