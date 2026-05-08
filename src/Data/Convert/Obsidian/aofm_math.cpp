#include "aofm_math.hpp"
#include "aofm_utils.hpp"
#include "aofm_ast_helpers.hpp"
#include "aofm_telemetry.hpp"
#include "convert.hpp"
#include <chrono>

namespace aofm {

tree
convert_latex_math_inline(const std::string& latex_source) {
  auto start = ::std::chrono::high_resolution_clock::now();
  tree converted = extract(
      latex_document_to_tree(tm_string("$" + latex_source + "$"), false, true),
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
  tree converted = extract(
      latex_document_to_tree(tm_string("$$" + latex_source + "$$"), false, true),
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
