/******************************************************************************
* MODULE     : clipboard_xml.cpp
* DESCRIPTION: Validate clipboard envelopes around the native XML tree codec
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#include "clipboard_xml.hpp"
#include "document_file_codec.hpp"
#include "node_metadata.hpp"

namespace athena::document {
namespace {
[[noreturn]] void invalid (const char* message) {
  throw codec_exception (codec_error::invalid_structure, message);
}

std::int64_t version_number (xml_format_version version) {
  switch (version) {
  case xml_format_version::v1: return 1;
  case xml_format_version::v2: return 2;
  case xml_format_version::v3: return 3;
  }
  throw codec_exception (codec_error::unsupported_version, "Unsupported clipboard XML version");
}

std::optional<xml_format_version> validate_selection (const tree& selection) {
  const bool ordinary= is_tuple (selection, "texmacs", 3);
  const bool move=
    is_tuple (selection, "texmacs", 4) &&
    is_tuple (selection[4], "athena-move-v1", 1) &&
    is_atomic (selection[4][1]) &&
    node::valid_id (std::string (
      as_charp (selection[4][1]->label), N(selection[4][1]->label)));
  if ((!ordinary && !move) ||
      !is_atomic (selection[2]) || !is_atomic (selection[3]))
    invalid ("Invalid ATHENA clipboard selection envelope");
  for (int i=0; i<N(selection); ++i)
    if (i != 1 && node::contains_metadata (selection[i]))
      invalid ("Clipboard descriptors cannot carry metadata");
  const auto* header= node::get (selection);
  if (!header) return {};
  if (!header->id.empty () || header->properties.size () != 1)
    invalid ("Clipboard header permits only the source version marker");
  const auto marker= header->properties.find (clipboard_source_version_key);
  if (marker == header->properties.end ())
    invalid ("Unknown clipboard header property");
  const auto* version= std::get_if<std::int64_t> (&marker->second.data);
  if (!version) invalid ("Clipboard source version must be a typed integer");
  switch (*version) {
  case 1: return xml_format_version::v1;
  case 2: return xml_format_version::v2;
  case 3: return xml_format_version::v3;
  default: invalid ("Invalid clipboard source version marker");
  }
}

bool native_v3_label (tree_label label) {
  switch (label) {
  case DELIMITED: case MATH_MIDDLE: case BIG_OPERATOR: case MATH_SCRIPTS:
  case MATH_ACCENT: case MATH_ANNOTATION: case MATH_LONG_ARROW: case MATH_GRID:
  case MATH_BLOCK: case MATH_VARIANT: case MATH_SYMBOL: return true;
  default: return false;
  }
}

// Generic v1/v2 codecs can spell native labels without preserving their math
// contract. Check semantic origin separately, including embedded rich values.
class payload_validator {
  bool legacy;
  codec_limits limits;
  std::size_t visited= 0;

  void enter (std::size_t depth) {
    if (depth > limits.depth || ++visited > limits.nodes)
      throw codec_exception (codec_error::resource_limit, "Clipboard payload traversal limit exceeded");
  }
  void property (const node::property& value, std::size_t depth) {
    enter (depth);
    if (const auto* rich= std::get_if<node::rich_text> (&value.data))
      inspect (rich->content, depth+1);
    else if (const auto* list= std::get_if<node::property::list> (&value.data))
      for (const auto& child: *list) property (child, depth+1);
    else if (const auto* dictionary= std::get_if<node::property::dictionary> (&value.data))
      for (const auto& entry: *dictionary) property (entry.second, depth+1);
  }
public:
  explicit payload_validator (xml_format_version version):
    legacy (version != xml_format_version::v3) { version_number (version); }

  void inspect (const tree& value, std::size_t depth= 0) {
    enter (depth);
    if (legacy && native_v3_label (L(value)))
      invalid ("Native v3 mathematics cannot have a legacy clipboard source or target");
    if (const auto* metadata= node::get (value)) {
      for (const auto& entry: metadata->properties) {
        if (entry.first == clipboard_source_version_key)
          invalid ("Clipboard origin markers cannot occur in source content");
        if (legacy && entry.first.compare (0, 5, "math:") == 0)
          invalid ("Native mathematical properties cannot have a legacy clipboard source or target");
        property (entry.second, depth+1);
      }
    }
    if (is_compound (value))
      for (int i=0; i<N(value); ++i) inspect (value[i], depth+1);
  }
};

xml_format_version source_version (const tree& selection) {
  const auto marker= validate_selection (selection);
  if (marker) return *marker;
  return node::contains_metadata (selection) ? xml_format_version::v2 : xml_format_version::v1;
}

std::string encode_lossless (const tree& selection, xml_format_version version) {
  payload_validator (version).inspect (selection);
  const auto bytes= encode_native_xml_bytes (selection, version, xml_kind::fragment);
  const auto restored= decode_native_xml_bytes (bytes, xml_kind::fragment);
  if (restored.version != version || restored.document != selection)
    invalid ("Clipboard XML roundtrip changed source content");
  return bytes;
}
}

std::optional<xml_format_version> clipboard_version (const tree& selection) {
  return validate_selection (selection);
}

tree clipboard_without_version (const tree& selection) {
  validate_selection (selection);
  tree result= copy (selection);
  node::clear (result);
  return result;
}

tree clipboard_with_version (const tree& selection, xml_format_version version) {
  const auto number= version_number (version);
  const auto previous= validate_selection (selection);
  if (previous && *previous != version)
    invalid ("Clipboard source version conflicts with its existing marker");
  tree result= copy (selection);
  node::metadata header;
  header.properties.emplace (clipboard_source_version_key, node::property (number));
  node::set (result, header);
  return result;
}

std::string write_clipboard_xml (const tree& selection) {
  return write_clipboard_xml (selection, source_version (selection));
}

std::string write_clipboard_xml (const tree& selection, xml_format_version version) {
  version_number (version);
  const auto marker= validate_selection (selection);
  if (marker && *marker != version)
    invalid ("Clipboard source version conflicts with the requested XML version");
  return encode_lossless (clipboard_without_version (selection), version);
}

tree read_clipboard_xml (std::string_view bytes) {
  auto decoded= decode_native_xml_bytes (bytes, xml_kind::fragment);
  if (validate_selection (decoded.document))
    invalid ("Clipboard origin marker is runtime-only and cannot appear in XML");
  payload_validator (decoded.version).inspect (decoded.document);
  return clipboard_with_version (decoded.document, decoded.version);
}

void validate_clipboard_paste (const tree& selection, xml_format_version target) {
  version_number (target);
  const auto origin= source_version (selection);
  const tree payload= clipboard_without_version (selection);
  encode_lossless (payload, origin);
  if (target != origin) encode_lossless (payload, target);
}
}
