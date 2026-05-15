#include "aofm_blocks.hpp"
#include "aofm_utils.hpp"
#include "aofm_ast_helpers.hpp"
#include "aofm_tree_utils.hpp"
#include "aofm_inline.hpp"
#include "aofm_callouts.hpp"
#include "aofm_math.hpp"
#include "vars.hpp"
#include <iostream>
#include <sstream>
#include <string>

extern const char* aofm_grammar;

namespace aofm {

static bool
is_standalone_transclusion_line(const std::string& line) {
  std::string trimmed = trim_copy(line);
  return trimmed.size() >= 5 &&
         trimmed.compare(0, 3, "![[") == 0 &&
         trimmed.compare(trimmed.size() - 2, 2, "]]") == 0;
}

tree
convert_paragraph(const AstPtr& ast) {
  std::stringstream in(strip_trailing_newlines(ast_source(ast)));
  std::string line;
  std::string chunk;
  tree out(DOCUMENT);
  bool has_standalone_embed = false;

  auto flush_chunk = [&]() {
    if (chunk.empty()) return;
    std::string content;
    std::string anchor;
    if (extract_trailing_anchor_inline_text(chunk, content, anchor)) {
      out << convert_inline_from_raw(content);
      out << make_aofm_anchor_inline_placeholder(anchor);
    }
    else {
      out << convert_inline_from_raw(chunk);
    }
    chunk.clear();
  };

  while (std::getline(in, line)) {
    if (trim_copy(line).empty()) {
      flush_chunk();
      continue;
    }
    if (is_standalone_transclusion_line(line)) {
      flush_chunk();
      append_document(out, convert_inline_from_raw(trim_copy(line)));
      has_standalone_embed = true;
      continue;
    }
    if (!chunk.empty()) chunk += ' ';
    chunk += line;
  }

  flush_chunk();
  return has_standalone_embed ? out : simplify_document(out);
}

static std::string
strip_closing_heading_hashes(std::string title) {
  title = trim_copy(title);
  size_t hash_start = title.size();
  while (hash_start > 0 && title[hash_start - 1] == '#') hash_start--;
  if (hash_start == title.size()) return title;
  if (hash_start == 0) return title;
  if (title[hash_start - 1] != ' ' && title[hash_start - 1] != '\t') return title;
  return trim_copy(title.substr(0, hash_start - 1));
}

static std::string
strip_heading_text(const std::string& raw) {
  size_t pos = 0;
  while (pos < raw.size() && (raw[pos] == ' ' || raw[pos] == '\t')) pos++;
  while (pos < raw.size() && raw[pos] == '#') pos++;
  while (pos < raw.size() && (raw[pos] == ' ' || raw[pos] == '\t')) pos++;
  return strip_closing_heading_hashes(strip_trailing_newlines(raw.substr(pos)));
}

tree
convert_heading(const AstPtr& ast) {
  std::string raw = ast_source(ast);
  size_t pos = 0;
  while (pos < raw.size() && (raw[pos] == ' ' || raw[pos] == '\t')) pos++;
  int level = 0;
  while (pos + level < raw.size() && raw[pos + level] == '#') level++;

  std::string title = strip_heading_text(raw);
  const char* tag = "section";
  switch (level) {
    case 1: tag = "section"; break;
    case 2: tag = "subsection"; break;
    case 3: tag = "subsubsection"; break;
    case 4: tag = "paragraph"; break;
    default: tag = "subparagraph"; break;
  }
  std::string label = "H" + std::to_string(level) + " " + title;
  tree out(DOCUMENT);
  out << compound("label", text_tree(label));
  out << compound(tag, convert_inline_from_raw(title));
  return simplify_document(out);
}

tree
convert_code_block(const AstPtr& ast) {
  return compound("code",
                  text_tree(extract_fenced_body(ast_source(ast), "```")));
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
  parser.set_logger([source_name](size_t line, size_t col, const std::string& msg,
                                  const std::string& rule) {
    report_aofm_parse_error(source_name, line, col, msg, rule);
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
convert_blockquote(const AstPtr& ast) {
  tree body = parse_embedded_aofm_blocks(strip_blockquote_markers(ast_source(ast)), "blockquote");
  return split_callout_proof_tail("quote-env", simplify_document(body));
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

  std::string content, anchor;
  if (extract_trailing_anchor_inline_text(joined, content, anchor)) {
    tree out(CONCAT);
    append_concat(out, convert_inline_from_raw(content));
    append_concat(out, make_aofm_anchor_inline_placeholder(anchor));
    return simplify_concat(out);
  }

  return convert_inline_from_raw(joined);
}

static bool
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

static std::vector<std::string>
split_table_row(std::string line) {
  line = trim_copy(strip_trailing_newlines(line));
  if (!line.empty() && line.front() == '|') line.erase(line.begin());
  if (!line.empty() && line.back() == '|') {
    size_t backslashes = 0;
    for (size_t i = line.size() - 1; i > 0 && line[i - 1] == '\\'; --i) {
      backslashes++;
    }
    if ((backslashes % 2) == 0) line.pop_back();
  }

  std::vector<std::string> cells;
  std::string cell;
  bool in_code = false;
  bool in_math = false;
  bool in_wikilink = false;

  for (size_t i = 0; i < line.size(); ++i) {
    char ch = line[i];

    if (ch == '\\') {
      if (i + 1 < line.size() && line[i + 1] == '|') {
        cell += '|';
        i++;
      }
      else {
        cell += ch;
      }
      continue;
    }

    if (!in_code && !in_math &&
        ((i + 1 < line.size() && line.compare(i, 2, "[[") == 0) ||
         (i + 2 < line.size() && line.compare(i, 3, "![[") == 0))) {
      in_wikilink = true;
      cell += ch;
      continue;
    }

    if (in_wikilink && i + 1 < line.size() &&
        line.compare(i, 2, "]]") == 0) {
      in_wikilink = false;
      cell += "]]";
      i++;
      continue;
    }

    if (!in_wikilink && !in_math && ch == '`') {
      in_code = !in_code;
      cell += ch;
      continue;
    }

    if (!in_wikilink && !in_code && ch == '$') {
      in_math = !in_math;
      cell += ch;
      continue;
    }

    if (!in_wikilink && !in_code && !in_math && ch == '|') {
      cells.push_back(trim_copy(cell));
      cell.clear();
      continue;
    }
    cell += ch;
  }
  cells.push_back(trim_copy(cell));
  return cells;
}

static std::vector<std::string>
split_table_lines(const std::string& raw) {
  std::stringstream in(strip_trailing_newlines(raw));
  std::vector<std::string> lines;
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (!trim_copy(line).empty()) lines.push_back(line);
  }
  return lines;
}

static std::string
table_alignment_from_separator(const std::string& cell) {
  std::string s = trim_copy(cell);
  if (s.empty()) return "l";
  bool left = s.front() == ':';
  bool right = s.back() == ':';
  if (left && right) return "c";
  if (right) return "r";
  return "l";
}

static tree
make_table_cell(const std::string& raw, bool header) {
  tree content = convert_inline_from_raw(trim_copy(raw));
  if (header) content = compound("strong", content);
  return tree(CELL, content);
}

static tree
make_table_row(const std::vector<std::string>& cells, size_t nr_cols,
               bool header) {
  tree row(ROW);
  for (size_t i = 0; i < nr_cols; ++i) {
    row << make_table_cell(i < cells.size() ? cells[i] : "", header);
  }
  return row;
}

static void
append_table_format(tree& fmt, const std::string& row1, const std::string& row2,
                    const std::string& col1, const std::string& col2,
                    const string& var, const char* value) {
  fmt << tree(CWITH, tm_string(row1), tm_string(row2),
              tm_string(col1), tm_string(col2), var, tm_string(value));
}

tree
convert_table(const AstPtr& ast) {
  std::vector<std::string> lines = split_table_lines(ast_source(ast));
  if (lines.size() < 2) {
    return text_tree(trim_copy(strip_trailing_newlines(ast_source(ast))));
  }

  std::vector<std::vector<std::string>> rows;
  rows.push_back(split_table_row(lines[0]));
  std::vector<std::string> separators = split_table_row(lines[1]);
  for (size_t i = 2; i < lines.size(); ++i) {
    rows.push_back(split_table_row(lines[i]));
  }

  size_t nr_cols = separators.size();
  for (const auto& row : rows) nr_cols = std::max(nr_cols, row.size());
  if (nr_cols == 0 || rows.empty()) {
    return text_tree(trim_copy(strip_trailing_newlines(ast_source(ast))));
  }

  tree table(TABLE);
  table << make_table_row(rows[0], nr_cols, true);
  for (size_t i = 1; i < rows.size(); ++i) {
    table << make_table_row(rows[i], nr_cols, false);
  }

  tree fmt(TFORMAT);
  append_table_format(fmt, "1", "-1", "1", "-1", CELL_LBORDER, "1ln");
  append_table_format(fmt, "1", "-1", "1", "-1", CELL_RBORDER, "1ln");
  append_table_format(fmt, "1", "-1", "1", "-1", CELL_TBORDER, "1ln");
  append_table_format(fmt, "1", "-1", "1", "-1", CELL_BBORDER, "1ln");
  append_table_format(fmt, "1", "1", "1", "-1", CELL_BACKGROUND, "pastel blue");

  for (size_t i = 0; i < nr_cols; ++i) {
    std::string align = table_alignment_from_separator(
        i < separators.size() ? separators[i] : "");
    append_table_format(fmt, "1", "-1", std::to_string(i + 1),
                        std::to_string(i + 1), CELL_HALIGN, align.c_str());
  }

  fmt << table;
  return compound("tabular", fmt);
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
      
      if (absorb_trailing_anchor(ast->nodes, i, converted_child,
                                 extended_to, extended_child)) {
        converted_child = extended_child;
        i = extended_to;
      }
      
      if (absorb_trailing_proof(ast->nodes, i, converted_child,
                                extended_to, extended_child)) {
        converted_child = extended_child;
        i = extended_to;
      }
      
      append_document(out, converted_child);
    }
    return out;
  }

  if (ast_is(ast, "BlankLine") || ast_is(ast, "EOF") ||
      ast_is(ast, "YAMLFrontmatter") || ast_is(ast, "HTMLCommentBlock")) {
    return "";
  }

  if (ast_is(ast, "AnchorBlock")) {
    return make_aofm_anchor_block_placeholder(extract_anchor_id(ast_source(ast)));
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

  if (ast_is(ast, "UnknownBlock")) {
    std::cerr << "aofm2athena: warning: Unknown block encountered: [" 
              << trim_copy(ast_source(ast)) << "]" << std::endl;
    return text_tree(trim_copy(strip_trailing_newlines(ast_source(ast))));
  }

  if (!ast->nodes.empty()) {
    tree out(DOCUMENT);
    for (const auto& child : ast->nodes) {
      append_document(out, convert_block(child));
    }
    return out;
  }

  return text_tree(trim_copy(strip_trailing_newlines(ast_source(ast))));
}

} // namespace aofm
