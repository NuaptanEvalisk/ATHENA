#ifndef AOFM_AST_HELPERS_H
#define AOFM_AST_HELPERS_H

#include <peglib.h>
#include <memory>
#include <string>

namespace aofm {

using AstPtr = std::shared_ptr<peg::Ast>;

// Global content for AST source extraction
extern std::string aofm_content;

bool ast_is(const AstPtr& ast, const char* rule_name);
bool ast_name_is(const AstPtr& ast, const char* rule_name);
std::string ast_source(const AstPtr& ast);

std::string extract_fenced_body(const std::string& raw, const std::string& fence);

AstPtr block_payload(const AstPtr& ast);

} // namespace aofm

#endif
