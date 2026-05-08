#ifndef AOFM_UTILS_H
#define AOFM_UTILS_H

#include <string>
#include <vector>
#include "tree.hpp"

namespace aofm {

string tm_string(const std::string& s);
string std_to_tm_string(const std::string& s);

void report_aofm_error(const std::string& message);
void report_aofm_parse_error(const std::string& source_name, size_t line, size_t col,
                             const std::string& msg, const std::string& rule = "");

std::string trim_copy(const std::string& s);
std::string rtrim_copy(const std::string& s);
std::string strip_trailing_newlines(const std::string& s);
std::string strip_wrapping(const std::string& s, size_t left, size_t right);
bool is_blank_line(const std::string& line);
bool erase_prefix(std::string& s, const char* prefix);

bool is_space_or_tab(char c);
void skip_spaces(std::string::size_type& pos, const std::string& s);
bool consume_prefix(std::string::size_type& pos, const std::string& s, const std::string& prefix);
bool starts_with_token(const std::string& s, std::string::size_type pos, const std::string& token);

bool is_proof_marker_text(const std::string& raw);

std::string strip_known_invisible_prefixes(std::string line);
std::string normalize_markdown_lines(const std::string& raw);

size_t leading_space_count(const std::string& line);
bool starts_blockquote_line(const std::string& line);
bool starts_callout_header_line(const std::string& line);
std::string strip_one_blockquote_marker(const std::string& line);

bool ends_with(const std::string& s, const std::string& suffix);

} // namespace aofm

#endif
