/******************************************************************************
* MODULE     : math_tree_v3.hpp
* DESCRIPTION: Native structural mathematical nodes for XML v3
* COPYRIGHT  : (C) 2026 ATHENA contributors
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
******************************************************************************/
#pragma once

#include "tree.hpp"
#include <optional>

namespace athena::math_v3 {

// Serialized glyphs are Unicode, not TeXmacs token spellings or font recipes.
// Empty glyphs are allowed only for an invisible endpoint of a pair.
bool glyph (const string& value, bool allow_empty= false);
bool size_level (const string& value);

// DELIMITED has exactly six physical slots: left, right, size policy, left
// manual size, right manual size, expression. Only the expression is content.
// s = 'a' automatic, 'f' natural-size, 'm' explicit independent sizes.
tree delimited (const string& left, const string& right, char sizing,
                const tree& body, const string& left_size= "",
                const string& right_size= "");
bool valid_delimited (const tree& value);

// BIG_OPERATOR is an atom, not a container for limits or the following body.
// u=0 means ordinary glyph; u=1 requests the upright integral presentation.
tree big_operator (const string& symbol, bool upright= false);
bool valid_big_operator (const tree& value);

// MATH_SCRIPTS has five CONTENT children, in the order base, left sub, left
// sup, right sub, right sup. Placement is a typed node presentation property;
// it serializes as the `p` XML attribute, never a sixth child or WITH wrapper.
inline constexpr const char* scripts_policy_key= "math:scripts-policy";
bool valid_scripts_policy (const string& value);
tree scripts (const tree& base, const tree& left_sub, const tree& left_sup,
              const tree& right_sub, const tree& right_sup,
              char placement= 'a');
bool valid_scripts (const tree& value);

// A stable v3 semantic symbol ID resolves through the authoritative registry;
// the legacy recipe key is a renderer implementation detail, never a stored
// XML alias or a fallback string interpreted as math markup.
std::string symbol_registry_identity (const string& semantic_id);

// Native mathematical grid layout policies are semantic and do not require
// an intermediate TFORMAT/CWITH tree. Empty result means inherited alignment.
string grid_column_alignment (const string& grid_kind, int column);

// These properties have mathematical meaning only on a direct MATH_GRID ROW.
// Missing numbering is the sole canonical spelling of inherit, including for
// already migrated grids. The stored string is "numbered" or "unnumbered";
// an explicit "inherit" string is invalid, not silently normalized by XML.
// A tag is optional rich_text, including an explicitly empty tree, independent
// of numbering. Both properties use ordinary typed XML metadata, not attributes.
// Only eqnarray/align/stack grids allow row overrides; matrix/cases/generic keep
// whole-block numbering. The renderer resolves inherit from a direct MathBlock
// parent only (never a containing/nested grid). A custom tag displays/binds
// without advancing the equation counter, regardless of the row policy.
// Row UUIDs, LABEL children and typed references retain their usual meaning;
// neither numbering nor a custom tag creates/replaces an identity or label.
inline constexpr const char* row_numbering_key= "math:row-numbering";
inline constexpr const char* row_tag_key= "math:row-tag";
enum class row_numbering { inherit, numbered, unnumbered };
// As with valid_native_math, this checks local schema/shape, not descendants;
// the codec recursively validates native content, including rich tag trees.
// The parent MATH_GRID validator additionally checks whether its kind allows
// row overrides; a detached ROW alone has no grid-kind context.
bool valid_grid_row (const tree& row);
// Accessors/mutators reject malformed grid rows with invalid_argument. Tag
// access returns a detached copy. Mutators are for detached trees only; live
// edits must use the observer/modification boundary. Text TABLE ROWs do not
// acquire these semantics merely by passing through the native/XML validator.
row_numbering get_row_numbering (const tree& row);
void set_row_numbering (tree& row, row_numbering policy);
std::optional<tree> get_row_tag (const tree& row);
void set_row_tag (tree& row, const tree& tag);
void clear_row_tag (tree& row);

// Validate the closed native mathematics grammar for a single v3 node.
// Non-math node types are validated by their separate DRD declarations.
bool valid_native_math (const tree& value);

} // namespace athena::math_v3
