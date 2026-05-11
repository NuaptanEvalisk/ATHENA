#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "convert.hpp"
#include "file.hpp"
#include "tree.hpp"
#include "url.hpp"
#include "vault.hpp"
#include "aofm_telemetry.hpp"

#include <chrono>

#if defined(__unix__) || defined(__APPLE__)
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <poll.h>
#include <thread>
#include <cstring>
#include <csignal>
#endif

#include "aofm_utils.hpp"

using namespace aofm;

namespace {

enum class IpcMsgType { PROGRESS, ERROR_MSG, DONE };

struct IpcTelemetryData {
  double time_parse_latex_doc;
  double time_latex_to_tree;
  double aofm_math_time;
  int aofm_math_count;
  double time_l2t_kill_space;
  double time_l2t_parsed_latex;
  double time_l2t_finalize_doc;
  double time_l2t_handle_matches;
  double time_l2t_upgrade_tex;
  double time_l2t_finalize_misc;
  double time_l2t_drd_correct;
  double time_l2t_style_check;
  double time_l2t_simplify_correct;
  double time_l2t_latex_correct;
  double time_l2t_guess_missing;
  double time_l2t_post_metadata;
  int count_l2t_is_document;
  int count_l2t_total;
};

struct IpcMessage {
  IpcMsgType type;
  union {
    char filename[256];
    IpcTelemetryData telemetry;
  };
};

struct AofmVaultAnchorInfo {
  std::string uuid;
  std::string transclusion_uuid;
  std::string path;
  std::string anchor_1;
  std::string anchor_2;
  std::string hlink_w;
};

enum class BlockKind {
  NONE,
  PARAGRAPH,
  CALLOUT,
  HEADING
};

struct BlockContext {
  BlockKind kind = BlockKind::NONE;
  std::vector<std::string> lines;

