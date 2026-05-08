#include "aofm_metadata.hpp"
#include "aofm_utils.hpp"
#include "aofm_inline.hpp"
#include <sstream>
#include <vector>

namespace aofm {

AofmDocumentMetadata aofm_metadata;

void
set_aofm_metadata_field(const std::string& key, const std::string& value) {
  if (key == "Created Time") aofm_metadata.created_time = value;
  else if (key == "Modified Time") aofm_metadata.modified_time = value;
  else if (key == "Content Hash") aofm_metadata.content_hash = value;
}

void
parse_aofm_metadata_line(const std::string& line) {
  std::string raw = strip_one_blockquote_marker(line);
  std::string trimmed = trim_copy(raw);
  size_t colon = trimmed.find(':');
  if (colon == std::string::npos) return;
  std::string key = trim_copy(trimmed.substr(0, colon));
  std::string value = strip_inline_code_quotes(trimmed.substr(colon + 1));
  set_aofm_metadata_field(key, value);
}

bool
is_aofm_metadata_header_line(const std::string& line) {
  std::string trimmed = trim_copy(line);
  return trimmed == "> [!info] Metadata" || trimmed == ">[!info] Metadata";
}

bool
is_aofm_metadata_line(const std::string& line) {
  std::string trimmed = trim_copy(line);
  return !trimmed.empty() && trimmed[0] == '>';
}

std::string
extract_aofm_document_metadata(const std::string& raw) {
  aofm_metadata.clear();

  std::vector<std::string> lines;
  std::stringstream in(raw);
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    lines.push_back(line);
  }

  size_t pos = 0;
  if (pos < lines.size() && trim_copy(lines[pos]) == "---") {
    pos++;
    while (pos < lines.size() && trim_copy(lines[pos]) != "---") pos++;
    if (pos < lines.size()) pos++;
  }
  while (pos < lines.size() && is_blank_line(lines[pos])) pos++;

  if (pos >= lines.size() || !is_aofm_metadata_header_line(lines[pos])) return raw;

  size_t block_end = pos + 1;
  while (block_end < lines.size() && is_aofm_metadata_line(lines[block_end])) {
    parse_aofm_metadata_line(lines[block_end]);
    block_end++;
  }

  size_t erase_end = block_end;
  if (erase_end < lines.size() && is_blank_line(lines[erase_end])) erase_end++;
  if (erase_end < lines.size() &&
      trim_copy(lines[erase_end]) == "<!-- End of Obindex Metadata -->") {
    erase_end++;
    if (erase_end < lines.size() && is_blank_line(lines[erase_end])) erase_end++;
  }

  std::string result;
  for (size_t i = 0; i < lines.size(); ++i) {
    if (i >= pos && i < erase_end) continue;
    if (!result.empty()) result += '\n';
    result += lines[i];
  }
  return result;
}

} // namespace aofm
