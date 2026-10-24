/******************************************************************************
* Native conversion paths and reachability. GNU GPL version 3 or later.
******************************************************************************/
#include "conversion_graph.hpp"
#include "conversion_registry.hpp"
#include "image_conversion.hpp"
#include "file.hpp"
#include "analyze.hpp"
#include <nlohmann/json.hpp>
#include <map>
#include <set>
#include <queue>
#include <algorithm>
#include <stdexcept>

namespace {
std::string bytes (string value) { return {value.data (), std::size_t (N(value))}; }
string text (const std::string& value) { return string (value.data (), value.size ()); }

struct graph {
  std::vector<conversion_edge> edges;
  std::map<std::string, std::vector<std::size_t>> forward, backward;

  void add (std::string from, std::string to, std::string executor) {
    auto index= edges.size ();
    for (auto old: forward[from])
      if (edges[old].to == to)
        throw std::runtime_error ("Duplicate conversion edge: " + from + " -> " + to);
    forward[from].push_back (index);
    backward[to].push_back (index);
    edges.push_back ({std::move (from), std::move (to), std::move (executor)});
  }

  graph () {
    string source;
    if (load_string (url ("$ATHENA_PATH/misc/convert/converters.json"), source, false))
      throw std::runtime_error ("Cannot load conversion graph");
    auto data= nlohmann::json::parse (source.data (), source.data () + N(source));
    if (data.at ("version").get<int> () != 1)
      throw std::runtime_error ("Unsupported conversion graph version");
    for (const auto& edge: data.at ("edges"))
      add (edge.at (0).get<std::string> (), edge.at (1).get<std::string> (),
           edge.at (2).get<std::string> ());
    for (const auto& language: data.at ("code_snippets")) {
      auto id= language.get<std::string> ();
      if (!conversion_format_exists (text (id)))
        throw std::runtime_error ("Unregistered source-code format: " + id);
      add ("texmacs-tree", id + "-snippet", "write-source-code");
      add (id + "-snippet", "texmacs-tree", "read-code-snippet");
    }
    if (data.at ("format_io").get<bool> ()) {
      auto formats= conversion_format_ids ();
      for (int i=0; i<N(formats); ++i) {
        auto id= bytes (formats[i]);
        add (id + "-document", id + "-file", "save-file");
        add (id + "-file", id + "-document", "load-file");
      }
    }
    if (data.at ("image_tools").get<bool> ()) {
      tree routes= conversion_image_routes ();
      for (int i=0; i<N(routes); ++i)
        add (bytes (scm_unquote (routes[i][0]->label)) + "-file",
             bytes (scm_unquote (routes[i][1]->label)) + "-file", "convert-image");
    }
  }
};

const graph& registry () { static const graph value; return value; }
}

std::optional<std::vector<conversion_edge>> conversion_graph_path (
    const std::string& from, const std::string& to) {
  const auto& graph= registry ();
  // All registered conversions have unit cost. BFS is the shortest-path
  // algorithm here, with JSON declaration order resolving equal-length paths.
  std::queue<std::string> pending;
  std::map<std::string, std::size_t> incoming;
  std::set<std::string> visited {from};
  pending.push (from);
  while (!pending.empty ()) {
    auto current= pending.front ();
    pending.pop ();
    if (current == to) {
      std::vector<conversion_edge> result;
      while (current != from) {
        const auto& edge= graph.edges[incoming.at (current)];
        result.push_back (edge);
        current= edge.from;
      }
      std::reverse (result.begin (), result.end ());
      return result;
    }
    auto next= graph.forward.find (current);
    if (next == graph.forward.end ()) continue;
    for (auto index: next->second) {
      const auto& edge= graph.edges[index];
      if (!visited.insert (edge.to).second) continue;
      incoming.emplace (edge.to, index);
      pending.push (edge.to);
    }
  }
  return std::nullopt;
}

tree conversion_graph_search (string from, string to) {
  auto path= conversion_graph_path (bytes (from), bytes (to));
  if (!path) return tree ("#f");
  tree result (TUPLE);
  result << tree (scm_quote (from));
  for (const auto& edge: *path) result << tree (scm_quote (text (edge.to)));
  return result;
}

tree conversion_graph_reachable (tree sources, bool backwards) {
  const auto& graph= registry ();
  const auto& adjacency= backwards ? graph.backward : graph.forward;
  std::queue<std::string> pending;
  std::set<std::string> seen;
  for (int i=0; i<N(sources); ++i) {
    auto source= bytes (scm_unquote (sources[i]->label));
    if (seen.insert (source).second) pending.push (source);
  }
  while (!pending.empty ()) {
    auto current= pending.front ();
    pending.pop ();
    auto next= adjacency.find (current);
    if (next == adjacency.end ()) continue;
    for (auto index: next->second) {
      const auto& edge= graph.edges[index];
      auto target= backwards ? edge.from : edge.to;
      if (seen.insert (target).second) pending.push (target);
    }
  }
  tree result (TUPLE);
  for (const auto& id: seen) result << tree (scm_quote (text (id)));
  return result;
}
