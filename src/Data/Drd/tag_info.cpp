
/******************************************************************************
* MODULE     : tag_info.cpp
* DESCRIPTION: DRD information about tags
* COPYRIGHT  : (C) 2003  Joris van der Hoeven
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "tag_info.hpp"
#include "drd_info.hpp"


/******************************************************************************
* Properties of the tag
******************************************************************************/

parent_info::parent_info (int a, int x, int am, int cm, bool native) {
  type             = TYPE_REGULAR;
  arity_mode       = am;
  arity_base       = a;
  arity_extra      = x;
  child_mode       = cm;
  border_mode      = BORDER_YES;
  block            = BLOCK_NO;
  with_like        = false;
  var_type         = VAR_MACRO;
  type_origin= arity_origin= border_origin= block_origin=
    with_origin= var_type_origin= native ? property_authority::builtin :
                                          property_authority::inferred;
}

parent_info::parent_info (tree t) {
  ASSERT (is_func (t, TUPLE, 15), "invalid node contract record");
  type= as_int (t[0]);
  arity_mode= as_int (t[1]);
  arity_base= as_int (t[2]);
  arity_extra= as_int (t[3]);
  child_mode= as_int (t[4]);
  border_mode= as_int (t[5]);
  block= as_int (t[6]);
  with_like= as_int (t[7]) != 0;
  var_type= as_int (t[8]);
  type_origin= (property_authority) as_int (t[9]);
  arity_origin= (property_authority) as_int (t[10]);
  border_origin= (property_authority) as_int (t[11]);
  block_origin= (property_authority) as_int (t[12]);
  with_origin= (property_authority) as_int (t[13]);
  var_type_origin= (property_authority) as_int (t[14]);
}

parent_info::operator tree () {
  tree result (TUPLE);
  for (int value: {type, arity_mode, arity_base, arity_extra, child_mode,
                  border_mode, block, (int) with_like, var_type,
                  (int) type_origin, (int) arity_origin, (int) border_origin,
                  (int) block_origin, (int) with_origin, (int) var_type_origin})
    result << as_string (value);
  return result;
}

bool
parent_info::operator == (const parent_info& pi) {
  return
    (type             == pi.type            ) &&
    (arity_mode       == pi.arity_mode      ) &&
    (arity_base       == pi.arity_base      ) &&
    (arity_extra      == pi.arity_extra     ) &&
    (child_mode       == pi.child_mode      ) &&
    (border_mode      == pi.border_mode     ) &&
    (block            == pi.block           ) &&
    (with_like        == pi.with_like       ) &&
    (var_type         == pi.var_type        ) &&
    (type_origin     == pi.type_origin     ) &&
    (arity_origin    == pi.arity_origin    ) &&
    (border_origin   == pi.border_origin   ) &&
    (block_origin    == pi.block_origin    ) &&
    (with_origin     == pi.with_origin     ) &&
    (var_type_origin == pi.var_type_origin );
}

bool
parent_info::operator != (const parent_info& pi) {
  return !(operator == (pi));
}

tm_ostream&
operator << (tm_ostream& out, parent_info pi) {
  return out << ((tree) pi);
}

/******************************************************************************
* Properties of the children of the tag
******************************************************************************/

child_info::child_info (bool native) {
  type               = TYPE_ADHOC;
  accessible         = ACCESSIBLE_NEVER;
  writability        = WRITABILITY_NORMAL;
  block              = 0;
  env= tree (WITH);
  type_origin= accessible_origin= writability_origin= block_origin=
    env_origin= native ? property_authority::builtin : property_authority::inferred;
}

child_info::child_info (tree t) {
  ASSERT (is_func (t, TUPLE, 10), "invalid field contract record");
  type= as_int (t[0]);
  accessible= as_int (t[1]);
  writability= as_int (t[2]);
  block= as_int (t[3]);
  env= t[4];
  type_origin= (property_authority) as_int (t[5]);
  accessible_origin= (property_authority) as_int (t[6]);
  writability_origin= (property_authority) as_int (t[7]);
  block_origin= (property_authority) as_int (t[8]);
  env_origin= (property_authority) as_int (t[9]);
}

