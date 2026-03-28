#include <peglib.h>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "convert.hpp"
#include "file.hpp"
#include "new_data.hpp"
#include "tm_configure.hpp"
#include "tree.hpp"
#include "url.hpp"

extern const char* aofm_grammar;
std::shared_ptr<peg::Ast> aofm_parse_file(const std::string& file_path);

namespace {

using AstPtr = std::shared_ptr<peg::Ast>;

std::string aofm_content;

string
tm_string(const std::string& s) {
  return string(s.c_str());
}

bool
ast_is(const AstPtr& ast, const char* rule_name) {
  return ast &&
         (ast->original_name == rule_name || ast->name == rule_name);
}

bool
ast_name_is(const AstPtr& ast, const char* rule_name) {
  return ast && ast->name == rule_name;
}

std::string
ast_source(const AstPtr& ast) {
  if (!ast) return "";
  return aofm_content.substr(ast->position, ast->length);
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
strip_trailing_newlines(const std::string& s) {
  size_t end = s.size();
  while (end > 0 && (s[end - 1] == '\n' || s[end - 1] == '\r')) {
    end--;
  }
  return s.substr(0, end);
}

std::string
strip_wrapping(const std::string& s, size_t left, size_t right) {
  if (s.size() < left + right) return s;
  return s.substr(left, s.size() - left - right);
}

bool
is_blank_line(const std::string& line) {
  return trim_copy(line).empty();
}

bool
erase_prefix(std::string& s, const char* prefix) {
  std::string p = prefix;
  if (s.compare(0, p.size(), p) != 0) return false;
  s.erase(0, p.size());
  return true;
}

std::string
strip_known_invisible_prefixes(std::string line) {
  while (true) {
    bool changed = false;
    changed = erase_prefix(line, "\xEF\xBB\xBF") || changed;
    changed = erase_prefix(line, "\xE2\x80\x8B") || changed;
    changed = erase_prefix(line, "\xEF\xBF\xBC") || changed;
    if (!changed) break;
  }
  return line;
}

std::string
normalize_markdown_lines(const std::string& raw) {
  std::vector<std::string> lines;
  std::stringstream in(raw);
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    lines.push_back(strip_known_invisible_prefixes(line));
  }

  std::string result;
  for (size_t i = 0; i < lines.size(); ++i) {
    if (i > 0) result += '\n';
    result += lines[i];
  }
  return result;
}

bool
is_anchor_line(const std::string& line) {
  std::string trimmed = trim_copy(line);
  return !trimmed.empty() && trimmed[0] == '^';
}

size_t
leading_space_count(const std::string& line) {
  size_t pos = 0;
  while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t')) pos++;
  return pos;
}

bool
starts_blockquote_line(const std::string& line) {
  size_t pos = leading_space_count(line);
  return pos < line.size() && line[pos] == '>';
}

bool
starts_callout_header_line(const std::string& line) {
  size_t pos = leading_space_count(line);
  if (pos >= line.size() || line[pos] != '>') return false;
  pos++;
  while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t')) pos++;
  return line.compare(pos, 3, "[!") == 0;
}

std::string
strip_one_blockquote_marker(const std::string& line) {
  size_t pos = leading_space_count(line);
  if (pos >= line.size() || line[pos] != '>') return line;
  pos++;
  if (pos < line.size() && line[pos] == ' ') pos++;
  return line.substr(pos);
}

bool
is_proof_marker_text(const std::string& raw) {
  static const char* kMarkers[] = {
      "**Proof:**",
      "**Proof：**",
      "**Solution:**",
      "**Solution：**",
      "**证明:**",
      "**证明：**",
      "**解:**",
      "**解：**"
  };

  std::string trimmed = trim_copy(raw);
  for (const char* marker : kMarkers) {
    std::string prefix = marker;
    if (trimmed.compare(0, prefix.size(), prefix) != 0) continue;
    if (trimmed.size() == prefix.size()) return true;
    char next = trimmed[prefix.size()];
    if (next == ' ' || next == '\t' || next == '\r' || next == '\n') {
      return true;
    }
  }
  return false;
}

std::string
preprocess_isolated_callout_proofs(const std::string& raw) {
  std::vector<std::string> lines;
  std::stringstream in(raw);
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    lines.push_back(line);
  }

  std::vector<std::string> out;

  for (size_t i = 0; i < lines.size(); ) {
    if (!starts_callout_header_line(lines[i])) {
      out.push_back(lines[i]);
      ++i;
      continue;
    }

    size_t block_end = i + 1;
    while (block_end < lines.size() && starts_blockquote_line(lines[block_end])) {
      block_end++;
    }

    size_t proof_index = block_end;
    for (size_t j = i + 1; j < block_end; ++j) {
      if (is_proof_marker_text(strip_one_blockquote_marker(lines[j]))) {
        proof_index = j;
        break;
      }
    }

    if (proof_index == block_end) {
      for (size_t j = i; j < block_end; ++j) out.push_back(lines[j]);
      i = block_end;
      continue;
    }

    std::vector<std::string> theorem_lines;
    for (size_t j = i; j < proof_index; ++j) theorem_lines.push_back(lines[j]);

    std::vector<std::string> proof_lines;
    for (size_t j = proof_index; j < block_end; ++j) {
      proof_lines.push_back(strip_one_blockquote_marker(lines[j]));
    }

    size_t cursor = block_end;
    bool saw_blank_after_callout = false;
    while (cursor < lines.size() && is_blank_line(lines[cursor])) {
      saw_blank_after_callout = true;
      cursor++;
    }

    std::string moved_anchor;
    if (saw_blank_after_callout && cursor < lines.size() &&
        is_anchor_line(lines[cursor])) {
      moved_anchor = lines[cursor];
      cursor++;
    } else {
      cursor = block_end;
    }

    for (const auto& theorem_line : theorem_lines) out.push_back(theorem_line);
    out.push_back("");
    if (!moved_anchor.empty()) {
      out.push_back(moved_anchor);
      out.push_back("");
    }
    for (const auto& proof_line : proof_lines) out.push_back(proof_line);

    i = cursor;
  }

