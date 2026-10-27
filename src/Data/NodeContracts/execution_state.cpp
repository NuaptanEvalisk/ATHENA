// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#include "execution_state.hpp"

namespace athena::contracts {
namespace {
thread_local evaluation_state* active_state= nullptr;
}

evaluation_state& current_evaluation_state () {
  if (active_state) return *active_state;
  static thread_local evaluation_state baseline (builtin_contracts ().snapshot ());
  return baseline;
}

evaluation_state* exchange_evaluation_state (evaluation_state* state) noexcept {
  auto* previous= active_state;
  active_state= state;
  return previous;
}

} // namespace athena::contracts
