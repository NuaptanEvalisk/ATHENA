/******************************************************************************
* MODULE     : redundant_wikilinks.cpp
* DESCRIPTION: Remove block wikilinks made redundant by radioactive links
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
******************************************************************************/

#include "ATHENA/Data/redundant_wikilinks.hpp"

#include "ATHENA/Data/artifact_radioactive_links.hpp"
#include "analyze.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <unordered_map>

namespace {

constexpr const char* wikilink_prefix= "tmfs://wikilink/";
constexpr const char* legacy_wikilink_prefix= "tmfs://Wikilink/";

struct LabelLocation {
  std::string value;
  path where;
};

struct BlockSpan {
  path upper;
  path lower;
  bool valid= false;
};

std::string
tm_bytes (string value) {
  return std::string (as_charp (value), (size_t) N(value));
}

tree
document_body (const tree& document) {
  if (!is_compound (document)) return document;
  for (int i=0; i<N(document); ++i)
    if (is_compound (document[i], "body", 1)) return document[i][0];
  return document;
}

void
collect_labels (const tree& value, path where,
                std::vector<LabelLocation>& labels) {
  if (is_atomic (value)) return;
  if (is_func (value, LABEL, 1))
    labels.push_back ({tm_bytes (tree_as_string (value[0])), where});
  for (int i=0; i<N(value); ++i)
    collect_labels (value[i], where * i, labels);
}

std::string
trimmed_anchor_key (std::string value) {
  value.erase (std::remove (value.begin (), value.end (), '{'), value.end ());
  value.erase (std::remove (value.begin (), value.end (), '}'), value.end ());
  auto space= [] (unsigned char c) { return std::isspace (c) != 0; };
  while (!value.empty () && space ((unsigned char) value.front ()))
    value.erase (value.begin ());
  while (!value.empty () && space ((unsigned char) value.back ()))
    value.pop_back ();
  return value;
}

BlockSpan
find_block_span (const tree& document, const std::string& upper_anchor) {
  BlockSpan result;
  if (upper_anchor.find ('{') == std::string::npos) return result;
  std::vector<LabelLocation> labels;
  collect_labels (document_body (document), path (), labels);
  std::string key= trimmed_anchor_key (upper_anchor);
  for (size_t i=0; i<labels.size (); ++i) {
    if (labels[i].value != upper_anchor) continue;
    for (size_t j=i+1; j<labels.size (); ++j) {
      if (labels[j].value.find ('}') == std::string::npos ||
          trimmed_anchor_key (labels[j].value) != key)
        continue;
      if (!path_less (labels[i].where, labels[j].where)) break;
      result.upper= labels[i].where;
      result.lower= labels[j].where;
      result.valid= true;
      return result;
    }
    return result;
  }
  return result;
}

bool
wikilink_uuid (const tree& destination, std::string& uuid) {
  uuid.clear ();
  if (!is_atomic (destination)) return false;
  std::string value= tm_bytes (destination->label);
  size_t prefix_size= 0;
  if (value.rfind (wikilink_prefix, 0) == 0)
    prefix_size= std::char_traits<char>::length (wikilink_prefix);
  else if (value.rfind (legacy_wikilink_prefix, 0) == 0)
    prefix_size= std::char_traits<char>::length (legacy_wikilink_prefix);
  else return false;
  size_t end= value.find ('/', prefix_size);
  uuid= value.substr (prefix_size, end == std::string::npos
                                    ? std::string::npos : end - prefix_size);
  return !uuid.empty ();
}

class Rewriter {
public:
  Rewriter (std::vector<AthenaRedundantWikilinkDocument>& documents,
            const std::vector<AthenaVaultMapNode>& map_nodes,
            const std::vector<AthenaArtifactRecord>& artifacts,
            AthenaRedundantWikilinkStats& stats)
    : documents (documents), artifacts (artifacts), stats (stats),
      matcher (artifacts) {
    for (size_t i=0; i<documents.size (); ++i)
      documents_by_path[documents[i].relative_path]= i;
    for (const AthenaVaultMapNode& node: map_nodes) nodes[node.uuid]= node;
    for (size_t i=0; i<artifacts.size (); ++i)
      artifacts_by_uuid[artifacts[i].uuid]= i;
  }