  std::string result;
  for (size_t i = 0; i < out.size(); ++i) {
    if (i > 0) result += '\n';
    result += out[i];
  }
  return result;
}

bool
ends_with(const std::string& s, const std::string& suffix) {
  return s.size() >= suffix.size() &&
         s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

tree
text_tree(const std::string& s) {
  return as_tree(tm_string(s));
}

tree
ensure_document_tree(tree t) {
  t = simplify_document(t);
  if (!is_document(t)) t = document(t);
  return t;
}

void
append_concat(tree& out, tree piece) {
  if (piece == "") return;
  if (is_concat(piece)) out << A(piece);
  else out << piece;
}

void
append_document(tree& out, tree piece) {
  if (piece == "") return;
  if (is_document(piece)) out << A(piece);
  else out << piece;
}

tree convert_inline(const AstPtr& ast);
tree convert_block(const AstPtr& ast);

tree
convert_latex_math_inline(const std::string& latex_source) {
  tree converted = extract(
      tracked_latex_to_texmacs(tm_string("$" + latex_source + "$"), false),
      "body");

  converted = simplify_document(converted);

  if (is_func(converted, DOCUMENT) && N(converted) > 0) {
    converted = converted[0];
  }

  if (is_compound(converted, "math", 1)) {
    return converted;
  }

  if (is_func(converted, WITH) && N(converted) >= 3 &&
      converted[0] == "mode" && converted[1] == "math") {
    converted = converted[N(converted) - 1];
  }

  return compound("math", converted);
}

tree
convert_latex_math_display(const std::string& latex_source) {
  tree converted = extract(
      tracked_latex_to_texmacs(tm_string("$$" + latex_source + "$$"), false),
      "body");

  if (is_document(converted)) {
    return simplify_document(converted);
  }

  return converted;
}

tree
convert_inline_children(const AstPtr& ast) {
  tree out(CONCAT);
  if (!ast) return out;
  for (const auto& child : ast->nodes) {
    append_concat(out, convert_inline(child));
  }
  return simplify_concat(out);
}

tree
convert_inline_from_raw(const std::string& raw) {
  tree out(CONCAT);
  std::string text;
  size_t i = 0;

  auto flush_text = [&]() {
    if (text.empty()) return;
    append_concat(out, text_tree(text));
    text.clear();
  };

  while (i < raw.size()) {
    if (raw.compare(i, 3, "***") == 0) {
      size_t close = raw.find("***", i + 3);
      if (close != std::string::npos) {
        flush_text();
        append_concat(out, compound("strong",
                                    compound("em",
                                             convert_inline_from_raw(
                                                 raw.substr(i + 3, close - i - 3)))));
        i = close + 3;
        continue;
      }
    }

    if (raw.compare(i, 2, "**") == 0) {
      size_t inner_italic = raw.find('*', i + 2);
      size_t close_triple = raw.find("***", i + 3);
      if (inner_italic != std::string::npos &&
          close_triple != std::string::npos &&
          inner_italic < close_triple) {
        flush_text();
        tree strong_body(CONCAT);
        append_concat(strong_body,
                      convert_inline_from_raw(
                          raw.substr(i + 2, inner_italic - i - 2)));
        append_concat(strong_body,
                      compound("em",
                               convert_inline_from_raw(
                                   raw.substr(inner_italic + 1,
                                              close_triple - inner_italic - 1))));
        append_concat(out, compound("strong", simplify_concat(strong_body)));
        i = close_triple + 3;
        continue;
      }

      size_t close = raw.find("**", i + 2);
      if (close != std::string::npos) {
        flush_text();
        append_concat(out, compound("strong",
                                    convert_inline_from_raw(
                                        raw.substr(i + 2, close - i - 2))));
        i = close + 2;
        continue;
      }
    }

    if (raw[i] == '*') {
      size_t inner_strong = raw.find("**", i + 1);
      size_t close_triple = raw.find("***", i + 2);
      if (inner_strong != std::string::npos &&
          close_triple != std::string::npos &&
          inner_strong < close_triple) {
        flush_text();
        tree em_body(CONCAT);
        append_concat(em_body,
                      convert_inline_from_raw(
                          raw.substr(i + 1, inner_strong - i - 1)));
        append_concat(em_body,
                      compound("strong",
                               convert_inline_from_raw(
                                   raw.substr(inner_strong + 2,
                                              close_triple - inner_strong - 2))));
        append_concat(out, compound("em", simplify_concat(em_body)));
        i = close_triple + 3;
        continue;
      }

      size_t close = raw.find('*', i + 1);
      if (close != std::string::npos) {
        flush_text();
        append_concat(out, compound("em",
                                    convert_inline_from_raw(
                                        raw.substr(i + 1, close - i - 1))));
        i = close + 1;
        continue;
      }
    }

    if (raw[i] == '`') {
      size_t close = raw.find('`', i + 1);
      if (close != std::string::npos) {
        flush_text();
        append_concat(out, compound("verbatim",
                                    text_tree(raw.substr(i + 1, close - i - 1))));
        i = close + 1;
        continue;
      }
    }

    if (raw[i] == '$') {
      size_t close = raw.find('$', i + 1);
      if (close != std::string::npos) {
        flush_text();
        append_concat(out, convert_latex_math_inline(
                               raw.substr(i + 1, close - i - 1)));
        i = close + 1;
        continue;
      }
    }

    if (raw[i] == '\\' && i + 1 < raw.size()) {
      text += raw[i + 1];
      i += 2;
      continue;
    }

    text += raw[i];
    i++;
  }

  flush_text();
  return simplify_concat(out);
}

tree
convert_emphasis_like(const AstPtr& ast, const char* tag,
                      size_t left, size_t right) {
  std::string raw = ast_source(ast);
  return compound(tag, convert_inline_from_raw(strip_wrapping(raw, left, right)));
}

tree
convert_inline(const AstPtr& ast) {
  if (!ast) return "";

  if (ast_is(ast, "NL")) {
    return "";
  }

  if (ast_name_is(ast, "Inline")) {
    tree out(CONCAT);
    for (const auto& child : ast->nodes) {
      append_concat(out, convert_inline(child));
    }
    return simplify_concat(out);
  }

  if (ast_is(ast, "Text") || ast_is(ast, "AnyChar")) {
    return text_tree(ast_source(ast));
  }

  if (ast_is(ast, "EscapeChar")) {
    std::string raw = ast_source(ast);
    if (!raw.empty() && raw[0] == '\\') raw.erase(0, 1);
    return text_tree(raw);
  }

  if (ast_is(ast, "Strong")) {
    return convert_emphasis_like(ast, "strong", 2, 2);
  }

  if (ast_is(ast, "Italic")) {
    return convert_emphasis_like(ast, "em", 1, 1);
  }

  if (ast_is(ast, "TripleBoth") ||
      ast_is(ast, "TripleItalicOuter") ||
      ast_is(ast, "TripleStrongOuter") ||
      ast_is(ast, "TripleRightItalic") ||
      ast_is(ast, "TripleRightStrong")) {
    std::string raw = strip_wrapping(ast_source(ast), 3, 3);
    return compound("strong", compound("em", convert_inline_from_raw(raw)));
  }

  if (ast_is(ast, "InlineCode")) {
    return compound("verbatim",
                    text_tree(strip_wrapping(ast_source(ast), 1, 1)));
  }

  if (ast_is(ast, "InlineMath")) {
    return convert_latex_math_inline(strip_wrapping(ast_source(ast), 1, 1));
  }

  if (ast_is(ast, "Strikethrough") || ast_is(ast, "Highlight") ||
      ast_is(ast, "WikiLink") || ast_is(ast, "Transclusion") ||
      ast_is(ast, "Image") || ast_is(ast, "PDF") ||
      ast_is(ast, "ExtLink") || ast_is(ast, "InlineAnchor")) {
    return text_tree(ast_source(ast));
  }

  if (!ast->nodes.empty()) {
    tree out(CONCAT);
    for (const auto& child : ast->nodes) {
      append_concat(out, convert_inline(child));
    }
    return simplify_concat(out);
  }

  return text_tree(ast_source(ast));
}

tree
convert_paragraph(const AstPtr& ast) {
  std::stringstream in(strip_trailing_newlines(ast_source(ast)));
  std::string line;
  std::string chunk;
  tree out(DOCUMENT);

  auto flush_chunk = [&]() {
    if (chunk.empty()) return;
    out << convert_inline_from_raw(chunk);
    chunk.clear();
  };

  while (std::getline(in, line)) {
    if (trim_copy(line).empty()) {
      flush_chunk();
      continue;
    }
    if (!chunk.empty()) chunk += ' ';
    chunk += line;
  }

  flush_chunk();
  return simplify_document(out);
}

bool
extract_proof_marker_body(const AstPtr& ast, std::string& body) {
  if (!ast_is(ast, "Paragraph")) return false;
  std::string raw = trim_copy(strip_trailing_newlines(ast_source(ast)));
  static const char* kMarkers[] = {
      "**Proof:**",
      "**Proof：**",
      "**Solution:**",
      "**Solution：**",
      "**证明:**",
      "**证明：**",
      "**解:**",
      "**解：**"
  };

  for (const char* marker : kMarkers) {
    std::string prefix = marker;
    if (raw.compare(0, prefix.size(), prefix) != 0) continue;
    if (raw.size() > prefix.size() &&
        raw[prefix.size()] != ' ' &&
        raw[prefix.size()] != '\t' &&
        raw[prefix.size()] != '\r' &&
        raw[prefix.size()] != '\n') {
      continue;
    }
    body = trim_copy(raw.substr(prefix.size()));
    return true;
  }

  return false;
}

AstPtr
block_payload(const AstPtr& ast) {
  if (!ast) return nullptr;
  if (!ast_name_is(ast, "Block")) return ast;
  for (const auto& child : ast->nodes) {
    if (!ast_is(child, "BlankLine")) return child;
  }
  return nullptr;
}

bool
is_proof_body_block(const AstPtr& ast) {
  return ast_is(ast, "Paragraph") || ast_is(ast, "Blockquote") ||
         ast_is(ast, "List") || ast_is(ast, "CodeBlock") ||
         ast_is(ast, "MathBlock") || ast_is(ast, "Table") ||
         ast_is(ast, "Callout");
}

bool
can_close_proof_with_qed(const AstPtr& ast) {
  return ast_is(ast, "Paragraph") || ast_is(ast, "Blockquote") ||
         ast_is(ast, "List") || ast_is(ast, "CodeBlock") ||
         ast_is(ast, "MathBlock") || ast_is(ast, "Table");
}

bool
strip_proof_qed_suffix(std::string& raw) {
  std::string trimmed = trim_copy(strip_trailing_newlines(raw));
  static const char* kSuffixes[] = {
      "$\\blacksquare$",
      "$\\blacksquare$.",
      "$\\blacksquare$。"
  };

  for (const char* suffix : kSuffixes) {
    std::string s = suffix;
    if (!ends_with(trimmed, s)) continue;
    trimmed.erase(trimmed.size() - s.size());
    raw = trim_copy(trimmed);
    return true;
  }
  return false;
}

bool
is_qed_math_tree(const tree& t) {
  return is_compound(t, "math", 1) &&
         tree_to_texmacs(t) == "<math|\\<blacksquare\\>>";
}

tree
strip_qed_from_right_edge(tree t) {
  if (t == "") return t;
  if (is_qed_math_tree(t)) return "";

  if (is_concat(t)) {
    tree out(CONCAT);
    for (int i = 0; i < N(t); ++i) {
      tree child = t[i];
      if (i == N(t) - 1) child = strip_qed_from_right_edge(child);
      if (child != "") out << child;
    }
    return simplify_concat(out);
  }

  if (is_document(t)) {
    tree out(DOCUMENT);
    for (int i = 0; i < N(t); ++i) {
      tree child = t[i];
      if (i == N(t) - 1) child = strip_qed_from_right_edge(child);
      append_document(out, child);
    }
    return simplify_document(out);
  }

  return t;
}

tree
sanitize_proof_trees(tree t) {
  if (t == "") return t;

  if (is_document(t)) {
    tree out(DOCUMENT);
    for (int i = 0; i < N(t); ++i) {
      append_document(out, sanitize_proof_trees(t[i]));
    }
    return simplify_document(out);
  }

  if (is_compound(t, "proof", 1)) {
    tree body = sanitize_proof_trees(ensure_document_tree(t[0]));
    body = strip_qed_from_right_edge(body);
    return compound("proof", ensure_document_tree(body));
  }

  if (is_compound(t, "proof-alternative", 1)) {
    tree body = sanitize_proof_trees(ensure_document_tree(t[0]));
    body = strip_qed_from_right_edge(body);
    return compound("proof-alternative", ensure_document_tree(body));
  }

  if (is_compound(t, "proof-standard", 1)) {
    tree body = sanitize_proof_trees(ensure_document_tree(t[0]));
    body = strip_qed_from_right_edge(body);
    return compound("proof-standard", ensure_document_tree(body));
  }

  if (is_compound(t, "theorem", 1)) {
    return compound("theorem", ensure_document_tree(sanitize_proof_trees(t[0])));
  }
  if (is_compound(t, "proposition", 1)) {
    return compound("proposition", ensure_document_tree(sanitize_proof_trees(t[0])));
  }
  if (is_compound(t, "lemma", 1)) {
    return compound("lemma", ensure_document_tree(sanitize_proof_trees(t[0])));
  }
  if (is_compound(t, "corollary", 1)) {
    return compound("corollary", ensure_document_tree(sanitize_proof_trees(t[0])));
  }
  if (is_compound(t, "remark", 1)) {
    return compound("remark", ensure_document_tree(sanitize_proof_trees(t[0])));
  }
  if (is_compound(t, "example", 1)) {
    return compound("example", ensure_document_tree(sanitize_proof_trees(t[0])));
  }
  if (is_compound(t, "definition", 1)) {
    return compound("definition", ensure_document_tree(sanitize_proof_trees(t[0])));
  }
  if (is_compound(t, "axiom", 1)) {
    return compound("axiom", ensure_document_tree(sanitize_proof_trees(t[0])));
  }
  if (is_compound(t, "conjecture", 1)) {
    return compound("conjecture", ensure_document_tree(sanitize_proof_trees(t[0])));
  }
  if (is_compound(t, "law", 1)) {
    return compound("law", ensure_document_tree(sanitize_proof_trees(t[0])));
  }
  if (is_compound(t, "note", 1)) {
    return compound("note", ensure_document_tree(sanitize_proof_trees(t[0])));
  }
  if (is_compound(t, "question", 1)) {
    return compound("question", ensure_document_tree(sanitize_proof_trees(t[0])));
  }
  if (is_compound(t, "warning", 1)) {
    return compound("warning", ensure_document_tree(sanitize_proof_trees(t[0])));
  }
  if (is_compound(t, "disambiguation", 1)) {
    return compound("disambiguation", ensure_document_tree(sanitize_proof_trees(t[0])));
  }
  if (is_compound(t, "quote-env", 1)) {
    return compound("quote-env", ensure_document_tree(sanitize_proof_trees(t[0])));
  }

  return t;
}

bool is_theorem_like_env_tree(const tree& t);

void
insert_label_before_trailing_proof(tree& doc, tree label) {
  if (!is_document(doc) || label == "") return;
  if (N(doc) < 2 ||
      !is_compound(doc[N(doc) - 1], "proof", 1) ||
      !is_theorem_like_env_tree(doc[N(doc) - 2])) {
    append_document(doc, label);
    return;
  }

  tree out(DOCUMENT);
  for (int i = 0; i < N(doc) - 1; ++i) out << doc[i];
  append_document(out, label);
  out << doc[N(doc) - 1];
  doc = simplify_document(out);
}

std::string
strip_heading_text(const std::string& raw) {
  size_t pos = 0;
  while (pos < raw.size() && raw[pos] == '#') pos++;
  while (pos < raw.size() && (raw[pos] == ' ' || raw[pos] == '\t')) pos++;
  return trim_copy(strip_trailing_newlines(raw.substr(pos)));
}

tree
convert_heading(const AstPtr& ast) {
  std::string raw = ast_source(ast);
  int level = 0;
  while (level < static_cast<int>(raw.size()) && raw[level] == '#') level++;

  std::string title = strip_heading_text(raw);
  const char* tag = "section";
  switch (level) {
    case 1: tag = "section"; break;
    case 2: tag = "subsection"; break;
    case 3: tag = "subsubsection"; break;
    case 4: tag = "paragraph"; break;
    default: tag = "subparagraph"; break;
  }
  return compound(tag, text_tree(title));
}

std::string
extract_fenced_body(const std::string& raw, const std::string& fence) {
  size_t body_start = raw.find('\n');
  if (body_start == std::string::npos) return "";
  size_t body_end = raw.rfind(fence);
  if (body_end == std::string::npos || body_end <= body_start) {
    return strip_trailing_newlines(raw.substr(body_start + 1));
  }
  return strip_trailing_newlines(raw.substr(body_start + 1,
      body_end - body_start - 1));
}

tree
convert_code_block(const AstPtr& ast) {
  return compound("code",
                  text_tree(extract_fenced_body(ast_source(ast), "```")));
}

tree
convert_math_block(const AstPtr& ast) {
  return convert_latex_math_display(
      extract_fenced_body(ast_source(ast), "$$"));
}

std::string
strip_blockquote_markers(const std::string& raw) {
  std::stringstream in(raw);
  std::string line;
  std::string out;
  bool first = true;

  while (std::getline(in, line)) {
    size_t pos = 0;
    while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t')) pos++;
    if (pos < line.size() && line[pos] == '>') {
      pos++;
      if (pos < line.size() && line[pos] == ' ') pos++;
    }
    if (!first) out += '\n';
    out += line.substr(pos);
    first = false;
  }

  return trim_copy(out);
}

tree
parse_embedded_aofm_blocks(const std::string& raw, const std::string& source_name) {
  std::string saved_content = aofm_content;
  std::string embedded_content = raw;
  if (!embedded_content.empty() && embedded_content.back() != '\n') {
    embedded_content += '\n';
  }

  peg::parser parser(aofm_grammar);
  if (!parser) {
    return text_tree(trim_copy(strip_trailing_newlines(raw)));
  }

  parser.enable_ast();
  parser.set_logger([](size_t line, size_t col, const std::string& msg,
                       const std::string& rule) {
    std::cerr << "AOFM Parse Error at " << line << ":" << col
              << " (Rule: " << rule << "): " << msg << std::endl;
  });

  AstPtr ast;
  aofm_content.swap(embedded_content);
  bool ok = parser.parse(aofm_content, ast, source_name.c_str());
  tree out = ok ? convert_block(ast)
                : text_tree(trim_copy(strip_trailing_newlines(raw)));
  aofm_content.swap(saved_content);
  return out;
}

tree
convert_blockquote_body(const std::string& raw, const std::string& source_name) {
  return parse_embedded_aofm_blocks(strip_blockquote_markers(raw), source_name);
}

tree
convert_blockquote(const AstPtr& ast) {
  tree body = convert_blockquote_body(ast_source(ast), "blockquote");
  return compound("quote-env", ensure_document_tree(body));
}

std::string
strip_anchor_lines(const std::string& raw) {
  std::stringstream in(raw);
  std::string line;
  std::string out;
  bool first = true;

  while (std::getline(in, line)) {
    std::string trimmed = trim_copy(line);
    if (!trimmed.empty() && trimmed[0] == '^') continue;
    if (!first) out += '\n';
    out += line;
    first = false;
  }

  return trim_copy(out);
}

std::string
extract_anchor_id(const std::string& raw) {
  std::string trimmed = trim_copy(strip_trailing_newlines(raw));
  if (!trimmed.empty() && trimmed[0] == '^') trimmed.erase(0, 1);
  return trim_copy(trimmed);
}

struct CalloutHeader {
  std::string base;
  std::string ext;
  std::string title;
};

bool
is_space_or_tab(char c) {
  return c == ' ' || c == '\t';
}

void
skip_spaces(std::string::size_type& pos, const std::string& s) {
  while (pos < s.size() && is_space_or_tab(s[pos])) pos++;
}

bool
consume_prefix(std::string::size_type& pos, const std::string& s,
               const std::string& prefix) {
  if (s.compare(pos, prefix.size(), prefix) != 0) return false;
  pos += prefix.size();
  return true;
}

bool
starts_with_token(const std::string& s, std::string::size_type pos,
                  const std::string& token) {
  if (s.compare(pos, token.size(), token) != 0) return false;
  std::string::size_type end = pos + token.size();
  return end >= s.size() || is_space_or_tab(s[end]);
}

bool
parse_callout_header(const std::string& raw, CalloutHeader& header) {
  size_t nl = raw.find('\n');
  std::string line = nl == std::string::npos ? raw : raw.substr(0, nl);
  std::string::size_type pos = 0;

  skip_spaces(pos, line);
  if (!consume_prefix(pos, line, ">")) return false;
  skip_spaces(pos, line);
  if (!consume_prefix(pos, line, "[!")) return false;

  std::string::size_type close = line.find(']', pos);
  if (close == std::string::npos) return false;
  header.base = line.substr(pos, close - pos);
  pos = close + 1;

  if (pos < line.size() && (line[pos] == '+' || line[pos] == '-')) pos++;
  skip_spaces(pos, line);

  static const char* kCalloutExts[] = {
      "Alternative Proof", "Standard Steps", "Disambiguation",
      "Proposition", "Corollary", "Conjecture", "Definition",
      "Question", "Theorem", "Example", "Caution", "Remark",
      "Paster", "Axiom", "Lemma", "Law"};

  header.ext.clear();
  for (const char* ext : kCalloutExts) {
    if (starts_with_token(line, pos, ext)) {
      header.ext = ext;
      pos += header.ext.size();
      break;
    }
  }

  skip_spaces(pos, line);
  header.title = trim_copy(line.substr(pos));
  return true;
}

bool
map_basic_callout_tag(const std::string& base, std::string& tag,
                      bool& use_quote_env) {
  use_quote_env = false;

  if (base == "question" || base == "help" || base == "faq") {
    tag = "question";
    return true;
  }

  if (base == "warning" || base == "caution" || base == "attention" ||
      base == "failure" || base == "fail" || base == "missing" ||
      base == "danger" || base == "error" || base == "bug") {
    tag = "warning";
    return true;
  }

  if (base == "example") {
    tag = "example";
    return true;
  }

  if (base == "quote" || base == "cite") {
    use_quote_env = true;
    return true;
  }

  if (base == "abstract" || base == "note" || base == "summary" ||
      base == "tldr" || base == "info" || base == "todo" ||
      base == "tip" || base == "hint" || base == "important" ||
      base == "success" || base == "check" || base == "done") {
    tag = "note";
    return true;
  }

  return false;
}

bool
map_extended_callout_tag(const CalloutHeader& header, std::string& tag,
                         bool& use_quote_env) {
  use_quote_env = false;

  if (header.base == "abstract" && header.ext == "Definition") {
    tag = "definition";
    return true;
  }
  if (header.base == "example" && header.ext == "Example") {
    tag = "example";
    return true;
  }
  if (header.base == "question" && header.ext == "Question") {
    tag = "question";
    return true;
  }
  if (header.base == "question" && header.ext == "Conjecture") {
    tag = "conjecture";
    return true;
  }
  if (header.base == "note" && header.ext == "Theorem") {
    tag = "theorem";
    return true;
  }
  if (header.base == "note" && header.ext == "Proposition") {
    tag = "proposition";
    return true;
  }
  if (header.base == "note" && header.ext == "Lemma") {
    tag = "lemma";
    return true;
  }
  if (header.base == "note" && header.ext == "Corollary") {
    tag = "corollary";
    return true;
  }
  if (header.base == "abstract" && header.ext == "Axiom") {
    tag = "axiom";
    return true;
  }
  if (header.base == "note" && header.ext == "Remark") {
    tag = "remark";
    return true;
  }
  if (header.base == "done" && header.ext == "Alternative Proof") {
    tag = "proof-alternative";
    return true;
  }
  if (header.base == "caution" && header.ext == "Caution") {
    tag = "warning";
    return true;
  }
  if (header.base == "done" && header.ext == "Standard Steps") {
    tag = "proof-standard";
    return true;
  }
  if (header.base == "note" && header.ext == "Law") {
    tag = "law";
    return true;
  }
  if (header.base == "cite" && header.ext == "Paster") {
    use_quote_env = true;
    return true;
  }
  if (header.base == "tip" && header.ext == "Disambiguation") {
    tag = "disambiguation";
    return true;
  }

  return false;
}

bool
is_theorem_like_callout_tag(const std::string& tag) {
  return tag == "theorem" || tag == "proposition" || tag == "lemma" ||
         tag == "corollary" || tag == "remark" || tag == "example" ||
         tag == "definition" || tag == "axiom" || tag == "conjecture" ||
         tag == "law";
}

bool
is_theorem_like_env_tree(const tree& t) {
  return is_compound(t, "theorem", 1) || is_compound(t, "proposition", 1) ||
         is_compound(t, "lemma", 1) || is_compound(t, "corollary", 1) ||
         is_compound(t, "remark", 1) || is_compound(t, "example", 1) ||
         is_compound(t, "definition", 1) || is_compound(t, "axiom", 1) ||
         is_compound(t, "conjecture", 1) || is_compound(t, "law", 1);
}

tree
split_callout_proof_tail(const std::string& tag, tree body) {
  if (!is_document(body) || !is_theorem_like_callout_tag(tag)) {
    return compound(tag.c_str(), ensure_document_tree(body));
  }

  int proof_index = -1;
  for (int i = 0; i < N(body); ++i) {
    if (is_compound(body[i], "proof", 1)) {
      proof_index = i;
      break;
    }
  }

  if (proof_index < 0) {
    return compound(tag.c_str(), ensure_document_tree(body));
  }

  tree head(DOCUMENT);
  tree tail(DOCUMENT);
  for (int i = 0; i < proof_index; ++i) head << body[i];
  for (int i = proof_index; i < N(body); ++i) tail << body[i];

  tree out(DOCUMENT);
  out << compound(tag.c_str(), ensure_document_tree(head));
  append_document(out, simplify_document(tail));
  return simplify_document(out);
}

bool
extend_theorem_callout_proof(const std::vector<AstPtr>& nodes, size_t index,
                             tree converted, size_t& consumed_to,
                             tree& result) {
  tree doc = ensure_document_tree(converted);
  if (!is_document(doc) || N(doc) < 2) return false;
  if (!is_theorem_like_env_tree(doc[0])) return false;
  if (!is_compound(doc[N(doc) - 1], "proof", 1)) return false;

  size_t cursor = index + 1;
  tree moved_labels(DOCUMENT);
  bool saw_anchor = false;
  while (cursor < nodes.size()) {
    AstPtr payload = block_payload(nodes[cursor]);
    if (!payload) {
      ++cursor;
      continue;
    }
    if (!ast_is(payload, "AnchorBlock")) break;
    moved_labels << compound("label",
                             text_tree(extract_anchor_id(ast_source(payload))));
    saw_anchor = true;
    ++cursor;
  }
  if (!saw_anchor) return false;

  while (cursor < nodes.size()) {
    AstPtr payload = block_payload(nodes[cursor]);
    if (!payload) {
      ++cursor;
      continue;
    }
    break;
  }

  if (cursor >= nodes.size()) return false;
  AstPtr payload = block_payload(nodes[cursor]);
  if (!payload || !is_proof_body_block(payload)) return false;

  tree continuation(DOCUMENT);
  size_t j = cursor;
  bool closed = false;
  while (j < nodes.size()) {
    AstPtr body_payload = block_payload(nodes[j]);
    if (!body_payload) {
      ++j;
      continue;
    }
    if (!is_proof_body_block(body_payload)) break;

    size_t k = j + 1;
    while (k < nodes.size()) {
      AstPtr next_payload = block_payload(nodes[k]);
      if (!next_payload) {
        ++k;
        continue;
      }
      break;
    }

    AstPtr next_payload =
        k < nodes.size() ? block_payload(nodes[k]) : nullptr;
    if (!next_payload || !is_proof_body_block(next_payload)) {
      std::string raw = ast_source(body_payload);
      if (strip_proof_qed_suffix(raw)) {
        if (!raw.empty()) {
          append_document(continuation,
                          parse_embedded_aofm_blocks(raw, "proof-body"));
        }
        ++j;
        closed = true;
        break;
      }
    }

    append_document(continuation, convert_block(nodes[j]));
    ++j;
  }

  if (!closed || N(continuation) == 0) return false;

  tree proof_body = ensure_document_tree(doc[N(doc) - 1][0]);
  append_document(proof_body, continuation);

  tree out(DOCUMENT);
  out << doc[0];
  append_document(out, simplify_document(moved_labels));
  out << compound("proof", ensure_document_tree(proof_body));
  result = simplify_document(out);
  consumed_to = j - 1;
  return true;
}

tree
consume_proof(const std::vector<AstPtr>& nodes, size_t start,
              size_t& consumed_to) {
  struct ProofFrame {
    tree body;
  };

  std::string first_chunk;
  if (start >= nodes.size() ||
      !extract_proof_marker_body(block_payload(nodes[start]), first_chunk)) {
    consumed_to = start;
    return "";
  }

  std::vector<ProofFrame> stack;
  stack.push_back(ProofFrame { tree(DOCUMENT) });
  if (!first_chunk.empty()) {
    stack.back().body << convert_inline_from_raw(first_chunk);
  }

  size_t j = start + 1;
  while (j < nodes.size() && !stack.empty()) {
    AstPtr payload = block_payload(nodes[j]);
    if (!payload) {
      ++j;
      continue;
    }

    if (ast_is(payload, "AnchorBlock")) {
      tree label = compound("label", text_tree(extract_anchor_id(ast_source(payload))));
      insert_label_before_trailing_proof(stack.back().body, label);
      ++j;
      continue;
    }

    std::string nested_chunk;
    if (extract_proof_marker_body(payload, nested_chunk)) {
      stack.push_back(ProofFrame { tree(DOCUMENT) });
      if (!nested_chunk.empty()) {
        stack.back().body << convert_inline_from_raw(nested_chunk);
      }
      ++j;
      continue;
    }

    if (!is_proof_body_block(payload)) break;

    if (ast_is(payload, "Callout") || ast_is(payload, "Blockquote")) {
      tree converted_callout = convert_block(nodes[j]);
      size_t extended_to = j;
      tree extended_callout;
      if (extend_theorem_callout_proof(nodes, j, converted_callout,
                                       extended_to, extended_callout)) {
        append_document(stack.back().body, extended_callout);
        j = extended_to + 1;
        continue;
      }
      append_document(stack.back().body, converted_callout);
      ++j;
      continue;
    }

    std::string raw = ast_source(payload);
    bool closes_proof = strip_proof_qed_suffix(raw);

    if (closes_proof) {
      if (!raw.empty()) {
        append_document(stack.back().body,
                        parse_embedded_aofm_blocks(raw, "proof-body"));
      }

      tree finished = compound("proof", ensure_document_tree(stack.back().body));
      stack.pop_back();
      if (stack.empty()) {
        consumed_to = j;
        return finished;
      }

      append_document(stack.back().body, finished);
      ++j;
      continue;
    }

    append_document(stack.back().body, convert_block(nodes[j]));
    ++j;
  }

  consumed_to = j > start ? j - 1 : start;

  while (stack.size() > 1) {
    tree finished = compound("proof", ensure_document_tree(stack.back().body));
    stack.pop_back();
    append_document(stack.back().body, finished);
  }

  if (stack.empty()) return "";
  return compound("proof", ensure_document_tree(stack.back().body));
}

std::string
extract_callout_body_source(const std::string& raw) {
  size_t nl = raw.find('\n');
  if (nl == std::string::npos || nl + 1 >= raw.size()) return "";
  return strip_blockquote_markers(raw.substr(nl + 1));
}

tree
prepend_callout_title(tree body, const std::string& title) {
  if (title.empty()) return body;

  tree out(DOCUMENT);
  out << convert_inline_from_raw(title);
  append_document(out, body);
  return simplify_document(out);
}

tree
convert_callout(const AstPtr& ast) {
  std::string raw = ast_source(ast);
  CalloutHeader header;
  if (!parse_callout_header(raw, header)) {
    return text_tree(trim_copy(strip_trailing_newlines(raw)));
  }

  tree body = parse_embedded_aofm_blocks(extract_callout_body_source(raw),
                                         "callout");
  body = prepend_callout_title(ensure_document_tree(body), header.title);
  body = sanitize_proof_trees(ensure_document_tree(body));

  std::string tag;
  bool use_quote_env = false;
  bool mapped = !header.ext.empty()
                    ? map_extended_callout_tag(header, tag, use_quote_env)
                    : map_basic_callout_tag(header.base, tag, use_quote_env);
  if (!mapped && !header.ext.empty()) {
    mapped = map_basic_callout_tag(header.base, tag, use_quote_env);
  }

  if (use_quote_env) {
    return compound("quote-env", ensure_document_tree(body));
  }
  if (mapped) {
    return split_callout_proof_tail(tag, simplify_document(body));
  }
  return text_tree(trim_copy(strip_trailing_newlines(raw)));
}

tree
convert_list_item_body(const AstPtr& ast) {
  std::vector<std::string> lines;

  for (const auto& child : ast->nodes) {
    if (ast_is(child, "LineContent")) {
      lines.push_back(trim_copy(ast_source(child)));
    } else if (ast_is(child, "TightContinuation")) {
      lines.push_back(trim_copy(ast_source(child)));
    }
  }

  std::string joined;
  for (size_t i = 0; i < lines.size(); ++i) {
    if (i > 0) joined += '\n';
    joined += lines[i];
  }

  return text_tree(trim_copy(joined));
}

bool
is_ordered_list(const AstPtr& ast) {
  if (!ast) return false;
  for (const auto& child : ast->nodes) {
    if (ast_is(child, "ListItem")) {
      for (const auto& grandchild : child->nodes) {
        if (ast_is(grandchild, "ListPrefix")) {
          std::string prefix = trim_copy(ast_source(grandchild));
          return !prefix.empty() && prefix[0] != '-';
        }
      }
      break;
    }
  }
  return false;
}

tree
convert_list(const AstPtr& ast) {
  tree items(DOCUMENT);

  for (const auto& child : ast->nodes) {
    if (!ast_is(child, "ListItem")) continue;

    tree item(CONCAT);
    item << compound("item");
    append_concat(item, convert_list_item_body(child));
    items << simplify_concat(item);
  }

  return compound(is_ordered_list(ast) ? "enumerate" : "itemize", items);
}

tree
convert_table(const AstPtr& ast) {
  return text_tree(trim_copy(strip_trailing_newlines(ast_source(ast))));
}

tree
convert_block(const AstPtr& ast) {
  if (!ast) return "";

  if (ast_name_is(ast, "Block") || ast_name_is(ast, "Document")) {
    tree out(DOCUMENT);
    for (size_t i = 0; i < ast->nodes.size(); ++i) {
      const auto& child = ast->nodes[i];
      AstPtr child_payload = block_payload(child);

      std::string first_proof_chunk;
      if (extract_proof_marker_body(child_payload, first_proof_chunk)) {
        size_t consumed_to = i;
        tree proof = consume_proof(ast->nodes, i, consumed_to);
        if (proof != "") {
          out << proof;
          i = consumed_to;
          continue;
        }
      }

      tree converted_child = convert_block(child);
      size_t extended_to = i;
      tree extended_child;
      if (extend_theorem_callout_proof(ast->nodes, i, converted_child,
                                       extended_to, extended_child)) {
        append_document(out, extended_child);
        i = extended_to;
        continue;
      }
      append_document(out, converted_child);
    }
    return simplify_document(out);
  }

  if (ast_is(ast, "BlankLine") || ast_is(ast, "EOF") ||
      ast_is(ast, "YAMLFrontmatter") || ast_is(ast, "HTMLCommentBlock")) {
    return "";
  }

  if (ast_is(ast, "AnchorBlock")) {
    return compound("label", text_tree(extract_anchor_id(ast_source(ast))));
  }

  if (ast_is(ast, "Paragraph")) return convert_paragraph(ast);
  if (ast_is(ast, "Heading")) return convert_heading(ast);
  if (ast_is(ast, "HorizontalRule")) return tree(APPLY, "hrule");
  if (ast_is(ast, "CodeBlock")) return convert_code_block(ast);
  if (ast_is(ast, "MathBlock")) return convert_math_block(ast);
  if (ast_is(ast, "List")) return convert_list(ast);
  if (ast_is(ast, "Blockquote")) return convert_blockquote(ast);
  if (ast_is(ast, "Table")) return convert_table(ast);
  if (ast_is(ast, "Callout")) return convert_callout(ast);

  if (!ast->nodes.empty()) {
    tree out(DOCUMENT);
    for (const auto& child : ast->nodes) {
      append_document(out, convert_block(child));
    }
    return simplify_document(out);
  }

  return text_tree(trim_copy(strip_trailing_newlines(ast_source(ast))));
}

tree
aofm_ast_to_texmacs_document(const AstPtr& ast) {
  tree body = convert_block(ast);
  if (!is_document(body)) body = document(body);
  body = simplify_document(body);

  new_data data;
  return attach_data(body, data, true);
}

std::string
aofm_output_path_for(const std::string& file_path) {
  size_t dot = file_path.find_last_of('.');
  size_t slash = file_path.find_last_of("/\\");
  if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) {
    return file_path + ".ath";
  }
  return file_path.substr(0, dot) + ".ath";
}

