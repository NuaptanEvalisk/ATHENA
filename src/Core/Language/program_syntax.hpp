#ifndef ATHENA_PROGRAM_SYNTAX_HPP
#define ATHENA_PROGRAM_SYNTAX_HPP

#include "tree.hpp"

enum class program_token
{
  code,
  string,
  comment
};
void prepare_program_syntax (tree source, string language);
program_token program_token_at (tree leaf, int offset);
program_token program_insertion_token (tree document, int row, tree leaf,
                                       int offset, string language);

#endif
