#include "aofm_math.hpp"
#include "aofm_utils.hpp"
#include "aofm_ast_helpers.hpp"
#include "aofm_telemetry.hpp"
#include "convert.hpp"
#include <cctype>
#include <chrono>

namespace aofm {

static bool
is_tex_command_char(char c) {
  return std::isalpha(static_cast<unsigned char>(c)) != 0;
}

static void
replace_all(std::string& text, const std::string& from, const std::string& to) {
  if (from.empty()) return;

  size_t pos = 0;
  while ((pos = text.find(from, pos)) != std::string::npos) {
    text.replace(pos, from.size(), to);
    pos += to.size();
  }
}

static std::string
normalize_latex_math_source(const std::string& source) {
  std::string out;
  out.reserve(source.size());

  for (size_t i = 0; i < source.size();) {
    if (source[i] != '\\') {
      out += source[i++];
      continue;
    }

    size_t command_start = i + 1;
    size_t command_end = command_start;
    while (command_end < source.size() &&
           is_tex_command_char(source[command_end])) {
      command_end++;
    }

    if (source.compare(command_start, command_end - command_start,
                       "textemdash") == 0) {
      out += "\\longminus";
      i = command_end;
    }
    else if (source.compare(command_start, command_end - command_start,
                            "left") == 0 &&
             command_end < source.size() && source[command_end] == '<') {
      out += "\\left\\langle ";
      i = command_end + 1;
    }
    else if (source.compare(command_start, command_end - command_start,
                            "right") == 0 &&
             command_end < source.size() && source[command_end] == '>') {
      out += "\\right\\rangle ";
      i = command_end + 1;
    }
    else {
      out.append(source, i, command_end - i);
      i = command_end;
    }
  }

  replace_all(out, "\\begin{gathered}", "\\begin{eqnarray*}");
  replace_all(out, "\\end{gathered}", "\\end{eqnarray*}");

  return out;
}

tree
convert_latex_math_inline(const std::string& latex_source) {
  auto start = ::std::chrono::high_resolution_clock::now();
  std::string normalized_source = normalize_latex_math_source(latex_source);
  tree converted = extract(
      latex_document_to_tree(tm_string("$" + normalized_source + "$"), false, true),
      "body");

  converted = simplify_document(converted);

  if (is_func(converted, DOCUMENT) && N(converted) > 0) {
    converted = converted[0];
  }

  if (is_compound(converted, "math", 1)) {
    auto end = ::std::chrono::high_resolution_clock::now();
    aofm_math_time += ::std::chrono::duration<double>(end - start).count();
    aofm_math_count++;
    return converted;
  }

  if (is_func(converted, WITH) && N(converted) >= 3 &&
      converted[0] == "mode" && converted[1] == "math") {
    converted = converted[N(converted) - 1];
  }

  auto end = ::std::chrono::high_resolution_clock::now();
  aofm_math_time += ::std::chrono::duration<double>(end - start).count();
  aofm_math_count++;
  return compound("math", converted);
}

tree
convert_latex_math_display(const std::string& latex_source) {
  auto start = ::std::chrono::high_resolution_clock::now();
  std::string normalized_source = normalize_latex_math_source(latex_source);
  tree converted = extract(
      latex_document_to_tree(tm_string("$$" + normalized_source + "$$"), false, true),
      "body");

  auto end = ::std::chrono::high_resolution_clock::now();
  aofm_math_time += ::std::chrono::duration<double>(end - start).count();
  aofm_math_count++;

  if (is_document(converted)) {
    return simplify_document(converted);
  }

  return converted;
}

tree
convert_math_block(const AstPtr& ast) {
  return convert_latex_math_display(
      extract_fenced_body(ast_source(ast), "$$"));
}

} // namespace aofm
