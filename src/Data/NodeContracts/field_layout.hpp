// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#pragma once

namespace athena::contracts {

enum class arity_kind { fixed, optional, repeated_suffix, repeated_prefix };
enum class field_mapping { uniform, split, detailed };

// Positional storage binding, independent of editor access and tree ownership.
struct field_layout {
  int base= 0, extra= 0;
  arity_kind arity= arity_kind::fixed;
  field_mapping mapping= field_mapping::uniform;

  int minimum () const { return arity == arity_kind::fixed ? base+extra : base; }
  int fields () const {
    if (base+extra == 0) return 0;
    return mapping == field_mapping::uniform ? 1 :
      mapping == field_mapping::split ? 2 : base+extra;
  }
  bool accepts (int size) const {
    if (size < minimum ()) return false;
    if (arity == arity_kind::fixed) return size == base+extra;
    if (arity == arity_kind::optional) return size <= base+extra;
    return (size-base) % extra == 0;
  }
  int index (int child, int size) const {
    if (mapping == field_mapping::uniform) return 0;
    const bool prefix= arity == arity_kind::repeated_prefix;
    if (mapping == field_mapping::split)
      return child < (prefix ? size-base : base) ? 0 : 1;
    if (arity == arity_kind::fixed || arity == arity_kind::optional) return child;
    if (!prefix) return child < base ? child : base+(child-base)%extra;
    return child < size-base ? child%extra : base+extra+child-size;
  }
  bool operator == (const field_layout& other) const {
    return base == other.base && extra == other.extra &&
      arity == other.arity && mapping == other.mapping;
  }
  bool operator != (const field_layout& other) const { return !(*this == other); }
};

} // namespace athena::contracts
