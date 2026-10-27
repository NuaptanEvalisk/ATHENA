// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#pragma once

#include "evaluation_state.hpp"
#include "builtin_contracts.hpp"

namespace athena::contracts {

evaluation_state& current_evaluation_state ();
evaluation_state* exchange_evaluation_state (evaluation_state*) noexcept;

// Borrow the state of this thread's active owner. The owner must outlive the
// scope; entering another actor does not share or copy its mutable state.
class evaluation_state_scope {
  evaluation_state* previous;
public:
  explicit evaluation_state_scope (evaluation_state* state):
    previous (exchange_evaluation_state (state)) {}
  ~evaluation_state_scope () { exchange_evaluation_state (previous); }
  evaluation_state_scope (const evaluation_state_scope&)= delete;
  evaluation_state_scope& operator= (const evaluation_state_scope&)= delete;
};

class owned_evaluation_scope {
  evaluation_state state;
  evaluation_state_scope current;
public:
  explicit owned_evaluation_scope (evaluation_state value):
    state (std::move (value)), current (&state) {}
};

} // namespace athena::contracts
