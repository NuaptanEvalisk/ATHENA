#include "aofm_utils.hpp"
#include "converter.hpp"
#include <cctype>
#include <iostream>
#include <sstream>
#include <algorithm>

namespace aofm {

string
tm_string(const std::string& s) {
  return utf8_to_cork(string(s.c_str()));
}

string
std_to_tm_string(const std::string& s) {
  return utf8_to_cork(string(s.c_str()));
}

void
report_aofm_error(const std::string& message) {
  std::cerr << "aofm2athena: error: " << message << std::endl;
}

void
report_aofm_parse_error(const std::string& source_name, size_t line, size_t col,
                        const std::string& msg, const std::string& rule) {
  std::ostringstream out;
  if (!source_name.empty()) out << source_name << ": ";
  out << "line " << line;
  if (col != 0) out << ":" << col;
  out << " (reason: " << msg;
  if (!rule.empty()) out << "; rule: " << rule;
  out << ")";
  report_aofm_error(out.str());
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
rtrim_copy(const std::string& s) {
  size_t end = s.size();
  while (end > 0 &&
         (s[end - 1] == ' ' || s[end - 1] == '\t' ||
          s[end - 1] == '\r' || s[end - 1] == '\n')) {
    end--;
  }
  return s.substr(0, end);
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

std::string
target_extension_lower(const std::string& target) {
  size_t slash = target.find_last_of('/');
  size_t dot = target.find_last_of('.');
  if (dot == std::string::npos ||
      (slash != std::string::npos && dot < slash)) {
    return "";
  }
  std::string ext = target.substr(dot + 1);
  for (char& ch : ext) {
    ch = (char) std::tolower((unsigned char) ch);
  }
  return ext;
}

bool
is_aofm_image_target(const std::string& target) {
  std::string ext = target_extension_lower(target);
  return ext == "png" || ext == "jpg" || ext == "jpeg" ||
         ext == "bmp" || ext == "svg";
}

bool
is_aofm_pdf_target(const std::string& target) {
  return target_extension_lower(target) == "pdf";
}

bool
is_decimal_digits(const std::string& s) {
  if (s.empty()) return false;
  for (char ch : s) {
    if (!std::isdigit((unsigned char) ch)) return false;
  }
  return true;
}

bool
is_proof_marker_text(const std::string& raw) {
  std::string trimmed = trim_copy(raw);
  if (trimmed.empty()) return false;

  size_t pos = 0;
  while (pos < trimmed.size() && trimmed[pos] == '#') pos++;
  while (pos < trimmed.size() &&
         (trimmed[pos] == ' ' || trimmed[pos] == '\t')) {
    pos++;
  }
  trimmed = trimmed.substr(pos);

  static const char* kMarkers[] = {
      "**Proof (Alternative):**",    "**Proof (Alternative)：**",
      "**Proof:**",                  "**Proof：**",
      "**Solution:**",               "**Solution：**",
      "**证明:**",                   "**证明：**",
      "**解:**",                     "**解：**",
      "Proof (Alternative):",        "Proof (Alternative)：",
      "Proof:",                      "Proof：",
      "Solution:",                   "Solution：",
      "证明:",                       "证明：",
      "解:",                         "解："};

  for (const char* marker : kMarkers) {
    std::string prefix = marker;
    if (trimmed.compare(0, prefix.size(), prefix) != 0) continue;
    if (trimmed.size() == prefix.size() ||
        trimmed[prefix.size()] == ' ' ||
        trimmed[prefix.size()] == '\t' ||
        trimmed[prefix.size()] == '\r' ||
        trimmed[prefix.size()] == '\n') {
      return true;
    }
  }

  if (trimmed.compare(0, 9, "**Proof (") == 0) {
    for (const std::string& delimiter :
         {std::string("):**"), std::string(")：**")}) {
      size_t pos = trimmed.find(delimiter, 9);
      if (pos == std::string::npos) continue;
      size_t after = pos + delimiter.size();
      if (after == trimmed.size() ||
          trimmed[after] == ' ' ||
          trimmed[after] == '\t' ||
          trimmed[after] == '\r' ||
          trimmed[after] == '\n') {
        return true;
      }
    }
  }

  return false;
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

    std::string content = line;
    bool is_bq = starts_blockquote_line(line);
    if (is_bq) {
      content = strip_one_blockquote_marker(line);
    }

    if (is_proof_marker_text(content)) {
      if (!is_bq && !lines.empty() && !trim_copy(lines.back()).empty()) {
        lines.push_back("");
      }
    }

    lines.push_back(strip_known_invisible_prefixes(line));
  }

  std::string result;
  for (size_t i = 0; i < lines.size(); ++i) {
    if (i > 0) result += '\n';
    result += lines[i];
  }
  return result;
}

static std::string
blockquote_prefix_of(const std::string& line) {
  size_t pos = leading_space_count(line);
  if (pos >= line.size() || line[pos] != '>') return "";

  while (pos < line.size() && line[pos] == '>') {
    pos++;
    if (pos < line.size() && line[pos] == ' ') pos++;
  }

  return line.substr(0, pos);
}

static std::vector<std::string>
split_embed_line(const std::string& line) {
  std::vector<std::string> out;
  std::string prefix = blockquote_prefix_of(line);
  std::string body = prefix.empty() ? line : line.substr(prefix.size());
  size_t pos = 0;
  size_t start = 0;

  while (true) {
    size_t bang = body.find("![[", pos);
    if (bang == std::string::npos) break;
    size_t close = body.find("]]", bang + 3);
    if (close == std::string::npos) break;

    std::string before = rtrim_copy(body.substr(start, bang - start));
    if (!before.empty()) out.push_back(prefix + before);

    out.push_back(prefix + body.substr(bang, close + 2 - bang));
    pos = close + 2;
    start = pos;
  }

  std::string tail = trim_copy(body.substr(start));
  if (!tail.empty()) out.push_back(prefix + tail);
  if (out.empty()) out.push_back(line);
  return out;
}

std::string
normalize_transclusion_lines(const std::string& raw) {
  std::vector<std::string> lines;
  std::stringstream in(raw);
  std::string line;
  bool in_fenced_code = false;

  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();

    std::string trimmed = trim_copy(line);
    if (trimmed.compare(0, 3, "```") == 0) {
      in_fenced_code = !in_fenced_code;
      lines.push_back(line);
      continue;
    }

    if (in_fenced_code || line.find("![[") == std::string::npos) {
      lines.push_back(line);
      continue;
    }

    std::vector<std::string> split = split_embed_line(line);
    lines.insert(lines.end(), split.begin(), split.end());
  }

  std::string result;
  for (size_t i = 0; i < lines.size(); ++i) {
    if (i > 0) result += '\n';
    result += lines[i];
  }
  return result;
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
  return line.compare(pos, 2, "[!") == 0;
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
ends_with(const std::string& s, const std::string& suffix) {
  return s.size() >= suffix.size() &&
         s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

} // namespace aofm
