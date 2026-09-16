/******************************************************************************
* MODULE     : vault_anchors.hpp
* DESCRIPTION: Native structural anchors for ATHENA vault documents
*******************************************************************************/

#ifndef ATHENA_VAULT_ANCHORS_HPP
#define ATHENA_VAULT_ANCHORS_HPP

#include "string.hpp"
#include "tree.hpp"
#include "url.hpp"

#include <utility>
#include <vector>

struct VaultAnchorSummary {
  size_t wrapped= 0;
  size_t dead_pairs= 0;
  std::vector<string> notes;
  size_t headings= 0;
  size_t updated= 0;
  std::vector<std::pair<string,string>> renames;

  bool empty () const {
    return wrapped == 0 && dead_pairs == 0 && headings == 0 && updated == 0;
  }
};

struct VaultAnchorTransform {
  tree body;
  VaultAnchorSummary summary;
};

bool vault_anchor_heading (tree t);
void vault_anchor_title_filter_invalidate ();
VaultAnchorSummary vault_anchor_plan (tree body);
VaultAnchorTransform vault_anchor_transform (tree body);
VaultAnchorTransform vault_anchor_transform_document (tree document);
string vault_anchor_renames_string (const VaultAnchorSummary& summary);
string vault_anchor_maintenance_file_native (url file, bool check_only);

#endif // ATHENA_VAULT_ANCHORS_HPP
