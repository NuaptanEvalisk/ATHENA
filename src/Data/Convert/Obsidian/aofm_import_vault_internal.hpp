#pragma once

#include <iosfwd>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "namespaces.hpp"
#include "tree.hpp"
#include "url.hpp"
#include "aofm_utils.hpp"

namespace aofm_import_vault_internal {

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
  PROOF,
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

struct AofmModelVaultInfo {
  bool active = false;
  std::string root;
  std::string prefs_rel;
  std::string namespace_db_rel = "ns.sqlite";
  std::vector<athena_namespace_definition> namespaces;
};

using AnchorMap = std::unordered_map<std::string, AofmVaultAnchorInfo>;
using AnchorOccurrenceMap =
    std::unordered_map<std::string, std::vector<AofmVaultAnchorInfo>>;
using AnchorOccurrenceCursor = std::unordered_map<std::string, size_t>;
using FileIndexMap = std::unordered_map<std::string, AofmVaultFileInfo>;
using HeadingMap = std::unordered_map<std::string, AofmVaultHeadingInfo>;
using HeadingOccurrenceMap =
    std::unordered_map<std::string, std::vector<std::string>>;
using HeadingOccurrenceCursor = std::unordered_map<std::string, size_t>;
using AssetIndexMap = std::unordered_map<std::string, AofmVaultAssetInfo>;
using DirChildrenMap = std::unordered_map<std::string, std::vector<std::string>>;

std::string tm_to_std_string(string s);
std::string path_stem(const std::string& path);
std::string path_basename(const std::string& path);
std::string path_dirname(const std::string& path);
std::string join_rel_paths(const std::string& left, const std::string& right);
std::vector<std::string> split_rel_path(const std::string& path);
std::string join_rel_parts(const std::vector<std::string>& parts, size_t start);
std::string normalize_rel_path(const std::string& path);
std::string strip_leading_slash(std::string path);
std::string lower_ascii(std::string s);
std::string asset_key(const std::string& rel_path);
bool parent_dir_of(const std::string& dir, std::string& parent);
std::string relative_path_from_dir(const std::string& from_dir,
                                   const std::string& to_path);
std::string path_stem_without_trailing_separators(const std::string& path);
std::string normalize_heading_target_text(const std::string& s);
std::string heading_map_key(const std::string& file_stem,
                            const std::string& heading);
std::string anchor_occurrence_key(const std::string& rel_ath_path,
                                  const std::string& anchor);
std::string heading_occurrence_key(const std::string& rel_ath_path,
                                   const std::string& label);
std::string normalized_heading_map_key(const std::string& file_stem,
                                       const std::string& heading);
bool is_vault_url_component_unreserved(unsigned char c);
std::string vault_url_component_encode(const std::string& s);
std::string make_wikilink_url(const std::string& uuid,
                              const std::string& file_hint,
                              const std::string& anchor_hint);
std::string scheme_quote_string(const std::string& s);
std::string tree_to_std_string(const tree& t);
tree text_tree(const std::string& s);
tree make_label_tree(const std::string& label);
bool is_label_tree_with(const tree& t, const std::string& label);
bool is_heading_tree(const tree& t);
int heading_level_from_label(const std::string& label);
void append_document(tree& out, tree piece);
void report_import_error(const std::string& message);
void report_import_warning(const std::string& message);
bool is_aofm_anchor_block_placeholder(const tree& t);
bool is_aofm_anchor_inline_placeholder(const tree& t);
std::string placeholder_anchor_id(const tree& t);
tree materialize_anchor_literal(const tree& t);
bool is_enunciation_like_tree(const tree& t);
bool is_theorem_like_tree(const tree& t);
bool can_own_separated_proof_tree(const tree& t);
bool is_separated_proof_tree(const tree& t);
bool is_anchor_placeholder_tree(const tree& t);
std::string plain_anchor_text(const tree& t);
std::string separated_proof_label_prefix(const tree& t);
std::string auto_separated_proof_anchor_id(const tree& t);
bool is_aofm_wikilink_placeholder(const tree& t);
bool is_aofm_transclusion_placeholder(const tree& t);
bool is_aofm_image_placeholder(const tree& t);
bool is_image_target(const std::string& target);
bool is_pdf_target(const std::string& target);
bool is_copyable_asset_target(const std::string& target);

const AofmVaultAssetInfo* find_asset_by_rel_path(const AssetIndexMap& asset_map,
                                                 const std::string& rel_path);
const AofmVaultAssetInfo* resolve_asset_target(const std::string& target,
                                               const std::string& rel_ath_path,
                                               const AssetIndexMap& asset_map,
                                               const DirChildrenMap& dir_children);
tree make_image_embed(const std::string& image_path, const std::string& width);
tree make_pdf_embed(const std::string& target);
const AofmVaultHeadingInfo* find_heading_info_by_label(
  const HeadingMap& heading_map, const std::string& rel_ath_path,
  const std::string& label);
tree resolve_anchor_placeholders(const tree& t, const AnchorMap& anchor_map,
                                 const AnchorOccurrenceMap& anchor_occurrences,
                                 const FileIndexMap& file_map,
                                 const HeadingMap& heading_map,
                                 const HeadingOccurrenceMap& heading_occurrences,
                                 const AssetIndexMap& asset_map,
                                 const DirChildrenMap& dir_children,
                                 const std::string& rel_ath_path,
                                 AnchorOccurrenceCursor* anchor_cursor = nullptr,
                                 HeadingOccurrenceCursor* heading_cursor = nullptr);

std::string trim_copy(const std::string& s);
std::string rtrim_copy(const std::string& s);
bool is_blank_line(const std::string& line);
std::string strip_closing_heading_hashes(std::string title);
bool extract_heading_label(const std::string& line, std::string& target,
                           std::string& label);
std::string collapse_whitespace(const std::string& s);
std::string replace_md_with_ath(const std::string& rel_path);
bool starts_with_blockquote(const std::string& line);
std::string strip_one_blockquote_marker(const std::string& line);
std::string to_lower_ascii(std::string s);
bool starts_with_token(const std::string& s, size_t pos,
                       const std::string& token);
std::string map_basic_callout_type(const std::string& base);
std::string map_extended_callout_type(const std::string& ext);
bool parse_callout_header_line(const std::string& raw,
                               CalloutHeaderInfo& out);
std::string first_bold_segment(const std::string& s);
std::string normalize_callout_auto_title_candidate(std::string s);
bool is_common_callout_auto_title_candidate(const std::string& s);
std::string wikilink_visible_text(const std::string& body);
std::string reduce_markdown_links_to_text(const std::string& s);
bool uses_parenthesized_title(const std::string& type);
bool extract_bold_proof_marker_line(const std::string& raw,
                                    std::string& tag,
                                    std::string& title,
                                    std::string& body);
bool is_cjk_codepoint(char32_t cp);
std::string sanitize_anchor_text(const std::string& s, size_t limit);
std::string make_paragraph_anchor_sample(const std::vector<std::string>& lines);
std::pair<std::string,std::string> make_paragraph_anchor_pair(
  const std::vector<std::string>& lines);
bool line_closes_isolated_proof(const std::string& raw);
std::string strip_isolated_proof_qed_suffix(std::string raw);
std::pair<std::string,std::string> make_proof_anchor_pair(
  const std::vector<std::string>& lines);
std::pair<std::string,std::string> make_callout_anchor_pair(
  const std::vector<std::string>& lines);
std::string anchor_label_key(const std::string& label);
std::string append_anchor_label_number(const std::string& label, int nr);
std::pair<std::string,std::string> make_unique_anchor_pair(
  const std::pair<std::string,std::string>& pair,
  std::unordered_map<std::string,int>& label_counts);
std::string make_unique_label(const std::string& label,
                              std::unordered_map<std::string,int>& label_counts);
bool extract_anchor_only(const std::string& line, std::string& anchor);
bool extract_trailing_anchor(const std::string& line,
                             std::string& content,
                             std::string& anchor);
void store_anchor(AnchorMap& map, AnchorOccurrenceMap& occurrences,
                  const std::string& anchor, const std::string& rel_ath_path,
                  const std::string& file_hint,
                  const std::pair<std::string,std::string>& pair,
                  std::unordered_map<std::string,int>& label_counts,
                  bool deduplicate = true);
void process_markdown_file(const ImportFileInfo& file_info, AnchorMap& map,
                           AnchorOccurrenceMap& occurrences,
                           HeadingOccurrenceMap& heading_occurrences,
                           HeadingMap& heading_map);
void dump_anchor_map(const AnchorMap& map, std::ostream& out);
void dump_heading_map(const HeadingMap& map, std::ostream& out);

bool scan_markdown_files(url source_root, url source_dir, url destination_dir,
                         std::vector<ImportFileInfo>& files,
                         AssetIndexMap& asset_map,
                         DirChildrenMap& dir_children);
bool validate_destination_dir(url destination_root, bool ignore_nonempty);
std::string join_unix_paths(const std::string& root, const std::string& rel);
bool write_vaultfile(const std::string& destination_root_path,
                     const std::string& vault_name,
                     const std::string& prefs_path = "",
                     const std::string& namespace_db_path = "ns.sqlite");
bool load_model_vault_info(const std::string& model_vault,
                           const std::string& destination_root_path,
                           AofmModelVaultInfo& info);
tree apply_model_namespace_style(tree doc, const AofmModelVaultInfo& model,
                                 const ImportFileInfo& file_info);
bool write_vault_database(url destination_root, const FileIndexMap& file_map,
                          const AnchorMap& anchor_map,
                          const HeadingMap& heading_map);

} // namespace aofm_import_vault_internal
