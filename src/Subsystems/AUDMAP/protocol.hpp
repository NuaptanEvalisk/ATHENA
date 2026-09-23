/******************************************************************************
* MODULE     : protocol.hpp
* DESCRIPTION: Transport and authorization constants independent of the server
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#pragma once
#include "codec.hpp"
namespace athena::interop {
inline constexpr unsigned audmap_endpoint_descriptor_version= 2;
inline constexpr unsigned audmap_protocol_version= 2;
inline constexpr unsigned audmap_document_model_version= 2;
enum class transport_opcode: unsigned { hello = 100, welcome, pending, ping, bye, rejected };
enum class trust_mode { full_access, confirm_operations, confirm_requests };
}
