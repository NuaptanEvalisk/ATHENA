// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#pragma once

#include <memory>

// Share source analysis and font selection within one layout pass. Each box
// still owns its source path, shaping options, positioned glyphs and carets.
class text_preparation_scope {
  struct state;
  std::unique_ptr<state> state_;
public:
  text_preparation_scope ();
  ~text_preparation_scope ();
  text_preparation_scope (const text_preparation_scope&)= delete;
  text_preparation_scope& operator= (const text_preparation_scope&)= delete;
};
