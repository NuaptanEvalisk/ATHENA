/******************************************************************************
* MODULE     : selection.hpp
* DESCRIPTION: AUDM selector and predicate data model
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#pragma once

#include <nlohmann/json.hpp>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace athena::interop {

using value = nlohmann::json;

struct predicate {
  enum class kind { constant, exists, comparison, conjunction, disjunction,
                    negation };
  kind type = kind::constant;
  bool constant = true;
  std::string property;
  std::string operation;
  value literal;
  std::vector<predicate> operands;

  bool matches (const value& properties) const;
};

struct traversal_limits {
  std::optional<std::uint64_t> max_depth;
  std::optional<std::uint64_t> max_matches;
  std::optional<std::uint64_t> max_duration_ms;
};

struct selector {
  enum class kind { default_resource, name, local, scoped, recursive };
  kind type = kind::name;
  std::string name;
  predicate filter;
  traversal_limits limits;
};

using selection = std::vector<selector>;
// Throws std::invalid_argument; parsing does not consult any resource subsystem.
selection parse_selection (const std::string& source);

} // namespace athena::interop
