// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#pragma once

#include <string>
#include <vector>

namespace athena::contracts {

enum class inference_state { unresolved, resolved, ambiguous, dynamic };

// Evidence about optional analysis, never a persisted node's schema type.
struct field_analysis {
  inference_state type= inference_state::unresolved;
  inference_state access= inference_state::unresolved;
  inference_state environment= inference_state::unresolved;
  bool conditional= false;
  std::vector<std::string> conflicts;

  bool operator == (const field_analysis& other) const {
    return type == other.type && access == other.access &&
      environment == other.environment && conditional == other.conditional &&
      conflicts == other.conflicts;
  }
};

} // namespace athena::contracts