child_info::operator tree () {
  tree result (TUPLE, as_string (type), as_string (accessible),
               as_string (writability), as_string (block), env);
  for (auto origin: {type_origin, accessible_origin, writability_origin,
                    block_origin, env_origin})
    result << as_string ((int) origin);
  return result;
}

bool
child_info::operator == (const child_info& ci) {
  return
    (type               == ci.type              ) &&
    (accessible         == ci.accessible        ) &&
    (writability        == ci.writability       ) &&
    (block              == ci.block             ) &&
    (env                == ci.env               ) &&
    (type_origin        == ci.type_origin       ) &&
    (accessible_origin  == ci.accessible_origin ) &&
    (writability_origin == ci.writability_origin) &&
    (block_origin       == ci.block_origin      ) &&
    (env_origin         == ci.env_origin        );
}

bool
child_info::operator != (const child_info& ci) {
  return !(operator == (ci));
}

tm_ostream&
operator << (tm_ostream& out, child_info ci) {
  return out << ((tree) ci);
}

/******************************************************************************
* Constructors, destructors and converters
******************************************************************************/

tag_info_rep::tag_info_rep (parent_info pi2, array<child_info> ci2, tree x):
  pi (pi2), ci (ci2), extra (x) {}

tag_info_rep::tag_info_rep (int a, int x, int am, int cm, bool frozen):
  pi (a, x, am, cm, frozen),
  ci ((a+x)==0? 0: (cm==CHILD_UNIFORM? 1: (cm==CHILD_BIFORM? 2: (a+x))))
{
  if (frozen) {
    int i, n= N(ci);
    for (i=0; i<n; i++)
      ci[i]= child_info (true);
  }
}

tag_info::tag_info (parent_info pi, array<child_info> ci, tree extra) {
  rep= tm_new<tag_info_rep> (pi, ci, extra);
}

tag_info::tag_info (int a, int x, int am, int cm, bool frozen) {
  rep= tm_new<tag_info_rep> (a, x, am, cm, frozen);
}

tag_info::tag_info (tree t) {
  if ((!is_func (t, TUPLE)) || (N(t)<2) || (L(t[1]) != TUPLE)) {
    failed_error << "t= " << t << "\n";
    FAILED ("bad tag_info");
  }
  parent_info pi (t[0]);
  int i, n= N(t[1]);
  array<child_info> ci (n);
  for (i=0; i<n; i++)
    ci[i]= child_info (t[1][i]);
  rep= tm_new<tag_info_rep> (pi, ci, N(t)==3? t[2]: tree (""));
}

tag_info::operator tree () {
  if (rep->extra == "") return tree (TUPLE, (tree) rep->pi, (tree) rep->ci);
  else return tree (TUPLE, (tree) rep->pi, (tree) rep->ci, rep->extra);
}

/******************************************************************************
* Access routines and getting the index of a child
******************************************************************************/

tag_info
tag_info_rep::inner_border () {
  pi.border_mode= BORDER_INNER;
  return tag_info (pi, ci, extra);
}

tag_info
tag_info_rep::outer_border () {
  pi.border_mode= BORDER_OUTER;
  return tag_info (pi, ci, extra);
}

tag_info
tag_info_rep::with_like () {
  pi.with_like= true;
  return tag_info (pi, ci, extra);
}

tag_info
tag_info_rep::var_parameter () {
  pi.var_type= VAR_PARAMETER;
  return tag_info (pi, ci, extra);
}

tag_info
tag_info_rep::var_macro_parameter () {
  pi.var_type= VAR_MACRO_PARAMETER;
  return tag_info (pi, ci, extra);
}

tag_info
tag_info_rep::type (int tp) {
  pi.type= tp;
  return tag_info (pi, ci, extra);
}

