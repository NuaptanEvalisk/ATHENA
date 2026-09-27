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
#include "QTMVaultLinkModel.hpp"
#include "ATHENA/Data/transclusion_cache.hpp"
#include "ATHENA/Data/node_reference.hpp"
#include "ATHENA/Data/enunciation_model.hpp"
#include "Data/Convert/Xml/athena_document_xml.hpp"
#include "node_metadata.hpp"
#include "convert.hpp"
#include "qt_utilities.hpp"
#include <deque>
#include <map>
#include <set>
#include <tuple>

namespace {
std::shared_ptr<const std::string> serialize (tree body) {
  return std::make_shared<const std::string> (athena::document::write_xml_v2 (
    body, athena::document::xml_kind::fragment));
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
  sources.emplace (source_path, Source {athena::document::read_xml_v2 (
    *source_body, athena::document::xml_kind::fragment), source_body});
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

AvailableEnunciations collect_available_source_enunciations (
  std::shared_ptr<const std::string> source_body, const QString& source_path,
  const std::function<athena::node_location::snapshot (
    const std::vector<std::string>&, const std::vector<std::string>&)>& resolve,
  const std::atomic<bool>& cancelled) {
  AvailableEnunciations result;
  struct Selection {
    QString file;
    std::shared_ptr<const std::string> xml;
    std::vector<std::string> ancestry;
  };
  std::deque<Selection> pending {{source_path, source_body, {}}};
  std::set<QString> emitted;
  std::set<std::string> visited;
  std::size_t inspected= 0;
  while (!pending.empty () && !cancelled.load ()) {
    auto selection= std::move (pending.front ());
    pending.pop_front ();
    try {
      tree body= athena::document::read_xml_v2 (*selection.xml,
        athena::document::xml_kind::fragment);
      const auto targets= vault_source_targets (body);
      std::map<QString, unsigned> counts;
      for (const auto& target: targets) ++counts[target.uuid];
      for (const auto& target: targets) {
        if (cancelled.load ()) return result;
        tree source= is_nil (target.where) ? body : subtree (body, target.where);
        if (!athena::enunciation::is_canonical (source) || selection.file.isEmpty ())
          continue;
        if (counts[target.uuid] != 1) {
          result.warnings << selection.file + ": duplicated source UUID " + target.uuid;
          continue;
        }
        if (emitted.insert (target.uuid).second)
          result.entries.push_back ({selection.file, {}, {}, target.title,
                                     target.kind, selection.xml, target.uuid});
      }
      std::vector<tree> stack {body};
      while (!stack.empty () && !cancelled.load ()) {
        tree value= std::move (stack.back ()); stack.pop_back ();
        if (++inspected > 1000000 || selection.ancestry.size () > 64) {
          result.warnings << "Document traversal limit reached.";
          return result;
        }
        if (athena::node_reference::canonical (value)) {
          auto ids= athena::node_reference::targets (value);
          auto located= resolve (ids, selection.ancestry);
          if (!located) continue;
          for (const auto& item: located->items) {
            if (item.state != athena::node_location::status::resolved) {
              result.warnings << selection.file + ": " +
                QString::fromStdString (item.id + ": " + item.diagnostic);
              continue;
            }
            if (!visited.insert (item.id).second) continue;
            if (visited.size () > 10000) {
              result.warnings << "Transclusion traversal limit reached.";
              return result;
            }
            auto ancestry= selection.ancestry;
            ancestry.push_back (item.id);
            pending.push_back ({QString::fromStdString (item.candidates.at (0).file),
              std::make_shared<const std::string> (item.fragment_xml), std::move (ancestry)});
          }
          continue;
        }
        if (is_compound (value))
          for (int i=N(value)-1; i>=0; --i) stack.push_back (value[i]);
      }
    }
    catch (const std::exception& error) {
      result.warnings << selection.file + ": " + QString::fromUtf8 (error.what ());
    }
  }
  return result;
}
