#ifndef AOFM_CALLOUTS_H
#define AOFM_CALLOUTS_H

#include <string>
#include <vector>
#include <peglib.h>
#include <memory>
#include "tree.hpp"

namespace aofm {

using AstPtr = std::shared_ptr<peg::Ast>;

struct CalloutHeader {
  std::string base;
  std::string ext;
  std::string title;
};

bool is_proof_marker_text(const std::string& raw);
std::string preprocess_isolated_callout_proofs(const std::string& raw);
std::string sanitize_markdown_blocks(const std::string& raw);

bool parse_callout_header(const std::string& raw, CalloutHeader& header);
bool map_basic_callout_tag(const std::string& base, std::string& tag, bool& use_quote_env);
bool map_extended_callout_tag(const CalloutHeader& header, std::string& tag, bool& use_quote_env);

tree convert_callout(const AstPtr& ast);
bool extend_theorem_callout_proof(const std::vector<AstPtr>& nodes, size_t index, tree converted, size_t& consumed_to, tree& result);
tree consume_proof(const std::vector<AstPtr>& nodes, size_t start, size_t& consumed_to);

bool strip_proof_qed_suffix(std::string& raw);
tree sanitize_proof_trees(tree t);

bool is_proof_body_block(const AstPtr& ast);
bool can_close_proof_with_qed(const AstPtr& ast);
bool is_qed_math_tree(const tree& t);
tree strip_qed_from_right_edge(tree t);

bool extract_proof_marker_body(const AstPtr& ast, std::string& body);
std::string strip_blockquote_markers(const std::string& raw);
std::string extract_callout_body_source(const std::string& raw);

tree prepend_callout_title(tree body, const std::string& title);
void insert_label_before_trailing_proof(tree& doc, tree label);

bool is_theorem_like_callout_tag(const std::string& tag);
bool is_theorem_like_env_tree(const tree& t);

std::string extract_anchor_id(const std::string& raw);
bool extract_trailing_anchor_inline_text(const std::string& raw, std::string& content, std::string& anchor);

tree split_callout_proof_tail(const std::string& tag, tree body);

} // namespace aofm

#endif
