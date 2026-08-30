#include "aofm_import_vault_internal.hpp"

#include <algorithm>
#include <fstream>

#include "file.hpp"
#include "vault.hpp"
#include "unicode_ranges.hpp"

namespace aofm_import_vault_internal {

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

bool
is_blank_line(const std::string& line) {
  return trim_copy(line).empty();
}

std::string
strip_closing_heading_hashes(std::string title) {
  title = rtrim_copy(title);
  size_t hash_start = title.size();
  while (hash_start > 0 && title[hash_start - 1] == '#') hash_start--;
  if (hash_start == title.size()) return title;
  if (hash_start == 0) return title;
  if (title[hash_start - 1] != ' ' && title[hash_start - 1] != '\t') return title;
  return rtrim_copy(title.substr(0, hash_start - 1));
}

bool
extract_heading_label(const std::string& line,
                      std::string& target,
                      std::string& label) {
  size_t pos = 0;
  while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t')) pos++;

  size_t level = 0;
  while (pos + level < line.size() && line[pos + level] == '#') level++;
  if (level == 0 || level > 6) return false;
  if (pos + level >= line.size()) return false;
  if (line[pos + level] != ' ' && line[pos + level] != '\t') return false;

  std::string title = trim_copy(line.substr(pos + level));
  title = strip_closing_heading_hashes(title);
  if (title.empty()) return false;

  target = title;
  label = "H" + std::to_string(level) + " " + title;
  return true;
}

std::string
collapse_whitespace(const std::string& s) {
  std::string out;
  bool last_space = false;
  for (char ch : s) {
    bool space = (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n');
    if (space) {
      if (!out.empty() && !last_space) out += ' ';
    }
    else {
      out += ch;
    }
    last_space = space;
  }
  return trim_copy(out);
}

std::string
replace_md_with_ath(const std::string& rel_path) {
  if (rel_path.size() >= 3 && rel_path.substr(rel_path.size() - 3) == ".md") {
    return rel_path.substr(0, rel_path.size() - 3) + ".ath";
  }
  return rel_path + ".ath";
}

bool
starts_with_blockquote(const std::string& line) {
  size_t pos = 0;
  while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t')) pos++;
  return pos < line.size() && line[pos] == '>';
}

std::string
strip_one_blockquote_marker(const std::string& line) {
  size_t pos = 0;
  while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t')) pos++;
  if (pos < line.size() && line[pos] == '>') pos++;
  if (pos < line.size() && line[pos] == ' ') pos++;
  return line.substr(pos);
}

std::string
to_lower_ascii(std::string s) {
  for (char& ch : s) ch = (char) std::tolower((unsigned char) ch);
  return s;
}

bool
starts_with_token(const std::string& s, size_t pos, const std::string& token) {
  if (s.compare(pos, token.size(), token) != 0) return false;
  size_t end = pos + token.size();
  return end >= s.size() || s[end] == ' ' || s[end] == '\t';
}

std::string
map_basic_callout_type(const std::string& base) {
  std::string b = to_lower_ascii(base);
  if (b == "question" || b == "help" || b == "faq") return "question";
  if (b == "warning" || b == "caution" || b == "attention" ||
      b == "failure" || b == "fail" || b == "missing" ||
      b == "danger" || b == "error" || b == "bug") {
    return "warning";
  }
  if (b == "example") return "example";
  if (b == "quote" || b == "cite") return "quote";
  return "note";
}

std::string
map_extended_callout_type(const std::string& ext) {
  if (ext == "Alternative Proof") return "proof-alternative";
  if (ext == "Standard Steps") return "proof-standard";
  if (ext == "Disambiguation") return "disambiguation";
  if (ext == "Caution") return "warning";
  if (ext == "Paster") return "blockquote";
  return to_lower_ascii(ext);
}

bool
parse_callout_header_line(const std::string& raw, CalloutHeaderInfo& out) {
  std::string line = strip_one_blockquote_marker(raw);
  size_t pos = 0;
  while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t')) pos++;
  if (line.compare(pos, 2, "[!") != 0) return false;
  pos += 2;
  size_t close = line.find(']', pos);
  if (close == std::string::npos) return false;

  std::string base = line.substr(pos, close - pos);
  pos = close + 1;
  if (pos < line.size() && (line[pos] == '+' || line[pos] == '-')) pos++;
  while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t')) pos++;

  static const char* kExts[] = {
      "Alternative Proof", "Standard Steps", "Disambiguation",
      "Proposition", "Corollary", "Conjecture", "Definition",
      "Question", "Theorem", "Example", "Caution", "Remark",
      "Paster", "Axiom", "Lemma", "Law"};

  std::string type = map_basic_callout_type(base);
  for (const char* ext : kExts) {
    std::string token = ext;
    if (!starts_with_token(line, pos, token)) continue;
    type = map_extended_callout_type(token);
    pos += token.size();
    while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t')) pos++;
    break;
  }

  out.type = type;
  out.header_tail = trim_copy(line.substr(pos));
  return true;
}

std::string
first_bold_segment(const std::string& s) {
  size_t open = s.find("**");
  if (open == std::string::npos) return "";
  size_t close = s.find("**", open + 2);
  if (close == std::string::npos || close <= open + 2) return "";
  return s.substr(open + 2, close - open - 2);
}

std::string
normalize_callout_auto_title_candidate(std::string s) {
  s = collapse_whitespace(trim_copy(s));

  while (!s.empty() && std::ispunct((unsigned char) s.front())) {
    s.erase(s.begin());
  }
  while (!s.empty() && std::ispunct((unsigned char) s.back())) {
    s.pop_back();
  }

  std::string out;
  for (char ch : s) {
    out += (char) std::tolower((unsigned char) ch);
  }
  return collapse_whitespace(out);
}

bool
is_common_callout_auto_title_candidate(const std::string& s) {
  std::string normalized = normalize_callout_auto_title_candidate(s);
  static const char* kIgnored[] = {
      "not", "cannot", "however", "but", "should not", "is", "is not",
      "can not"};
  for (const char* ignored : kIgnored) {
    if (normalized == ignored) return true;
  }
  return false;
}

std::string
wikilink_visible_text(const std::string& body) {
  size_t pipe = body.find('|');
  if (pipe != std::string::npos) return trim_copy(body.substr(pipe + 1));
  return trim_copy(body);
}

std::string
reduce_markdown_links_to_text(const std::string& s) {
  std::string out;
  for (size_t i = 0; i < s.size(); ) {
    if (i + 2 <= s.size() && s.compare(i, 2, "[[") == 0) {
      size_t close = s.find("]]", i + 2);
      if (close != std::string::npos) {
        out += wikilink_visible_text(s.substr(i + 2, close - i - 2));
        i = close + 2;
        continue;
      }
    }

    if ((i == 0 || s[i - 1] != '!') && s[i] == '[') {
      size_t close_text = s.find(']', i + 1);
      if (close_text != std::string::npos &&
          close_text + 1 < s.size() && s[close_text + 1] == '(') {
        size_t close_url = s.find(')', close_text + 2);
        if (close_url != std::string::npos) {
          out += s.substr(i + 1, close_text - i - 1);
          i = close_url + 1;
          continue;
        }
      }
    }

    out += s[i++];
  }
  return out;
}

bool
uses_parenthesized_title(const std::string& type) {
  return type == "theorem" || type == "lemma" || type == "proposition" ||
         type == "corollary" || type == "conjecture" || type == "question";
}

bool
decode_utf8_codepoint(const std::string& s, size_t& pos,
                      char32_t& cp, std::string& original) {
  if (pos >= s.size()) return false;

  unsigned char lead = (unsigned char) s[pos];
  size_t len = 0;
  if (lead < 0x80) {
    cp = lead;
    len = 1;
  }
  else if ((lead & 0xE0) == 0xC0 && pos + 1 < s.size()) {
    cp = ((lead & 0x1F) << 6) |
         ((unsigned char) s[pos + 1] & 0x3F);
    len = 2;
  }
  else if ((lead & 0xF0) == 0xE0 && pos + 2 < s.size()) {
    cp = ((lead & 0x0F) << 12) |
         (((unsigned char) s[pos + 1] & 0x3F) << 6) |
         ((unsigned char) s[pos + 2] & 0x3F);
    len = 3;
  }
  else if ((lead & 0xF8) == 0xF0 && pos + 3 < s.size()) {
    cp = ((lead & 0x07) << 18) |
         (((unsigned char) s[pos + 1] & 0x3F) << 12) |
         (((unsigned char) s[pos + 2] & 0x3F) << 6) |
         ((unsigned char) s[pos + 3] & 0x3F);
    len = 4;
  }
  else {
    pos++;
    return false;
  }

  original = s.substr(pos, len);
  pos += len;
  return true;
}

bool
is_cjk_codepoint(char32_t cp) {
  return unicode_is_cjk_ideograph ((std::uint32_t) cp);
}

std::string
sanitize_anchor_text(const std::string& s, size_t limit) {
  std::string out;
  size_t count = 0;
  for (size_t pos = 0; pos < s.size(); ) {
    char32_t cp = 0;
    std::string original;
    size_t before = pos;
    if (!decode_utf8_codepoint(s, pos, cp, original)) continue;
    if (cp < 0x80) {
      if (cp == ' ' || cp == '\t' || cp == '\r' || cp == '\n') {
        out += ' ';
      }
      else if (std::isalnum((unsigned char) cp)) {
        out += (char) cp;
      }
      else {
        continue;
      }
      count++;
    }
    else if (is_cjk_codepoint(cp)) {
      out += original;
      count++;
    }
    else if (pos == before) {
      pos++;
    }
    if (limit != 0 && count >= limit) break;
  }
  return out;
}

std::string
make_paragraph_anchor_sample(const std::vector<std::string>& lines) {
  std::string text;
  for (size_t i = 0; i < lines.size(); ++i) {
    if (i > 0) text += ' ';
    text += lines[i];
  }
  text = collapse_whitespace(text);
  text = reduce_markdown_links_to_text(text);
  if (text.size() > 50) text = text.substr(0, 50);
  return text;
}

std::pair<std::string,std::string>
make_paragraph_anchor_pair(const std::vector<std::string>& lines) {
  std::string sample = make_paragraph_anchor_sample(lines);
  if (sample.empty()) sample = "paragraph";
  return std::make_pair(sample + " {", sample + " }");
}

bool
extract_bold_proof_marker_line(const std::string& raw,
                               std::string& tag,
                               std::string& title,
                               std::string& body) {
  std::string line = trim_copy(aofm::strip_trailing_newlines(raw));
  if (line.compare(0, 2, "**") != 0) return false;

  size_t marker_end = std::string::npos;
  size_t delimiter_size = 0;
  for (const std::string& delimiter : {std::string(":**"), std::string("：**")}) {
    size_t pos = line.find(delimiter, 2);
    if (pos != std::string::npos &&
        (marker_end == std::string::npos || pos < marker_end)) {
      marker_end = pos;
      delimiter_size = delimiter.size();
    }
  }
  if (marker_end == std::string::npos) return false;

  size_t after = marker_end + delimiter_size;
  if (line.size() > after &&
      line[after] != ' ' &&
      line[after] != '\t' &&
      line[after] != '\r' &&
      line[after] != '\n') {
    return false;
  }

  std::string marker = trim_copy(line.substr(2, marker_end - 2));
  title.clear();
  if (marker == "Proof") {
    tag = "proof";
  }
  else if (marker == "Proof (Alternative)") {
    tag = "proof-alternative";
  }
  else if (marker == "Proof (Standard)") {
    tag = "proof-standard";
  }
  else if (marker.compare(0, 7, "Proof (") == 0 &&
           marker.size() > 8 && marker.back() == ')') {
    tag = "proof";
    title = trim_copy(marker.substr(7, marker.size() - 8));
    if (title.empty()) return false;
  }
  else if (marker == "证明") {
    tag = "proof";
  }
  else if (marker == "Solution" || marker == "解") {
    tag = "solution";
  }
  else {
    return false;
  }

  body = trim_copy(line.substr(after));
  return true;
}

bool
line_closes_isolated_proof(const std::string& raw) {
  std::string trimmed = trim_copy(aofm::strip_trailing_newlines(raw));
  static const char* kSuffixes[] = {
      "$\\blacksquare$",
      "$\\blacksquare$.",
      "$\\blacksquare$。"
  };
  for (const char* suffix : kSuffixes) {
    if (aofm::ends_with(trimmed, suffix)) return true;
  }
  return false;
}

std::string
strip_isolated_proof_qed_suffix(std::string raw) {
  std::string trimmed = trim_copy(aofm::strip_trailing_newlines(raw));
  static const char* kSuffixes[] = {
      "$\\blacksquare$",
      "$\\blacksquare$.",
      "$\\blacksquare$。"
  };
  for (const char* suffix : kSuffixes) {
    std::string s = suffix;
    if (!aofm::ends_with(trimmed, s)) continue;
    trimmed.erase(trimmed.size() - s.size());
    return trim_copy(trimmed);
  }
  return trim_copy(raw);
}

std::pair<std::string,std::string>
make_proof_anchor_pair(const std::vector<std::string>& lines) {
  std::string tag = "proof";
  std::string first_body;
  size_t start = 0;

  if (!lines.empty()) {
    std::string parsed_tag;
    std::string parsed_title;
    std::string parsed_body;
    if (extract_bold_proof_marker_line(lines[0], parsed_tag,
                                       parsed_title, parsed_body)) {
      tag = parsed_tag;
      first_body = parsed_title;
      if (!parsed_body.empty()) {
        if (!first_body.empty()) first_body += ' ';
        first_body += parsed_body;
      }
      start = 1;
    }
  }

  std::string sample_source = first_body;
  for (size_t i = start; i < lines.size(); ++i) {
    if (!sample_source.empty()) sample_source += ' ';
    sample_source += lines[i];
  }
  sample_source = collapse_whitespace(sample_source);
  sample_source = strip_isolated_proof_qed_suffix(sample_source);
  sample_source = reduce_markdown_links_to_text(sample_source);

  std::string id = sanitize_anchor_text(sample_source, 100);
  if (id.empty()) id = tag;
  std::string prefix = tag + ":" + id;
  return std::make_pair(prefix + " {", prefix + " }");
}

std::pair<std::string,std::string>
make_callout_anchor_pair(const std::vector<std::string>& lines) {
  CalloutHeaderInfo header;
  if (lines.empty() || !parse_callout_header_line(lines[0], header)) {
    std::string sample = sanitize_anchor_text(collapse_whitespace(lines.empty() ? "" : lines[0]), 100);
    if (sample.empty()) sample = "anchor";
    return std::make_pair("note:" + sample + " {", "note:" + sample + " }");
  }

  std::string sample_source = header.header_tail;
  for (size_t i = 1; i < lines.size(); ++i) {
    if (!sample_source.empty()) sample_source += ' ';
    sample_source += strip_one_blockquote_marker(lines[i]);
  }
  sample_source = collapse_whitespace(sample_source);
  sample_source = reduce_markdown_links_to_text(sample_source);

  std::string bold = first_bold_segment(sample_source);
  std::string title;
  if (uses_parenthesized_title(header.type)) {
    if (!bold.empty() && bold[0] == '(') {
      size_t close = bold.find(')');
      if (close != std::string::npos && close > 1) {
        title = bold.substr(1, close - 1);
      }
    }
  }
  else if (!bold.empty()) {
    if (!is_common_callout_auto_title_candidate(bold)) {
      title = bold;
    }
  }

  std::string id = sanitize_anchor_text(title, 100);
  if (id.empty()) {
    // No bold-derived title: use the first sanitized content after the
    // callout type, including the body when the header line itself is bare.
    id = sanitize_anchor_text(sample_source, 100);
  }
  if (id.empty()) id = header.type;

  std::string prefix = header.type + ":" + id;
  return std::make_pair(prefix + " {", prefix + " }");
}

std::string
anchor_label_key(const std::string& label) {
  if (label.size() >= 2 &&
      label[label.size() - 2] == ' ' &&
      (label[label.size() - 1] == '{' || label[label.size() - 1] == '}')) {
    return label.substr(0, label.size() - 2);
  }
  return label;
}

std::string
append_anchor_label_number(const std::string& label, int nr) {
  std::string suffix;
  std::string base = label;
  if (label.size() >= 2 &&
      label[label.size() - 2] == ' ' &&
      (label[label.size() - 1] == '{' || label[label.size() - 1] == '}')) {
    suffix = label.substr(label.size() - 2);
    base = label.substr(0, label.size() - 2);
  }
  return base + " (" + std::to_string(nr) + ")" + suffix;
}

std::pair<std::string,std::string>
make_unique_anchor_pair(const std::pair<std::string,std::string>& pair,
                        std::unordered_map<std::string,int>& label_counts) {
  if (pair.first.empty()) return pair;

  std::string key = anchor_label_key(pair.first);
  int count = label_counts[key]++;
  if (count == 0) return pair;

  return std::make_pair(append_anchor_label_number(pair.first, count),
                        pair.second.empty() ? "" :
                        append_anchor_label_number(pair.second, count));
}

std::string
make_unique_label(const std::string& label,
                  std::unordered_map<std::string,int>& label_counts) {
  int count = label_counts[anchor_label_key(label)]++;
  return count == 0 ? label : append_anchor_label_number(label, count);
}

bool
extract_anchor_only(const std::string& line, std::string& anchor) {
  std::string trimmed = trim_copy(line);
  if (trimmed.empty() || trimmed[0] != '^') return false;
  anchor = trim_copy(trimmed.substr(1));
  return !anchor.empty();
}

bool
extract_trailing_anchor(const std::string& line,
                        std::string& content,
                        std::string& anchor) {
  std::string trimmed = rtrim_copy(line);
  if (trimmed.empty()) return false;

  size_t pos = trimmed.find_last_of('^');
  if (pos == std::string::npos || pos == 0) return false;
  if (!(trimmed[pos - 1] == ' ' || trimmed[pos - 1] == '\t')) return false;

  std::string candidate = trim_copy(trimmed.substr(pos + 1));
  if (candidate.empty()) return false;
  for (char ch : candidate) {
    if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') return false;
  }

  content = rtrim_copy(trimmed.substr(0, pos));
  anchor = candidate;
  return true;
}

void
store_anchor(AnchorMap& map, AnchorOccurrenceMap& occurrences,
             const std::string& anchor, const std::string& rel_ath_path,
             const std::string& file_hint, const std::pair<std::string,std::string>& pair,
             std::unordered_map<std::string,int>& label_counts,
             bool deduplicate) {
  std::pair<std::string,std::string> unique_pair =
      deduplicate ? make_unique_anchor_pair(pair, label_counts) : pair;
  std::string uuid = tm_to_std_string(vault_generate_uuid());
  AofmVaultAnchorInfo info;
  info.uuid = uuid;
  if (!unique_pair.second.empty()) {
    info.transclusion_uuid = tm_to_std_string(vault_generate_uuid());
  }
  info.path = rel_ath_path;
  info.anchor_1 = unique_pair.first;
  info.anchor_2 = unique_pair.second;
  info.hlink_w = make_wikilink_url(uuid, file_hint, info.anchor_1);

  std::string occurrence_key = anchor_occurrence_key(rel_ath_path, anchor);
  std::vector<AofmVaultAnchorInfo>& anchor_occurrences =
      occurrences[occurrence_key];
  size_t occurrence_index = anchor_occurrences.size();
  anchor_occurrences.push_back(info);

  if (map.find(anchor) == map.end()) {
    map[anchor] = info;
  }
  else {
    std::string duplicate_key;
    int suffix = (int) occurrence_index + 1;
    do {
      duplicate_key = anchor + "#duplicate-" + std::to_string(suffix++);
    } while (map.find(duplicate_key) != map.end());
    map[duplicate_key] = info;
  }
}

void
finalize_current_block(BlockContext& current, BlockContext& last) {
  if (current.kind == BlockKind::NONE || current.lines.empty()) return;
  last = current;
  current.clear();
}

void
set_heading_end_label(HeadingMap& heading_map, const OpenHeadingInfo& open,
                      const std::string& end_label) {
  auto it = heading_map.find(open.key);
  if (it != heading_map.end() && it->second.end_label.empty())
    it->second.end_label = end_label;

  if (!open.normalized_key.empty() && open.normalized_key != open.key) {
    auto normalized = heading_map.find(open.normalized_key);
    if (normalized != heading_map.end() && normalized->second.end_label.empty())
      normalized->second.end_label = end_label;
  }
}

OpenHeadingInfo
store_heading(HeadingMap& heading_map, HeadingOccurrenceMap& occurrences,
              const std::string& rel_ath_path, const std::string& file_hint,
              const std::string& target, const std::string& normalized_key,
              const std::string& raw_label, int level,
              std::unordered_map<std::string,int>& label_counts) {
  std::string unique_label = make_unique_label(raw_label, label_counts);
  std::string key = heading_map_key(file_hint, target);
  std::string map_key = key;
  if (heading_map.find(map_key) != heading_map.end()) {
    int suffix = 2;
    do {
      map_key = key + "\nduplicate-" + std::to_string(suffix++);
    } while (heading_map.find(map_key) != heading_map.end());
  }

  AofmVaultHeadingInfo info;
  info.uuid = tm_to_std_string(vault_generate_uuid());
  info.transclusion_uuid = tm_to_std_string(vault_generate_uuid());
  info.path = rel_ath_path;
  info.label = unique_label;
  info.level = level;
  heading_map[map_key] = info;

  if (map_key == key && normalized_key != key &&
      heading_map.find(normalized_key) == heading_map.end()) {
    heading_map[normalized_key] = info;
  }

  occurrences[heading_occurrence_key(rel_ath_path, raw_label)].push_back(map_key);

  OpenHeadingInfo open;
  open.level = level;
  open.key = map_key;
  open.normalized_key = (map_key == key) ? normalized_key : "";
  return open;
}

void
process_markdown_file(const ImportFileInfo& file_info, AnchorMap& map,
                      AnchorOccurrenceMap& occurrences,
                      HeadingOccurrenceMap& heading_occurrences,
                      HeadingMap& heading_map) {
  std::ifstream in(as_charp(concretize(file_info.source_url)), std::ios::in | std::ios::binary);
  if (!in.is_open()) {
    report_import_error("could not open file: " + tm_to_std_string(as_string(file_info.source_url)));
    return;
  }

  std::vector<std::string> lines;
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    lines.push_back(line);
  }

  BlockContext current;
  BlockContext last;
  std::string file_hint = path_stem(file_info.relative_md_path);
  std::vector<OpenHeadingInfo> open_headings;
  std::unordered_map<std::string,int> label_counts;

  for (size_t i = 0; i < lines.size(); ++i) {
    std::string anchor;
    std::string anchorless;
    std::string heading_target;
    std::string heading_label;

    if (extract_anchor_only(lines[i], anchor)) {
      const BlockContext& context =
          (current.kind != BlockKind::NONE && !current.lines.empty()) ? current : last;
      if (context.kind == BlockKind::CALLOUT) {
        store_anchor(map, occurrences, anchor, file_info.relative_ath_path, file_hint,
                     make_callout_anchor_pair(context.lines), label_counts);
      }
      else if (context.kind == BlockKind::PROOF) {
        store_anchor(map, occurrences, anchor, file_info.relative_ath_path, file_hint,
                     make_proof_anchor_pair(context.lines), label_counts);
      }
      else if (context.kind == BlockKind::PARAGRAPH) {
        store_anchor(map, occurrences, anchor, file_info.relative_ath_path, file_hint,
                     make_paragraph_anchor_pair(context.lines), label_counts);
      }
      else if (context.kind == BlockKind::HEADING && !context.lines.empty()) {
        store_anchor(map, occurrences, anchor, file_info.relative_ath_path, file_hint,
                     std::make_pair(context.lines[0], context.lines[0] + " }"),
                     label_counts, false);
      }
      finalize_current_block(current, last);
      continue;
    }

    if (is_blank_line(lines[i])) {
      if (current.kind == BlockKind::PROOF) {
        current.lines.push_back(lines[i]);
        continue;
      }
      finalize_current_block(current, last);
      continue;
    }

    if (extract_heading_label(lines[i], heading_target, heading_label)) {
      finalize_current_block(current, last);
      last.kind = BlockKind::HEADING;
      last.lines.clear();
      std::string normalized_key = normalized_heading_map_key(file_hint, heading_target);
      int level = heading_level_from_label(heading_label);
      OpenHeadingInfo open =
          store_heading(heading_map, heading_occurrences,
                        file_info.relative_ath_path, file_hint,
                        heading_target, normalized_key, heading_label,
                        level, label_counts);
      std::string unique_heading_label = heading_map[open.key].label;
      last.lines.push_back(unique_heading_label);
      while (!open_headings.empty() && open_headings.back().level >= level) {
        set_heading_end_label(heading_map, open_headings.back(), unique_heading_label);
        open_headings.pop_back();
      }
      open_headings.push_back(open);
      continue;
    }

    bool has_trailing_anchor = extract_trailing_anchor(lines[i], anchorless, anchor);
    std::string effective_line = has_trailing_anchor ? anchorless : lines[i];
    bool is_callout = starts_with_blockquote(effective_line);
    std::string proof_tag;
    std::string proof_title;
    std::string proof_body;
    bool is_proof = !is_callout &&
                    extract_bold_proof_marker_line(effective_line,
                                                   proof_tag, proof_title,
                                                   proof_body);
    BlockKind next_kind = is_callout ? BlockKind::CALLOUT :
                          (is_proof || current.kind == BlockKind::PROOF
                           ? BlockKind::PROOF
                           : BlockKind::PARAGRAPH);

    if (current.kind != BlockKind::NONE && current.kind != next_kind) {
      finalize_current_block(current, last);
    }
    if (current.kind == BlockKind::NONE) current.kind = next_kind;
    current.lines.push_back(effective_line);

    if (has_trailing_anchor) {
      if (current.kind == BlockKind::CALLOUT) {
        store_anchor(map, occurrences, anchor, file_info.relative_ath_path, file_hint,
                     make_callout_anchor_pair(current.lines), label_counts);
      }
      else if (current.kind == BlockKind::PROOF) {
        store_anchor(map, occurrences, anchor, file_info.relative_ath_path, file_hint,
                     make_proof_anchor_pair(current.lines), label_counts);
      }
      else {
        store_anchor(map, occurrences, anchor, file_info.relative_ath_path, file_hint,
                     make_paragraph_anchor_pair(current.lines), label_counts);
      }
      finalize_current_block(current, last);
    }
    else if (current.kind == BlockKind::PROOF &&
             line_closes_isolated_proof(effective_line)) {
      finalize_current_block(current, last);
    }
  }
}

void
dump_anchor_map(const AnchorMap& map, std::ostream& out) {
  std::vector<std::string> anchors;
  anchors.reserve(map.size());
  for (const auto& entry : map) anchors.push_back(entry.first);
  std::sort(anchors.begin(), anchors.end());

  out << "--- AOFM VAULT ANCHOR MAP BEGIN ---" << std::endl;
  for (const std::string& anchor : anchors) {
    const AofmVaultAnchorInfo& info = map.at(anchor);
    out << anchor << " -> ("
              << "uuid=" << info.uuid
              << ", transclusion_uuid=" << info.transclusion_uuid
              << ", path=" << info.path
              << ", anchor_1=" << info.anchor_1
              << ", anchor_2=" << info.anchor_2
              << ", hlink_w=" << info.hlink_w
              << ")" << std::endl;
  }
  out << "--- AOFM VAULT ANCHOR MAP END ---" << std::endl;
}

void
dump_heading_map(const HeadingMap& map, std::ostream& out) {
  std::vector<std::string> headings;
  headings.reserve(map.size());
  for (const auto& entry : map) headings.push_back(entry.first);
  std::sort(headings.begin(), headings.end());

  out << "--- AOFM VAULT HEADING MAP BEGIN ---" << std::endl;
  for (const std::string& heading : headings) {
    const AofmVaultHeadingInfo& info = map.at(heading);
    out << heading << " -> ("
        << "uuid=" << info.uuid
        << ", transclusion_uuid=" << info.transclusion_uuid
        << ", path=" << info.path
        << ", label=" << info.label
        << ", end_label=" << info.end_label
        << ", level=" << info.level
        << ")" << std::endl;
  }
  out << "--- AOFM VAULT HEADING MAP END ---" << std::endl;
}

bool
scan_markdown_files(url source_root, url source_dir, url destination_dir,
                    std::vector<ImportFileInfo>& files,
                    AssetIndexMap& asset_map,
                    DirChildrenMap& dir_children) {
  bool err = false;
  array<string> entries = read_directory(source_dir, err);
  if (err) return false;

  for (int i = 0; i < N(entries); ++i) {
    string entry = entries[i];
    if (N(entry) > 0 && entry[0] == '.') continue;

    url src = source_dir * url(entry);
    if (is_directory(src)) {
      std::string rel_dir =
          normalize_rel_path(tm_to_std_string(as_unix_string(delta(source_root * url(""), src))));
      std::string parent_dir = path_dirname(rel_dir);
      dir_children[parent_dir].push_back(rel_dir);

      mkdir(destination_dir * url(entry));
      if (!scan_markdown_files(source_root, src, destination_dir * url(entry),
                               files, asset_map, dir_children)) {
        return false;
      }
      continue;
    }

    std::string rel_path =
        normalize_rel_path(tm_to_std_string(as_unix_string(delta(source_root * url(""), src))));
    if (is_copyable_asset_target(rel_path)) {
      url dst = destination_dir * url(entry);
      copy(src, dst);
      if (!exists(dst)) {
        report_import_warning("failed to copy asset: " + rel_path);
      }
      AofmVaultAssetInfo info;
      info.relative_path = rel_path;
      asset_map[asset_key(rel_path)] = info;
      continue;
    }

    if (suffix(src) != "md") continue;

    std::string rel_md = rel_path;
    ImportFileInfo info;
    info.source_url = src;
    info.relative_md_path = rel_md;
    info.relative_ath_path = replace_md_with_ath(rel_md);
    files.push_back(info);
  }

  for (auto& entry : dir_children) {
    std::sort(entry.second.begin(), entry.second.end());
  }

  return true;
}


} // namespace aofm_import_vault_internal
