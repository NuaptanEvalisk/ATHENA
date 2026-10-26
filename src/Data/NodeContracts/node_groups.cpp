#include "node_groups.hpp"
#include "file.hpp"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <stdexcept>

namespace athena::contracts {
namespace {
void append_unique (std::vector<std::string>& into, const std::string& value) {
  if (std::find (into.begin (), into.end (), value) == into.end ())
    into.push_back (value);
}
}

node_groups::node_groups (std::string_view source) {
  const auto data= nlohmann::json::parse (source);
  if (data.at ("version") != 1)
    throw std::runtime_error ("Unsupported node group declaration version");
  for (const auto& [name, entries]: data.at ("groups").items ()) {
    auto& result= declarations_[name];
    for (const auto& value: entries)
      if (value.is_string ()) result.push_back ({value.get<std::string> (), false});
      else result.push_back ({value.at ("group").get<std::string> (), true});
  }
  std::vector<std::string> active;
  for (const auto& [name, entries]: declarations_) resolve (name, active);
  for (const auto& pair: data.at ("alternates")) {
    if (!pair.is_array () || pair.size () != 2)
      throw std::runtime_error ("A node alternate requires exactly two tags");
    const auto a= pair[0].get<std::string> (), b= pair[1].get<std::string> ();
    for (const auto& edge: {std::make_pair (a, b), std::make_pair (b, a)}) {
      auto [it, added]= alternates_.emplace (edge);
      if (!added && it->second != edge.second)
        throw std::runtime_error ("Conflicting node alternate for " + edge.first);
    }
  }
}

void node_groups::resolve (const std::string& group, std::vector<std::string>& active) {
  if (members_.count (group)) return;
  if (std::find (active.begin (), active.end (), group) != active.end ()) {
    std::string chain;
    for (const auto& name: active) chain+= name + " -> ";
    throw std::runtime_error ("Cyclic node group: " + chain + group);
  }
  const auto declaration= declarations_.find (group);
  if (declaration == declarations_.end ())
    throw std::runtime_error ("Unknown node group " + group +
                              " referenced by " + active.back ());
  active.push_back (group);
  std::vector<std::string> values;
  auto& dependencies= dependencies_[group];
  dependencies.insert (group);
  for (const auto& entry: declaration->second) {
    if (!entry.group) append_unique (values, entry.name);
    else {
      resolve (entry.name, active);
      dependencies.insert (dependencies_.at (entry.name).begin (),
                           dependencies_.at (entry.name).end ());
      for (const auto& tag: members_.at (entry.name)) append_unique (values, tag);
    }
  }
  active.pop_back ();
  members_.emplace (group, std::move (values));
}

const std::vector<std::string>& node_groups::members (std::string_view group) const {
  const auto it= members_.find (group);
  if (it == members_.end ())
    throw std::invalid_argument ("Unknown node group: " + std::string (group));
  return it->second;
}

bool node_groups::contains (std::string_view group, std::string_view tag) const {
  const auto& values= members (group);
  return std::find (values.begin (), values.end (), tag) != values.end ();
}

bool node_groups::includes (std::string_view group, std::string_view subgroup) const {
  (void) members (group);
  return dependencies_.find (group)->second.count (std::string (subgroup)) != 0;
}

std::string node_groups::find (std::string_view group, std::string_view tag) const {
  (void) members (group);
  const auto& entries= declarations_.find (group)->second;
  for (const auto& entry: entries)
    if (!entry.group && entry.name == tag) return std::string (group);
  for (const auto& entry: entries)
    if (entry.group && contains (entry.name, tag)) return find (entry.name, tag);
  return {};
}

std::string node_groups::alternate (std::string_view tag) const {
  const auto it= alternates_.find (tag);
  return it == alternates_.end () ? std::string () : it->second;
}

std::vector<std::string> node_groups::variants (std::string_view tag, bool numbered) const {
  std::string base (tag);
  const bool starred= !base.empty () && base.back () == '*';
  if (starred) base.pop_back ();
  const bool has_number= contains ("numbered-tag", base);
  if (!has_number) base= std::string (tag);
  const auto group= find (numbered ? "similar-tag" : "variant-tag", base);
  std::vector<std::string> result= group.empty () ?
    std::vector<std::string> {base} : members (group);
  if (has_number && numbered) {
    const auto plain= result;
    for (const auto& name: plain) append_unique (result, name + "*");
  }
  else if (has_number && starred)
    for (auto& name: result) name+= "*";
  return result;
}

const node_groups& standard_node_groups () {
  static const node_groups value= [] {
    string source;
    if (load_string (url ("$ATHENA_PATH/misc/node-groups.json"), source, false))
      throw std::runtime_error ("Cannot load ATHENA node-groups.json");
    return node_groups (std::string_view (source.data (), N (source)));
  } ();
  return value;
}
} // namespace athena::contracts
