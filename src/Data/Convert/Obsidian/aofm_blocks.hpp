#ifndef AOFM_BLOCKS_H
#define AOFM_BLOCKS_H

#include <string>
#include <peglib.h>
#include <memory>
#include "tree.hpp"

namespace aofm {

using AstPtr = std::shared_ptr<peg::Ast>;

tree convert_block(const AstPtr& ast);
tree convert_paragraph(const AstPtr& ast);
tree convert_heading(const AstPtr& ast);
tree convert_list(const AstPtr& ast);
tree convert_list_item_body(const AstPtr& ast);
tree convert_blockquote(const AstPtr& ast);
tree convert_table(const AstPtr& ast);
tree convert_code_block(const AstPtr& ast);

tree parse_embedded_aofm_blocks(const std::string& raw, const std::string& source_name);

} // namespace aofm

#endif