tag_info
tag_info_rep::type (int i, int tp) {
  if (i < 0 || i >= N(ci)) cout << i << " out of " << N(ci) << "\n";
  ASSERT (i >= 0 && i<N(ci), "index out of range");
  ci[i].type= tp;
  return tag_info (pi, ci, extra);
}

tag_info
tag_info_rep::accessible (int i) {
  if (i < 0 || i >= N(ci)) cout << i << " out of " << N(ci) << "\n";
  ASSERT (i >= 0 && i<N(ci), "index out of range");
  ci[i].type= TYPE_REGULAR;
  ci[i].accessible= ACCESSIBLE_ALWAYS;
  return tag_info (pi, ci, extra);
}

tag_info
tag_info_rep::hidden (int i) {
  if (i < 0 || i >= N(ci)) cout << i << " out of " << N(ci) << "\n";
  ASSERT (i >= 0 && i<N(ci), "index out of range");
  ci[i].type= TYPE_REGULAR;
  ci[i].accessible= ACCESSIBLE_HIDDEN;
  return tag_info (pi, ci, extra);
}

tag_info
tag_info_rep::disable_writable (int i) {
  if (i < 0 || i >= N(ci)) cout << i << " out of " << N(ci) << "\n";
  ASSERT (i >= 0 && i<N(ci), "index out of range");
  ci[i].writability= WRITABILITY_DISABLE;
  return tag_info (pi, ci, extra);
}

tag_info
tag_info_rep::enable_writable (int i) {
  if (i < 0 || i >= N(ci)) cout << i << " out of " << N(ci) << "\n";
  ASSERT (i >= 0 && i<N(ci), "index out of range");
  ci[i].writability= WRITABILITY_ENABLE;
  return tag_info (pi, ci, extra);
}

tag_info
tag_info_rep::locals (int i, string var, string val) {
  if (i < 0 || i >= N(ci)) cout << i << " out of " << N(ci) << "\n";
  ASSERT (i >= 0 && i<N(ci), "index out of range");
  ci[i].env= drd_env_write (ci[i].env, var, val);
  return tag_info (pi, ci, extra);
}

void
tag_info_rep::set_attribute (string which, tree val) {
  if (extra == "") extra= tree (ATTR);
  for (int i=0; i+1<N(extra); i+=2)
    if (extra[i] == tree (which)) {
      extra[i+1]= val;
      return;
    }
  extra << tree (which) << val;
}

tree
tag_info_rep::get_attribute (string which) {
  if (!is_func (extra, ATTR)) return "";
  int i, n= N(extra);
  for (i=0; i+1<n; i+=2)
    if (extra[i] == which)
      return extra[i+1];
  return "";
}

tag_info
tag_info_rep::name (string s) {
  set_attribute ("name", s);
  return tag_info (pi, ci, extra);
}

tag_info
tag_info_rep::long_name (string s) {
  set_attribute ("long-name", s);
  return tag_info (pi, ci, extra);
}

tag_info
tag_info_rep::name (int i, string s) {
  if (i < 0 || i >= N(ci)) cout << i << " out of " << N(ci) << "\n";
  ASSERT (i >= 0 && i<N(ci), "index out of range");
  set_attribute ("name-" * as_string (i), s);
  return tag_info (pi, ci, extra);
}

tag_info
tag_info_rep::long_name (int i, string s) {
  if (i < 0 || i >= N(ci)) cout << i << " out of " << N(ci) << "\n";
  ASSERT (i >= 0 && i<N(ci), "index out of range");
  set_attribute ("long-name-" * as_string (i), s);
  return tag_info (pi, ci, extra);
}

int
tag_info_rep::get_index (int child, int n) const {
  return pi.layout ().index (child, n);
}

