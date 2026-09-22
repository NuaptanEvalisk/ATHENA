/******************************************************************************
* MODULE     : interop_document_source.cpp
* DESCRIPTION: Preserve full document envelopes without export-time filtering
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#include "interop_document_source.hpp"
#include "convert.hpp"
#include "merge_sort.hpp"
#include "iterator.hpp"

std::string interop_document_source_error (const tree& source) {
  if (!is_document (source)) return "Missing native document root";
  int bodies= 0;
  for (int i= 0; i < N (source); ++i) {
    if (is_compound (source[i], "body")) {
      if (N (source[i]) != 1) return "The document body field requires one child";
      ++bodies;
    }
  }
  if (bodies > 1) return "Duplicate document body fields";
  // Legacy empty documents may omit the body; newly created ones include it.
  return {};
}

namespace {
int field (tree source, const char* name) {
  for (int i= 0; i < N (source); ++i)
    if (is_compound (source[i], name, 1)) return i;
  return -1;
}

void scalar_field (tree& source, const char* name, const tree& value, bool required) {
  int index= field (source, name);
  if (index >= 0) {
    if (source[index][0] != value) source[index][0]= copy (value);
  }
  else if (required) source << compound (name, copy (value));
}

void collection_field (tree& source, const char* name, hashmap<string,tree> values) {
  int index= field (source, name);
  if (index < 0) {
    if (N (values) != 0) source << compound (name, copy (make_collection (values)));
    return;
  }
  tree& target= source[index][0];
  if (!is_func (target, COLLECTION)) target= tree (COLLECTION);
  // Keep unchanged associations rather than replacing an entire environment
  // collection when only one preference or reference value has changed.
  hashmap<string,bool> seen (false);
  for (int i= 0; i < N (target);) {
    tree entry= target[i];
    if (!is_func (entry, ASSOCIATE, 2) || !is_atomic (entry[0]) ||
        !values->contains (entry[0]->label) || seen[entry[0]->label]) {
      remove (target, i, 1);
      continue;
    }
    const string key= entry[0]->label;
    seen (key)= true;
    if (entry[1] != values[key]) entry[1]= copy (values[key]);
    ++i;
  }
  iterator<string> keys= iterate (values);
  while (keys->busy ()) {
    string key= keys->next ();
    if (!seen[key]) target << tree (ASSOCIATE, copy (key), copy (values[key]));
  }
}
}

void refresh_interop_document_source (tree& source, const tree& body, new_data data) {
  if (!is_document (source)) source= tree (DOCUMENT);
  int body_index= field (source, "body");
  if (body_index < 0) source << compound ("body", body);
  else source[body_index][0]= body;
  scalar_field (source, "style", data->style, data->style != tree (TUPLE));
  collection_field (source, "initial", data->init);
  collection_field (source, "final", data->fin);
  collection_field (source, "attachments", data->att);
  collection_field (source, "references", data->ref);
  collection_field (source, "auxiliary", data->aux);
}

void append_interop_source_attributes (tree& snapshot, const tree& source) {
  if (!is_document (source)) return;
  for (int i= 0; i < N (source); ++i) {
    bool standard= false;
    for (const auto* name: {"TeXmacs", "style", "body", "initial", "final",
                            "attachments", "references", "auxiliary"})
      if (is_compound (source[i], name)) { standard= true; break; }
    if (!standard) snapshot << copy (source[i]);
  }
}
