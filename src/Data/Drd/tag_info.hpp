
/******************************************************************************
* MODULE     : tag_info.hpp
* DESCRIPTION: DRD information about tags
* COPYRIGHT  : (C) 2003  Joris van der Hoeven
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef TAG_INFO_H
#define TAG_INFO_H
#include "tree.hpp"
#include "Data/NodeContracts/property_authority.hpp"
#include "Data/NodeContracts/descriptor_values.hpp"
#include "Data/NodeContracts/field_layout.hpp"
#include "Data/NodeContracts/contract_scope.hpp"

using athena::contracts::property_authority;

/******************************************************************************
* The parent_info class contains outer information about tags
*
* - The type field specifies the type of the return value of the tag.
*   For instance, regular content has the type TYPE_REGULAR and
*   the tag PLUS which performs a numerical operation admits
*   the type TYPE_NUMERIC.
*
* - The arity fields together with the child_mode determine the possible
*   arities and how to convert logical indices of children to
*   physical indices in the array 'ci'.
*
*    o ARITY_NORMAL    : the arity is given by arity_base+arity_extra
*    o ARITY_OPTIONS   : arity_base <= arity < arity_base+arity_extra
*    o ARITY_REPEAT    : arity= arity_base + any_number * arity_extra
*    o ARITY_VAR_REPEAT: as ARITY_REPEAT, but repetition "comes first"
*
*    o CHILD_UNIFORM : all children have the same properties
*    o CHILD_BIFORM  : two types of properties (corresponds to base/extra)
*    o CHILD_DETAILED: as many as arity_base+arity_extra types of properties
*
* - The border_mode field specifies whether the cursor may be put behind and
*   before the tag or not (the 0 and 1 paths). For instance, this field
*   is BORDER_INNER for CONCAT and BORDER_YES for FRAC.
*
* - The block field specifies when the parent should be considered
*   as a block. In the case of BLOCK_OR, the parent is a block
*   if one of the children satisfying BLOCK_REQUIRE_NONE is a block.
*
* - Property authorities distinguish inferred, declared and native facts.
******************************************************************************/

struct parent_info {
  int type, arity_mode, arity_base, arity_extra, child_mode, border_mode, block;
  bool with_like;
  int var_type;
  property_authority type_origin, arity_origin, border_origin, block_origin;
  property_authority with_origin, var_type_origin;

  parent_info (int arity, int extra, int amode, int cmode, bool native= false);
  parent_info (tree t);
  athena::contracts::field_layout layout () const {
    return {arity_base, arity_extra,
      static_cast<athena::contracts::arity_kind> (arity_mode),
      static_cast<athena::contracts::field_mapping> (child_mode)};
  }
  inline ~parent_info () {}
  operator tree ();
  bool operator == (const parent_info& pi);
  bool operator != (const parent_info& pi);
  friend tm_ostream& operator << (tm_ostream& out, parent_info pi);
};

/******************************************************************************
* The child_info class contains more detailed information about each of
* the children of the tag.
*
* - The type field specifies the type of the field.
*   Fields with regular content admit TYPE_REGULAR as their type.
*
* - The accessible field specifies whether the field can be accessed.
*   ACCESSIBLE_ALWAYS children can always be accessed, ACCESSIBLE_NEVER
*   children can only be accessed in source mode and ACCESSIBLE_HIDDEN
*   children may require unfolding in order to be accessed.
*
* - The writability field specifies whether an accessible field can be edited.
*   When the writability of a child is disabled, its whole descendance
*   becomes read-only, except for those parts whose writability are re-enabled.
*
* - The block field specifies whether the field is required to be
*   a block structure, an inline structure, or any of the two.
*
* - The mode field specifies the mode for each child. In case of MODE_PARENT,
*   the mode of the child is the same as the mode of its parent.
*
* - Environments are actor-local values, not process-global numeric handles.
******************************************************************************/

struct child_info {
  int type, accessible, writability, block;
  tree env;
  property_authority type_origin, accessible_origin, writability_origin;
  property_authority block_origin, env_origin;

  child_info (bool native= false);
  child_info (tree t);
  inline ~child_info () {}
  operator tree ();
  bool operator == (const child_info& pi);
  bool operator != (const child_info& pi);
  friend tm_ostream& operator << (tm_ostream& out, child_info ci);
};

class tag_info;
class tag_info_rep: concrete_struct {
public:
  parent_info       pi;
  array<child_info> ci;
  tree              extra;

  tag_info_rep (parent_info pi, array<child_info> ci, tree extra);
  tag_info_rep (int arity, int extra, int amode, int cmode, bool frozen);
  inline ~tag_info_rep () {}

  tag_info inner_border ();
  tag_info outer_border ();
  tag_info with_like ();
  tag_info var_parameter ();
  tag_info var_macro_parameter ();
  tag_info type (int tp);
  tag_info type (int i, int tp);
  tag_info accessible (int i);
  tag_info hidden (int i);
  tag_info disable_writable (int i);
  tag_info enable_writable (int i);
  tag_info locals (int i, string var, string val);
  tag_info name (string s);
  tag_info long_name (string s);
  tag_info name (int i, string s);
  tag_info long_name (int i, string s);
  int      get_index (int child, int n) const;
  void     set_attribute (string which, tree val);
  tree     get_attribute (string which);

  friend class tag_info;
};

/******************************************************************************
* The main tag_info class consists of parent_info and an array of child_info
******************************************************************************/

class tag_info {
  CONCRETE(tag_info);
  tag_info (parent_info pi, array<child_info> ci, tree extra);
  tag_info (int arity=0, int extra=0,
	    int am=ARITY_NORMAL, int cm= CHILD_UNIFORM,
	    bool frozen= false);
  tag_info (tree t);
  operator tree ();
  child_info& operator () (int child, int n);
};
CONCRETE_CODE(tag_info);

bool operator == (tag_info ti1, tag_info ti2);
bool operator != (tag_info ti1, tag_info ti2);
tm_ostream& operator << (tm_ostream& out, tag_info ti);
tag_info copy (tag_info ti);

// Temporary declaration/cache builders. Live queries use contract_scope;
// neither conversion retains a mutable tag_info in a published snapshot.
athena::contracts::node_contract freeze_descriptor (tag_info ti);
tag_info descriptor_builder (const athena::contracts::node_contract& value);

#endif // defined TAG_INFO_H
