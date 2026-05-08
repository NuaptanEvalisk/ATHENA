#include "aofm_ast_helpers.hpp"
#include "aofm_utils.hpp"

namespace aofm {

bool
ast_is(const AstPtr& ast, const char* rule_name) {
  return ast &&
         (ast->original_name == rule_name || ast->name == rule_name);
}

bool
ast_name_is(const AstPtr& ast, const char* rule_name) {
  return ast && ast->name == rule_name;
}

std::string
ast_source(const AstPtr& ast) {
  if (!ast) return "";
  return aofm_content.substr(ast->position, ast->length);
}

std::string
extract_fenced_body(const std::string& raw, const std::string& fence) {
  size_t body_start = raw.find('\n');
  if (body_start == std::string::npos) return "";
  size_t body_end = raw.rfind(fence);
  if (body_end == std::string::npos || body_end <= body_start) {
    return strip_trailing_newlines(raw.substr(body_start + 1));
  }
  return strip_trailing_newlines(raw.substr(body_start + 1,
      body_end - body_start - 1));
}

AstPtr
block_payload(const AstPtr& ast) {
  if (!ast) return nullptr;
  if (!ast_name_is(ast, "Block")) return ast;
  for (const auto& child : ast->nodes) {
    if (!ast_is(child, "BlankLine")) return child;
  }
  return nullptr;
}

} // namespace aofm
