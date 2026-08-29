/******************************************************************************
* MODULE     : redundant_wikilinks.hpp
* DESCRIPTION: Remove block wikilinks made redundant by radioactive links
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
******************************************************************************/

#ifndef ATHENA_REDUNDANT_WIKILINKS_HPP
#define ATHENA_REDUNDANT_WIKILINKS_HPP

#include "ATHENA/Data/artifacts.hpp"
#include "ATHENA/Data/vault_map_sqlite.hpp"
#include "tree.hpp"

#include <cstddef>
#include <string>
#include <vector>

struct AthenaRedundantWikilinkDocument {
  std::string relative_path;
  tree document;
  size_t removals= 0;
};

struct AthenaRedundantWikilinkStats {
  size_t files_scanned= 0;
  size_t block_wikilinks_scanned= 0;
  size_t full_text_matches= 0;
  size_t links_removed= 0;
  size_t files_changed= 0;
  size_t unverified_targets= 0;
};

void athena_remove_redundant_block_wikilinks (
  std::vector<AthenaRedundantWikilinkDocument>& documents,
  const std::vector<AthenaVaultMapNode>& map_nodes,
  const std::vector<AthenaArtifactRecord>& artifacts,
  AthenaRedundantWikilinkStats& stats);

#endif // ATHENA_REDUNDANT_WIKILINKS_HPP
