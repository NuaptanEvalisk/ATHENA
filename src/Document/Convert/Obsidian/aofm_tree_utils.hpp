#ifndef AOFM_TREE_UTILS_H
#define AOFM_TREE_UTILS_H

#include <string>
#include "tree.hpp"

namespace aofm {

tree text_tree(const std::string& s);
tree ensure_document_tree(tree t);
void append_concat(tree& out, tree piece);
void append_document(tree& out, tree piece);

bool is_useless_tree(const tree& t);
std::string tree_to_std_string(const tree& t);

tree make_aofm_anchor_block_placeholder(const std::string& anchor_id);
tree make_aofm_anchor_inline_placeholder(const std::string& anchor_id);
tree make_aofm_wikilink_placeholder(const std::string& target, const std::string& sub, const std::string& alias);
tree make_aofm_transclusion_placeholder(const std::string& target, const std::string& sub, const std::string& alias);
tree make_aofm_image_placeholder(const std::string& target, const std::string& width);

bool is_aofm_anchor_block_placeholder(const tree& t);
bool is_aofm_anchor_inline_placeholder(const tree& t);
bool is_aofm_wikilink_placeholder(const tree& t);
bool is_aofm_transclusion_placeholder(const tree& t);
bool is_aofm_image_placeholder(const tree& t);

tree materialize_aofm_anchor_literals(const tree& t);

} // namespace aofm

#endif
