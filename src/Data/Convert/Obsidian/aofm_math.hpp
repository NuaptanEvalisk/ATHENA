#ifndef AOFM_MATH_H
#define AOFM_MATH_H

#include <string>
#include <peglib.h>
#include <memory>
#include "tree.hpp"

namespace aofm {

using AstPtr = std::shared_ptr<peg::Ast>;

tree convert_latex_math_inline(const std::string& latex_source);
tree convert_latex_math_display(const std::string& latex_source);
tree convert_math_block(const AstPtr& ast);

} // namespace aofm

#endif
