/******************************************************************************
* MODULE     : math_tree_v3.cpp
* DESCRIPTION: Validated native math constructors, independent of XML codecs
* COPYRIGHT  : (C) 2026 ATHENA contributors
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
******************************************************************************/
#include "math_tree_v3.hpp"
#include "node_metadata.hpp"
#include "named_symbol.hpp"
#include "unicode_text.hpp"
#include <charconv>
#include <stdexcept>
#include <string>
#include <string_view>

namespace athena::math_v3 {
namespace {

bool one_of (const string& value, const char* options) {
  return N (value) == 1 &&
    std::string_view (options).find (value[0]) != std::string_view::npos;
}

bool atomic (const tree& value) { return is_atomic (value); }

} // namespace

bool glyph (const string& value, bool allow_empty) {
  if (N (value) == 0) return allow_empty;
  const std::string_view s (value.data (), std::size_t (N (value)));
  if (!text::valid_utf8 (s)) return false;
  // A delimiter denotes one Unicode glyph, not a historical <name> token.
  return text::next_scalar (s, 0) == s.size ();
}

bool size_level (const string& value) {
  if (N (value) == 0) return false;
  int result= 0;
  auto start= value.data (), end= start + N (value);
  const auto parsed= std::from_chars (start, end, result);
  return parsed.ec == std::errc () && parsed.ptr == end &&
    result >= 0 && result <= 1024 &&
    (result == 0 ? value == "0" : value[0] != '0');
}

bool valid_delimited (const tree& value) {
  if (!is_func (value, DELIMITED, 6)) return false;
  for (int i= 0; i < 5; ++i)
    // These five scalar descriptors are encoded as attributes of <d>.
    // Any nested UUID/property would be silently discarded by that encoding.
    if (!atomic (value[i]) || athena::node::get (value[i])) return false;
  if (!glyph (value[0]->label, true) || !glyph (value[1]->label, true) ||
      (value[0]->label == "" && value[1]->label == "") ||
      !one_of (value[2]->label, "afm"))
    return false;
  return value[2]->label == "m" ?
    size_level (value[3]->label) && size_level (value[4]->label) :
    value[3]->label == "" && value[4]->label == "";
}

tree delimited (const string& left, const string& right, char sizing,
                const tree& body, const string& left_size,
                const string& right_size) {
  const char option[]= {sizing, 0};
  tree result (DELIMITED, tree (left), tree (right), tree (option),
               tree (left_size), tree (right_size), body);
  if (!valid_delimited (result))
    throw std::invalid_argument ("Invalid v3 mathematical delimiter");
  return result;
}

bool valid_big_operator (const tree& value) {
  if (!is_func (value, BIG_OPERATOR, 2) ||
      !atomic (value[0]) || !atomic (value[1]) ||
      athena::node::get (value[0]) || athena::node::get (value[1]) ||
      !one_of (value[1]->label, "01")) return false;
  return glyph (value[0]->label);
}

tree big_operator (const string& symbol, bool upright) {
  tree result (BIG_OPERATOR, tree (symbol), tree (upright ? "1" : "0"));
  if (!valid_big_operator (result))
    throw std::invalid_argument ("Invalid v3 big operator");
  return result;
}

bool valid_scripts_policy (const string& value) {
  return one_of (value, "ador"); // auto, display, over/under, right
}

tree scripts (const tree& base, const tree& left_sub, const tree& left_sup,
              const tree& right_sub, const tree& right_sup, char placement) {
  const char option[]= {placement, 0};
  if (!valid_scripts_policy (string (option)))
    throw std::invalid_argument ("Invalid v3 script placement");
  tree result (MATH_SCRIPTS, base, left_sub, left_sup, right_sub, right_sup);
  athena::node::metadata meta;
  meta.properties.emplace (scripts_policy_key,
                           athena::node::property (std::string (option)));
  athena::node::set (result, meta);
  return result;
}

bool valid_scripts (const tree& value) {
  if (!is_func (value, MATH_SCRIPTS, 5)) return false;
  const auto* meta= athena::node::get (value);
  if (!meta) return false;
  auto policy= meta->properties.find (scripts_policy_key);
  if (policy == meta->properties.end ()) return false;
  const auto* text= std::get_if<std::string> (&policy->second.data);
  if (!text) return false;
  return valid_scripts_policy (string (text->data (), int (text->size ())));
}

std::string symbol_registry_identity (const string& semantic_id) {
  if (N (semantic_id) == 0 || N (semantic_id) > 64) return {};
  const std::string_view name (semantic_id.data (), std::size_t (N (semantic_id)));
  std::string rendered;
  if (name == "differential-d") rendered= "texmacs:mathd";
  else if (name == "multiple-integral-with-dots") rendered= "texmacs:idotsint";
  else if (name == "backward-assignment") rendered= "texmacs:backassign";
  else if (name == "long-hookrightarrow") rendered= "texmacs:longhookrightarrow";
  else {
    // Those four glyphs have one descriptive, canonical v3 identity only.
    if (name == "mathd" || name == "idotsint" ||
        name == "backassign" || name == "longhookrightarrow") return {};
    for (const unsigned char c: name)
      if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_')) return {};
    rendered= "texmacs:" + std::string (name);
  }
  return athena::text::standard_named_symbols ().lookup (rendered) ?
         std::move (rendered) : std::string ();
}

string grid_column_alignment (const string& kind, int column) {
  if (column < 0) return "";
  if (kind == "eqnarray") {
    if (column % 3 == 0) return "r";
    return column % 3 == 1 ? string ("c") : string ("l");
  }
  if (kind == "align") return column % 2 == 0 ? string ("r") : string ("l");
  if (kind == "matrix" || kind == "stack") return "c";
  if (kind == "cases") return "l";
  return "";
}

bool valid_grid_row (const tree& row) {
  if (!is_func (row, ROW) || N (row) == 0) return false;
  if (const auto* meta= athena::node::get (row)) {
    const auto policy= meta->properties.find (row_numbering_key);
    if (policy != meta->properties.end ()) {
      const auto* text= std::get_if<std::string> (&policy->second.data);
      if (!text || (*text != "numbered" && *text != "unnumbered")) return false;
    }
    const auto tag= meta->properties.find (row_tag_key);
    if (tag != meta->properties.end () &&
        !std::holds_alternative<athena::node::rich_text> (tag->second.data))
      return false;
  }
  for (int i=0; i<N (row); ++i) {
    if (!is_func (row[i], CELL, 1)) return false;
    const auto* cell_meta= athena::node::get (row[i]);
    if (!cell_meta) continue;
    const auto format= cell_meta->properties.find ("math:cell-format");
    if (format == cell_meta->properties.end ()) continue;
    const auto* fields= std::get_if<athena::node::property::dictionary> (
      &format->second.data);
    if (!fields || fields->empty ()) return false;
    for (const auto& [name, entry]: *fields) {
      if (name != "border-bottom" && name != "border-top" &&
          name != "border-left" && name != "border-right" &&
          name != "background" &&
          name != "height" && name != "height-mode" &&
          name != "align" && name != "color") return false;
      if (!std::holds_alternative<std::string> (entry.data)) return false;
      if (name == "height-mode") {
        const auto& mode= std::get<std::string> (entry.data);
        // These are the actual cell-height policies implemented by the
        // Typesetter. Unknown values used to roundtrip as if valid native
        // math while silently behaving as the default mode.
        if (mode != "auto" && mode != "exact" &&
            mode != "min" && mode != "max") return false;
      }
    }
  }
  return true;
}

namespace {

void require_grid_row (const tree& row) {
  if (!valid_grid_row (row))
    throw std::invalid_argument ("Invalid v3 mathematical grid row");
}

athena::node::metadata grid_row_metadata (const tree& row) {
  require_grid_row (row);
  const auto* meta= athena::node::get (row);
  return meta ? *meta : athena::node::metadata ();
}

} // namespace

row_numbering get_row_numbering (const tree& row) {
  require_grid_row (row);
  const auto* meta= athena::node::get (row);
  if (!meta) return row_numbering::inherit;
  const auto policy= meta->properties.find (row_numbering_key);
  if (policy == meta->properties.end ()) return row_numbering::inherit;
  return std::get<std::string> (policy->second.data) == "numbered" ?
         row_numbering::numbered : row_numbering::unnumbered;
}

void set_row_numbering (tree& row, row_numbering policy) {
  auto meta= grid_row_metadata (row);
  switch (policy) {
  case row_numbering::inherit:
    meta.properties.erase (row_numbering_key);
    break;
  case row_numbering::numbered:
  case row_numbering::unnumbered:
    meta.properties.insert_or_assign (row_numbering_key,
      athena::node::property (std::string (
        policy == row_numbering::numbered ? "numbered" : "unnumbered")));
    break;
  default: throw std::invalid_argument ("Invalid v3 grid row numbering policy");
  }
  athena::node::set (row, meta);
}

std::optional<tree> get_row_tag (const tree& row) {
  require_grid_row (row);
  const auto* meta= athena::node::get (row);
  if (!meta) return std::nullopt;
  const auto tag= meta->properties.find (row_tag_key);
  if (tag == meta->properties.end ()) return std::nullopt;
  return std::get<athena::node::rich_text> (tag->second.data).content;
}

void set_row_tag (tree& row, const tree& tag) {
  auto meta= grid_row_metadata (row);
  meta.properties.insert_or_assign (row_tag_key,
    athena::node::property (athena::node::rich_text {tag}));
  athena::node::set (row, meta);
}

void clear_row_tag (tree& row) {
  auto meta= grid_row_metadata (row);
  meta.properties.erase (row_tag_key);
  athena::node::set (row, meta);
}

bool valid_native_math (const tree& value) {
  if (!is_compound (value)) return true;
  switch (L (value)) {
  case DELIMITED: return valid_delimited (value);
  case BIG_OPERATOR: return valid_big_operator (value);
  case MATH_SCRIPTS: return valid_scripts (value);
  case MATH_MIDDLE:
    return N (value) == 1 && is_atomic (value[0]) &&
           !athena::node::get (value[0]) &&
           glyph (value[0]->label);
  case MATH_ACCENT: {
    if (N (value) != 3 || !is_atomic (value[0]) ||
        !is_atomic (value[1]) || !glyph (value[0]->label) ||
        !one_of (value[1]->label, "ou")) return false;
    const auto* meta= athena::node::get (value);
    if (!meta) return false;
    const auto found= meta->properties.find ("math:accent-stretch");
    return found != meta->properties.end () &&
           std::holds_alternative<bool> (found->second.data);
  }
  case MATH_ANNOTATION:
    // The fixed three-slot layout must also represent an editor's unfinished
    // annotation with an empty label. It still has exactly one canonical AST.
    return N (value) == 3;
  case MATH_LONG_ARROW:
    return N (value) == 3 &&
           ((is_atomic (value[0]) && glyph (value[0]->label)) ||
            (is_func (value[0], MATH_SYMBOL, 1) &&
             is_atomic (value[0][0]) &&
             !symbol_registry_identity (value[0][0]->label).empty ()));
  case MATH_GRID: {
    if (N (value) == 0) return false;
    bool row_overrides= false;
    if (const auto* meta= athena::node::get (value)) {
      auto kind= meta->properties.find ("math:grid-kind");
      if (kind == meta->properties.end ()) return false;
      const auto* data= std::get_if<std::string> (&kind->second.data);
      if (!data || !(*data == "matrix" || *data == "eqnarray" ||
                     *data == "align" || *data == "cases" ||
                     *data == "stack" ||
                     *data == "generic")) return false;
      row_overrides= *data == "eqnarray" || *data == "align" || *data == "stack";
    }
    else return false;
    for (int i=0; i<N (value); ++i) {
      if (!valid_grid_row (value[i])) return false;
      if (!row_overrides)
        if (const auto* meta= athena::node::get (value[i]))
          if (meta->properties.count (row_numbering_key) ||
              meta->properties.count (row_tag_key)) return false;
    }
    return true;
  }
  case MATH_BLOCK:
    if (N (value) != 1 || is_func (value[0], DOCUMENT)) return false;
    if (const auto* meta= athena::node::get (value)) {
      auto numbered= meta->properties.find ("math:block-numbered");
      return numbered != meta->properties.end () &&
             std::holds_alternative<bool> (numbered->second.data);
    }
    return false;
  case MATH_VARIANT: {
    if (N (value) != 2 || !is_atomic (value[0])) return false;
    const string& kind= value[0]->label;
    return kind == "bb" || kind == "rm" || kind == "it" ||
           kind == "bf" || kind == "ss" || kind == "tt" ||
           kind == "sl" || kind == "cal" || kind == "frak" ||
           kind == "mbf" || kind == "mbfu";
  }
  case MATH_SYMBOL: {
    if (N (value) != 1 || !is_atomic (value[0])) return false;
    return !symbol_registry_identity (value[0]->label).empty ();
  }
  default: return true;
  }
}

bool descriptor_slot (const tree& value, int index) {
  if (!is_compound (value) || index < 0 || index >= N (value))
    return false;
  switch (L (value)) {
  case DELIMITED: return index < 5;
  case BIG_OPERATOR: return true;
  case MATH_MIDDLE: case MATH_SYMBOL: return index == 0;
  case MATH_ACCENT: return index < 2;
  case MATH_VARIANT: return index == 0;
  case MATH_LONG_ARROW: return index == 0;
  default: return false;
  }
}

bool hidden_search_descriptor_slot (const tree& value, int index) {
  if (!descriptor_slot (value, index)) return false;
  if (index == 0 &&
      (L (value) == BIG_OPERATOR || L (value) == MATH_MIDDLE ||
       (L (value) == MATH_LONG_ARROW && is_atomic (value[index]))))
    return false;
  return true;
}

} // namespace athena::math_v3
