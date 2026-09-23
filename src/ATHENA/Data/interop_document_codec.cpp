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
#include "tree.hpp"
#include <QStringConverter>
#include <limits>
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
} // namespace

value document_node_to_value (const tree& node, document_codec_limits limits) {
  budget count {limits};
  return encode (node, count, 0);
}

tree document_node_from_value (const value& node, document_codec_limits limits) {
  budget count {limits};
  return decode (node, count, 0);
}
} // namespace athena::interop
