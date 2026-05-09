#include "aofm_inline.hpp"
#include "aofm_utils.hpp"
#include "aofm_ast_helpers.hpp"
#include "aofm_tree_utils.hpp"
#include "aofm_math.hpp"
#include <algorithm>

namespace aofm {

static bool
parse_image_embed_inner(const std::string& inner,
                        std::string& target,
                        std::string& width) {
  size_t pipe = inner.find('|');
  target = trim_copy(pipe == std::string::npos ? inner : inner.substr(0, pipe));
  width = pipe == std::string::npos ? "" : trim_copy(inner.substr(pipe + 1));
  return is_aofm_image_target(target) &&
         (width.empty() || is_decimal_digits(width));
}

static bool
parse_image_embed_source(const std::string& raw,
                         std::string& target,
                         std::string& width) {
  if (raw.size() < 5 ||
      raw.compare(0, 3, "![[") != 0 ||
      raw.compare(raw.size() - 2, 2, "]]") != 0) {
    return false;
  }
  return parse_image_embed_inner(raw.substr(3, raw.size() - 5), target, width);
}

static size_t
find_unescaped_char(const std::string& raw, char needle, size_t start) {
  for (size_t i = start; i < raw.size(); ++i) {
    if (raw[i] != needle) continue;
    size_t backslashes = 0;
    size_t j = i;
    while (j > 0 && raw[--j] == '\\') backslashes++;
    if ((backslashes % 2) == 0) return i;
  }
  return std::string::npos;
}

static bool
parse_external_link_at(const std::string& raw,
                       size_t pos,
                       std::string& label,
                       std::string& destination,
                       size_t& end) {
  if (pos >= raw.size() || raw[pos] != '[') return false;
  if (pos > 0 && raw[pos - 1] == '!') return false;
  if (pos + 1 < raw.size() && raw[pos + 1] == '[') return false;

  size_t close_label = find_unescaped_char(raw, ']', pos + 1);
  if (close_label == std::string::npos ||
      close_label + 1 >= raw.size() ||
      raw[close_label + 1] != '(') {
    return false;
  }

  size_t close_dest = find_unescaped_char(raw, ')', close_label + 2);
  if (close_dest == std::string::npos) return false;

  label = raw.substr(pos + 1, close_label - pos - 1);
  destination = raw.substr(close_label + 2, close_dest - close_label - 2);
  end = close_dest + 1;
  return !label.empty() && !destination.empty();
}

static bool
parse_external_link_source(const std::string& raw,
                           std::string& label,
                           std::string& destination) {
  size_t end = 0;
  return parse_external_link_at(raw, 0, label, destination, end) &&
         end == raw.size();
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
  std::string text_str;
  size_t i = 0;

  auto flush_text = [&]() {
    if (text_str.empty()) return;
    append_concat(out, text_tree(text_str));
    text_str.clear();
  };

  while (i < raw.size()) {
    // 1. Escaped characters
    if (raw[i] == '\\' && i + 1 < raw.size()) {
      char next = raw[i + 1];
      if (next == '$' || next == '*' || next == '_' || next == '`' ||
          next == '[' || next == ']' || next == '^' || next == '=' ||
          next == '\\') {
        text_str += next;
        i += 2;
        continue;
      }
    }

    // 2. Inline Anchors (at the end of content)
    if (raw[i] == ' ' && i + 2 < raw.size() && raw[i + 1] == '^') {
      size_t j = i + 2;
      while (j < raw.size() && !isspace(raw[j])) ++j;
      if (j > i + 2 && (j == raw.size() || (j < raw.size() && isspace(raw[j])))) {
        flush_text();
        append_concat(out, make_aofm_anchor_inline_placeholder(raw.substr(i + 2, j - (i + 2))));
        i = j;
        continue;
      }
    }

    // 3. Formatting (Bold, Italic, Highlight)
    if (raw.compare(i, 2, "==") == 0) {
      size_t close = raw.find("==", i + 2);
      if (close != std::string::npos && close > i + 2) {
        flush_text();
        append_concat(out, compound("marked",
                                    convert_inline_from_raw(
                                      raw.substr(i + 2, close - i - 2))));
        i = close + 2;
        continue;
      }
    }
    if (raw.compare(i, 3, "***") == 0) {
      size_t close = raw.find("***", i + 3);
      if (close != std::string::npos) {
        flush_text();
        append_concat(out, compound("strong", compound("em", convert_inline_from_raw(raw.substr(i + 3, close - i - 3)))));
        i = close + 3;
        continue;
      }
    }
    if (raw.compare(i, 2, "**") == 0) {
      size_t close = raw.find("**", i + 2);
      if (close != std::string::npos) {
        flush_text();
        append_concat(out, compound("strong", convert_inline_from_raw(raw.substr(i + 2, close - i - 2))));
        i = close + 2;
        continue;
      }
    }
    if (raw[i] == '*') {
      size_t close = raw.find('*', i + 1);
      if (close != std::string::npos) {
        flush_text();
        append_concat(out, compound("em", convert_inline_from_raw(raw.substr(i + 1, close - i - 1))));
        i = close + 1;
        continue;
      }
    }

    // 4. Inline Code
    if (raw[i] == '`') {
      size_t close = raw.find('`', i + 1);
      if (close != std::string::npos) {
        flush_text();
        append_concat(out, compound("verbatim", text_tree(raw.substr(i + 1, close - i - 1))));
        i = close + 1;
        continue;
      }
    }

    // 5. Inline Math (Robust Multi-line)
    if (raw[i] == '$') {
      size_t j = i + 1;
      while (j < raw.size()) {
        if (raw[j] == '$' && raw[j-1] != '\\') break;
        j++;
      }
      if (j < raw.size()) {
        flush_text();
        append_concat(out, convert_latex_math_inline(raw.substr(i + 1, j - i - 1)));
        i = j + 1;
        continue;
      }
    }

    // 6. Images, Transclusions, and Wikilinks
    bool is_trans = (raw.compare(i, 3, "![[") == 0);
    bool is_wiki = (raw.compare(i, 2, "[[") == 0);
    if (is_trans || is_wiki) {
      size_t start = i + (is_trans ? 3 : 2);
      size_t close = raw.find("]]", start);
      if (close != std::string::npos) {
        flush_text();
        std::string inner = raw.substr(start, close - start);
        std::string image_target, image_width;
        if (is_trans && parse_image_embed_inner(inner, image_target, image_width)) {
          append_concat(out, make_aofm_image_placeholder(image_target, image_width));
          i = close + 2;
          continue;
        }
        size_t pipe = inner.find('|');
        size_t hash = inner.find('#');
        std::string target, sub, alias;
        if (pipe != std::string::npos) {
          alias = inner.substr(pipe + 1);
          inner = inner.substr(0, pipe);
        }
        if (hash != std::string::npos) {
          sub = inner.substr(hash + 1);
          if (!sub.empty() && sub[0] == '^') sub.erase(0, 1);
          target = inner.substr(0, hash);
        } else {
          target = inner;
        }
        if (is_trans) append_concat(out, make_aofm_transclusion_placeholder(target, sub, alias));
        else append_concat(out, make_aofm_wikilink_placeholder(target, sub, alias));
        i = close + 2;
        continue;
      }
    }

    // 7. External Links
    if (raw[i] == '[') {
      std::string label, destination;
      size_t end = 0;
      if (parse_external_link_at(raw, i, label, destination, end)) {
        flush_text();
        append_concat(out, compound("hlink",
                                    convert_inline_from_raw(label),
                                    text_tree(destination)));
        i = end;
        continue;
      }
    }

    text_str += raw[i];
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

  if (ast_is(ast, "Highlight")) {
    return compound("marked",
                    convert_inline_from_raw(
                      strip_wrapping(ast_source(ast), 2, 2)));
  }

  if (ast_is(ast, "InlineMath")) {
    return convert_latex_math_inline(strip_wrapping(ast_source(ast), 1, 1));
  }

  if (ast_is(ast, "InlineAnchor")) {
    std::string raw = ast_source(ast);
    size_t pos = raw.find('^');
    if (pos != std::string::npos && pos + 1 < raw.size()) {
      return make_aofm_anchor_inline_placeholder(trim_copy(raw.substr(pos + 1)));
    }
    return text_tree(raw);
  }

  if (ast_is(ast, "WikiLink") || ast_is(ast, "Transclusion")) {
    bool is_transclusion = ast_is(ast, "Transclusion");
    std::string target, sub, alias;
    for (const auto& node : ast->nodes) {
      if (ast_is(node, "LinkTarget")) target = ast_source(node);
      if (ast_is(node, "SubTarget")) {
        sub = ast_source(node);
        if (!sub.empty() && sub[0] == '^') sub.erase(0, 1);
      }
      if (ast_is(node, "Alias")) alias = ast_source(node);
    }
    if (is_transclusion) {
      return make_aofm_transclusion_placeholder(target, sub, alias);
    }
    return make_aofm_wikilink_placeholder(target, sub, alias);
  }

  if (ast_is(ast, "Image")) {
    std::string target, width;
    if (parse_image_embed_source(ast_source(ast), target, width)) {
      return make_aofm_image_placeholder(target, width);
    }
    return text_tree(ast_source(ast));
  }

  if (ast_is(ast, "Strikethrough") || ast_is(ast, "PDF")) {
    return text_tree(ast_source(ast));
  }

  if (ast_is(ast, "ExtLink")) {
    std::string label, destination;
    if (parse_external_link_source(ast_source(ast), label, destination)) {
      return compound("hlink",
                      convert_inline_from_raw(label),
                      text_tree(destination));
    }
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

std::string
strip_inline_code_quotes(const std::string& s) {
  std::string trimmed = trim_copy(s);
  if (trimmed.size() >= 2 && trimmed.front() == '`' && trimmed.back() == '`') {
    return trimmed.substr(1, trimmed.size() - 2);
  }
  return trimmed;
}

} // namespace aofm
