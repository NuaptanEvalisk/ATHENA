/******************************************************************************
* MODULE     : QTMVaultAvailableEnunciations.cpp
* DESCRIPTION: Enumerate enunciations through actual recursive transclusion ranges
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#include "QTMVaultAvailableEnunciations.hpp"
#include "QTMVaultAnchorModel.hpp"
#include "ATHENA/Data/transclusion_cache.hpp"
#include "convert.hpp"
#include "qt_utilities.hpp"
#include <deque>
#include <map>
#include <set>
#include <tuple>

namespace {
std::shared_ptr<const std::string> serialize (tree body) {
  string bytes= tree_to_scheme (body);
  return std::make_shared<const std::string> (bytes.data (), N (bytes));
}
struct Source {
  tree body;
  std::shared_ptr<const std::string> bytes;
};
struct Range { QString file, begin, end; unsigned depth= 0; };
using Key= std::tuple<QString, QString, QString>;
}

AvailableEnunciations collect_available_enunciations (
  std::shared_ptr<const std::string> source_body, const QString& source_path,
  const std::function<bool (const std::string&, AthenaVaultMapNode&)>& locate,
  const std::function<tree (const QString&)>& load,
  const std::atomic<bool>& cancelled) {
  AvailableEnunciations result;
  std::map<QString, Source> sources;
  sources.emplace (source_path, Source {scheme_to_tree (
    string (source_body->data (), source_body->size ())), source_body});
  std::deque<Range> pending {{source_path, {}, {}, 0}};
  std::set<Key> visited, emitted;
  std::size_t inspected= 0;
  while (!pending.empty () && !cancelled.load ()) {
    Range range= std::move (pending.front ()); pending.pop_front ();
    if (!visited.emplace (range.file, range.begin, range.end).second) continue;
    if (visited.size () > 10000 || range.depth > 64) {
      result.warnings << "Transclusion traversal limit reached.";
      break;
    }
    try {
      auto source= sources.find (range.file);
      if (source == sources.end ()) {
        tree body= load (range.file);
        if (body == UNINIT || is_func (body, _ERROR))
          throw std::runtime_error ("Cannot read transclusion source");
        source= sources.emplace (range.file, Source {body, serialize (body)}).first;
      }
      tree selected= athena_transclusion_source_range (source->second.body,
        from_qstring (range.begin), from_qstring (range.end));
      if (selected == UNINIT) throw std::runtime_error ("Transclusion anchors not found");
      std::vector<WikilinkAnchorEntry> anchors;
      collect_anchors (selected, path (), anchors);
      for (const auto& pair: collect_transclusion_pairs (anchors)) {
        if (cancelled.load ()) return result;
        if (!anchor_pair_is_enunciation (pair) || range.file.isEmpty ()) continue;
        if (!emitted.emplace (range.file, pair.upper, pair.lower).second) continue;
        result.entries.push_back ({range.file, pair.upper, pair.lower,
          anchor_pair_key (pair.upper), anchor_pair_tag (pair.upper), source->second.bytes});
      }
      std::vector<tree> stack {selected};
      while (!stack.empty () && !cancelled.load ()) {
        tree node= std::move (stack.back ()); stack.pop_back ();
        if (++inspected > 1000000) {
          result.warnings << "Document traversal limit reached.";
          return result;
        }
        if (is_atomic (node)) continue;
        if (is_compound (node, "transclude")) {
          if (N (node) != 4 || !is_atomic (node[0])) {
            result.warnings << range.file + ": malformed transclusion";
            continue;
          }
          AthenaVaultMapNode target;
          const string uuid= node[0]->label;
          if (!locate (std::string (uuid.data (), N (uuid)), target)) {
            result.warnings << range.file + ": transclusion UUID not found";
            continue;
          }
          pending.push_back ({to_qstring (string (target.path.data (), target.path.size ())),
            to_qstring (string (target.anchor_begin.data (), target.anchor_begin.size ())),
            to_qstring (string (target.anchor_end.data (), target.anchor_end.size ())),
            range.depth + 1});
          continue;
        }
        for (int i= N (node) - 1; i>=0; --i) stack.push_back (node[i]);
      }
    }
    catch (const std::exception& error) {
      result.warnings << range.file + ": " + QString::fromUtf8 (error.what ());
    }
  }
  return result;
}
