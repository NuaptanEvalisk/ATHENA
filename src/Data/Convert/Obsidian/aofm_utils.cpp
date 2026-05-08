#include "aofm_utils.hpp"
#include "converter.hpp"
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

bool
is_proof_marker_text(const std::string& raw) {
  std::string trimmed = trim_copy(raw);
  if (trimmed.empty()) return false;

  std::string lower = trimmed;
  std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

  bool has_keyword = (lower.find("proof") != std::string::npos ||
                      lower.find("solution") != std::string::npos ||
                      lower.find("证明") != std::string::npos ||
                      lower.find("解") != std::string::npos);
  if (!has_keyword) return false;

  if (trimmed.find("**") != std::string::npos ||
      trimmed.find("#") != std::string::npos) {
    return true;
  }

  char last = trimmed.back();
  if (last == ':') return true;

  // Check for Chinese colon ： (UTF-8: EF BC 9A)
  if (trimmed.size() >= 3) {
    if ((unsigned char)trimmed[trimmed.size() - 3] == 0xEF &&
        (unsigned char)trimmed[trimmed.size() - 2] == 0xBC &&
        (unsigned char)trimmed[trimmed.size() - 1] == 0x9A) {
      return true;
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
    if (starts_blockquote_line(line)) {
      content = strip_one_blockquote_marker(line);
    }

    if (is_proof_marker_text(content)) {
      if (!lines.empty() && !trim_copy(lines.back()).empty()) {
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
ends_with(const std::string& s, const std::string& suffix) {
  return s.size() >= suffix.size() &&
         s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

} // namespace aofm
