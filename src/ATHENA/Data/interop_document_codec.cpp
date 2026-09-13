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
#include "converter.hpp"
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

void encode_text (value& result, const char* key, const char* raw_key,
                   const string& input, budget& count) {
  count.text (N (input));
  string converted= cork_to_utf8 (input);
  std::string utf8 (converted.data (), N (converted));
  if (valid_utf8 (utf8) && utf8_to_cork (converted) == input) {
    // Count expansion as well; Cork tokens and UTF8 have different byte widths.
    if (utf8.size () > std::size_t (N (input)))
      count.text (utf8.size () - N (input));
    result[key]= std::move (utf8);
  }
  else result[raw_key]= value::binary (
    std::vector<std::uint8_t> (input.data (), input.data () + N (input)));
}

string decode_text (const value& input, bool raw, budget& count) {
  if (raw) {
    if (!input.is_binary ()) throw std::invalid_argument ("Cork text requires MessagePack BIN");
    const auto& bytes= input.get_binary ();
    count.text (bytes.size ());
    return string (reinterpret_cast<const char*> (bytes.data ()), int (bytes.size ()));
  }
  if (!input.is_string ()) throw std::invalid_argument ("Document text must be a UTF8 string");
  const auto& utf8= input.get_ref<const std::string&> ();
  count.text (utf8.size ());
  if (!valid_utf8 (utf8)) throw std::invalid_argument ("Document text is not valid UTF8");
  string native= utf8_to_cork (native_bytes (utf8));
  if (std::size_t (N (native)) > utf8.size ()) count.text (N (native) - utf8.size ());
  return native;
}

value encode (const tree& node, budget& count, std::size_t depth) {
  count.node (depth);
  value result= value::object ();
  if (is_atomic (node)) encode_text (result, "text", "cork", node->label, count);
  else if (is_compound (node)) {
    encode_text (result, "tag", "tag_cork", as_string (L (node)), count);
    if (std::size_t (N (node)) > count.limits.nodes - count.nodes)
      throw std::length_error ("Document node limit exceeded");
    value children= value::array ();
    auto& array= children.get_ref<value::array_t&> ();
    array.reserve (N (node));
    for (int i= 0; i < N (node); ++i) array.push_back (encode (node[i], count, depth + 1));
    result["children"]= std::move (children);
  }
  else throw std::invalid_argument ("Opaque native objects are not document nodes");
  return result;
}

tree decode (const value& node, budget& count, std::size_t depth) {
  count.node (depth);
  if (!node.is_object ()) throw std::invalid_argument ("Document node must be an object");
  if (node.size () == 1 && node.contains ("text"))
    return tree (decode_text (node.at ("text"), false, count));
  if (node.size () == 1 && node.contains ("cork"))
    return tree (decode_text (node.at ("cork"), true, count));
  if (node.size () != 2 || !node.contains ("children") ||
      !(node.contains ("tag") != node.contains ("tag_cork")))
    throw std::invalid_argument ("Expected text, cork, or tag and children");
  const auto& children= node.at ("children");
  if (!children.is_array ()) throw std::invalid_argument ("Node children must be an array");
  if (children.size () > count.limits.nodes - count.nodes ||
      children.size () > std::size_t (std::numeric_limits<int>::max ()))
    throw std::length_error ("Document node limit exceeded");
  const bool raw= node.contains ("tag_cork");
  string tag= decode_text (node.at (raw ? "tag_cork" : "tag"), raw, count);
  if (N (tag) == 0) throw std::invalid_argument ("Document tag must not be empty");
  tree_label label= make_tree_label (tag);
  if (label <= TMSTRING)
    throw std::invalid_argument ("Atomic/opaque tag is not a compound node");
  tree result (label, int (children.size ()));
  for (std::size_t i= 0; i < children.size (); ++i)
    result[int (i)]= decode (children[i], count, depth + 1);
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
