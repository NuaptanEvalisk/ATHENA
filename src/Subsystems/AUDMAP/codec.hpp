/******************************************************************************
* MODULE     : codec.hpp
* DESCRIPTION: AUDMAP wire opcodes, portable values and codec interfaces
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#pragma once
#include "selection.hpp"
#include <string_view>

namespace athena::interop {
enum class opcode: unsigned {
  req = 1, ack, ask, acx, opr, rsp, err, rel, lin, cnl, fin
};
constexpr std::size_t wire_size_limit = 8 * 1024 * 1024;
std::string encode_message (const value& message);
value decode_message (std::string_view bytes);
} // namespace athena::interop
