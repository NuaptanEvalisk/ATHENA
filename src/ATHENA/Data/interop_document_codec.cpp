/******************************************************************************
* MODULE     : interop_document_codec.cpp
* DESCRIPTION: Bounded JSON/MessagePack conversion of native document nodes
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#include "interop_document_codec.hpp"
#include "node_metadata.hpp"
#include "tree.hpp"
#include <QStringConverter>
#include <charconv>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <set>
#include <stdexcept>

namespace athena::interop {
namespace {
struct budget {
  document_codec_limits limits;
  std::size_t nodes= 0, bytes= 0;
  void node (std::size_t depth) {
    if (depth > limits.depth || nodes >= limits.nodes)
      throw std::length_error ("Document node/depth limit exceeded");
    ++nodes;
  }
  void text (std::size_t size) {
    if (size > limits.bytes - bytes || size > std::size_t (std::numeric_limits<int>::max ()))
      throw std::length_error ("Document text limit exceeded");
    bytes+= size;
  }
};

bool valid_utf8 (const std::string& input) {
  QStringDecoder decoder (QStringDecoder::Utf8, QStringConverter::Flag::Stateless);
  (void) QString (decoder (QByteArrayView (input.data (), qsizetype (input.size ()))));
  return !decoder.hasError ();
}

string native_bytes (const std::string& input) {
  return string (input.data (), int (input.size ()));
}

void encode_text (value& result, const char* key, const string& input, budget& count) {
  count.text (N (input));
  std::string utf8 (input.data (), N (input));
  if (!valid_utf8 (utf8)) throw std::invalid_argument ("Document text is not valid UTF8");
  result[key]= std::move (utf8);
}

string decode_text (const value& input, budget& count) {
  if (!input.is_string ()) throw std::invalid_argument ("Document text must be a UTF8 string");
  const auto& utf8= input.get_ref<const std::string&> ();
  count.text (utf8.size ());
  if (!valid_utf8 (utf8)) throw std::invalid_argument ("Document text is not valid UTF8");
  return native_bytes (utf8);
}

value encode_raw (const string& input, budget& count) {
  count.text (N(input));
  return value {{"raw", value::binary (
    std::vector<std::uint8_t> (input.data (), input.data () + N(input)))}};
}

string decode_raw (const value& input, budget& count) {
  if (!input.is_object () || input.size () != 1 || !input.contains ("raw") ||
      !input.at ("raw").is_binary ())
    throw std::invalid_argument ("RAW_DATA payload requires MessagePack BIN");
  const auto& bytes= input.at ("raw").get_binary ();
  count.text (bytes.size ());
  return string (reinterpret_cast<const char*> (bytes.data ()), int (bytes.size ()));
}

value encode (const tree& node, budget& count, std::size_t depth, bool raw= false) {
  count.node (depth);
  const auto* metadata= athena::node::get (node);
  if (metadata && !metadata->empty ())
    throw std::invalid_argument ("Document model v2 cannot preserve node metadata");
  value result= value::object ();
  if (is_atomic (node)) {
    if (raw) return encode_raw (node->label, count);
    encode_text (result, "text", node->label, count);
  }
  else if (is_compound (node)) {
    if (raw) throw std::invalid_argument ("RAW_DATA payload must be atomic bytes");
    encode_text (result, "tag", as_string (L (node)), count);
    if (std::size_t (N (node)) > count.limits.nodes - count.nodes)
      throw std::length_error ("Document node limit exceeded");
    const bool raw_children= L(node) == RAW_DATA;
    if (raw_children && N(node) != 1)
      throw std::invalid_argument ("RAW_DATA requires exactly one byte payload child");
    value children= value::array ();
    auto& array= children.get_ref<value::array_t&> ();
    array.reserve (N (node));
    for (int i= 0; i < N (node); ++i)
      array.push_back (encode (node[i], count, depth + 1, raw_children));
    result["children"]= std::move (children);
  }
  else throw std::invalid_argument ("Opaque native objects are not document nodes");
  return result;
}

tree decode (const value& node, budget& count, std::size_t depth, bool raw= false) {
  count.node (depth);
  if (raw) return tree (decode_raw (node, count));
  if (!node.is_object ()) throw std::invalid_argument ("Document node must be an object");
  if (node.size () == 1 && node.contains ("text"))
    return tree (decode_text (node.at ("text"), count));
  if (node.size () != 2 || !node.contains ("children") || !node.contains ("tag"))
    throw std::invalid_argument ("Expected UTF8 text or tag and children");
  const auto& children= node.at ("children");
  if (!children.is_array ()) throw std::invalid_argument ("Node children must be an array");
  if (children.size () > count.limits.nodes - count.nodes ||
      children.size () > std::size_t (std::numeric_limits<int>::max ()))
    throw std::length_error ("Document node limit exceeded");
  string tag= decode_text (node.at ("tag"), count);
  if (N (tag) == 0) throw std::invalid_argument ("Document tag must not be empty");
  tree_label label= make_tree_label (tag);
  if (label <= TMSTRING)
    throw std::invalid_argument ("Atomic/opaque tag is not a compound node");
  tree result (label, int (children.size ()));
  const bool raw_children= label == RAW_DATA;
  if (raw_children && children.size () != 1)
    throw std::invalid_argument ("RAW_DATA requires exactly one byte payload child");
  for (std::size_t i= 0; i < children.size (); ++i)
    result[int (i)]= decode (children[i], count, depth + 1, raw_children);
  return result;
}

// V3 uses the XML v2 property type names. Every value is {"type": TYPE,
// "value": PAYLOAD}: string -> UTF8, boolean -> bool, int64 -> canonical signed
// decimal string, double -> finite round-trip decimal string, reference -> UUID,
// rich_tree -> one v3 node, list -> array of typed values, dictionary -> array
// of {"name": UTF8, "value": TYPED_VALUE}. Node properties use that same named
// entry array. Entry arrays preserve duplicate names for validation, unlike
// JSON objects. Writers sort names via the native map and omit empty metadata.
// Numeric strings preserve int64 precision, type and negative zero in JSON and
// MessagePack without relying on a peer's number representation. Doubles are
// written using the shortest round-trip decimal, independent of locale.
// Tree nodes and typed values each consume one node/depth level. Named-entry
// and metadata containers add no level. Keys, IDs, references and scalar values
// consume semantic UTF8/decoded bytes; booleans consume their XML spelling size.
// Node UUIDs are unique across the entire payload, including rich trees.
// Reference UUIDs may repeat and need not resolve within this payload.
template<typename T> std::string number_text (T number) {
  char buffer[128];
  auto result= std::to_chars (buffer, buffer + sizeof (buffer), number);
  if (result.ec != std::errc ())
    throw std::invalid_argument ("Invalid numeric property");
  return {buffer, result.ptr};
}

void shape (const value& input, std::initializer_list<const char*> required,
            std::initializer_list<const char*> optional= {}) {
  if (!input.is_object ()) throw std::invalid_argument ("Expected document object");
  if (input.size () < required.size () || input.size () > required.size () + optional.size ())
    throw std::invalid_argument ("Unexpected document object members");
  for (const auto* key: required)
    if (!input.contains (key)) throw std::invalid_argument ("Missing document object member");
  for (auto it= input.begin (); it != input.end (); ++it) {
    bool allowed= false;
    for (const auto* key: required) if (it.key () == key) allowed= true;
    for (const auto* key: optional) if (it.key () == key) allowed= true;
    if (!allowed) throw std::invalid_argument ("Unexpected document object member");
  }
}

class v3_codec {
  budget count;
  std::set<std::string> identities;

  const std::string& text (const std::string& input) {
    count.text (input.size ());
    if (!valid_utf8 (input)) throw std::invalid_argument ("Document text is not valid UTF8");
    return input;
  }
  const std::string& text (const value& input) {
    if (!input.is_string ()) throw std::invalid_argument ("Expected UTF8 string");
    return text (input.get_ref<const std::string&> ());
  }
  const std::string& identity (const std::string& input, bool unique) {
    text (input);
    if (!athena::node::valid_id (input))
      throw std::invalid_argument ("Node identity must be a canonical UUID");
    if (unique && !identities.insert (input).second)
      throw std::invalid_argument ("Duplicate node UUID");
    return input;
  }
  void collection (std::size_t size) {
    if (size > count.limits.nodes - count.nodes ||
        size > std::size_t (std::numeric_limits<int>::max ()))
      throw std::length_error ("Document node limit exceeded");
  }
  void collection (const value& input) {
    if (!input.is_array ()) throw std::invalid_argument ("Expected document array");
    collection (input.size ());
  }
  value encode_properties (const athena::node::property::dictionary& properties,
                           std::size_t depth) {
    collection (properties.size ());
    value result= value::array ();
    auto& entries= result.get_ref<value::array_t&> ();
    entries.reserve (properties.size ());
    for (const auto& entry: properties) {
      if (entry.first.empty ()) throw std::invalid_argument ("Empty property name");
      text (entry.first);
      entries.push_back (value {{"name", entry.first},
                                {"value", encode_property (entry.second, depth)}});
    }
    return result;
  }
  value encode_property (const athena::node::property& property, std::size_t depth) {
    count.node (depth);
    const auto& data= property.data;
    if (const auto* s= std::get_if<std::string> (&data))
      return {{"type", "string"}, {"value", text (*s)}};
    if (const auto* b= std::get_if<bool> (&data)) {
      count.text (*b ? 4 : 5);
      return {{"type", "boolean"}, {"value", *b}};
    }
    if (const auto* i= std::get_if<std::int64_t> (&data))
      return {{"type", "int64"}, {"value", text (number_text (*i))}};
    if (const auto* d= std::get_if<double> (&data)) {
      if (!std::isfinite (*d)) throw std::invalid_argument ("Nonfinite numeric property");
      return {{"type", "double"}, {"value", text (number_text (*d))}};
    }
    if (const auto* list= std::get_if<athena::node::property::list> (&data)) {
      collection (list->size ());
      value items= value::array ();
      auto& array= items.get_ref<value::array_t&> ();
      array.reserve (list->size ());
      for (const auto& item: *list) array.push_back (encode_property (item, depth + 1));
      return {{"type", "list"}, {"value", std::move (items)}};
    }
    if (const auto* dict= std::get_if<athena::node::property::dictionary> (&data))
      return {{"type", "dictionary"}, {"value", encode_properties (*dict, depth + 1)}};
    if (const auto* ref= std::get_if<athena::node::reference> (&data))
      return {{"type", "reference"}, {"value", identity (ref->id, false)}};
    if (const auto* rich= std::get_if<athena::node::rich_text> (&data))
      return {{"type", "rich_tree"}, {"value", encode_node (rich->content, depth + 1, false, true)}};
    throw std::invalid_argument ("Invalid property variant");
  }
  void encode_metadata (value& result, const tree& node, std::size_t depth) {
    const auto* metadata= athena::node::get (node);
    if (!metadata || metadata->empty ()) return;
    if (!metadata->id.empty ()) result["id"]= identity (metadata->id, true);
    if (!metadata->properties.empty ())
      result["properties"]= encode_properties (metadata->properties, depth + 1);
  }
  athena::node::property::dictionary decode_properties (const value& input,
                                                        std::size_t depth) {
    collection (input);
    athena::node::property::dictionary result;
    for (const auto& entry: input) {
      shape (entry, {"name", "value"});
      const auto& name= text (entry.at ("name"));
      if (name.empty ()) throw std::invalid_argument ("Empty property name");
      if (result.find (name) != result.end ())
        throw std::invalid_argument ("Duplicate property name");
      result.emplace (name, decode_property (entry.at ("value"), depth));
    }
    return result;
  }
  athena::node::property decode_property (const value& input, std::size_t depth) {
    using property= athena::node::property;
    count.node (depth);
    shape (input, {"type", "value"});
    if (!input.at ("type").is_string ()) throw std::invalid_argument ("Invalid property type");
    const auto& type= input.at ("type").get_ref<const std::string&> ();
    const auto& data= input.at ("value");
    if (type == "string") return property (text (data));
    if (type == "boolean") {
      if (!data.is_boolean ()) throw std::invalid_argument ("Expected boolean property");
      bool result= data.get<bool> ();
      count.text (result ? 4 : 5);
      return property (result);
    }
    if (type == "int64") {
      const auto& number= text (data);
      std::int64_t result;
      auto parsed= std::from_chars (number.data (), number.data () + number.size (), result);
      if (parsed.ec != std::errc () || parsed.ptr != number.data () + number.size () ||
          number_text (result) != number)
        throw std::invalid_argument ("Invalid int64 property");
      return property (result);
    }
    if (type == "double") {
      const auto& number= text (data);
      double result;
      auto parsed= std::from_chars (number.data (), number.data () + number.size (), result);
      if (parsed.ec != std::errc () || parsed.ptr != number.data () + number.size () ||
          !std::isfinite (result))
        throw std::invalid_argument ("Invalid double property");
      return property (result);
    }
    if (type == "list") {
      collection (data);
      property::list result;
      result.reserve (data.size ());
      for (const auto& item: data) result.push_back (decode_property (item, depth + 1));
      return property (std::move (result));
    }
    if (type == "dictionary") return property (decode_properties (data, depth + 1));
    if (type == "reference") {
      if (!data.is_string ()) throw std::invalid_argument ("Expected reference UUID");
      return property (athena::node::reference {
        identity (data.get_ref<const std::string&> (), false)});
    }
    if (type == "rich_tree")
      return property (athena::node::rich_text {decode_node (data, depth + 1, false, true)});
    throw std::invalid_argument ("Unknown property type");
  }
  athena::node::metadata decode_metadata (const value& input, std::size_t depth) {
    athena::node::metadata result;
    if (input.contains ("id")) {
      if (!input.at ("id").is_string ()) throw std::invalid_argument ("Expected node UUID");
      result.id= identity (input.at ("id").get_ref<const std::string&> (), true);
    }
    if (input.contains ("properties"))
      result.properties= decode_properties (input.at ("properties"), depth + 1);
    return result;
  }
  tree decode_bytes (const value& input) {
    const auto& data= input.at ("raw");
    if (!data.is_binary ())
      throw std::invalid_argument ("RAW_DATA payload requires MessagePack BIN");
    const auto& bytes= data.get_binary ();
    count.text (bytes.size ());
    return tree (string (reinterpret_cast<const char*> (bytes.data ()), int (bytes.size ())));
  }
public:
  explicit v3_codec (document_codec_limits limits): count {limits} {}

  value encode_node (const tree& node, std::size_t depth= 0, bool raw= false,
                     bool rich= false) {
    count.node (depth);
    if (rich && (L(node) == RAW_DATA || L(node) == UNINIT))
      throw std::invalid_argument ("Rich text must not contain opaque data");
    value result= value::object ();
    if (is_atomic (node)) {
      if (raw) result= encode_raw (node->label, count);
      else encode_text (result, "text", node->label, count);
      encode_metadata (result, node, depth);
    }
    else if (is_compound (node)) {
      if (raw) throw std::invalid_argument ("RAW_DATA payload must be atomic bytes");
      string tag= as_string (L(node));
      if (N(tag) == 0 || !existing_tree_label (tag) || as_tree_label (tag) != L(node))
        throw std::invalid_argument ("Unregistered document tag");
      encode_text (result, "tag", tag, count);
      encode_metadata (result, node, depth);
      collection (std::size_t (N(node)));
      const bool raw_children= L(node) == RAW_DATA;
      if (raw_children && N(node) != 1)
        throw std::invalid_argument ("RAW_DATA requires exactly one byte payload child");
      value children= value::array ();
      auto& array= children.get_ref<value::array_t&> ();
      array.reserve (N(node));
      for (int i= 0; i < N(node); ++i)
        array.push_back (encode_node (node[i], depth + 1, raw_children, rich));
      result["children"]= std::move (children);
    }
    else throw std::invalid_argument ("Opaque native objects are not document nodes");
    return result;
  }

  tree decode_node (const value& input, std::size_t depth= 0, bool raw= false,
                    bool rich= false) {
    count.node (depth);
    tree result;
    if (raw) {
      shape (input, {"raw"}, {"id", "properties"});
      result= decode_bytes (input);
      auto metadata= decode_metadata (input, depth);
      if (!metadata.empty ()) athena::node::set (result, metadata);
      return result;
    }
    if (input.is_object () && input.contains ("text")) {
      shape (input, {"text"}, {"id", "properties"});
      result= tree (decode_text (input.at ("text"), count));
      auto metadata= decode_metadata (input, depth);
      if (!metadata.empty ()) athena::node::set (result, metadata);
      return result;
    }
    shape (input, {"tag", "children"}, {"id", "properties"});
    string tag= decode_text (input.at ("tag"), count);
    if (N(tag) == 0) throw std::invalid_argument ("Document tag must not be empty");
    tree_label label= make_tree_label (tag);
    if (label <= TMSTRING) throw std::invalid_argument ("Invalid compound tag");
    if (rich && (label == RAW_DATA || label == UNINIT))
      throw std::invalid_argument ("Rich text must not contain opaque data");
    auto metadata= decode_metadata (input, depth);
    const auto& children= input.at ("children");
    collection (children);
    const bool raw_children= label == RAW_DATA;
    if (raw_children && children.size () != 1)
      throw std::invalid_argument ("RAW_DATA requires exactly one byte payload child");
    result= tree (label, int (children.size ()));
    for (std::size_t i= 0; i < children.size (); ++i)
      result[int (i)]= decode_node (children[i], depth + 1, raw_children, rich);
    if (!metadata.empty ()) athena::node::set (result, metadata);
    return result;
  }
};
} // namespace

value document_node_to_value (const tree& node, document_codec_limits limits) {
  budget count {limits};
  return encode (node, count, 0);
}

tree document_node_from_value (const value& node, document_codec_limits limits) {
  budget count {limits};
  return decode (node, count, 0);
}

value document_node_to_value_v3 (const tree& node, document_codec_limits limits) {
  return v3_codec (limits).encode_node (node);
}

tree document_node_from_value_v3 (const value& node, document_codec_limits limits) {
  return v3_codec (limits).decode_node (node);
}
} // namespace athena::interop
