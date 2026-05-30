#include "aofm_import_vault_internal.hpp"

#include <iostream>

#include "converter.hpp"

namespace aofm_import_vault_internal {

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
anchor_occurrence_key(const std::string& rel_ath_path, const std::string& anchor) {
  return rel_ath_path + "\n" + anchor;
}

std::string
heading_occurrence_key(const std::string& rel_ath_path, const std::string& label) {
  return rel_ath_path + "\n" + label;
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
  return as_tree(aofm::std_to_tm_string(s));
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
  if (label.size() >= 3 && label[0] == 'H' &&
      label[1] >= '1' && label[1] <= '6' &&
      (label[2] == ' ' || label[2] == '\t')) {
    return label[1] - '0';
  }

  int level = 0;
  while (level < (int) label.size() && label[level] == '#') level++;
  if (level == 0 || level > 6) return 0;
  if (level < (int) label.size() &&
      label[level] != ' ' &&
      label[level] != '\t') return 0;
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
         tag == "proof" || tag == "solution" || tag == "solution*" ||
         tag == "law" ||
         tag == "disambiguation" || tag == "proof-alternative" ||
         tag == "proof-standard" || tag == "proof-of";
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
can_own_separated_proof_tree(const tree& t) {
  if (is_theorem_like_tree(t)) return true;
  if (!is_compound(t)) return false;
  std::string tag = std::string(as_charp(as_string(L(t))));
  return tag == "question";
}

bool
is_separated_proof_tree(const tree& t) {
  if (!is_compound(t)) return false;
  std::string tag = std::string(as_charp(as_string(L(t))));
  return tag == "proof" || tag == "proof-alternative" ||
         tag == "proof-standard" || tag == "proof-of" ||
         tag == "solution" || tag == "solution*";
}

bool
is_anchor_placeholder_tree(const tree& t) {
  return is_aofm_anchor_block_placeholder(t) || is_aofm_anchor_inline_placeholder(t);
}

std::string
plain_anchor_text(const tree& t) {
  if (is_atomic(t)) return tree_to_std_string(t);
  if (!is_compound(t)) return "";

  std::string tag = std::string(as_charp(as_string(L(t))));
  if (tag == "label" || tag == "reference" || tag == "pageref" ||
      tag == "image" || tag == "include" || tag == "bibliography" ||
      tag == "transclude") {
    return "";
  }
  if (tag == "hlink" && N(t) > 0) return plain_anchor_text(t[0]);

  std::string out;
  for (int i = 0; i < N(t); ++i) {
    std::string child = plain_anchor_text(t[i]);
    if (child.empty()) continue;
    if (!out.empty()) out += ' ';
    out += child;
  }
  return collapse_whitespace(out);
}

std::string
separated_proof_label_prefix(const tree& t) {
  if (!is_compound(t)) return "proof";
  std::string tag = std::string(as_charp(as_string(L(t))));
  if (tag == "proof-alternative" || tag == "proof-standard") return tag;
  if (tag == "solution" || tag == "solution*") return "solution";
  if (tag == "proof-of" && N(t) >= 1) {
    std::string title = sanitize_anchor_text(tree_to_std_string(t[0]), 80);
    if (!title.empty()) return "proof:" + title;
  }
  return "proof";
}

std::string
auto_separated_proof_anchor_id(const tree& t) {
  std::string prefix = separated_proof_label_prefix(t);
  tree body = "";
  if (is_compound(t, "proof-of", 2)) body = t[1];
  else if (is_compound(t) && N(t) > 0) body = t[0];

  std::string sample = sanitize_anchor_text(plain_anchor_text(body), 100);
  if (sample.empty()) return prefix;
  return prefix + (prefix.find(':') == std::string::npos ? ":" : " ") + sample;
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


} // namespace aofm_import_vault_internal