bool
aofm_convert_file(const std::string& file_path,
                  std::string& output_path,
                  string& serialized_document) {
  auto ast = aofm_parse_file(file_path);
  if (!ast) return false;

  tree doc = aofm_ast_to_texmacs_document(ast);
  serialized_document = tree_to_texmacs(doc);
  output_path = aofm_output_path_for(file_path);
  return !save_string(url_system(tm_string(output_path)), serialized_document);
}

} // namespace

std::shared_ptr<peg::Ast> aofm_parse_file(const std::string& file_path) {
    peg::parser parser(aofm_grammar);

    if (!parser) {
        std::cerr << "aofm_parse_file: Failed to initialize parser from aofm_grammar" << std::endl;
        return nullptr;
    }

    parser.enable_ast();

    // Set logger for detailed error reporting
    parser.set_logger([](size_t line, size_t col, const std::string& msg, const std::string& rule) {
        std::cerr << "AOFM Parse Error at " << line << ":" << col 
                  << " (Rule: " << rule << "): " << msg << std::endl;
    });

    std::ifstream ifs(file_path, std::ios::in | std::ios::binary);
    if (!ifs.is_open()) {
        std::cerr << "aofm_parse_file: Could not open file: " << file_path << std::endl;
        return nullptr;
    }

    // Keep memory alive for string_view references stored in AST nodes.
    aofm_content.assign((std::istreambuf_iterator<char>(ifs)),
                        (std::istreambuf_iterator<char>()));
    aofm_content = normalize_markdown_lines(aofm_content);
    aofm_content = preprocess_isolated_callout_proofs(aofm_content);

    if (!aofm_content.empty() && aofm_content.back() != '\n') {
        aofm_content += '\n';
    }

    std::shared_ptr<peg::Ast> ast;
    if (parser.parse(aofm_content, ast, file_path.c_str())) {
        return ast;
    } else {
        std::cerr << "aofm_parse_file: Parsing failed for file: " << file_path << std::endl;
        return nullptr;
    }
}

