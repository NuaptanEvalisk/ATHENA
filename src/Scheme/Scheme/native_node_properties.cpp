/******************************************************************************
* MODULE     : native_node_properties.cpp
* DESCRIPTION: Typed Scheme properties and owner-checked undoable node edits
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#include "native_interfaces.hpp"
#include "object.hpp"
#include "ATHENA/Data/document_node_model.hpp"
#include "buffer_actor.hpp"
#include "buffer_state.hpp"
#include "editor.hpp"
#include "scheme_execution_context.hpp"
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>
#include <type_traits>

namespace {
namespace node= athena::node;
namespace model= athena::document_node;
using property= node::property;

string native (const std::string& value) {
  return string (value.data (), static_cast<int> (value.size ()));
}
std::string text (object value) {
  if (!is_string (value)) throw std::invalid_argument ("Expected a UTF-8 string");
  const string s= as_string (value);
  return std::string (s.data (), N(s));
}
object success (object value) { return list_object (symbol_object ("ok"), value); }
object error (const std::string& message) {
  return list_object (symbol_object ("error"), object (native (message)));
}

// Explicit tags distinguish a reference from text and an integer from a real.
// Lists are bounded and checked for cycles before their members are interpreted.
class property_reader {
  std::size_t nodes= 0, bytes= 0;
  void enter (std::size_t depth) {
    if (depth > 256 || ++nodes > 1000000)
      throw std::invalid_argument ("Scheme properties exceed their structural budget");
  }
  std::string string_value (object value) {
    auto s= text (value);
    if (s.size () > 64 * 1024 * 1024 - bytes)
      throw std::invalid_argument ("Scheme properties exceed their text budget");
    bytes+= s.size ();
    return s;
  }
  std::vector<object> sequence (object value, std::size_t depth) {
    std::vector<object> result;
    std::set<scm_t_bits> seen;
    while (!is_null (value)) {
      enter (depth);
      const auto raw= object_to_tmscm (value);
      if (!scm_is_pair (raw) || !seen.insert (SCM_UNPACK (raw)).second)
        throw std::invalid_argument ("Expected an acyclic proper property list");
      result.push_back (car (value));
      value= cdr (value);
    }
    return result;
  }
  property value (object source, std::size_t depth) {
    enter (depth);
    auto pair= sequence (source, depth);
    if (pair.size () != 2 || !is_symbol (pair[0]))
      throw std::invalid_argument ("Expected (property-type value)");
    const string tag= as_symbol (pair[0]);
    object payload= pair[1];
    if (tag == "string") return property (string_value (payload));
    if (tag == "reference") return property (node::reference {string_value (payload)});
    if (tag == "boolean" && is_bool (payload)) return property (as_bool (payload));
    if (tag == "integer" && scm_is_signed_integer (object_to_tmscm (payload),
          std::numeric_limits<std::int64_t>::min (),
          std::numeric_limits<std::int64_t>::max ()))
      return property (scm_to_int64 (object_to_tmscm (payload)));
    if (tag == "real" && is_double (payload) &&
        scm_is_true (scm_inexact_p (object_to_tmscm (payload)))) {
      double number= as_double (payload);
      if (std::isfinite (number)) return property (number);
    }
    if (tag == "rich-text" && is_tree (payload))
      return property (node::rich_text {as_tree (payload)});
    if (tag == "list") {
      property::list result;
      for (auto item: sequence (payload, depth + 1))
        result.push_back (value (item, depth + 1));
      return property (std::move (result));
    }
    if (tag == "dictionary") return property (dictionary (payload, depth + 1));
    throw std::invalid_argument ("Unknown property type or incorrect payload");
  }
public:
  property::dictionary dictionary (object source, std::size_t depth= 0) {
    enter (depth);
    property::dictionary result;
    for (auto entry: sequence (source, depth)) {
      auto pair= sequence (entry, depth);
      if (pair.size () != 2)
        throw std::invalid_argument ("Expected (property-name typed-value)");
      auto key= string_value (pair[0]);
      if (key.empty () || result.count (key))
        throw std::invalid_argument ("Empty or duplicate property name");
      result.emplace (std::move (key), value (pair[1], depth + 1));
    }
    return result;
  }
  std::vector<std::string> removals (object source) {
    std::vector<std::string> result;
    for (auto entry: sequence (source, 0)) result.push_back (string_value (entry));
    return result;
  }
};

object encode_property (const property& source);
object encode_dictionary (const property::dictionary& properties) {
  array<object> result;
  for (const auto& entry: properties)
    result << list_object (object (native (entry.first)), encode_property (entry.second));
  return as_list_object (result);
}
object encode_property (const property& source) {
  return std::visit ([] (const auto& value) -> object {
    using T= std::decay_t<decltype (value)>;
    string tag;
    object result;
    if constexpr (std::is_same_v<T, std::string>) { tag= "string"; result= object (native (value)); }
    else if constexpr (std::is_same_v<T, bool>) { tag= "boolean"; result= object (value); }
    else if constexpr (std::is_same_v<T, std::int64_t>) {
      tag= "integer"; result= tmscm_to_object (scm_from_int64 (value));
    }
    else if constexpr (std::is_same_v<T, double>) { tag= "real"; result= object (value); }
    else if constexpr (std::is_same_v<T, node::reference>) {
      tag= "reference"; result= object (native (value.id));
    }
    else if constexpr (std::is_same_v<T, node::rich_text>) {
      tag= "rich-text"; result= object (copy (value.content));
    }
    else if constexpr (std::is_same_v<T, property::dictionary>) {
      tag= "dictionary"; result= encode_dictionary (value);
    }
    else {
      tag= "list";
      array<object> items;
      for (const auto& item: value) items << encode_property (item);
      result= as_list_object (items);
    }
    return list_object (symbol_object (tag), result);
  }, source.data);
}

// A detached tree is edited locally. Attached source requires an editor
// capability, the matching actor-owned root, and a writable buffer. Never use
// GUI registries or reconstruct source identities from a stale path.
object update (tree source, const model::property_edit& edit, bool return_id) {
  tree scope= source;
  model::source_path where;
  path ip= obtain_ip (source);
  if (ip_attached (ip)) {
    const auto* context= current_scheme_execution_context ();
    if (!context || !context->actor || !context->editor ||
        !context->has (SCHEME_CAPABILITY_BUFFER))
      return error ("Attached node properties require their BufferActor editor");
    if (context->actor->current_state ()->read_only)
      return error ("Document buffer is read-only");
    editor_rep* ed= context->editor;
    path root= ed->the_buffer_path ();
    path absolute= reverse (ip);
    if (!(root <= absolute)) return error ("Node belongs to another document");
    path relative= absolute / root;
    scope= ed->the_buffer ();
    if (!has_subtree (scope, relative) || !strong_equal (subtree (scope, relative), source))
      return error ("Node is no longer attached to this document");
    for (path p= relative; !is_nil (p); p= p->next) where.push_back (p->item);
  }
  auto prepared= model::prepare_property_edit (scope, where, edit);
  if (!prepared.ok ()) {
    const auto& diagnostic= prepared.diagnostics.front ();
    return error (diagnostic.property.empty () ? diagnostic.detail :
                  diagnostic.property + ": " + diagnostic.detail);
  }
  if (prepared.change) ::apply (scope, *prepared.change);
  return success (return_id ? object (native (prepared.id)) : object (source));
}
}

object tree_node_properties (tree source) {
  const auto* metadata= athena::node::get (source);
  return metadata ? encode_dictionary (metadata->properties) : null_object ();
}

object tree_update_node_properties (tree source, object replacements, object removals) {
  try {
    property_reader reader;
    model::property_edit edit;
    edit.set= reader.dictionary (replacements);
    edit.remove= reader.removals (removals);
    return update (source, edit, false);
  }
  catch (const std::exception& failure) { return error (failure.what ()); }
}

object tree_ensure_node_id (tree source) {
  try {
    model::property_edit edit;
    edit.ensure_id= true;
    return update (source, edit, true);
  }
  catch (const std::exception& failure) { return error (failure.what ()); }
}
