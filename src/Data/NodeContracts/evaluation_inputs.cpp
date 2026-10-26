// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#include "evaluation_inputs.hpp"
#include <vector>

namespace athena::contracts {

struct evaluation_input_snapshot::root {
  std::set<std::string> names;
};
struct evaluation_input_snapshot::batch {
  std::shared_ptr<const batch> parent;
  std::map<std::string, bool> changes;
  unsigned depth;
};

evaluation_input_snapshot evaluation_input_snapshot::baseline (std::set<std::string> names) {
  evaluation_input_snapshot result;
  result.base= std::make_shared<const root> (root {std::move (names)});
  return result;
}

std::set<std::string> evaluation_input_snapshot::names () const {
  auto result= base ? base->names : std::set<std::string> {};
  std::vector<const batch*> chain;
  for (auto p= head.get (); p; p= p->parent.get ()) chain.push_back (p);
  for (auto p= chain.rbegin (); p != chain.rend (); ++p)
    for (const auto& [name, present]: (*p)->changes) {
      if (present) result.insert (name);
      else result.erase (name);
    }
  return result;
}

std::set<std::string> evaluation_input_snapshot::changed_since (
    const evaluation_input_snapshot& before) const {
  if (base != before.base) {
    auto result= names ();
    const auto previous= before.names ();
    result.insert (previous.begin (), previous.end ());
    return result;
  }
  std::set<std::string> result;
  auto a= head.get (), b= before.head.get ();
  const auto collect= [&] (const batch*& p) {
    for (const auto& [name, present]: p->changes) result.insert (name);
    p= p->parent.get ();
  };
  while (a != b) {
    if (a && (!b || a->depth >= b->depth)) collect (a);
    else collect (b);
  }
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
  const auto depth= current.head ? current.head->depth : 0;
  current.head= std::make_shared<const evaluation_input_snapshot::batch> (
    evaluation_input_snapshot::batch {current.head, std::move (pending), depth + 1});
  pending.clear ();
  // Bound retained history independently of the number of editing commands.
  // Compaction visits native names, never the environment or expression trees.
  if (depth >= 63) current= evaluation_input_snapshot::baseline (current.names ());
  return current;
}

} // namespace athena::contracts