  void run () {
    stats.files_scanned= documents.size ();
    for (AthenaRedundantWikilinkDocument& document: documents) {
      size_t before= stats.links_removed;
      document.document= rewrite (document.document);
      document.removals= stats.links_removed - before;
      if (document.removals != 0) ++stats.files_changed;
    }
  }

private:
  bool full_radioactive_match (
      const tree& display, AthenaArtifactRadioactiveMatch& match) const {
    string text= tree_as_string (display);
    if (N(text) == 0) return false;
    std::vector<AthenaArtifactRadioactiveMatch> matches=
      matcher.matches (text);
    if (matches.size () != 1 || matches[0].start != 0 ||
        matches[0].end != N(text))
      return false;
    match= std::move (matches[0]);
    return !match.uuids.empty ();
  }

  bool target_contains_artifact (
      const AthenaVaultMapNode& node,
      const AthenaArtifactRadioactiveMatch& match) {
    auto document_hit= documents_by_path.find (node.path);
    if (document_hit == documents_by_path.end ()) return false;
    const tree& target= documents[document_hit->second].document;
    auto span_hit= block_spans.find (node.uuid);
    if (span_hit == block_spans.end ())
      span_hit= block_spans.emplace (
        node.uuid, find_block_span (target, node.anchor_end)).first;
    const BlockSpan& span= span_hit->second;
    if (!span.valid) return false;
    for (const std::string& uuid: match.uuids) {
      auto artifact_hit= artifacts_by_uuid.find (uuid);
      if (artifact_hit == artifacts_by_uuid.end ()) continue;
      const AthenaArtifactRecord& artifact= artifacts[artifact_hit->second];
      if (artifact.relative_path != node.path) continue;
      auto source_hit= artifact_sources.find (artifact.uuid);
      if (source_hit == artifact_sources.end ()) {
        path source;
        std::string error;
        bool valid= athena_artifact_locate_source (
          target, artifact, source, error);
        source_hit= artifact_sources.emplace (
          artifact.uuid, std::make_pair (valid, source)).first;
      }
      if (!source_hit->second.first) continue;
      const path& source= source_hit->second.second;
      if (path_less (span.upper, source) && path_less (source, span.lower))
        return true;
    }
    return false;
  }

  tree rewrite (const tree& value) {
    if (is_atomic (value)) return value;
    if ((is_func (value, HLINK, 2) || is_compound (value, "hlink", 2))) {
      std::string uuid;
      if (wikilink_uuid (value[1], uuid)) {
        auto node_hit= nodes.find (uuid);
        if (node_hit != nodes.end ()) {
          const AthenaVaultMapNode& node= node_hit->second;
          if (node.anchor_begin.empty () &&
              node.anchor_end.find ('{') != std::string::npos) {
            ++stats.block_wikilinks_scanned;
            AthenaArtifactRadioactiveMatch match;
            if (full_radioactive_match (value[0], match)) {
              ++stats.full_text_matches;
              if (target_contains_artifact (node, match)) {
                ++stats.links_removed;
                return rewrite (value[0]);
              }
              ++stats.unverified_targets;
            }
          }
        }
      }
    }
    tree result (L(value));
    for (int i=0; i<N(value); ++i) result << rewrite (value[i]);
    return result;
  }

  std::vector<AthenaRedundantWikilinkDocument>& documents;
  const std::vector<AthenaArtifactRecord>& artifacts;
  AthenaRedundantWikilinkStats& stats;
  AthenaArtifactRadioactiveMatcher matcher;
  std::unordered_map<std::string,size_t> documents_by_path;
  std::unordered_map<std::string,AthenaVaultMapNode> nodes;
  std::unordered_map<std::string,size_t> artifacts_by_uuid;
  std::unordered_map<std::string,BlockSpan> block_spans;
  std::unordered_map<std::string,std::pair<bool,path>> artifact_sources;
};

} // namespace

void
athena_remove_redundant_block_wikilinks (
  std::vector<AthenaRedundantWikilinkDocument>& documents,
  const std::vector<AthenaVaultMapNode>& map_nodes,
  const std::vector<AthenaArtifactRecord>& artifacts,
  AthenaRedundantWikilinkStats& stats) {
  stats= AthenaRedundantWikilinkStats ();
  Rewriter (documents, map_nodes, artifacts, stats).run ();
}
