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

tree
text_tree(const std::string& s) {
  return as_tree(tm_string(s));
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
    while (pos < line.size() && line[pos] == '>') {
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
convert_blockquote(const AstPtr& ast) {
  return text_tree(strip_blockquote_markers(ast_source(ast)));
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

  if (ast_name_is(ast, "Block")) {
    tree out(DOCUMENT);
    for (const auto& child : ast->nodes) {
      append_document(out, convert_block(child));
    }
    return simplify_document(out);
  }

  if (ast_is(ast, "BlankLine") || ast_is(ast, "EOF") ||
      ast_is(ast, "YAMLFrontmatter") || ast_is(ast, "HTMLCommentBlock") ||
      ast_is(ast, "AnchorBlock")) {
    return "";
  }

  if (ast_is(ast, "Paragraph")) return convert_paragraph(ast);
  if (ast_is(ast, "Heading")) return convert_heading(ast);
  if (ast_is(ast, "HorizontalRule")) return tree(APPLY, "hrule");
  if (ast_is(ast, "CodeBlock")) return convert_code_block(ast);
  if (ast_is(ast, "MathBlock")) return convert_math_block(ast);
  if (ast_is(ast, "List")) return convert_list(ast);
  if (ast_is(ast, "Blockquote")) return convert_blockquote(ast);
  if (ast_is(ast, "Table")) return convert_table(ast);

  if (ast_is(ast, "Callout")) {
    return text_tree(strip_anchor_lines(trim_copy(ast_source(ast))));
  }

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
