#ifndef AOFM_INLINE_H
#define AOFM_INLINE_H

#include <string>
#include <peglib.h>
#include <memory>
#include "tree.hpp"

namespace aofm {

using AstPtr = std::shared_ptr<peg::Ast>;

tree convert_inline(const AstPtr& ast);
tree convert_inline_children(const AstPtr& ast);
tree convert_inline_from_raw(const std::string& raw);
tree convert_emphasis_like(const AstPtr& ast, const char* tag, size_t left, size_t right);

std::string strip_inline_code_quotes(const std::string& s);

} // namespace aofm

#endif
