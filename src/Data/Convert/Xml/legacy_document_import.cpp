/******************************************************************************
* MODULE     : legacy_document_import.cpp
* DESCRIPTION: Role-aware Cork tree import preserving symbols and source positions
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#include "legacy_document_import.hpp"
#include "legacy_document_reader.hpp"
#include "drd_std.hpp"
#include <algorithm>
#include <limits>

namespace athena::document {
namespace {
std::string bytes (const string& s) { return {as_charp (s), static_cast<std::size_t> (N (s))}; }
tree atom (const std::string& s) {
  if (s.size () > static_cast<std::size_t> (std::numeric_limits<int>::max ()))
    throw codec_exception (codec_error::resource_limit, "Imported atom too large");
  return tree (string (s.data (), static_cast<int> (s.size ())));
}
document_path child_path (document_path path, int index) { path.push_back (index); return path; }

legacy_text_role standard_role (const tree& parent, int i, legacy_text_role inherited) {
  // A code block or scalar container does not turn ordinary descendants into
  // presentation text. Explicit child types can further restrict a field.
  if (L (parent) < START_EXTENSIONS) {
    switch (standard_drd_for_thread ()->get_type_child (parent, i)) {
    case TYPE_CODE: return legacy_text_role::code;
    case TYPE_VARIABLE: case TYPE_ARGUMENT: case TYPE_BINDING:
    case TYPE_IDENTIFIER: case TYPE_URL:
    case TYPE_BOOLEAN: case TYPE_INTEGER: case TYPE_LENGTH:
    case TYPE_NUMERIC: case TYPE_COLOR: case TYPE_DURATION:
    case TYPE_FONT_SIZE: case TYPE_GRAPHICAL_ID:
      return legacy_text_role::identifier;
    case TYPE_STRING: return legacy_text_role::scalar;
    default: break;
    }
  }
  return inherited;
}

class importer {
  const legacy_cork_table& table;
  codec_limits limits;
  std::size_t position_limit, positions= 0;
  const legacy_slot_policy& policy;
  std::size_t nodes= 0, input_bytes= 0, output_bytes= 0;
  std::vector<legacy_node_mapping> mappings;

  void count (std::size_t n, std::size_t& used, std::size_t max) {
    if (used > max || n > max - used)
      throw codec_exception (codec_error::resource_limit, "Legacy document exceeds import budget");
    used += n;
  }
  tree text (const tree& source, const document_path& old, const document_path& dest,
             legacy_text_role role) {
    const auto raw= bytes (source->label);
    count (raw.size (), input_bytes, limits.input_bytes);
    auto pieces= table.decode (raw, role, limits.text_bytes - output_bytes,
                               position_limit - positions);
    count (std::max<std::size_t> (pieces.size (), 1), positions, position_limit);
    const bool structured= std::any_of (pieces.begin (), pieces.end (), [] (const auto& p) {
      return p.kind == legacy_piece_kind::named_symbol;
    });
    legacy_node_mapping map {old, dest, {}};
    tree result (CONCAT);
    std::string run;
    for (const auto& p: pieces) {
      count (p.value.size (), output_bytes, limits.text_bytes);
      document_path target= structured ? child_path (dest, N (result)) : dest;
      if (p.kind == legacy_piece_kind::named_symbol) {
        if (!run.empty ()) {
          result << atom (run); run.clear (); target= child_path (dest, N (result));
          count (1, nodes, limits.nodes);
        }
        result << compound ("named-symbol", atom (p.value));
        count (2, nodes, limits.nodes);
        map.spans.push_back ({p.begin, p.end, {target, 0}, {target, 1}});
      }
      else {
        auto begin= run.size ();
        run += p.value;
        map.spans.push_back ({p.begin, p.end, {target, begin}, {target, run.size ()}, p.byte_identity});
      }
    }
    if (!structured) result= atom (run);
    else if (!run.empty ()) { result << atom (run); count (1, nodes, limits.nodes); }
    // Empty text still has a relocatable position, including empty arguments.
    if (pieces.empty ()) map.spans.push_back ({0, 0, {dest, 0}, {dest, 0}});
    mappings.push_back (std::move (map));
    return result;
  }

  tree visit (const tree& source, const document_path& old, const document_path& dest,
              legacy_text_role role, std::size_t depth) {
    if (depth > limits.depth) throw codec_exception (codec_error::resource_limit, "Legacy tree too deep");
    count (1, nodes, limits.nodes);
    if (is_atomic (source)) {
      try { return text (source, old, dest, role); }
      catch (const legacy_text_error& error) { throw legacy_document_error (old, error); }
    }
    if (is_func (source, RAW_DATA)) {
      if (N (source) != 1 || !is_atomic (source[0]))
        throw codec_exception (codec_error::invalid_structure, "Invalid legacy binary payload");
      count (N (source[0]->label), input_bytes, limits.input_bytes);
      count (N (source[0]->label), output_bytes, limits.text_bytes);
      count (1, nodes, limits.nodes);
      mappings.push_back ({old, dest, {}});
      // Payload positions are byte identities, never text cursor offsets.
      mappings.push_back ({child_path (old, 0), child_path (dest, 0), {}});
      return copy (source);
    }
    std::string name;
    auto old_name= bytes (as_string (L (source)));
    count (old_name.size (), input_bytes, limits.input_bytes);
    for (const auto& p: table.decode (old_name, legacy_text_role::identifier,
                                     limits.text_bytes - output_bytes)) name += p.value;
    count (name.size (), output_bytes, limits.text_bytes);
    tree result (make_tree_label (atom (name)->label), 0);
    mappings.push_back ({old, dest, {}});
    for (int i= 0; i < N (source); ++i) {
      auto from= child_path (old, i);
      bool version= is_compound (source[i], "TeXmacs", 1) && is_atomic (source[i][0]);
      if ((is_func (source[i], APPLY, 2) || is_func (source[i], EXPAND, 2)) &&
          source[i][0] == "TeXmacs" && is_atomic (source[i][1])) version= true;
      if (depth == 0 && version) {
        count (2, nodes, limits.nodes);
        count (N (source[i][N (source[i]) - 1]->label), input_bytes, limits.input_bytes);
        mappings.push_back ({std::move (from), std::nullopt, {}});
        continue;
      }
      auto child_role= standard_role (source, i, role);
      if (policy) child_role= policy (source, i, child_role);
      result << visit (source[i], from, child_path (dest, N (result)), child_role, depth + 1);
    }
    return result;
  }
public:
  importer (const legacy_cork_table& t, legacy_import_limits l, const legacy_slot_policy& p):
    table (t), limits (l.codec), position_limit (l.positions), policy (p) {}
  legacy_document_result run (const tree& source) {
    if (!is_func (source, DOCUMENT))
      throw codec_exception (codec_error::invalid_structure, "Expected legacy document envelope");
    init_std_drd ();
    auto result= visit (source, {}, {}, legacy_text_role::content, 0);
    // Enforce the actual output shape/size, including symbol-induced nodes.
    write_xml (result, xml_kind::document, limits);
    return {std::move (result), std::move (mappings)};
  }
};
} // namespace

std::optional<document_position> legacy_document_result::relocate (
  const document_path& path, std::size_t byte, boundary_affinity affinity) const {
  const auto found= std::lower_bound (mappings.begin (), mappings.end (), path,
    [] (const auto& map, const auto& key) { return map.source < key; });
  if (found != mappings.end () && found->source == path && found->destination) {
    const auto& map= *found;
    std::optional<document_position> before, after;
    for (const auto& span: map.spans) {
      if (span.byte_identity && byte > span.begin && byte < span.end)
        return document_position {span.first.node, span.first.offset + byte - span.begin};
      if (span.end == byte) before= span.last;
      if (span.begin == byte) after= span.first;
    }
    if (affinity == boundary_affinity::preceding) return before ? before : after;
    return after ? after : before;
  }
  return std::nullopt;
}

std::optional<document_path> legacy_document_result::relocate_node (const document_path& path) const {
  const auto found= std::lower_bound (mappings.begin (), mappings.end (), path,
    [] (const auto& map, const auto& key) { return map.source < key; });
  if (found == mappings.end () || found->source != path) return std::nullopt;
  return found->destination;
}

legacy_document_result import_legacy_document (
  const tree& source, const legacy_cork_table& table, legacy_import_limits limits,
  const legacy_slot_policy& policy) {
  return importer (table, limits, policy).run (source);
}

legacy_document_result import_legacy_document_bytes (
  std::string_view input, const legacy_cork_table& table, legacy_import_limits limits,
  const legacy_slot_policy& policy) {
  tree source;
  if (input.substr (0, 9) == "<TeXmacs|") source= read_legacy_markup (input, limits.codec);
  else source= read_legacy_scheme (input, limits.codec);
  return import_legacy_document (source, table, limits, policy);
}
} // namespace athena::document
