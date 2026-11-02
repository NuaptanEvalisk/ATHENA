// Copyright (C) 2006 Joris van der Hoeven, 2026 ATHENA contributors.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "link_markup.hpp"
#include "link.hpp"
#include "new_buffer.hpp"
#include "convert.hpp"
#include "analyze.hpp"
#include "file.hpp"
#include "ATHENA/Data/application_lifecycle.hpp"
#include <QSaveFile>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <map>
#include <mutex>
#include <set>
#include <stdexcept>
#include <vector>

namespace {
using names= std::vector<std::string>;
std::string bytes (string s) { return {s.data (), std::size_t (N(s))}; }
string native (const std::string& s) { return string (s.data (), s.size ()); }

bool add_unique (names& values, const std::string& value) {
  if (std::find (values.begin (), values.end (), value) != values.end ()) return false;
  values.push_back (value);
  return true;
}

struct registry {
  std::mutex mutex;
  bool loaded= false;
  bool dirty= false;
  std::map<std::string, names> files;
  std::map<std::string, std::string> ids;
  std::map<std::string, names> locators;

  void load () {
    if (loaded) return;
    const url json_file ("$ATHENA_HOME_PATH/system/link-locations.json");
    const url legacy_file ("$ATHENA_HOME_PATH/system/registry.scm");
    string text;
    if (exists (json_file)) {
      if (load_string (json_file, text, false))
        throw std::runtime_error ("Cannot read link location registry");
      const auto data= nlohmann::json::parse (bytes (text));
      if (data.at ("version") != 1)
        throw std::runtime_error ("Unsupported link location registry version");
      files= data.at ("files").get<std::map<std::string, names>> ();
    }
    else if (exists (legacy_file)) {
      if (load_string (legacy_file, text, false))
        throw std::runtime_error ("Cannot read legacy link location data");
      const tree data= string_to_scheme_tree (text);
      if (!is_tuple (data)) throw std::runtime_error ("Invalid legacy link location data");
      for (int i= 0; i < N(data); ++i) {
        const tree row= data[i];
        if (!is_tuple (row) || N(row) == 0)
          throw std::runtime_error ("Invalid legacy link location entry");
        names values;
        for (int j= 0; j < N(row); ++j)
          if (!is_atomic (row[j]) || !is_quoted (row[j]->label))
            throw std::runtime_error ("Invalid legacy link location string");
        for (int j= 1; j < N(row); ++j) values.push_back (bytes (scm_unquote (row[j]->label)));
        files[bytes (scm_unquote (row[0]->label))]= std::move (values);
      }
      dirty= true;
    }
    for (const auto& entry: files)
      for (const auto& file: entry.second) ids[file]= entry.first;
    loaded= true;
    save ();
  }

  void save () {
    if (!dirty) return;
    const url destination ("$ATHENA_HOME_PATH/system/link-locations.json");
    mkdir (head (destination));
    const string path= concretize (destination);
    QSaveFile output (QString::fromUtf8 (path.data (), N(path)));
    const std::string text= nlohmann::json {{"version", 1}, {"files", files}}.dump (2) + "\n";
    if (!output.open (QIODevice::WriteOnly) ||
        output.write (text.data (), qint64 (text.size ())) != qint64 (text.size ()) ||
        !output.commit ())
      throw std::runtime_error ("Cannot save link location registry: " +
                                output.errorString ().toStdString ());
    dirty= false;
  }

  void add (const std::string& id, const std::string& file) {
    if (add_unique (files[id], file)) dirty= true;
    ids[file]= id;
  }

  std::string file_id (const std::string& file) {
    auto found= ids.find (file);
    if (found != ids.end ()) return found->second;
    const std::string id= bytes (native_link_unique_id ());
    add (id, file);
    return id;
  }

