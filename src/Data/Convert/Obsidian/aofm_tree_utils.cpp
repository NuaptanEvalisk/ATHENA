#include "aofm_tree_utils.hpp"
#include "aofm_utils.hpp"
#include "converter.hpp"
#include "new_data.hpp"

namespace aofm {

tree
text_tree(const std::string& s) {
  return as_tree(tm_string(s));
}

tree
ensure_document_tree(tree t) {
  if (is_document(t)) return t;
  t = simplify_document(t);
  if (!is_document(t)) t = document(t);
  return t;
}

void
append_concat(tree& out, tree piece) {
  if (is_useless_tree(piece)) return;
  if (is_useless_tree(out)) {
    out = piece;
  } else if (is_func(out, CONCAT)) {
    if (is_func(piece, CONCAT)) out << A(piece);
    else out << piece;
  } else {
    out = concat(out, piece);
  }
}

void
append_document(tree& out, tree piece) {
  if (is_useless_tree(piece)) return;
  if (is_useless_tree(out)) {
    if (is_func(piece, DOCUMENT)) out = piece;
    else out = document(piece);
  } else if (is_func(out, DOCUMENT)) {
    if (is_func(piece, DOCUMENT)) out << A(piece);
    else out << piece;
  } else {
    out = document(out, piece);
  }
}

bool
is_useless_tree(const tree& t) {
  if (is_atomic(t)) return t == "";
  if (is_func(t, CONCAT) || is_func(t, DOCUMENT)) {
    for (int i = 0; i < N(t); ++i) {
      if (!is_useless_tree(t[i])) return false;
    }
    return true;
  }
  return false;
}

std::string
tree_to_std_string(const tree& t) {
  return std::string(as_charp(cork_to_utf8(as_string(t))));
}

tree
make_aofm_anchor_block_placeholder(const std::string& anchor_id) {
  return compound("__aofm_anchor_block", text_tree(anchor_id));
}

tree
make_aofm_anchor_inline_placeholder(const std::string& anchor_id) {
  return compound("__aofm_anchor_inline", text_tree(anchor_id));
}

tree
make_aofm_wikilink_placeholder(const std::string& target,
                               const std::string& sub,
                               const std::string& alias) {
  return compound("__aofm_wikilink", text_tree(target), text_tree(sub), text_tree(alias));
}

tree
make_aofm_transclusion_placeholder(const std::string& target,
                                   const std::string& sub,
                                   const std::string& alias) {
  return compound("__aofm_transclusion", text_tree(target), text_tree(sub), text_tree(alias));
}

tree
make_aofm_image_placeholder(const std::string& target,
                            const std::string& width) {
  return compound("__aofm_image", text_tree(target), text_tree(width));
}

bool
is_aofm_anchor_block_placeholder(const tree& t) {
  return is_compound(t, "__aofm_anchor_block", 1);
}

bool
is_aofm_anchor_inline_placeholder(const tree& t) {
  return is_compound(t, "__aofm_anchor_inline", 1);
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

tree
materialize_aofm_anchor_literals(const tree& t) {
  if (is_aofm_anchor_inline_placeholder(t) || is_aofm_anchor_block_placeholder(t)) {
    return text_tree("^" + tree_to_std_string(t[0]));
  }
  if (is_aofm_image_placeholder(t)) {
    std::string width = tree_to_std_string(t[1]);
    if (!width.empty()) width += "guipx";
    else width = "0.8par";
    tree image = compound("image", t[0], text_tree(width),
                          text_tree(""), text_tree(""), text_tree(""));
    return compound("big-figure", image, text_tree(""));
  }
  if (is_atomic(t)) return t;
  tree out(L(t), N(t));
  for (int i = 0; i < N(t); ++i) {
    out[i] = materialize_aofm_anchor_literals(t[i]);
  }
  return out;
}

} // namespace aofm
