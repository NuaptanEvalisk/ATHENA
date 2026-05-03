#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "convert.hpp"
#include "file.hpp"
#include "tree.hpp"
#include "url.hpp"
#include "vault.hpp"

namespace {

struct AofmVaultAnchorInfo {
  std::string uuid;
  std::string path;
  std::string anchor_1;
  std::string anchor_2;
  std::string hlink_w;
};

enum class BlockKind {
  NONE,
  PARAGRAPH,
  CALLOUT
};

struct BlockContext {
  BlockKind kind = BlockKind::NONE;
  std::vector<std::string> lines;

  void clear() {
    kind = BlockKind::NONE;
    lines.clear();
  }
};

struct ImportFileInfo {
  url source_url;
  std::string relative_md_path;
  std::string relative_ath_path;
};

struct CalloutHeaderInfo {
  std::string type;
  std::string header_tail;
};

using AnchorMap = std::unordered_map<std::string, AofmVaultAnchorInfo>;

std::string
tm_to_std_string(string s) {
  return std::string(as_charp(s));
}

string
std_to_tm_string(const std::string& s) {
  return string(s.c_str());
}

std::string
tree_to_std_string(const tree& t) {
  return std::string(as_charp(as_string(t)));
}

tree
text_tree(const std::string& s) {
  return as_tree(string(s.c_str()));
}

tree
make_label_tree(const std::string& label) {
  return compound("label", text_tree(label));
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

bool
is_aofm_anchor_block_placeholder(const tree& t) {
  return is_compound(t, "__aofm_anchor_block", 1);
}

bool
is_aofm_inline_anchor_placeholder(const tree& t) {
  return is_compound(t, "__aofm_anchor_inline", 1);
}

std::string
placeholder_anchor_id(const tree& t) {
  if (!is_aofm_anchor_block_placeholder(t) &&
      !is_aofm_inline_anchor_placeholder(t)) {
    return "";
  }
  return tree_to_std_string(t[0]);
}

tree
materialize_anchor_literal(const tree& t) {
  if (is_aofm_anchor_block_placeholder(t)) {
    return text_tree("^" + placeholder_anchor_id(t));
  }
  if (is_aofm_inline_anchor_placeholder(t)) {
    return text_tree(" ^" + placeholder_anchor_id(t));
  }
  return t;
}

bool
is_enunciation_like_tree(const tree& t) {
  if (!is_compound(t) || N(t) != 1) return false;
  std::string tag = std::string(as_charp(as_string(L(t))));
  return tag == "theorem" || tag == "lemma" || tag == "corollary" ||
         tag == "proposition" || tag == "axiom" || tag == "definition" ||
         tag == "conjecture" || tag == "remark" || tag == "note" ||
         tag == "example" || tag == "warning" || tag == "question" ||
         tag == "proof" || tag == "solution" || tag == "law" ||
         tag == "disambiguation" || tag == "proof-alternative" ||
         tag == "proof-standard";
}

tree
resolve_anchor_placeholders(const tree& t, const AnchorMap& anchor_map,
                            const std::string& rel_ath_path) {
  if (is_aofm_inline_anchor_placeholder(t)) {
    std::string anchor = placeholder_anchor_id(t);
    auto it = anchor_map.find(anchor);
    if (it == anchor_map.end()) {
      report_import_error("anchor '^" + anchor + "' not found while converting " +
                          rel_ath_path);
      return materialize_anchor_literal(t);
    }
    return make_label_tree(it->second.anchor_1);
  }

  if (is_aofm_anchor_block_placeholder(t)) {
    std::string anchor = placeholder_anchor_id(t);
    auto it = anchor_map.find(anchor);
    if (it == anchor_map.end()) {
      report_import_error("anchor '^" + anchor + "' not found while converting " +
                          rel_ath_path);
      return materialize_anchor_literal(t);
    }
    return make_label_tree(it->second.anchor_1);
  }

  if (is_atomic(t)) return t;

  if (is_document(t)) {
    tree out(DOCUMENT);
    for (int i = 0; i < N(t); ++i) {
      const tree& child = t[i];
      if (is_aofm_anchor_block_placeholder(child)) {
        std::string anchor = placeholder_anchor_id(child);
        auto it = anchor_map.find(anchor);
        if (it == anchor_map.end()) {
          report_import_error("anchor '^" + anchor + "' not found while converting " +
                              rel_ath_path);
          append_document(out, materialize_anchor_literal(child));
          continue;
        }

        if (!it->second.anchor_2.empty() &&
            N(out) > 0 &&
            is_enunciation_like_tree(out[N(out) - 1])) {
          tree previous = out[N(out) - 1];
          out[N(out) - 1] = make_label_tree(it->second.anchor_1);
          append_document(out, previous);
          append_document(out, make_label_tree(it->second.anchor_2));
          continue;
        }

        append_document(out, make_label_tree(it->second.anchor_1));
        continue;
      }

      append_document(out,
                      resolve_anchor_placeholders(child, anchor_map,
                                                  rel_ath_path));
    }
    return simplify_document(out);
  }

  tree out(t, N(t));
  for (int i = 0; i < N(t); ++i) {
    out[i] = resolve_anchor_placeholders(t[i], anchor_map, rel_ath_path);
  }
  return out;
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

bool
is_blank_line(const std::string& line) {
  return trim_copy(line).empty();
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
path_stem(const std::string& path) {
  size_t slash = path.find_last_of("/\\");
  size_t start = (slash == std::string::npos) ? 0 : slash + 1;
  size_t dot = path.find_last_of('.');
  if (dot == std::string::npos || dot < start) return path.substr(start);
  return path.substr(start, dot - start);
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
  return
    (cp >= 0x3400 && cp <= 0x4DBF) ||
    (cp >= 0x4E00 && cp <= 0x9FFF) ||
    (cp >= 0xF900 && cp <= 0xFAFF) ||
    (cp >= 0x20000 && cp <= 0x2A6DF) ||
    (cp >= 0x2A700 && cp <= 0x2B73F) ||
    (cp >= 0x2B740 && cp <= 0x2B81F) ||
    (cp >= 0x2B820 && cp <= 0x2CEAF) ||
    (cp >= 0x2CEB0 && cp <= 0x2EBEF) ||
    (cp >= 0x30000 && cp <= 0x3134F);
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
  if (text.size() > 50) text = text.substr(0, 50);
  return text;
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
    title = bold;
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
store_anchor(AnchorMap& map, const std::string& anchor, const std::string& rel_ath_path,
             const std::string& file_hint, const std::pair<std::string,std::string>& pair) {
  std::string uuid = tm_to_std_string(vault_generate_uuid());
  AofmVaultAnchorInfo info;
  info.uuid = uuid;
  info.path = rel_ath_path;
  info.anchor_1 = pair.first;
  info.anchor_2 = pair.second;
  info.hlink_w = "tmfs://wikilink/" + uuid + "/" + file_hint + "/" + info.anchor_1;
  map[anchor] = info;
}

void
finalize_current_block(BlockContext& current, BlockContext& last) {
  if (current.kind == BlockKind::NONE || current.lines.empty()) return;
  last = current;
  current.clear();
}

void
process_markdown_file(const ImportFileInfo& file_info, AnchorMap& map) {
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

  for (size_t i = 0; i < lines.size(); ++i) {
    std::string anchor;
    std::string anchorless;

    if (extract_anchor_only(lines[i], anchor)) {
      const BlockContext& context =
          (current.kind != BlockKind::NONE && !current.lines.empty()) ? current : last;
      if (context.kind == BlockKind::CALLOUT) {
        store_anchor(map, anchor, file_info.relative_ath_path, file_hint,
                     make_callout_anchor_pair(context.lines));
      }
      else if (context.kind == BlockKind::PARAGRAPH) {
        store_anchor(map, anchor, file_info.relative_ath_path, file_hint,
                     std::make_pair(make_paragraph_anchor_sample(context.lines), ""));
      }
      finalize_current_block(current, last);
      continue;
    }

    if (is_blank_line(lines[i])) {
      finalize_current_block(current, last);
      continue;
    }

    bool has_trailing_anchor = extract_trailing_anchor(lines[i], anchorless, anchor);
    std::string effective_line = has_trailing_anchor ? anchorless : lines[i];
    bool is_callout = starts_with_blockquote(effective_line);
    BlockKind next_kind = is_callout ? BlockKind::CALLOUT : BlockKind::PARAGRAPH;

    if (current.kind != BlockKind::NONE && current.kind != next_kind) {
      finalize_current_block(current, last);
    }
    if (current.kind == BlockKind::NONE) current.kind = next_kind;
    current.lines.push_back(effective_line);

    if (has_trailing_anchor) {
      if (current.kind == BlockKind::CALLOUT) {
        store_anchor(map, anchor, file_info.relative_ath_path, file_hint,
                     make_callout_anchor_pair(current.lines));
      }
      else {
        store_anchor(map, anchor, file_info.relative_ath_path, file_hint,
                     std::make_pair(make_paragraph_anchor_sample(current.lines), ""));
      }
      finalize_current_block(current, last);
    }
  }
}

void
dump_anchor_map(const AnchorMap& map) {
  std::vector<std::string> anchors;
  anchors.reserve(map.size());
  for (const auto& entry : map) anchors.push_back(entry.first);
  std::sort(anchors.begin(), anchors.end());

  std::cout << "--- AOFM VAULT ANCHOR MAP BEGIN ---" << std::endl;
  for (const std::string& anchor : anchors) {
    const AofmVaultAnchorInfo& info = map.at(anchor);
    std::cout << anchor << " -> ("
              << "uuid=" << info.uuid
              << ", path=" << info.path
              << ", anchor_1=" << info.anchor_1
              << ", anchor_2=" << info.anchor_2
              << ", hlink_w=" << info.hlink_w
              << ")" << std::endl;
  }
  std::cout << "--- AOFM VAULT ANCHOR MAP END ---" << std::endl;
}

bool
scan_markdown_files(url source_root, url source_dir, url destination_dir,
                    std::vector<ImportFileInfo>& files) {
  bool err = false;
  array<string> entries = read_directory(source_dir, err);
  if (err) return false;

  for (int i = 0; i < N(entries); ++i) {
    string entry = entries[i];
    if (N(entry) > 0 && entry[0] == '.') continue;

    url src = source_dir * url(entry);
    if (is_directory(src)) {
      mkdir(destination_dir * url(entry));
      if (!scan_markdown_files(source_root, src, destination_dir * url(entry), files)) {
        return false;
      }
      continue;
    }

    if (suffix(src) != "md") continue;

    std::string rel_md = tm_to_std_string(as_unix_string(delta(source_root * url(""), src)));
    ImportFileInfo info;
    info.source_url = src;
    info.relative_md_path = rel_md;
    info.relative_ath_path = replace_md_with_ath(rel_md);
    files.push_back(info);
  }

  return true;
}

bool
validate_destination_dir(url destination_root) {
  if (!exists(destination_root)) {
    mkdir(destination_root);
    return true;
  }
  if (!is_directory(destination_root)) {
    report_import_error("destination path is not a directory");
    return false;
  }

  bool err = false;
  array<string> entries = read_directory(destination_root, err);
  if (err) {
    report_import_error("could not inspect destination directory");
    return false;
  }
  for (int i = 0; i < N(entries); ++i) {
    if (entries[i] == "." || entries[i] == "..") continue;
    report_import_error("destination directory is not empty");
    return false;
  }
  return true;
}

std::string
join_unix_paths(const std::string& root, const std::string& rel) {
  if (root.empty()) return rel;
  if (rel.empty()) return root;
  if (root[root.size() - 1] == '/') return root + rel;
  return root + "/" + rel;
}

} // namespace

bool
aofm_import_vault(string source_dir, string destination_dir) {
  url source_root = url_system(source_dir);
  url destination_root = url_system(destination_dir);
  if (!is_rooted(source_root)) {
    source_root = resolve(url_pwd(), "") * source_root;
  }
  if (!is_rooted(destination_root)) {
    destination_root = resolve(url_pwd(), "") * destination_root;
  }

  if (!exists(source_root) || !is_directory(source_root)) {
    report_import_error("source path is not a directory");
    return false;
  }
  if (!validate_destination_dir(destination_root)) return false;

  std::vector<ImportFileInfo> files;
  if (!scan_markdown_files(source_root, source_root, destination_root, files)) {
    report_import_error("failed to scan source vault");
    return false;
  }

  AnchorMap anchor_map;
  for (const ImportFileInfo& file_info : files) {
    process_markdown_file(file_info, anchor_map);
  }

  dump_anchor_map(anchor_map);

  std::string destination_root_path =
      tm_to_std_string(as_unix_string(destination_root));
  for (const ImportFileInfo& file_info : files) {
    tree document;
    if (!aofm_convert_tree(as_unix_string(file_info.source_url), document, false)) {
      report_import_error("failed to convert file: " + file_info.relative_md_path);
      return false;
    }

    tree resolved =
        resolve_anchor_placeholders(document, anchor_map, file_info.relative_ath_path);
    string serialized = tree_to_texmacs(resolved);
    std::string destination_path =
        join_unix_paths(destination_root_path, file_info.relative_ath_path);
    if (save_string(url_system(std_to_tm_string(destination_path)), serialized)) {
      report_import_error("failed to write destination file: " + destination_path);
      return false;
    }
  }

  return true;
}
