/******************************************************************************
* MODULE     : global_transformation.hpp
* DESCRIPTION: Transactional Scheme-driven transformations over a Vault
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
*******************************************************************************/

#ifndef ATHENA_GLOBAL_TRANSFORMATION_HPP
#define ATHENA_GLOBAL_TRANSFORMATION_HPP

#include "tree.hpp"

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

struct AthenaGlobalTransformationRewrite {
  std::filesystem::path path;
  std::filesystem::path relative_path;
  std::string original;
  std::string serialized;
  tree transformed;
};

struct AthenaGlobalTransformationPlan {
  std::filesystem::path root;
  std::filesystem::path backup_root;
  size_t scanned= 0;
  std::vector<AthenaGlobalTransformationRewrite> rewrites;
};

using AthenaGlobalTransformationCallback= std::function<bool (
  const std::string& relative_path, const tree& input, tree& output,
  std::string& error)>;

using AthenaGlobalTransformationProgress= std::function<bool (
  size_t current, size_t total, const std::string& relative_path)>;

bool athena_global_transformation_prepare (
  const std::filesystem::path& root,
  const AthenaGlobalTransformationCallback& transform,
  const AthenaGlobalTransformationProgress& progress,
  AthenaGlobalTransformationPlan& plan, std::string& error);

bool athena_global_transformation_commit (
  AthenaGlobalTransformationPlan& plan, std::string& error);

#endif // ATHENA_GLOBAL_TRANSFORMATION_HPP