child_info&
tag_info::operator () (int child, int n) {
  int index= rep->get_index (child, n);
  if (index < 0 || index >= N(rep->ci)) {
    failed_error << "child       = " << child << "\n";
    failed_error << "out of      = " << n << "\n";
    failed_error << "child_mode  = " << rep->pi.child_mode << "\n";
    failed_error << "arity_mode  = " << rep->pi.arity_mode << "\n";
    failed_error << "arity_base  = " << rep->pi.arity_base << "\n";
    failed_error << "arity_extra = " << rep->pi.arity_extra << "\n";
    failed_error << "N(ci)       = " << N(rep->ci) << "\n";
    FAILED ("index out of range");
  }
  return rep->ci [index];
}

/******************************************************************************
* Usual extra routines
******************************************************************************/

tm_ostream&
operator << (tm_ostream& out, tag_info ti) {
  out << "[ " << ti->pi << ", " << ti->ci;
  if (ti->extra != "") out << ", " << ti->extra << "\n";
  return out << " ]";
}

tag_info
copy (tag_info ti) {
  return tag_info (ti->pi, copy (ti->ci), copy (ti->extra));
}

bool
operator == (tag_info ti1, tag_info ti2) {
  return
    (ti1->pi == ti2->pi) && (ti1->ci == ti2->ci) && (ti1->extra == ti2->extra);
}

bool
operator != (tag_info ti1, tag_info ti2) {
  return !(ti1 == ti2);
}

athena::contracts::node_contract
freeze_descriptor (tag_info ti) {
  using namespace athena::contracts;
  node_contract result;
  const auto& parent= ti->pi;
  result.layout= parent.layout ();
  result.type= parent.type;
  result.border= parent.border_mode;
  result.block= parent.block;
  result.with_like= parent.with_like;
  result.variable_type= parent.var_type;
  result.type_origin= parent.type_origin;
  result.layout_origin= parent.arity_origin;
  result.border_origin= parent.border_origin;
  result.block_origin= parent.block_origin;
  result.with_origin= parent.with_origin;
  result.variable_type_origin= parent.var_type_origin;
  for (int i=0; i<N(ti->ci); ++i) {
    const auto& child= ti->ci[i];
    result.fields.push_back ({child.type, child.accessible, child.writability,
      child.block, athena::node::frozen_tree (child.env), child.type_origin,
      child.accessible_origin, child.writability_origin, child.block_origin,
      child.env_origin});
  }
  if (is_func (ti->extra, ATTR))
    for (int i=0; i+1<N(ti->extra); i+=2) {
      const string& key= ti->extra[i]->label;
      result.attributes.emplace (std::string (key.data (), N(key)),
        athena::node::frozen_tree (ti->extra[i+1]));
    }
  return result;
}

tag_info
descriptor_builder (const athena::contracts::node_contract& value) {
  parent_info parent (value.layout.base, value.layout.extra,
    static_cast<int> (value.layout.arity), static_cast<int> (value.layout.mapping));
  parent.type= value.type;
  parent.border_mode= value.border;
  parent.block= value.block;
  parent.with_like= value.with_like;
  parent.var_type= value.variable_type;
  parent.type_origin= value.type_origin;
  parent.arity_origin= value.layout_origin;
  parent.border_origin= value.border_origin;
  parent.block_origin= value.block_origin;
  parent.with_origin= value.with_origin;
  parent.var_type_origin= value.variable_type_origin;
  array<child_info> children (value.fields.size ());
  for (int i=0; i<N(children); ++i) {
    const auto& field= value.fields[i];
    auto& child= children[i];
    child.type= field.type;
    child.accessible= field.accessible;
    child.writability= field.writability;
    child.block= field.block;
    child.env= tree (field.environment);
    child.type_origin= field.type_origin;
    child.accessible_origin= field.accessible_origin;
    child.writability_origin= field.writability_origin;
    child.block_origin= field.block_origin;
    child.env_origin= field.environment_origin;
  }
  tree attributes (ATTR);
  for (const auto& [key, val]: value.attributes)
    attributes << string (key.data (), key.size ()) << tree (val);
  return tag_info (parent, children, attributes);
}
