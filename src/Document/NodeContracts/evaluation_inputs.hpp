// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#pragma once

#include <memory>
#include <set>
#include <string>

namespace athena::contracts {

// Name/presence changes only. Values stay in the matching owner environment;
// this record neither retains legacy trees nor discovers changes by scanning it.
class evaluation_input_snapshot {
  struct root;
  std::shared_ptr<const root> base;
  friend class evaluation_inputs;
public:
  evaluation_input_snapshot ()= default;
  static evaluation_input_snapshot baseline (std::set<std::string> names);
  bool same_snapshot (const evaluation_input_snapshot& other) const { return base == other.base; }
  bool same_receipt (const evaluation_input_snapshot&, const std::string& name) const;
  std::set<std::string> changed_since (const evaluation_input_snapshot&) const;
};

class evaluation_inputs {
  evaluation_input_snapshot current;
public:
  void record (std::string name, bool present);
  void reset (evaluation_input_snapshot snapshot);
  evaluation_input_snapshot snapshot () const { return current; }
};

} // namespace athena::contracts
