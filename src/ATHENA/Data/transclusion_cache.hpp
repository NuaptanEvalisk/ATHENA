/******************************************************************************
* MODULE     : transclusion_cache.hpp
* DESCRIPTION: Cached structural resolution of Vault transclusions
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
*******************************************************************************/

#ifndef ATHENA_TRANSCLUSION_CACHE_HPP
#define ATHENA_TRANSCLUSION_CACHE_HPP

#include "tree.hpp"

struct AthenaTransclusionResolution {
  tree content;
  string cache_key;
  string source_relative_path;
  bool ok= false;
};

AthenaTransclusionResolution
athena_resolve_transclusion_content (tree transclusion);

tree athena_resolve_transclusion_display (tree transclusion,
                                           string* cache_key= nullptr);

// Shared final presentation contract for resolved transclusions.  Both the
// legacy map-backed path and native node-reference path use this compact frame
// so spacing, background and border behavior cannot drift apart again.
tree athena_transclusion_compact_frame (tree content);

// Structural source range, retaining labels. No shared caches, GUI or Scheme.
// The caller owns the document and the returned tree on its current thread.
tree athena_transclusion_source_range (tree body, string begin, string end);

void athena_clear_transclusion_caches ();

#endif // ATHENA_TRANSCLUSION_CACHE_HPP