  void locate (const std::string& id, names& result, std::set<std::string>& visited) const {
    if (!visited.insert (id).second) return;
    auto known= files.find (id);
    if (known != files.end ()) {
      for (const auto& file: known->second) add_unique (result, file);
      return;
    }
    auto refs= locators.find (id);
    if (refs != locators.end ())
      for (const auto& ref: refs->second) locate (ref, result, visited);
  }
};

registry locations;

names locate (const std::string& id) {
  names result;
  std::set<std::string> visited;
  locations.locate (id, result, visited);
  return result;
}
}

list<string> native_link_constellation () {
  std::lock_guard<std::mutex> lock (locations.mutex);
  locations.load ();
  list<string> result;
  for (const auto& entry: locations.ids) result= list<string> (native (entry.first), result);
  return reverse (result);
}

void native_register_link_locations (url here, tree collection) {
  std::lock_guard<std::mutex> lock (locations.mutex);
  locations.load ();
  for (int i= 0; i < N(collection); ++i) {
    const tree entry= collection[i];
    if (is_func (entry, ID, 1))
      locations.add (bytes (entry[0]->label), bytes (as_unix_string (here)));
    else if (is_compound (entry, "target", 2)) {
      const url target= relative (here, url_unix (entry[1]->label));
      if (exists (target)) locations.add (bytes (entry[0]->label), bytes (as_unix_string (target)));
    }
    else if (is_compound (entry, "locator", 2) && is_func (entry[1], ID, 1))
      add_unique (locations.locators[bytes (entry[0]->label)], bytes (entry[1][0]->label));
  }
  locations.save ();
}

tree native_get_link_locations (url here, tree t) {
  const array<tree> links= native_complete_links (t);
  std::map<std::string, names> live;
  std::set<std::string> requested;
  for (int i= 0; i < N(links); ++i)
    for (int j= 2; j < N(links[i]); ++j)
      if (is_func (links[i][j], ID, 1)) requested.insert (bytes (links[i][j][0]->label));
  for (const auto& id: requested)
    for (list<tree> nodes= get_trees (native (id)); !is_nil (nodes); nodes= nodes->next) {
      const path ip= obtain_ip (nodes->item);
      if (ip_attached (ip)) {
        const url owner= path_to_buffer (reverse (ip));
        if (!is_none (owner)) add_unique (live[id], bytes (as_unix_string (owner)));
      }
    }

  std::lock_guard<std::mutex> lock (locations.mutex);
  locations.load ();
  tree result (COLLECTION), targets (COLLECTION), refs (COLLECTION);
  std::set<std::string> emitted;
  for (const auto& id: requested) {
    const auto found= live.find (id);
    const names files= found == live.end () ? locate (id) : found->second;
    for (const auto& file: files) {
      const url destination= url_unix (native (file));
      if (destination == here) continue;
      const std::string file_id= locations.file_id (file);
      if (emitted.insert (file_id).second)
        targets << compound ("target", native (file_id), as_unix_string (delta (here, destination)));
      refs << compound ("locator", native (id), tree (ID, native (file_id)));
    }
  }
  if (N(refs) != 0) {
    result << tree (ID, native (locations.file_id (bytes (as_unix_string (here)))));
    for (int i= 0; i < N(targets); ++i) result << targets[i];
    for (int i= 0; i < N(refs); ++i) result << refs[i];
  }
  locations.save ();
  return result;
}

bool native_resolve_link_id (string id, std::set<std::string>& attempted,
                             std::function<void(url)> completed) {
  string target;
  {
    std::lock_guard<std::mutex> lock (locations.mutex);
    locations.load ();
    for (const auto& candidate: locate (bytes (id))) {
      if (attempted.count (candidate) || !exists (url_unix (native (candidate)))) continue;
      attempted.insert (candidate);
      target= native (candidate);
      break;
    }
  }
  if (target == "") return false;
  // The file-opening frontend retains its actor-to-UI dispatch and prompts.
  application_load_document (url_unix (target), application_document_load_options {}, std::move (completed));
  return true;
}
