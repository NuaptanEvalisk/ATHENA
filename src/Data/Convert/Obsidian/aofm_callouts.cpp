#include "aofm_callouts.hpp"
#include "aofm_utils.hpp"
#include "aofm_ast_helpers.hpp"
#include "aofm_tree_utils.hpp"
#include "aofm_inline.hpp"
#include "aofm_blocks.hpp"
#include "aofm_math.hpp"
#include "aofm_metadata.hpp"
#include "convert.hpp"
#include <algorithm>
#include <iostream>
#include <sstream>
#include <vector>
#include <string>

namespace aofm {

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

    size_t cursor = block_end;
    bool saw_blank_after_callout = false;
    while (cursor < lines.size() && is_blank_line(lines[cursor])) {
      saw_blank_after_callout = true;
      cursor++;
    }

    bool is_anchor = false;
    if (cursor < lines.size()) {
       std::string trimmed = trim_copy(lines[cursor]);
       if (!trimmed.empty() && trimmed[0] == '^') is_anchor = true;
    }

    std::string moved_anchor;
    if (saw_blank_after_callout && is_anchor) {
      moved_anchor = lines[cursor];
      cursor++;
    }

    // If no proof was found, we still want to move the anchor if it exists.
    if (proof_index == block_end) {
      for (size_t j = i; j < block_end; ++j) out.push_back(lines[j]);
      if (!moved_anchor.empty()) {
        out.push_back("");
        out.push_back(moved_anchor);
      }
      else if (saw_blank_after_callout && cursor < lines.size()) {
        out.push_back("");
      }
      i = cursor;
      continue;
    }

    std::vector<std::string> theorem_lines;
    for (size_t j = i; j < proof_index; ++j) theorem_lines.push_back(lines[j]);

    // Trim trailing empty blockquote lines or blank lines from theorem_lines
    while (!theorem_lines.empty()) {
      std::string last = trim_copy(theorem_lines.back());
      if (last == ">" || last.empty()) {
        theorem_lines.pop_back();
      } else {
        break;
      }
    }

    std::vector<std::string> proof_lines;
    for (size_t j = proof_index; j < block_end; ++j) {
      proof_lines.push_back(strip_one_blockquote_marker(lines[j]));
    }

    for (const auto& theorem_line : theorem_lines) out.push_back(theorem_line);
    out.push_back("");
    if (!moved_anchor.empty()) {
      out.push_back(moved_anchor);
      out.push_back("");
    }
    for (const auto& proof_line : proof_lines) out.push_back(proof_line);
    if (saw_blank_after_callout && cursor < lines.size()) {
      out.push_back("");
    }

    i = cursor;
  }

  std::string result;
  for (size_t i = 0; i < out.size(); ++i) {
    if (i > 0) result += '\n';
    result += out[i];
  }
  return result;
}

std::string
sanitize_markdown_blocks(const std::string& raw) {
  std::istringstream stream(raw);
  std::string line;
  std::vector<std::string> out;

  while (std::getline(stream, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }

    std::string trimmed = trim_copy(line);
    if (trimmed == "$$") {
      out.push_back("$$");
    } else if (trimmed.size() >= 3 && trimmed.substr(0, 3) == "```") {
      out.push_back(trimmed);
    } else if (trimmed.empty()) {
      out.push_back("");
    } else {
      out.push_back(line);
    }
  }

  std::string result;
  for (size_t i = 0; i < out.size(); ++i) {
    if (i > 0) result += '\n';
    result += out[i];
  }
  return result;
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
    tag = "quote-env";
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
absorb_trailing_anchor(const std::vector<AstPtr>& nodes, size_t index,
                       tree converted, size_t& consumed_to, tree& result) {
  tree doc = ensure_document_tree(converted);
  if (!is_document(doc) || N(doc) < 1) return false;

  tree env = is_document(doc) && N(doc) == 1 ? doc[0] : doc;
  if (!is_theorem_like_env_tree(env)) return false;

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
    moved_labels << make_aofm_anchor_block_placeholder(
        extract_anchor_id(ast_source(payload)));
    saw_anchor = true;
    ++cursor;
  }
  
  if (!saw_anchor) return false;

  tree out(DOCUMENT);
  out << converted;
  append_document(out, simplify_document(moved_labels));
  
  result = out;
  consumed_to = cursor - 1;
  return true;
}