  void clear() {
    kind = BlockKind::NONE;
    lines.clear();
  }
};

struct ImportFileInfo {
  url source_url;
  std::string relative_md_path;
  std::string relative_ath_path;
};

struct CalloutHeaderInfo {
  std::string type;
  std::string header_tail;
};

struct AofmVaultFileInfo {
  std::string uuid;
  std::string relative_ath_path;
  std::string stem;
};

struct AofmVaultHeadingInfo {
  std::string uuid;
  std::string transclusion_uuid;
  std::string path;
  std::string label;
  std::string end_label;
  int level = 0;
};

struct OpenHeadingInfo {
  int level;
  std::string key;
  std::string normalized_key;
};

struct AofmVaultAssetInfo {
  std::string relative_path;
};

using AnchorMap = std::unordered_map<std::string, AofmVaultAnchorInfo>;
using FileIndexMap = std::unordered_map<std::string, AofmVaultFileInfo>;
using HeadingMap = std::unordered_map<std::string, AofmVaultHeadingInfo>;
using AssetIndexMap = std::unordered_map<std::string, AofmVaultAssetInfo>;
using DirChildrenMap = std::unordered_map<std::string, std::vector<std::string>>;

std::string
tm_to_std_string(string s) {
  return std::string(as_charp(s));
}

std::string
path_stem(const std::string& path) {
  size_t slash = path.find_last_of("/\\");
  size_t start = (slash == std::string::npos) ? 0 : slash + 1;
  size_t dot = path.find_last_of('.');
  if (dot == std::string::npos || dot < start) return path.substr(start);
  return path.substr(start, dot - start);
}

std::string
path_basename(const std::string& path) {
  size_t slash = path.find_last_of("/\\");
  return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string
path_dirname(const std::string& path) {
  size_t slash = path.find_last_of("/\\");
  return slash == std::string::npos ? "" : path.substr(0, slash);
}

std::string
join_rel_paths(const std::string& left, const std::string& right) {
  if (left.empty()) return right;
  if (right.empty()) return left;
  return left + "/" + right;
}

std::vector<std::string>
split_rel_path(const std::string& path) {
  std::vector<std::string> parts;
  size_t start = 0;
  while (start <= path.size()) {
    size_t slash = path.find('/', start);
    std::string part = path.substr(start, slash == std::string::npos ?
                                           std::string::npos : slash - start);
    if (!part.empty() && part != ".") {
      if (part == ".." && !parts.empty()) parts.pop_back();
      else if (part != "..") parts.push_back(part);
    }
    if (slash == std::string::npos) break;
    start = slash + 1;
  }
  return parts;
}

std::string
join_rel_parts(const std::vector<std::string>& parts, size_t start) {
  std::string out;
  for (size_t i = start; i < parts.size(); ++i) {
    if (!out.empty()) out += "/";
    out += parts[i];
  }
  return out;
}

std::string
normalize_rel_path(const std::string& path) {
  return join_rel_parts(split_rel_path(path), 0);
}

std::string
strip_leading_slash(std::string path) {
  while (!path.empty() && path[0] == '/') path.erase(0, 1);
  return path;
}

std::string
lower_ascii(std::string s) {
  for (char& ch : s) ch = (char) std::tolower((unsigned char) ch);
  return s;
}

std::string
asset_key(const std::string& rel_path) {
  return lower_ascii(normalize_rel_path(strip_leading_slash(rel_path)));
}

bool
parent_dir_of(const std::string& dir, std::string& parent) {
  if (dir.empty()) return false;
  parent = path_dirname(dir);
  return true;
}

std::string
relative_path_from_dir(const std::string& from_dir,
                       const std::string& to_path) {
  std::vector<std::string> from = split_rel_path(from_dir);
  std::vector<std::string> to = split_rel_path(to_path);
  size_t common = 0;
  while (common < from.size() && common < to.size() &&
         from[common] == to[common]) {
    common++;
  }

  std::vector<std::string> out;
  for (size_t i = common; i < from.size(); ++i) out.push_back("..");
  for (size_t i = common; i < to.size(); ++i) out.push_back(to[i]);
  return join_rel_parts(out, 0);
}

std::string
path_stem_without_trailing_separators(const std::string& path) {
  size_t end = path.find_last_not_of("/\\");
  if (end == std::string::npos) return "";
  return path_stem(path.substr(0, end + 1));
}

std::string
normalize_heading_target_text(const std::string& s) {
  std::string out;
  bool last_space = false;
  bool after_math_open = false;
  for (unsigned char c : s) {
    if (c == '\\') continue;
    bool space = (c == ' ' || c == '\t' || c == '\r' || c == '\n');
    if (space) {
      if (!out.empty() && !last_space && !after_math_open) {
        out += ' ';
        last_space = true;
      }
      continue;
    }
    if (c == '$') {
      if (!out.empty() && out.back() == ' ') out.pop_back();
      out += '$';
      last_space = false;
      after_math_open = true;
      continue;
    }
    out += (char) (c < 0x80 ? std::tolower(c) : c);
    last_space = false;
    after_math_open = false;
  }
  if (!out.empty() && out.back() == ' ') out.pop_back();
  return out;
}

std::string
heading_map_key(const std::string& file_stem, const std::string& heading) {
  return file_stem + "\n" + heading;
}

std::string
normalized_heading_map_key(const std::string& file_stem,
                           const std::string& heading) {
  return heading_map_key(file_stem, normalize_heading_target_text(heading));
}

bool
is_vault_url_component_unreserved(unsigned char c) {
  return std::isalnum(c) || c == '-' || c == '.' || c == '_' || c == '~';
}

std::string
vault_url_component_encode(const std::string& s) {
  static const char* hex = "0123456789ABCDEF";
  std::string out;
  for (unsigned char c : s) {
    if (c >= 0x80 || is_vault_url_component_unreserved(c)) {
      out += (char) c;
    }
    else {
      out += '%';
      out += hex[c >> 4];
      out += hex[c & 15];
    }
  }
  return out;
}

std::string
make_wikilink_url(const std::string& uuid,
                  const std::string& file_hint,
                  const std::string& anchor_hint) {
  return "tmfs://wikilink/" + vault_url_component_encode(uuid) + "/" +
         vault_url_component_encode(file_hint) + "/" +
         vault_url_component_encode(anchor_hint);
}

std::string
scheme_quote_string(const std::string& s) {
  std::string out = "\"";
  for (char c : s) {
    if (c == '\\' || c == '"') out += '\\';
    out += c;
  }
  out += "\"";
  return out;
}

std::string
tree_to_std_string(const tree& t) {
  return std::string(as_charp(cork_to_utf8(as_string(t))));
}

tree
text_tree(const std::string& s) {
  return as_tree(std_to_tm_string(s));
}

tree
make_label_tree(const std::string& label) {
  return compound("label", text_tree(label));
}

bool
is_label_tree_with(const tree& t, const std::string& label) {
  return is_compound(t, "label", 1) && tree_to_std_string(t[0]) == label;
}

bool
is_heading_tree(const tree& t) {
  return is_compound(t, "section", 1) ||
         is_compound(t, "subsection", 1) ||
         is_compound(t, "subsubsection", 1) ||
         is_compound(t, "paragraph", 1) ||
         is_compound(t, "subparagraph", 1);
}

int
heading_level_from_label(const std::string& label) {
  int level = 0;
  while (level < (int) label.size() && label[level] == '#') level++;
  if (level == 0 || level > 6) return 0;
  if (level < (int) label.size() &&
      label[level] != ' ' &&
      label[level] != '\t') {
    return 0;
  }
  return level;
}

void
append_document(tree& out, tree piece) {
  if (piece == "") return;
  if (is_document(piece)) out << A(piece);
  else out << piece;
}

void
report_import_error(const std::string& message) {
  std::cerr << "aofm2athena: error: " << message << std::endl;
}

void
report_import_warning(const std::string& message) {
  std::cerr << "aofm2athena: warning: " << message << std::endl;
}

bool
is_aofm_anchor_block_placeholder(const tree& t) {
  return is_compound(t, "__aofm_anchor_block", 1);
}

bool
is_aofm_anchor_inline_placeholder(const tree& t) {
  return is_compound(t, "__aofm_anchor_inline", 1);
}

std::string
placeholder_anchor_id(const tree& t) {
  if (!is_aofm_anchor_block_placeholder(t) &&
      !is_aofm_anchor_inline_placeholder(t)) {
    return "";
  }
  return tree_to_std_string(t[0]);
}

tree
materialize_anchor_literal(const tree& t) {
  if (is_aofm_anchor_block_placeholder(t)) {
    return text_tree("^" + placeholder_anchor_id(t));
  }
  if (is_aofm_anchor_inline_placeholder(t)) {
    return text_tree(" ^" + placeholder_anchor_id(t));
  }
  return t;
}

bool
is_enunciation_like_tree(const tree& t) {
  if (!is_compound(t)) return false;
  std::string tag = std::string(as_charp(as_string(L(t))));
  return tag == "theorem" || tag == "lemma" || tag == "corollary" ||
         tag == "proposition" || tag == "axiom" || tag == "definition" ||
         tag == "conjecture" || tag == "remark" || tag == "note" ||
         tag == "example" || tag == "warning" || tag == "question" ||
         tag == "proof" || tag == "solution" || tag == "law" ||
         tag == "disambiguation" || tag == "proof-alternative" ||
         tag == "proof-standard";
}

bool
is_theorem_like_tree(const tree& t) {
  if (!is_compound(t)) return false;
  std::string tag = std::string(as_charp(as_string(L(t))));
  return tag == "theorem" || tag == "lemma" || tag == "corollary" ||
         tag == "proposition" || tag == "axiom" || tag == "definition" ||
         tag == "conjecture" || tag == "remark" || tag == "law" || 
         tag == "example";
}

bool
is_aofm_wikilink_placeholder(const tree& t) {
  return is_compound(t, "__aofm_wikilink", 3);
}

bool
is_aofm_transclusion_placeholder(const tree& t) {
  return is_compound(t, "__aofm_transclusion", 3);
}

bool
is_aofm_image_placeholder(const tree& t) {
  return is_compound(t, "__aofm_image", 2);
}

bool
is_image_target(const std::string& target) {
  return aofm::is_aofm_image_target(target);
}

bool
is_pdf_target(const std::string& target) {
  return aofm::is_aofm_pdf_target(target);
}

bool
is_copyable_asset_target(const std::string& target) {
  return is_image_target(target) || is_pdf_target(target);
}

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
                            const FileIndexMap& file_map,
                            const HeadingMap& heading_map,
                            const AssetIndexMap& asset_map,
                            const DirChildrenMap& dir_children,
                            const std::string& rel_ath_path) {
  if (is_aofm_anchor_inline_placeholder(t)) {
    std::string anchor = placeholder_anchor_id(t);
    auto it = anchor_map.find(anchor);
    if (it == anchor_map.end()) {
      report_import_error("anchor '^" + anchor + "' not found while converting " +
                          rel_ath_path);
      return materialize_anchor_literal(t);
    }
    return make_label_tree(it->second.anchor_1);
  }

  if (is_aofm_anchor_block_placeholder(t)) {
    std::string anchor = placeholder_anchor_id(t);
    auto it = anchor_map.find(anchor);
    if (it == anchor_map.end()) {
      report_import_error("anchor '^" + anchor + "' not found while converting " +
                          rel_ath_path);
      return materialize_anchor_literal(t);
    }
    return make_label_tree(it->second.anchor_1);
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
    return make_image_embed(image_path, width.empty() ? "0.8par" :
                            width + "guipx");
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
    for (int i = 0; i < N(t); ++i) {
      const tree& child = t[i];
      if (is_aofm_anchor_block_placeholder(child) ||
          is_aofm_anchor_inline_placeholder(child)) {
        std::string anchor = placeholder_anchor_id(child);
        auto it = anchor_map.find(anchor);
        if (it == anchor_map.end()) {
          report_import_error("anchor '^" + anchor + "' not found while converting " +
                              rel_ath_path);
          append_document(out, materialize_anchor_literal(child));
          continue;
        }

        if (!it->second.anchor_2.empty() &&
            N(out) > 1 &&
            is_heading_tree(out[N(out) - 1]) &&
            is_label_tree_with(out[N(out) - 2], it->second.anchor_1)) {
          continue;
        }

        if (!it->second.anchor_2.empty() && N(out) > 0) {
          tree previous = out[N(out) - 1];
          out[N(out) - 1] = make_label_tree(it->second.anchor_1);
          append_document(out, previous);
          append_document(out, make_label_tree(it->second.anchor_2));

          // Dual-Wrap logic for separated proofs
          if (is_theorem_like_tree(previous) && i + 1 < N(t) && is_compound(t[i + 1], "proof")) {
            std::string t_label1 = it->second.anchor_1;
            std::string t_label2 = it->second.anchor_2;

            auto derive_proof_label = [](const std::string& l) {
              size_t colon = l.find(':');
              std::string suffix = (colon == std::string::npos ? l : l.substr(colon + 1));
              // Ensure we remove the trailing ' {' or ' }' if they are part of the string
              size_t space = suffix.find_last_not_of(" {}");
              if (space != std::string::npos) suffix = suffix.substr(0, space + 1);
              return "proof:" + suffix;
            };

            append_document(out, make_label_tree(derive_proof_label(t_label1) + " {"));
            append_document(out, resolve_anchor_placeholders(t[i+1], anchor_map,
                                                            file_map, heading_map,
                                                            asset_map, dir_children,
                                                            rel_ath_path));
            append_document(out, make_label_tree(derive_proof_label(t_label2) + " }"));

            i++; // Consume the proof node
          }
          continue;
        }

        if (it->second.anchor_2.empty() &&
            N(out) > 0 &&
            is_label_tree_with(out[N(out) - 1], it->second.anchor_1)) {
          continue;
        }
        if (it->second.anchor_2.empty() &&
            N(out) > 1 &&
            is_heading_tree(out[N(out) - 1]) &&
            is_label_tree_with(out[N(out) - 2], it->second.anchor_1)) {
          continue;
        }

        append_document(out, make_label_tree(it->second.anchor_1));
        continue;
      }

      if (is_compound(child, "label", 1)) {
        std::string label = tree_to_std_string(child[0]);
        const AofmVaultHeadingInfo* info =
            find_heading_info_by_label(heading_map, rel_ath_path, label);
        if (info != nullptr) {
          close_heading_ranges(out, open_headings, info->level);
          append_document(out, child);
          open_headings.push_back(info);
          continue;
        }
      }

      append_document(out,
                      resolve_anchor_placeholders(child, anchor_map, file_map,
                                                  heading_map,
                                                  asset_map, dir_children,
                                                  rel_ath_path));
    }
    close_heading_ranges(out, open_headings, 0);
    return simplify_document(out);
  }

  tree out(t, N(t));
  for (int i = 0; i < N(t); ++i) {
    out[i] = resolve_anchor_placeholders(t[i], anchor_map, file_map, heading_map,
                                         asset_map, dir_children,
                                         rel_ath_path);
  }
  return out;
}

std::string
trim_copy(const std::string& s) {
  size_t start = 0;
  size_t end = s.size();
  while (start < end &&
         (s[start] == ' ' || s[start] == '\t' ||
          s[start] == '\r' || s[start] == '\n')) {
    start++;
  }
  while (end > start &&
         (s[end - 1] == ' ' || s[end - 1] == '\t' ||
          s[end - 1] == '\r' || s[end - 1] == '\n')) {
    end--;
  }
  return s.substr(start, end - start);
}

std::string
rtrim_copy(const std::string& s) {
  size_t end = s.size();
  while (end > 0 &&
         (s[end - 1] == ' ' || s[end - 1] == '\t' ||
          s[end - 1] == '\r' || s[end - 1] == '\n')) {
    end--;
  }
  return s.substr(0, end);
}

bool
is_blank_line(const std::string& line) {
  return trim_copy(line).empty();
}

std::string
strip_closing_heading_hashes(std::string title) {
  title = rtrim_copy(title);
  size_t hash_start = title.size();
  while (hash_start > 0 && title[hash_start - 1] == '#') hash_start--;
  if (hash_start == title.size()) return title;
  if (hash_start == 0) return title;
  if (title[hash_start - 1] != ' ' && title[hash_start - 1] != '\t') return title;
  return rtrim_copy(title.substr(0, hash_start - 1));
}

bool
extract_heading_label(const std::string& line,
                      std::string& target,
                      std::string& label) {
  size_t pos = 0;
  while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t')) pos++;

  size_t level = 0;
  while (pos + level < line.size() && line[pos + level] == '#') level++;
  if (level == 0 || level > 6) return false;
  if (pos + level >= line.size()) return false;
  if (line[pos + level] != ' ' && line[pos + level] != '\t') return false;

  std::string title = trim_copy(line.substr(pos + level));
  title = strip_closing_heading_hashes(title);
  if (title.empty()) return false;

  target = title;
  label = std::string(level, '#') + " " + title;
  return true;
}

std::string
collapse_whitespace(const std::string& s) {
  std::string out;
  bool last_space = false;
  for (char ch : s) {
    bool space = (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n');
    if (space) {
      if (!out.empty() && !last_space) out += ' ';
    }
    else {
      out += ch;
    }
    last_space = space;
  }
  return trim_copy(out);
}

std::string
replace_md_with_ath(const std::string& rel_path) {
  if (rel_path.size() >= 3 && rel_path.substr(rel_path.size() - 3) == ".md") {
    return rel_path.substr(0, rel_path.size() - 3) + ".ath";
  }
  return rel_path + ".ath";
}

bool
starts_with_blockquote(const std::string& line) {
  size_t pos = 0;
  while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t')) pos++;
  return pos < line.size() && line[pos] == '>';
}

std::string
strip_one_blockquote_marker(const std::string& line) {
  size_t pos = 0;
  while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t')) pos++;
  if (pos < line.size() && line[pos] == '>') pos++;
  if (pos < line.size() && line[pos] == ' ') pos++;
  return line.substr(pos);
}

std::string
to_lower_ascii(std::string s) {
  for (char& ch : s) ch = (char) std::tolower((unsigned char) ch);
  return s;
}

bool
starts_with_token(const std::string& s, size_t pos, const std::string& token) {
  if (s.compare(pos, token.size(), token) != 0) return false;
  size_t end = pos + token.size();
  return end >= s.size() || s[end] == ' ' || s[end] == '\t';
}

std::string
map_basic_callout_type(const std::string& base) {
  std::string b = to_lower_ascii(base);
  if (b == "question" || b == "help" || b == "faq") return "question";
  if (b == "warning" || b == "caution" || b == "attention" ||
      b == "failure" || b == "fail" || b == "missing" ||
      b == "danger" || b == "error" || b == "bug") {
    return "warning";
  }
  if (b == "example") return "example";
  if (b == "quote" || b == "cite") return "quote";
  return "note";
}

std::string
map_extended_callout_type(const std::string& ext) {
  if (ext == "Alternative Proof") return "proof-alternative";
  if (ext == "Standard Steps") return "proof-standard";
  if (ext == "Disambiguation") return "disambiguation";
  if (ext == "Caution") return "warning";
  if (ext == "Paster") return "blockquote";
  return to_lower_ascii(ext);
}

bool
parse_callout_header_line(const std::string& raw, CalloutHeaderInfo& out) {
  std::string line = strip_one_blockquote_marker(raw);
  size_t pos = 0;
  while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t')) pos++;
  if (line.compare(pos, 2, "[!") != 0) return false;
  pos += 2;
  size_t close = line.find(']', pos);
  if (close == std::string::npos) return false;

  std::string base = line.substr(pos, close - pos);
  pos = close + 1;
  if (pos < line.size() && (line[pos] == '+' || line[pos] == '-')) pos++;
  while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t')) pos++;

  static const char* kExts[] = {
      "Alternative Proof", "Standard Steps", "Disambiguation",
      "Proposition", "Corollary", "Conjecture", "Definition",
      "Question", "Theorem", "Example", "Caution", "Remark",
      "Paster", "Axiom", "Lemma", "Law"};

  std::string type = map_basic_callout_type(base);
  for (const char* ext : kExts) {
    std::string token = ext;
    if (!starts_with_token(line, pos, token)) continue;
    type = map_extended_callout_type(token);
    pos += token.size();
    while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t')) pos++;
    break;
  }

  out.type = type;
  out.header_tail = trim_copy(line.substr(pos));
  return true;
}

std::string
first_bold_segment(const std::string& s) {
  size_t open = s.find("**");
  if (open == std::string::npos) return "";
  size_t close = s.find("**", open + 2);
  if (close == std::string::npos || close <= open + 2) return "";
  return s.substr(open + 2, close - open - 2);
}

std::string
normalize_callout_auto_title_candidate(std::string s) {
  s = collapse_whitespace(trim_copy(s));

  while (!s.empty() && std::ispunct((unsigned char) s.front())) {
    s.erase(s.begin());
  }
  while (!s.empty() && std::ispunct((unsigned char) s.back())) {
    s.pop_back();
  }

  std::string out;
  for (char ch : s) {
    out += (char) std::tolower((unsigned char) ch);
  }
  return collapse_whitespace(out);
}

bool
is_common_callout_auto_title_candidate(const std::string& s) {
  std::string normalized = normalize_callout_auto_title_candidate(s);
  static const char* kIgnored[] = {
      "not", "cannot", "however", "but", "should not", "is", "is not",
      "can not"};
  for (const char* ignored : kIgnored) {
    if (normalized == ignored) return true;
  }
  return false;
}

std::string
wikilink_visible_text(const std::string& body) {
  size_t pipe = body.find('|');
  if (pipe != std::string::npos) return trim_copy(body.substr(pipe + 1));
  return trim_copy(body);
}

std::string
reduce_markdown_links_to_text(const std::string& s) {
  std::string out;
  for (size_t i = 0; i < s.size(); ) {
    if (i + 2 <= s.size() && s.compare(i, 2, "[[") == 0) {
      size_t close = s.find("]]", i + 2);
      if (close != std::string::npos) {
        out += wikilink_visible_text(s.substr(i + 2, close - i - 2));
        i = close + 2;
        continue;
      }
    }

    if ((i == 0 || s[i - 1] != '!') && s[i] == '[') {
      size_t close_text = s.find(']', i + 1);
      if (close_text != std::string::npos &&
          close_text + 1 < s.size() && s[close_text + 1] == '(') {
        size_t close_url = s.find(')', close_text + 2);
        if (close_url != std::string::npos) {
          out += s.substr(i + 1, close_text - i - 1);
          i = close_url + 1;
          continue;
        }
      }
    }

    out += s[i++];
  }
  return out;
}

bool
uses_parenthesized_title(const std::string& type) {
  return type == "theorem" || type == "lemma" || type == "proposition" ||
         type == "corollary" || type == "conjecture" || type == "question";
}

bool
decode_utf8_codepoint(const std::string& s, size_t& pos,
                      char32_t& cp, std::string& original) {
  if (pos >= s.size()) return false;

  unsigned char lead = (unsigned char) s[pos];
  size_t len = 0;
  if (lead < 0x80) {
    cp = lead;
    len = 1;
  }
  else if ((lead & 0xE0) == 0xC0 && pos + 1 < s.size()) {
    cp = ((lead & 0x1F) << 6) |
         ((unsigned char) s[pos + 1] & 0x3F);
    len = 2;
  }
  else if ((lead & 0xF0) == 0xE0 && pos + 2 < s.size()) {
    cp = ((lead & 0x0F) << 12) |
         (((unsigned char) s[pos + 1] & 0x3F) << 6) |
         ((unsigned char) s[pos + 2] & 0x3F);
    len = 3;
  }
  else if ((lead & 0xF8) == 0xF0 && pos + 3 < s.size()) {
    cp = ((lead & 0x07) << 18) |
         (((unsigned char) s[pos + 1] & 0x3F) << 12) |
         (((unsigned char) s[pos + 2] & 0x3F) << 6) |
         ((unsigned char) s[pos + 3] & 0x3F);
    len = 4;
  }
  else {
    pos++;
    return false;
  }

  original = s.substr(pos, len);
  pos += len;
  return true;
}

bool
is_cjk_codepoint(char32_t cp) {
  return
    (cp >= 0x3400 && cp <= 0x4DBF) ||
    (cp >= 0x4E00 && cp <= 0x9FFF) ||
    (cp >= 0xF900 && cp <= 0xFAFF) ||
    (cp >= 0x20000 && cp <= 0x2A6DF) ||
    (cp >= 0x2A700 && cp <= 0x2B73F) ||
    (cp >= 0x2B740 && cp <= 0x2B81F) ||
    (cp >= 0x2B820 && cp <= 0x2CEAF) ||
    (cp >= 0x2CEB0 && cp <= 0x2EBEF) ||
    (cp >= 0x30000 && cp <= 0x3134F);
}

std::string
sanitize_anchor_text(const std::string& s, size_t limit) {
  std::string out;
  size_t count = 0;
  for (size_t pos = 0; pos < s.size(); ) {
    char32_t cp = 0;
    std::string original;
    size_t before = pos;
    if (!decode_utf8_codepoint(s, pos, cp, original)) continue;
    if (cp < 0x80) {
      if (cp == ' ' || cp == '\t' || cp == '\r' || cp == '\n') {
        out += ' ';
      }
      else if (std::isalnum((unsigned char) cp)) {
        out += (char) cp;
      }
      else {
        continue;
      }
      count++;
    }
    else if (is_cjk_codepoint(cp)) {
      out += original;
      count++;
    }
    else if (pos == before) {
      pos++;
    }
    if (limit != 0 && count >= limit) break;
  }
  return out;
}

std::string
make_paragraph_anchor_sample(const std::vector<std::string>& lines) {
  std::string text;
  for (size_t i = 0; i < lines.size(); ++i) {
    if (i > 0) text += ' ';
    text += lines[i];
  }
  text = collapse_whitespace(text);
  text = reduce_markdown_links_to_text(text);
  if (text.size() > 50) text = text.substr(0, 50);
  return text;
}

std::pair<std::string,std::string>
make_paragraph_anchor_pair(const std::vector<std::string>& lines) {
  std::string sample = make_paragraph_anchor_sample(lines);
  if (sample.empty()) sample = "paragraph";
  return std::make_pair(sample + " {", sample + " }");
}

std::pair<std::string,std::string>
make_callout_anchor_pair(const std::vector<std::string>& lines) {
  CalloutHeaderInfo header;
  if (lines.empty() || !parse_callout_header_line(lines[0], header)) {
    std::string sample = sanitize_anchor_text(collapse_whitespace(lines.empty() ? "" : lines[0]), 100);
    if (sample.empty()) sample = "anchor";
    return std::make_pair("note:" + sample + " {", "note:" + sample + " }");
  }

  std::string sample_source = header.header_tail;
  for (size_t i = 1; i < lines.size(); ++i) {
    if (!sample_source.empty()) sample_source += ' ';
    sample_source += strip_one_blockquote_marker(lines[i]);
  }
  sample_source = collapse_whitespace(sample_source);
  sample_source = reduce_markdown_links_to_text(sample_source);

  std::string bold = first_bold_segment(sample_source);
  std::string title;
  if (uses_parenthesized_title(header.type)) {
    if (!bold.empty() && bold[0] == '(') {
      size_t close = bold.find(')');
      if (close != std::string::npos && close > 1) {
        title = bold.substr(1, close - 1);
      }
    }
  }
  else if (!bold.empty()) {
    if (!is_common_callout_auto_title_candidate(bold)) {
      title = bold;
    }
  }

  std::string id = sanitize_anchor_text(title, 100);
  if (id.empty()) {
    // No bold-derived title: use the first sanitized content after the
    // callout type, including the body when the header line itself is bare.
    id = sanitize_anchor_text(sample_source, 100);
  }
  if (id.empty()) id = header.type;

  std::string prefix = header.type + ":" + id;
  return std::make_pair(prefix + " {", prefix + " }");
}

bool
extract_anchor_only(const std::string& line, std::string& anchor) {
  std::string trimmed = trim_copy(line);
  if (trimmed.empty() || trimmed[0] != '^') return false;
  anchor = trim_copy(trimmed.substr(1));
  return !anchor.empty();
}

bool
extract_trailing_anchor(const std::string& line,
                        std::string& content,
                        std::string& anchor) {
  std::string trimmed = rtrim_copy(line);
  if (trimmed.empty()) return false;

  size_t pos = trimmed.find_last_of('^');
  if (pos == std::string::npos || pos == 0) return false;
  if (!(trimmed[pos - 1] == ' ' || trimmed[pos - 1] == '\t')) return false;

  std::string candidate = trim_copy(trimmed.substr(pos + 1));
  if (candidate.empty()) return false;
  for (char ch : candidate) {
    if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') return false;
  }

  content = rtrim_copy(trimmed.substr(0, pos));
  anchor = candidate;
  return true;
}

void
store_anchor(AnchorMap& map, const std::string& anchor, const std::string& rel_ath_path,
             const std::string& file_hint, const std::pair<std::string,std::string>& pair) {
  std::string uuid = tm_to_std_string(vault_generate_uuid());
  AofmVaultAnchorInfo info;
  info.uuid = uuid;
  if (!pair.second.empty()) {
    info.transclusion_uuid = tm_to_std_string(vault_generate_uuid());
  }
  info.path = rel_ath_path;
  info.anchor_1 = pair.first;
  info.anchor_2 = pair.second;
  info.hlink_w = make_wikilink_url(uuid, file_hint, info.anchor_1);
  map[anchor] = info;
}

void
finalize_current_block(BlockContext& current, BlockContext& last) {
  if (current.kind == BlockKind::NONE || current.lines.empty()) return;
  last = current;
  current.clear();
}

void
set_heading_end_label(HeadingMap& heading_map, const OpenHeadingInfo& open,
                      const std::string& end_label) {
  auto it = heading_map.find(open.key);
  if (it != heading_map.end() && it->second.end_label.empty())
    it->second.end_label = end_label;

  if (!open.normalized_key.empty() && open.normalized_key != open.key) {
    auto normalized = heading_map.find(open.normalized_key);
    if (normalized != heading_map.end() && normalized->second.end_label.empty())
      normalized->second.end_label = end_label;
  }
}

void
process_markdown_file(const ImportFileInfo& file_info, AnchorMap& map,
                      HeadingMap& heading_map) {
  std::ifstream in(as_charp(concretize(file_info.source_url)), std::ios::in | std::ios::binary);
  if (!in.is_open()) {
    report_import_error("could not open file: " + tm_to_std_string(as_string(file_info.source_url)));
    return;
  }

  std::vector<std::string> lines;
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    lines.push_back(line);
  }

  BlockContext current;
  BlockContext last;
  std::string file_hint = path_stem(file_info.relative_md_path);
  std::vector<OpenHeadingInfo> open_headings;

  for (size_t i = 0; i < lines.size(); ++i) {
    std::string anchor;
    std::string anchorless;
    std::string heading_target;
    std::string heading_label;

    if (extract_anchor_only(lines[i], anchor)) {
      const BlockContext& context =
          (current.kind != BlockKind::NONE && !current.lines.empty()) ? current : last;
      if (context.kind == BlockKind::CALLOUT) {
        store_anchor(map, anchor, file_info.relative_ath_path, file_hint,
                     make_callout_anchor_pair(context.lines));
      }
      else if (context.kind == BlockKind::PARAGRAPH) {
        store_anchor(map, anchor, file_info.relative_ath_path, file_hint,
                     make_paragraph_anchor_pair(context.lines));
      }
      else if (context.kind == BlockKind::HEADING && !context.lines.empty()) {
        store_anchor(map, anchor, file_info.relative_ath_path, file_hint,
                     std::make_pair(context.lines[0], context.lines[0] + " }"));
      }
      finalize_current_block(current, last);
      continue;
    }

    if (is_blank_line(lines[i])) {
      finalize_current_block(current, last);
      continue;
    }

    if (extract_heading_label(lines[i], heading_target, heading_label)) {
      finalize_current_block(current, last);
      last.kind = BlockKind::HEADING;
      last.lines.clear();
      last.lines.push_back(heading_label);
      std::string key = heading_map_key(file_hint, heading_target);
      std::string normalized_key = normalized_heading_map_key(file_hint, heading_target);
      int level = heading_level_from_label(heading_label);
      while (!open_headings.empty() && open_headings.back().level >= level) {
        set_heading_end_label(heading_map, open_headings.back(), heading_label);
        open_headings.pop_back();
      }
      if (heading_map.find(key) == heading_map.end()) {
        AofmVaultHeadingInfo info;
        info.uuid = tm_to_std_string(vault_generate_uuid());
        info.transclusion_uuid = tm_to_std_string(vault_generate_uuid());
        info.path = file_info.relative_ath_path;
        info.label = heading_label;
        info.level = level;
        heading_map[key] = info;
        if (normalized_key != key &&
            heading_map.find(normalized_key) == heading_map.end()) {
          heading_map[normalized_key] = info;
        }
        OpenHeadingInfo open;
        open.level = level;
        open.key = key;
        open.normalized_key = normalized_key;
        open_headings.push_back(open);
      }
      continue;
    }

    bool has_trailing_anchor = extract_trailing_anchor(lines[i], anchorless, anchor);
    std::string effective_line = has_trailing_anchor ? anchorless : lines[i];
    bool is_callout = starts_with_blockquote(effective_line);
    BlockKind next_kind = is_callout ? BlockKind::CALLOUT : BlockKind::PARAGRAPH;

    if (current.kind != BlockKind::NONE && current.kind != next_kind) {
      finalize_current_block(current, last);
    }
    if (current.kind == BlockKind::NONE) current.kind = next_kind;
    current.lines.push_back(effective_line);

    if (has_trailing_anchor) {
      if (current.kind == BlockKind::CALLOUT) {
        store_anchor(map, anchor, file_info.relative_ath_path, file_hint,
                     make_callout_anchor_pair(current.lines));
      }
      else {
        store_anchor(map, anchor, file_info.relative_ath_path, file_hint,
                     make_paragraph_anchor_pair(current.lines));
      }
      finalize_current_block(current, last);
    }
  }
}

void
dump_anchor_map(const AnchorMap& map, std::ostream& out) {
  std::vector<std::string> anchors;
  anchors.reserve(map.size());
  for (const auto& entry : map) anchors.push_back(entry.first);
  std::sort(anchors.begin(), anchors.end());

  out << "--- AOFM VAULT ANCHOR MAP BEGIN ---" << std::endl;
  for (const std::string& anchor : anchors) {
    const AofmVaultAnchorInfo& info = map.at(anchor);
    out << anchor << " -> ("
              << "uuid=" << info.uuid
              << ", transclusion_uuid=" << info.transclusion_uuid
              << ", path=" << info.path
              << ", anchor_1=" << info.anchor_1
              << ", anchor_2=" << info.anchor_2
              << ", hlink_w=" << info.hlink_w
              << ")" << std::endl;
  }
  out << "--- AOFM VAULT ANCHOR MAP END ---" << std::endl;
}

void
dump_heading_map(const HeadingMap& map, std::ostream& out) {
  std::vector<std::string> headings;
  headings.reserve(map.size());
  for (const auto& entry : map) headings.push_back(entry.first);
  std::sort(headings.begin(), headings.end());

  out << "--- AOFM VAULT HEADING MAP BEGIN ---" << std::endl;
  for (const std::string& heading : headings) {
    const AofmVaultHeadingInfo& info = map.at(heading);
    out << heading << " -> ("
        << "uuid=" << info.uuid
        << ", transclusion_uuid=" << info.transclusion_uuid
        << ", path=" << info.path
        << ", label=" << info.label
        << ", end_label=" << info.end_label
        << ", level=" << info.level
        << ")" << std::endl;
  }
  out << "--- AOFM VAULT HEADING MAP END ---" << std::endl;
}

bool
scan_markdown_files(url source_root, url source_dir, url destination_dir,
                    std::vector<ImportFileInfo>& files,
                    AssetIndexMap& asset_map,
                    DirChildrenMap& dir_children) {
  bool err = false;
  array<string> entries = read_directory(source_dir, err);
  if (err) return false;

  for (int i = 0; i < N(entries); ++i) {
    string entry = entries[i];
    if (N(entry) > 0 && entry[0] == '.') continue;

    url src = source_dir * url(entry);
    if (is_directory(src)) {
      std::string rel_dir =
          normalize_rel_path(tm_to_std_string(as_unix_string(delta(source_root * url(""), src))));
      std::string parent_dir = path_dirname(rel_dir);
      dir_children[parent_dir].push_back(rel_dir);

      mkdir(destination_dir * url(entry));
      if (!scan_markdown_files(source_root, src, destination_dir * url(entry),
                               files, asset_map, dir_children)) {
        return false;
      }
      continue;
    }

    std::string rel_path =
        normalize_rel_path(tm_to_std_string(as_unix_string(delta(source_root * url(""), src))));
    if (is_copyable_asset_target(rel_path)) {
      url dst = destination_dir * url(entry);
      copy(src, dst);
      if (!exists(dst)) {
        report_import_warning("failed to copy asset: " + rel_path);
      }
      AofmVaultAssetInfo info;
      info.relative_path = rel_path;
      asset_map[asset_key(rel_path)] = info;
      continue;
    }

    if (suffix(src) != "md") continue;

    std::string rel_md = rel_path;
    ImportFileInfo info;
    info.source_url = src;
    info.relative_md_path = rel_md;
    info.relative_ath_path = replace_md_with_ath(rel_md);
    files.push_back(info);
  }

  for (auto& entry : dir_children) {
    std::sort(entry.second.begin(), entry.second.end());
  }

  return true;
}

bool
validate_destination_dir(url destination_root, bool ignore_nonempty) {
  if (!exists(destination_root)) {
    mkdir(destination_root);
    return true;
  }
  if (!is_directory(destination_root)) {
    report_import_error("destination path is not a directory");
    return false;
  }

  bool err = false;
  array<string> entries = read_directory(destination_root, err);
  if (err) {
    report_import_error("could not inspect destination directory");
    return false;
  }
  for (int i = 0; i < N(entries); ++i) {
    if (entries[i] == "." || entries[i] == "..") continue;
    if (ignore_nonempty) {
      report_import_warning("destination directory is not empty");
      return true;
    }
    report_import_error("destination directory is not empty");
    return false;
  }
  return true;
}

std::string
join_unix_paths(const std::string& root, const std::string& rel) {
  if (root.empty()) return rel;
  if (rel.empty()) return root;
  if (root[root.size() - 1] == '/') return root + rel;
  return root + "/" + rel;
}

bool
write_vaultfile(const std::string& destination_root_path,
                const std::string& vault_name) {
  std::string vaultfile_path = join_unix_paths(destination_root_path, "Vaultfile");
  std::ofstream vaultfile(vaultfile_path);
  if (!vaultfile.is_open()) return false;
  vaultfile << "(" << scheme_quote_string(vault_name) << " \"map.tmdb\")\n";
  return (bool) vaultfile;
}

void
set_vault_db_node(url db_url, const std::string& uuid, const std::string& path,
                  const std::string& anchor_begin,
                  const std::string& anchor_end) {
  strings s_path;
  s_path << std_to_tm_string(path);
  strings s_begin;
  s_begin << std_to_tm_string(anchor_begin);
  strings s_end;
  s_end << std_to_tm_string(anchor_end);

  set_field(db_url, std_to_tm_string(uuid), "v-path", s_path, 0);
  set_field(db_url, std_to_tm_string(uuid), "v-anchor-begin", s_begin, 0);
  set_field(db_url, std_to_tm_string(uuid), "v-anchor-end", s_end, 0);
}

bool
write_vault_database(url destination_root, const FileIndexMap& file_map,
                     const AnchorMap& anchor_map,
                     const HeadingMap& heading_map) {
  url db_url = destination_root * url("map.tmdb");

  for (const auto& entry : file_map) {
    const AofmVaultFileInfo& info = entry.second;
    set_vault_db_node(db_url, info.uuid, info.relative_ath_path, "", "");
  }

  for (const auto& entry : anchor_map) {
    const AofmVaultAnchorInfo& info = entry.second;
    set_vault_db_node(db_url, info.uuid, info.path, "", info.anchor_1);
    if (!info.transclusion_uuid.empty() && !info.anchor_2.empty()) {
      set_vault_db_node(db_url, info.transclusion_uuid, info.path,
                        info.anchor_1, info.anchor_2);
    }
  }

  for (const auto& entry : heading_map) {
    const AofmVaultHeadingInfo& info = entry.second;
    set_vault_db_node(db_url, info.uuid, info.path, "", info.label);
    if (!info.transclusion_uuid.empty()) {
      set_vault_db_node(db_url, info.transclusion_uuid, info.path,
                        info.label, info.end_label);
    }
  }

  sync_databases();
  return exists(db_url);
}

} // namespace

