/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#include "tree_label.hpp"
#include "vars.hpp"
#include <mutex>

void
init_standard_tree_labels () {
  static std::once_flag initialized;
  std::call_once (initialized, [] {
#define init(label, name, ...) make_tree_label (label, name)
#define init_var(name, ...) make_tree_label (name)
#include "standard_tree_labels.inc"
#undef init_var
#undef init
    // Declarations come from trusted shipped style sources, never input XML.
#include "shipped_macro_labels.inc"
  });
}