bool
absorb_trailing_proof(const std::vector<AstPtr>& nodes, size_t index,
                      tree converted, size_t& consumed_to, tree& result) {
  tree doc = ensure_document_tree(converted);
  if (!is_document(doc) || N(doc) < 1) return false;

  tree env = is_document(doc) && N(doc) == 1 ? doc[0] : doc;
  if (!is_theorem_like_env_tree(env)) return false;
  if (!is_compound(env[N(env) - 1], "proof", 1)) return false;

  size_t cursor = index + 1;
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

  tree proof_body = ensure_document_tree(env[N(env) - 1][0]);
  append_document(proof_body, continuation);

  tree new_env(env, N(env));
  new_env[N(env) - 1] = compound("proof", ensure_document_tree(proof_body));

  if (is_document(doc) && N(doc) == 1) {
    result = document(new_env);
  } else {
    result = new_env;
  }

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

  bool closes_immediately = strip_proof_qed_suffix(first_chunk);

  if (!first_chunk.empty()) {
    stack.back().body << convert_inline_from_raw(first_chunk);
  }

  if (closes_immediately) {
    tree finished = compound("proof", ensure_document_tree(stack.back().body));
    stack.pop_back();
    consumed_to = start;
    return finished;
  }

  size_t j = start + 1;
  while (j < nodes.size() && !stack.empty()) {
    AstPtr payload = block_payload(nodes[j]);
    if (!payload) {
      ++j;
      continue;
    }

    if (ast_is(payload, "AnchorBlock")) {
      tree label = make_aofm_anchor_block_placeholder(
          extract_anchor_id(ast_source(payload)));
      insert_label_before_trailing_proof(stack.back().body, label);
      ++j;
      continue;
    }

    std::string nested_chunk;
    if (extract_proof_marker_body(payload, nested_chunk)) {
      // 【新增】：同样的逻辑应用到可能存在的嵌套 Proof
      bool closes_nested = strip_proof_qed_suffix(nested_chunk);

      stack.push_back(ProofFrame { tree(DOCUMENT) });
      if (!nested_chunk.empty()) {
        stack.back().body << convert_inline_from_raw(nested_chunk);
      }

      // 【新增】：立即闭合嵌套 Proof
      if (closes_nested) {
        tree finished = compound("proof", ensure_document_tree(stack.back().body));
        stack.pop_back();
        append_document(stack.back().body, finished);
      }
      ++j;
      continue;
    }

    if (!is_proof_body_block(payload)) break;

    if (ast_is(payload, "Callout") || ast_is(payload, "Blockquote")) {
      std::string body_raw;
      if (ast_is(payload, "Callout")) body_raw = extract_callout_body_source(ast_source(payload));
      else body_raw = strip_blockquote_markers(ast_source(payload));

      bool closes_callout_proof = strip_proof_qed_suffix(body_raw);

      tree converted_callout = convert_block(nodes[j]);
      size_t extended_to = j;
      tree extended_callout;
      if (absorb_trailing_anchor(nodes, j, converted_callout,
                                 extended_to, extended_callout)) {
        converted_callout = extended_callout;
        j = extended_to;
      }
      if (absorb_trailing_proof(nodes, j, converted_callout,
                                extended_to, extended_callout)) {
        converted_callout = extended_callout;
        j = extended_to;
      }
      append_document(stack.back().body, converted_callout);
      ++j;

      if (closes_callout_proof) {
        tree finished =
            compound("proof", ensure_document_tree(stack.back().body));
        stack.pop_back();
        if (stack.empty()) {
          consumed_to = j - 1;
          return finished;
        }
        append_document(stack.back().body, finished);
      }
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

bool
is_theorem_like_callout_tag(const std::string& tag) {
  return tag == "theorem" || tag == "proposition" || tag == "lemma" ||
         tag == "corollary" || tag == "remark" || tag == "example" ||
         tag == "definition" || tag == "axiom" || tag == "conjecture" ||
         tag == "law" || tag == "quote-env";
}

bool
is_theorem_like_env_tree(const tree& t) {
  return is_compound(t, "theorem", 1) || is_compound(t, "proposition", 1) ||
         is_compound(t, "lemma", 1) || is_compound(t, "corollary", 1) ||
         is_compound(t, "remark", 1) || is_compound(t, "example", 1) ||
         is_compound(t, "definition", 1) || is_compound(t, "axiom", 1) ||
         is_compound(t, "conjecture", 1) || is_compound(t, "law", 1) ||
         is_compound(t, "quote-env", 1);
}

std::string
extract_anchor_id(const std::string& raw) {
  std::string trimmed = trim_copy(strip_trailing_newlines(raw));
  if (!trimmed.empty() && trimmed[0] == '^') trimmed.erase(0, 1);
  return trim_copy(trimmed);
}

bool
extract_trailing_anchor_inline_text(const std::string& raw,
                                    std::string& content,
                                    std::string& anchor) {
  std::string trimmed = rtrim_copy(raw);
  if (trimmed.empty()) return false;

  size_t pos = trimmed.find_last_of('^');
  if (pos == std::string::npos || pos == 0) return false;
  if (trimmed[pos - 1] != ' ' && trimmed[pos - 1] != '\t') return false;

  std::string candidate = trim_copy(trimmed.substr(pos + 1));
  if (candidate.empty()) return false;
  for (char ch : candidate) {
    if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') return false;
  }

  content = rtrim_copy(trimmed.substr(0, pos));
  anchor = candidate;
  return true;
}

tree
split_callout_proof_tail(const std::string& tag, tree body) {
  if (is_compound(body, "proof", 1)) {
    return body;
  }

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
  tree simplified_head = simplify_document(head);
  if (simplified_head != "" && !is_useless_tree(simplified_head)) {
    out << compound(tag.c_str(), ensure_document_tree(simplified_head));
  }
  append_document(out, simplify_document(tail));
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

  if (mapped) {
    return split_callout_proof_tail(tag, simplify_document(body));
  }
  return text_tree(trim_copy(strip_trailing_newlines(raw)));
}

} // namespace aofm