void
print_progress_bar(size_t current, size_t total, const std::string& filename) {
  int bar_width = 30;
  float progress = (float)current / (float)total;
  int pos = (int)(bar_width * progress);

  std::cout << "\r[" ;
  for (int i = 0; i < bar_width; ++i) {
    if (i < pos) std::cout << "=";
    else if (i == pos) std::cout << ">";
    else std::cout << " ";
  }

  std::string display_path = filename;
  if (display_path.length() > 40) {
    display_path = "..." + display_path.substr(display_path.length() - 37);
  }

  std::cout << "] " << (int)(progress * 100.0) << "% "
            << "[" << current << "/" << total << "] "
            << "Converting: " << display_path << "                             " << std::flush;
}

bool
aofm_import_vault(string source_dir, string destination_dir,
                  bool ignore_nonempty, int parallelism) {
  time_parse_latex_doc = 0.0;
  time_latex_to_tree = 0.0;
  aofm_math_time = 0.0;
  aofm_math_count = 0;
  
  time_l2t_kill_space = 0.0;
  time_l2t_parsed_latex = 0.0;
  time_l2t_finalize_doc = 0.0;
  time_l2t_handle_matches = 0.0;
  time_l2t_upgrade_tex = 0.0;
  time_l2t_finalize_misc = 0.0;
  time_l2t_drd_correct = 0.0;
  time_l2t_style_check = 0.0;
  time_l2t_simplify_correct = 0.0;
  time_l2t_latex_correct = 0.0;
  time_l2t_guess_missing = 0.0;
  time_l2t_post_metadata = 0.0;
  
  count_l2t_is_document = 0;
  count_l2t_total = 0;

  url source_root = url_system(source_dir);
  url destination_root = url_system(destination_dir);
  if (!is_rooted(source_root)) {
    source_root = resolve(url_pwd(), "") * source_root;
  }
  if (!is_rooted(destination_root)) {
    destination_root = resolve(url_pwd(), "") * destination_root;
  }

  if (!exists(source_root) || !is_directory(source_root)) {
    report_import_error("source path is not a directory");
    return false;
  }
  if (!validate_destination_dir(destination_root, ignore_nonempty)) return false;

  std::string destination_root_path =
      tm_to_std_string(as_unix_string(destination_root));
  std::string vault_name =
      path_stem_without_trailing_separators(tm_to_std_string(as_unix_string(source_root)));
  if (vault_name.empty()) vault_name = "Vault";
  if (!write_vaultfile(destination_root_path, vault_name)) {
    report_import_error("failed to write Vaultfile");
    return false;
  }

  std::vector<ImportFileInfo> files;
  AssetIndexMap asset_map;
  DirChildrenMap dir_children;
  if (!scan_markdown_files(source_root, source_root, destination_root, files,
                           asset_map, dir_children)) {
    report_import_error("failed to scan source vault");
    return false;
  }

  AnchorMap anchor_map;
  FileIndexMap file_map;
  HeadingMap heading_map;
  for (const ImportFileInfo& file_info : files) {
    std::string stem = path_stem(file_info.relative_md_path);
    AofmVaultFileInfo f_info;
    f_info.uuid = tm_to_std_string(vault_generate_uuid());
    f_info.relative_ath_path = file_info.relative_ath_path;
    f_info.stem = stem;
    file_map[stem] = f_info;

    process_markdown_file(file_info, anchor_map, heading_map);
  }

  std::string dump_path = join_unix_paths(destination_root_path, "anchor_map.txt");
  std::ofstream dump_file(dump_path);
  if (dump_file.is_open()) {
    dump_anchor_map(anchor_map, dump_file);
    dump_file.close();
  }

  std::string heading_dump_path = join_unix_paths(destination_root_path, "heading_map.txt");
  std::ofstream heading_dump_file(heading_dump_path);
  if (heading_dump_file.is_open()) {
    dump_heading_map(heading_map, heading_dump_file);
    heading_dump_file.close();
  }

  std::cout << "Starting vault conversion of " << files.size() << " files..." << std::endl;

  size_t total_files = files.size();
  size_t current_index = 0;

#if defined(__unix__) || defined(__APPLE__)
  int num_workers = parallelism > 0 ?
      parallelism : (int) std::thread::hardware_concurrency();
  if (num_workers <= 0) num_workers = 1;
  if (num_workers > (int)files.size()) num_workers = (int)files.size();

  std::vector<int> pipes(num_workers);
  std::vector<pid_t> pids;

  for (int i = 0; i < num_workers; ++i) {
    int fd[2];
    if (pipe(fd) == -1) {
      report_import_error("failed to create pipe");
      return false;
    }

    pid_t pid = fork();
    if (pid == -1) {
      report_import_error("failed to fork");
      return false;
    }

    if (pid == 0) {
      // Child process
      close(fd[0]);
      size_t start = (files.size() * i) / num_workers;
      size_t end = (files.size() * (i + 1)) / num_workers;

      // Reset local telemetry for the child to avoid double-counting inherited values
      time_parse_latex_doc = 0.0;
      time_latex_to_tree = 0.0;
      aofm_math_time = 0.0;
      aofm_math_count = 0;
      time_l2t_kill_space = 0.0;
      time_l2t_parsed_latex = 0.0;
      time_l2t_finalize_doc = 0.0;
      time_l2t_handle_matches = 0.0;
      time_l2t_upgrade_tex = 0.0;
      time_l2t_finalize_misc = 0.0;
      time_l2t_drd_correct = 0.0;
      time_l2t_style_check = 0.0;
      time_l2t_simplify_correct = 0.0;
      time_l2t_latex_correct = 0.0;
      time_l2t_guess_missing = 0.0;
      time_l2t_post_metadata = 0.0;
      count_l2t_is_document = 0;
      count_l2t_total = 0;

      for (size_t j = start; j < end; ++j) {
        const ImportFileInfo& file_info = files[j];
        tree document;
        if (!aofm_convert_tree(as_unix_string(file_info.source_url), document, false)) {
          IpcMessage msg;
          msg.type = IpcMsgType::ERROR_MSG;
          strncpy(msg.filename, file_info.relative_md_path.c_str(), 255);
          msg.filename[255] = '\0';
          write(fd[1], &msg, sizeof(IpcMessage));
          _exit(1);
        }

        tree resolved = resolve_anchor_placeholders(document, anchor_map, file_map,
                                                    heading_map,
                                                    asset_map, dir_children,
                                                    file_info.relative_ath_path);
        string serialized = tree_to_texmacs(resolved);
        std::string destination_path = join_unix_paths(destination_root_path, file_info.relative_ath_path);
        if (save_string(url_system(std_to_tm_string(destination_path)), serialized)) {
          IpcMessage msg;
          msg.type = IpcMsgType::ERROR_MSG;
          std::string err = "write fail: " + file_info.relative_ath_path;
          strncpy(msg.filename, err.c_str(), 255);
          msg.filename[255] = '\0';
          write(fd[1], &msg, sizeof(IpcMessage));
          _exit(1);
        }

        IpcMessage msg;
        msg.type = IpcMsgType::PROGRESS;
        strncpy(msg.filename, file_info.relative_md_path.c_str(), 255);
        msg.filename[255] = '\0';
        write(fd[1], &msg, sizeof(IpcMessage));
      }

      IpcMessage msg;
      msg.type = IpcMsgType::DONE;
      msg.telemetry.time_parse_latex_doc = time_parse_latex_doc;
      msg.telemetry.time_latex_to_tree = time_latex_to_tree;
      msg.telemetry.aofm_math_time = aofm_math_time;
      msg.telemetry.aofm_math_count = aofm_math_count;
      msg.telemetry.time_l2t_kill_space = time_l2t_kill_space;
      msg.telemetry.time_l2t_parsed_latex = time_l2t_parsed_latex;
      msg.telemetry.time_l2t_finalize_doc = time_l2t_finalize_doc;
      msg.telemetry.time_l2t_handle_matches = time_l2t_handle_matches;
      msg.telemetry.time_l2t_upgrade_tex = time_l2t_upgrade_tex;
      msg.telemetry.time_l2t_finalize_misc = time_l2t_finalize_misc;
      msg.telemetry.time_l2t_drd_correct = time_l2t_drd_correct;
      msg.telemetry.time_l2t_style_check = time_l2t_style_check;
      msg.telemetry.time_l2t_simplify_correct = time_l2t_simplify_correct;
      msg.telemetry.time_l2t_latex_correct = time_l2t_latex_correct;
      msg.telemetry.time_l2t_guess_missing = time_l2t_guess_missing;
      msg.telemetry.time_l2t_post_metadata = time_l2t_post_metadata;
      msg.telemetry.count_l2t_is_document = count_l2t_is_document;
      msg.telemetry.count_l2t_total = count_l2t_total;
      write(fd[1], &msg, sizeof(IpcMessage));
      _exit(0);
    } else {
      // Parent process
      close(fd[1]);
      pipes[i] = fd[0];
      pids.push_back(pid);
    }
  }

  // Parent monitoring loop
  std::vector<pollfd> poll_fds(num_workers);
  for (int i = 0; i < num_workers; ++i) {
    poll_fds[i].fd = pipes[i];
    poll_fds[i].events = POLLIN;
  }

  int active_workers = num_workers;
  while (active_workers > 0) {
    int ret = poll(poll_fds.data(), num_workers, -1);
    if (ret <= 0) continue;

    for (int i = 0; i < num_workers; ++i) {
      if (poll_fds[i].fd != -1 && (poll_fds[i].revents & POLLIN)) {
        IpcMessage msg;
        ssize_t n = read(poll_fds[i].fd, &msg, sizeof(IpcMessage));
        if (n <= 0) {
          if (n == 0) {
            close(poll_fds[i].fd);
            poll_fds[i].fd = -1;
            active_workers--;
          }
          continue;
        }

        if (msg.type == IpcMsgType::PROGRESS) {
          current_index++;
          print_progress_bar(current_index, total_files, msg.filename);
        } else if (msg.type == IpcMsgType::ERROR_MSG) {
          std::cout << std::endl;
          report_import_error("child error: " + std::string(msg.filename));
          // For simplicity, we abort on first child error
          for (pid_t p : pids) kill(p, SIGTERM);
          return false;
        } else if (msg.type == IpcMsgType::DONE) {
          time_parse_latex_doc += msg.telemetry.time_parse_latex_doc;
          time_latex_to_tree += msg.telemetry.time_latex_to_tree;
          aofm_math_time += msg.telemetry.aofm_math_time;
          aofm_math_count += msg.telemetry.aofm_math_count;
          time_l2t_kill_space += msg.telemetry.time_l2t_kill_space;
          time_l2t_parsed_latex += msg.telemetry.time_l2t_parsed_latex;
          time_l2t_finalize_doc += msg.telemetry.time_l2t_finalize_doc;
          time_l2t_handle_matches += msg.telemetry.time_l2t_handle_matches;
          time_l2t_upgrade_tex += msg.telemetry.time_l2t_upgrade_tex;
          time_l2t_finalize_misc += msg.telemetry.time_l2t_finalize_misc;
          time_l2t_drd_correct += msg.telemetry.time_l2t_drd_correct;
          time_l2t_style_check += msg.telemetry.time_l2t_style_check;
          time_l2t_simplify_correct += msg.telemetry.time_l2t_simplify_correct;
          time_l2t_latex_correct += msg.telemetry.time_l2t_latex_correct;
          time_l2t_guess_missing += msg.telemetry.time_l2t_guess_missing;
          time_l2t_post_metadata += msg.telemetry.time_l2t_post_metadata;
          count_l2t_is_document += msg.telemetry.count_l2t_is_document;
          count_l2t_total += msg.telemetry.count_l2t_total;
        }
      } else if (poll_fds[i].fd != -1 && (poll_fds[i].revents & (POLLHUP | POLLERR | POLLNVAL))) {
        close(poll_fds[i].fd);
        poll_fds[i].fd = -1;
        active_workers--;
      }
    }
  }

  for (pid_t pid : pids) {
    waitpid(pid, NULL, 0);
  }
#else
  for (const ImportFileInfo& file_info : files) {
    current_index++;
    print_progress_bar(current_index, total_files, file_info.relative_md_path);

    tree document;
    if (!aofm_convert_tree(as_unix_string(file_info.source_url), document, false)) {
      std::cout << std::endl;
      report_import_error("failed to convert file: " + file_info.relative_md_path);
      return false;
    }

    tree resolved =
        resolve_anchor_placeholders(document, anchor_map, file_map, heading_map,
                                    asset_map, dir_children,
                                    file_info.relative_ath_path);
    string serialized = tree_to_texmacs(resolved);
    std::string destination_path =
        join_unix_paths(destination_root_path, file_info.relative_ath_path);
    if (save_string(url_system(std_to_tm_string(destination_path)), serialized)) {
      std::cout << std::endl;
      report_import_error("failed to write destination file: " + destination_path);
      return false;
    }
  }
#endif

  if (!write_vault_database(destination_root, file_map, anchor_map, heading_map)) {
    std::cout << std::endl;
    report_import_error("failed to write vault database map.tmdb");
    return false;
  }

  std::cout << "\nVault conversion completed successfully." << std::endl;

  if (aofm_math_count > 0) {
    std::cout << "AOFM] Total math processing: " << aofm_math_time << "s (" << aofm_math_count << " formulas, avg " << (aofm_math_time / aofm_math_count) << "s)" << std::endl;
  }
  std::cout << "AOFM] Total parse_latex_document: " << time_parse_latex_doc << "s" << std::endl;
  std::cout << "AOFM] Total latex_to_tree: " << time_latex_to_tree << "s" << std::endl;
  std::cout << "  - kill_space_invaders: " << time_l2t_kill_space << "s" << std::endl;
  std::cout << "  - parsed_latex_to_tree: " << time_l2t_parsed_latex << "s" << std::endl;
  std::cout << "  - finalize_doc/preamble: " << time_l2t_finalize_doc << "s" << std::endl;
  std::cout << "  - handle_matches: " << time_l2t_handle_matches << "s" << std::endl;
  std::cout << "  - upgrade_tex: " << time_l2t_upgrade_tex << "s" << std::endl;
  std::cout << "  - finalize_misc/textm: " << time_l2t_finalize_misc << "s" << std::endl;
  std::cout << "  - drd_correct: " << time_l2t_drd_correct << "s" << std::endl;
  std::cout << "  - style_check (exists): " << time_l2t_style_check << "s" << std::endl;
  std::cout << "  - simplify_correct: " << time_l2t_simplify_correct << "s" << std::endl;
  std::cout << "  - latex_correct: " << time_l2t_latex_correct << "s" << std::endl;
  std::cout << "  - guess_missing: " << time_l2t_guess_missing << "s" << std::endl;
  std::cout << "  - postprocess_metadata: " << time_l2t_post_metadata << "s" << std::endl;
  std::cout << "AOFM] is_document counts: " << count_l2t_is_document << " / " << count_l2t_total << std::endl;

  return true;
}
