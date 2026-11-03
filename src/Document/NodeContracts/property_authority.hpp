#pragma once

namespace athena::contracts {

enum class property_authority { inferred, declared, builtin };

// Equal-priority declarations follow scope/package order. Inference never
// replaces explicit declarations, and neither can rewrite a native invariant.
inline bool accepts (property_authority current, property_authority incoming) {
  return static_cast<int> (incoming) >= static_cast<int> (current);
}

} // namespace athena::contracts