void aofm_dump_ast(std::shared_ptr<peg::Ast> ast) {
    if (ast) {
        std::function<std::string(const peg::Ast&, int)> extra =
            [](const peg::Ast& node, int) {
              std::ostringstream out;
              out << "    [pos=" << node.position
                  << ", len=" << node.length
                  << ", src=(" << aofm_content.substr(node.position, node.length) << ")";
              if (node.is_token) {
                out << ", tok=(" << node.token_to_string() << ")";
              }
              out << "]\n";
              return out.str();
            };
        std::cout << "--- AST DUMP BEGIN ---" << std::endl;
        std::cout << peg::ast_to_s(ast, extra) << std::endl;
        std::cout << "--- AST DUMP END ---" << std::endl;
        std::cout.flush();
    } else {
        std::cout << "aofm_dump_ast: AST is null" << std::endl;
    }
}

void aofm_debug_dump(const std::string& file_path) {
    std::string output_path;
    string serialized_document;
    if (!aofm_convert_file(file_path, output_path, serialized_document)) {
        std::cerr << "aofm_debug_dump: Conversion failed for file: "
                  << file_path << std::endl;
        return;
    }

    std::cout << "--- ATH DUMP BEGIN ---" << std::endl;
    std::cout << as_charp(serialized_document) << std::endl;
    std::cout << "--- ATH DUMP END ---" << std::endl;
    std::cout << "Saved to: " << output_path << std::endl;
}
