/******************************************************************************
* MODULE     : aofm_chatgpt.hpp
* DESCRIPTION: Repair ChatGPT clipboard Markdown before AOFM conversion
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
*******************************************************************************/

#ifndef AOFM_CHATGPT_HPP
#define AOFM_CHATGPT_HPP

#include "string.hpp"
#include "tree.hpp"

#include <string>

std::string aofm_normalize_chatgpt_markdown (const std::string& source);
tree aofm_chatgpt_to_tree (string source);

#endif // AOFM_CHATGPT_HPP
