/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#include "hodarium_inventory.hpp"
#include "Data/Convert/Xml/athena_document_xml.hpp"
#include "interop_document_source.hpp"
#include "node_metadata.hpp"
#include <map>
#include <set>
#include <algorithm>
#include <stdexcept>

namespace athena::hodarium {
source_inventory inventory_sources (const std::filesystem::path& root,
  const std::atomic<bool>& cancelled,
  const std::map<std::string, source_inventory::cached_source>& previous,
  background::source_watch* watch) {
  auto files= background::inventory (root, &cancelled, watch);
  filesystem::confined_root directory (root);
  source_inventory result;
  std::map<std::string, std::string> identities;
  std::set<std::string> duplicates;
  std::set<std::string> paths;
  std::uint64_t bytes_held= 0;
  for (const auto& file: files) {
    if (cancelled.load ()) return {};
    if (!paths.insert (file.path).second) continue;
    try {
      auto entry= directory.open (file.path);
      auto before= entry.stat ();
      if (!filesystem::same_revision (before, file.revision))
        throw std::runtime_error ("Source changed during inventory");
      auto cached= previous.find (file.path);
      if (cached != previous.end () && cached->second.published &&
          filesystem::same_revision (before, cached->second.revision)) {
        result.cache.emplace (file.path, cached->second);
        auto known= identities.emplace (cached->second.object, file.path);
        if (!known.second) {
          duplicates.insert (cached->second.object);
          result.errors.push_back ("Duplicate source UUID: " + known.first->second + " and " + file.path);
        }
        continue;
      }
      if (before.size > 4ULL*1024*1024*1024 - bytes_held)
        throw std::length_error ("Hodarium source snapshot exceeds 4 GiB budget");
      auto bytes= std::make_shared<const std::string> (entry.read (document::codec_limits ().input_bytes));
      if (!filesystem::same_revision (before, entry.stat ()))
        throw std::runtime_error ("Source changed while reading");
      auto source= document::read_xml_v2 (*bytes);
      auto error= interop_document_source_error (source);
      if (!error.empty ()) throw std::runtime_error (error);
      std::string id;
      for (int i= 0; i < N(source); ++i)
        if (is_compound (source[i], "body", 1)) id= node::id (source[i][0]);
      if (!node::valid_id (id)) throw std::runtime_error ("Source body has no valid persistent UUID");
      auto known= identities.emplace (id, file.path);
      if (!known.second) {
        duplicates.insert (id);
        result.errors.push_back ("Duplicate source UUID: " + known.first->second + " and " + file.path);
      }
      bytes_held+= bytes->size ();
      result.cache.emplace (file.path, source_inventory::cached_source{before, id, false});
      result.documents.push_back ({file.path, std::move (id), before, std::move (bytes)});
    }
    catch (const std::exception& e) { result.errors.push_back (file.path + ": " + e.what ()); }
  }
  result.documents.erase (std::remove_if (result.documents.begin (), result.documents.end (),
    [&] (const auto& entry) { return duplicates.count (entry.object); }), result.documents.end ());
  for (auto& [path, entry]: result.cache) if (duplicates.count (entry.object)) entry.published= false;
  return result;
}
}
