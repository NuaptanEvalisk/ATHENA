#include "aofm_import_vault_internal.hpp"

namespace aofm_import_vault_internal {

const AofmVaultAssetInfo*
find_asset_by_rel_path(const AssetIndexMap& asset_map,
                       const std::string& rel_path) {
  auto it = asset_map.find(asset_key(rel_path));
  return it == asset_map.end() ? nullptr : &it->second;
}

const AofmVaultAssetInfo*
contains_asset_file(const AssetIndexMap& asset_map,
                    const std::string& dir,
                    const std::string& link_name) {
  return find_asset_by_rel_path(asset_map, join_rel_paths(dir, link_name));
}

const AofmVaultAssetInfo*
bfs_search_asset(const AssetIndexMap& asset_map,
                 const DirChildrenMap& dir_children,
                 const std::string& link_name,
                 const std::string& start_dir) {
  std::vector<std::string> queue;
  queue.push_back(start_dir);
  size_t pos = 0;

  while (pos < queue.size()) {
    std::string dir = queue[pos++];
    const AofmVaultAssetInfo* asset =
        contains_asset_file(asset_map, dir, link_name);
    if (asset != nullptr) return asset;

    auto it = dir_children.find(dir);
    if (it == dir_children.end()) continue;
    for (const std::string& child : it->second) queue.push_back(child);
  }

  return nullptr;
}

const AofmVaultAssetInfo*
resolve_asset_target(const std::string& target,
                     const std::string& rel_ath_path,
                     const AssetIndexMap& asset_map,
                     const DirChildrenMap& dir_children) {
  std::string link_name = trim_copy(target);
  if (link_name.empty()) return nullptr;

  std::string current_dir = path_dirname(rel_ath_path);
  bool absolute = !link_name.empty() && link_name[0] == '/';
  link_name = strip_leading_slash(link_name);

  if (absolute) {
    return find_asset_by_rel_path(asset_map, link_name);
  }

  const AofmVaultAssetInfo* forward =
      bfs_search_asset(asset_map, dir_children, link_name, current_dir);
  if (forward != nullptr) return forward;

  std::string prev = current_dir;
  std::string curr;
  bool has_curr = parent_dir_of(current_dir, curr);
  while (has_curr) {
    const AofmVaultAssetInfo* parent_asset =
        contains_asset_file(asset_map, curr, link_name);
    if (parent_asset != nullptr) return parent_asset;

    auto it = dir_children.find(curr);
    if (it != dir_children.end()) {
      for (const std::string& sibling : it->second) {
        if (sibling == prev) continue;
        const AofmVaultAssetInfo* asset =
            bfs_search_asset(asset_map, dir_children, link_name, sibling);
        if (asset != nullptr) return asset;
      }
    }

    prev = curr;
    std::string parent;
    has_curr = parent_dir_of(curr, parent);
    curr = parent;
  }

  return nullptr;
}

tree
make_image_embed(const std::string& image_path, const std::string& width) {
  tree image = compound("image", text_tree(image_path), text_tree(width),
                        text_tree(""), text_tree(""), text_tree(""));
  return compound("big-figure", image, text_tree(""));
}

tree
make_pdf_embed(const std::string& target) {
  return compound("cardlink", text_tree(""), text_tree(target));
}

const AofmVaultHeadingInfo*
find_heading_info_by_label(const HeadingMap& heading_map,
                           const std::string& rel_ath_path,
                           const std::string& label) {
  for (const auto& entry : heading_map) {
    const AofmVaultHeadingInfo& info = entry.second;
    if (info.path == rel_ath_path && info.label == label &&
        !info.end_label.empty() && info.level > 0) {
      return &info;
    }
  }
  return nullptr;
}

void
close_heading_ranges(tree& out,
                     std::vector<const AofmVaultHeadingInfo*>& open_headings,
                     int min_level) {
  (void) out;
  while (!open_headings.empty() &&
         open_headings.back()->level >= min_level) {
    open_headings.pop_back();
  }
}

tree
resolve_anchor_placeholders(const tree& t, const AnchorMap& anchor_map,
                            const AnchorOccurrenceMap& anchor_occurrences,
                            const FileIndexMap& file_map,
                            const HeadingMap& heading_map,
                            const HeadingOccurrenceMap& heading_occurrences,
                            const AssetIndexMap& asset_map,
                            const DirChildrenMap& dir_children,
                            const std::string& rel_ath_path,
                            AnchorOccurrenceCursor* anchor_cursor,
                            HeadingOccurrenceCursor* heading_cursor) {
  AnchorOccurrenceCursor local_anchor_cursor;
  if (anchor_cursor == nullptr) anchor_cursor = &local_anchor_cursor;
  HeadingOccurrenceCursor local_heading_cursor;
  if (heading_cursor == nullptr) heading_cursor = &local_heading_cursor;

  auto next_anchor_occurrence = [&](const std::string& anchor)
      -> const AofmVaultAnchorInfo* {
    std::string occurrence_key = anchor_occurrence_key(rel_ath_path, anchor);
    auto occurrence_it = anchor_occurrences.find(occurrence_key);
    if (occurrence_it != anchor_occurrences.end() &&
        !occurrence_it->second.empty()) {
      size_t& pos = (*anchor_cursor)[occurrence_key];
      if (pos < occurrence_it->second.size()) {
        return &occurrence_it->second[pos++];
      }
      return &occurrence_it->second.back();
    }

    auto it = anchor_map.find(anchor);
    return it == anchor_map.end() ? nullptr : &it->second;
  };

  auto next_heading_occurrence = [&](const std::string& label)
      -> const AofmVaultHeadingInfo* {
    std::string occurrence_key = heading_occurrence_key(rel_ath_path, label);
    auto occurrence_it = heading_occurrences.find(occurrence_key);
    if (occurrence_it != heading_occurrences.end() &&
        !occurrence_it->second.empty()) {
      size_t& pos = (*heading_cursor)[occurrence_key];
      if (pos >= occurrence_it->second.size()) pos = occurrence_it->second.size() - 1;
      const std::string& map_key = occurrence_it->second[pos++];
      auto info_it = heading_map.find(map_key);
      return info_it == heading_map.end() ? nullptr : &info_it->second;
    }
    return find_heading_info_by_label(heading_map, rel_ath_path, label);
  };

  if (is_aofm_anchor_inline_placeholder(t)) {
    std::string anchor = placeholder_anchor_id(t);
    const AofmVaultAnchorInfo* info = next_anchor_occurrence(anchor);
    if (info == nullptr) {
      report_import_error("anchor '^" + anchor + "' not found while converting " +
                          rel_ath_path);
      return materialize_anchor_literal(t);
    }
    return make_label_tree(info->anchor_1);
  }

  if (is_aofm_anchor_block_placeholder(t)) {
    std::string anchor = placeholder_anchor_id(t);
    const AofmVaultAnchorInfo* info = next_anchor_occurrence(anchor);
    if (info == nullptr) {
      report_import_error("anchor '^" + anchor + "' not found while converting " +
                          rel_ath_path);
      return materialize_anchor_literal(t);
    }
    return make_label_tree(info->anchor_1);
  }

  if (is_aofm_image_placeholder(t)) {
    std::string target = tree_to_std_string(t[0]);
    std::string width = tree_to_std_string(t[1]);
    const AofmVaultAssetInfo* asset =
        resolve_asset_target(target, rel_ath_path, asset_map, dir_children);
    if (asset == nullptr) {
      report_import_warning("image '" + target + "' not found while converting " +
                            rel_ath_path);
      return text_tree("![[" + target + (width.empty() ? "" : "|" + width) + "]]");
    }

    std::string image_path =
        relative_path_from_dir(path_dirname(rel_ath_path), asset->relative_path);
    std::string athena_width =
        width.empty() ? "0.8par" : aofm::obsidian_image_width_to_athena_length(width);
    if (athena_width.empty()) athena_width = "0.8par";
    return make_image_embed(image_path, athena_width);
  }

  if (is_aofm_wikilink_placeholder(t) || is_aofm_transclusion_placeholder(t)) {
    bool is_trans = is_aofm_transclusion_placeholder(t);
    std::string target = tree_to_std_string(t[0]);
    std::string sub = tree_to_std_string(t[1]);
    std::string alias = tree_to_std_string(t[2]);

    std::string uuid, file_hint, anchor_hint, display;
    std::string target_stem = target.empty() ? path_stem(rel_ath_path) : path_stem(target);
    if (!target.empty() && is_pdf_target(target)) {
      const AofmVaultAssetInfo* asset =
          resolve_asset_target(target, rel_ath_path, asset_map, dir_children);
      if (asset == nullptr) {
        report_import_warning("pdf '" + target + "' not found while converting " +
                              rel_ath_path);
        return text_tree((is_trans ? "!" : "") + std::string("[[") + target + "]]");
      }
      std::string pdf_path =
          relative_path_from_dir(path_dirname(rel_ath_path), asset->relative_path);
      return make_pdf_embed(pdf_path);
    }

    if (is_trans && sub.empty()) {
      if (is_image_target(target)) {
        const AofmVaultAssetInfo* asset =
            resolve_asset_target(target, rel_ath_path, asset_map, dir_children);
        if (asset == nullptr) {
          report_import_warning("image '" + target + "' not found while converting " +
                                rel_ath_path);
          return text_tree("![[" + target + "]]");
        }
        std::string image_path =
            relative_path_from_dir(path_dirname(rel_ath_path), asset->relative_path);
        return make_image_embed(image_path, "0.8par");
      }
    }

    if (target.empty()) {
      // Local link
      file_hint = path_stem(rel_ath_path);
    }
    else {
      auto it = file_map.find(target_stem);
      if (it != file_map.end()) {
        uuid = it->second.uuid;
        file_hint = it->second.stem;
      }
      else {
        file_hint = target;
      }
    }

    if (!sub.empty()) {
      auto it = anchor_map.find(sub);
      if (it != anchor_map.end()) {
        if (is_trans && !it->second.anchor_2.empty()) {
          uuid = it->second.transclusion_uuid;
          anchor_hint = it->second.anchor_2;
        }
        else {
          uuid = it->second.uuid;
          anchor_hint = it->second.anchor_1;
        }
      }
      else {
        auto h_it = heading_map.find(heading_map_key(target_stem, sub));
        if (h_it == heading_map.end()) {
          h_it = heading_map.find(normalized_heading_map_key(target_stem, sub));
        }
        if (h_it != heading_map.end()) {
          uuid = (is_trans && !h_it->second.transclusion_uuid.empty()) ?
                 h_it->second.transclusion_uuid : h_it->second.uuid;
          anchor_hint = h_it->second.label;
        }
        else {
          anchor_hint = sub;
        }
      }
    }

    if (is_trans) {
      std::string anchor_begin;
      if (!sub.empty()) {
        auto it = anchor_map.find(sub);
        if (it != anchor_map.end() && !it->second.anchor_2.empty()) {
          anchor_begin = it->second.anchor_1;
        }
      }
      return compound("transclude", text_tree(uuid), text_tree(file_hint),
                      text_tree(anchor_begin), text_tree(anchor_hint));
    }

    if (!alias.empty()) {
      display = alias;
    }
    else {
      display = target;
      if (!sub.empty()) {
        if (!display.empty()) display += "#";
        display += sub;
      }
    }

    std::string hlink = make_wikilink_url(uuid, file_hint, anchor_hint);
    return compound("hlink", text_tree(display), text_tree(hlink));
  }

  if (is_atomic(t)) return t;

  if (is_document(t)) {
    tree out(DOCUMENT);
    std::vector<const AofmVaultHeadingInfo*> open_headings;
    std::unordered_map<std::string,int> auto_proof_label_counts;
    for (int i = 0; i < N(t); ++i) {
      const tree& child = t[i];
      if (is_anchor_placeholder_tree(child)) {
        std::string anchor = placeholder_anchor_id(child);
        const AofmVaultAnchorInfo* info = next_anchor_occurrence(anchor);
        if (info == nullptr) {
          report_import_error("anchor '^" + anchor + "' not found while converting " +
                              rel_ath_path);
          append_document(out, materialize_anchor_literal(child));
          continue;
        }

        if (!info->anchor_2.empty() &&
            N(out) > 1 &&
            is_heading_tree(out[N(out) - 1]) &&
            is_label_tree_with(out[N(out) - 2], info->anchor_1)) {
          continue;
        }

        if (!info->anchor_2.empty() && N(out) > 0) {
          tree previous = out[N(out) - 1];
          out[N(out) - 1] = make_label_tree(info->anchor_1);
          append_document(out, previous);
          append_document(out, make_label_tree(info->anchor_2));

          // Dual-Wrap logic for separated proofs
          if (can_own_separated_proof_tree(previous) &&
              i + 1 < N(t) && is_separated_proof_tree(t[i + 1])) {
            std::string t_label1 = info->anchor_1;
            std::string t_label2 = info->anchor_2;
            std::string proof_prefix = separated_proof_label_prefix(t[i + 1]);

            auto derive_proof_label = [&](const std::string& l) {
              size_t colon = l.find(':');
              std::string suffix = (colon == std::string::npos ? l : l.substr(colon + 1));
              // Ensure we remove the trailing ' {' or ' }' if they are part of the string
              size_t space = suffix.find_last_not_of(" {}");
              if (space != std::string::npos) suffix = suffix.substr(0, space + 1);
              return proof_prefix + ":" + suffix;
            };

            append_document(out, make_label_tree(derive_proof_label(t_label1) + " {"));
            append_document(out, resolve_anchor_placeholders(t[i+1], anchor_map,
                                                            anchor_occurrences,
                                                            file_map, heading_map,
                                                            heading_occurrences,
                                                            asset_map, dir_children,
                                                            rel_ath_path,
                                                            anchor_cursor,
                                                            heading_cursor));
            append_document(out, make_label_tree(derive_proof_label(t_label2) + " }"));

            i++; // Consume the proof node
          }
          continue;
        }

        if (info->anchor_2.empty() &&
            N(out) > 0 &&
            is_label_tree_with(out[N(out) - 1], info->anchor_1)) {
          continue;
        }
        if (info->anchor_2.empty() &&
            N(out) > 1 &&
            is_heading_tree(out[N(out) - 1]) &&
            is_label_tree_with(out[N(out) - 2], info->anchor_1)) {
          continue;
        }

        append_document(out, make_label_tree(info->anchor_1));
        continue;
      }

      if (is_separated_proof_tree(child) &&
          (i + 1 >= N(t) || !is_anchor_placeholder_tree(t[i + 1]))) {
        std::string id = auto_separated_proof_anchor_id(child);
        int count = auto_proof_label_counts[id]++;
        if (count > 0) id += " " + std::to_string(count);
        append_document(out, make_label_tree(id + " {"));
        append_document(out,
                        resolve_anchor_placeholders(child, anchor_map,
                                                    anchor_occurrences, file_map,
                                                    heading_map,
                                                    heading_occurrences,
                                                    asset_map, dir_children,
                                                    rel_ath_path,
                                                    anchor_cursor,
                                                    heading_cursor));
        append_document(out, make_label_tree(id + " }"));
        continue;
      }

      if (is_compound(child, "label", 1)) {
        std::string label = tree_to_std_string(child[0]);
        const AofmVaultHeadingInfo* info =
            next_heading_occurrence(label);
        if (info != nullptr) {
          close_heading_ranges(out, open_headings, info->level);
          append_document(out, make_label_tree(info->label));
          open_headings.push_back(info);
          continue;
        }
      }

      append_document(out,
                      resolve_anchor_placeholders(child, anchor_map,
                                                  anchor_occurrences, file_map,
                                                  heading_map,
                                                  heading_occurrences,
                                                  asset_map, dir_children,
                                                  rel_ath_path,
                                                  anchor_cursor,
                                                  heading_cursor));
    }
    close_heading_ranges(out, open_headings, 0);
    return out;
  }

  tree out(t, N(t));
  for (int i = 0; i < N(t); ++i) {
    out[i] = resolve_anchor_placeholders(t[i], anchor_map, anchor_occurrences,
                                         file_map, heading_map,
                                         heading_occurrences, asset_map,
                                         dir_children, rel_ath_path,
                                         anchor_cursor, heading_cursor);
  }
  return out;
}


} // namespace aofm_import_vault_internal
