// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#include "evaluation_inputs.hpp"
#include <immer/map.hpp>
#include <immer/algorithm.hpp>

namespace athena::contracts {

struct evaluation_input_snapshot::root {
  struct revision {};
  using entries= immer::map<std::string, std::shared_ptr<const revision>>;
  entries names;
};

evaluation_input_snapshot evaluation_input_snapshot::baseline (std::set<std::string> names) {
  evaluation_input_snapshot result;
  auto next= std::make_shared<root> ();
  const auto revision= std::make_shared<const root::revision> ();
  for (const auto& name: names)
    next->names= std::move (next->names).set (name, revision);
  result.base= std::move (next);
  return result;
}

std::set<std::string> evaluation_input_snapshot::changed_since (
    const evaluation_input_snapshot& before) const {
  if (base == before.base) return {};
  const root::entries empty;
  std::set<std::string> result;
  const auto collect= [&] (const auto& entry) { result.insert (entry.first); };
  immer::diff (before.base ? before.base->names : empty, base ? base->names : empty,
    immer::make_differ (collect, collect,
      [&] (const auto&, const auto& entry) { collect (entry); }));
  return result;
}

void evaluation_inputs::record (std::string name, bool present) {
  pending.insert_or_assign (std::move (name), present);
}

void evaluation_inputs::reset (evaluation_input_snapshot snapshot) {
  current= std::move (snapshot);
  pending.clear ();
}

evaluation_input_snapshot evaluation_inputs::snapshot () const {
  if (pending.empty ()) return current;
  using root= evaluation_input_snapshot::root;
  auto next= current.base ? std::make_shared<root> (*current.base) : std::make_shared<root> ();
  // Even an assignment to an existing name receives a new receipt. Presence
  // alone cannot identify a changed value or a branch restored from a snapshot.
  const auto revision= std::make_shared<const root::revision> ();
  for (const auto& [name, present]: pending) {
    if (present) next->names= std::move (next->names).set (name, revision);
    else next->names= std::move (next->names).erase (name);
  }
  current.base= std::move (next);
  pending.clear ();
  return current;
}

} // namespace athena::contracts
