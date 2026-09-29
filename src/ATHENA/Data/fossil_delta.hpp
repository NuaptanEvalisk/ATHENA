/******************************************************************************
* MODULE     : fossil_delta.hpp
* DESCRIPTION: Fossil-compatible delta encoding/decoding for document history
* COPYRIGHT  : (C) 2026 ATHENA contributors
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
******************************************************************************/

#ifndef ATHENA_FOSSIL_DELTA_HPP
#define ATHENA_FOSSIL_DELTA_HPP

#include <optional>
#include <string>
#include <string_view>

namespace athena::history {

std::string fossil_delta_create (std::string_view source,
                                 std::string_view target);
std::optional<std::string> fossil_delta_apply (std::string_view source,
                                               std::string_view delta);
std::optional<std::size_t> fossil_delta_output_size (std::string_view delta);

} // namespace athena::history

#endif // ATHENA_FOSSIL_DELTA_HPP
