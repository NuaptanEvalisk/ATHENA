
/******************************************************************************
* MODULE     : builtin_contracts.cpp
* DESCRIPTION: immutable native contracts for builtin nodes and variables
* COPYRIGHT  : (C) 2003  Joris van der Hoeven
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "builtin_contracts.hpp"
#include "vars.hpp"
#include "Data/NodeContracts/native_fields.hpp"
#include "Data/NodeContracts/editing_query.hpp"
#include <mutex>

namespace athena::contracts {
namespace {
contract_scope baseline;
std::map<std::string, int, std::less<>> labels;

// This transient builder writes the native contract directly. The standard
// label table owns declarations, not a second mutable descriptor format.
struct bootstrap_contract {
  node_contract value;

  explicit bootstrap_contract (field_layout layout) {
    value.layout= layout;
    value.type= TYPE_REGULAR;
    value.border= BORDER_YES;
    value.block= BLOCK_NO;
    value.variable_type= VAR_MACRO;
    value.type_origin= value.layout_origin= value.border_origin= value.block_origin=
      value.with_origin= value.variable_type_origin= property_authority::builtin;
    value.fields.resize (layout.fields ());
    for (auto& field: value.fields) {
      field.type= TYPE_ADHOC;
      field.accessible= ACCESSIBLE_NEVER;
      field.writability= WRITABILITY_NORMAL;
      field.block= BLOCK_REQUIRE_BLOCK;
      field.environment= athena::node::frozen_tree (tree (WITH));
      field.type_origin= field.accessible_origin= field.writability_origin=
        field.block_origin= field.environment_origin= property_authority::builtin;
    }
  }
  bootstrap_contract* operator -> () { return this; }
  bootstrap_contract& inner_border () { value.border= BORDER_INNER; return *this; }
  bootstrap_contract& with_like () { value.with_like= true; return *this; }
  bootstrap_contract& var_parameter () { value.variable_type= VAR_PARAMETER; return *this; }
  bootstrap_contract& type (int type) { value.type= type; return *this; }
  bootstrap_contract& type (int index, int type) {
    value.fields.at (index).type= type;
    return *this;
  }
  bootstrap_contract& accessible (int index) {
    value.fields.at (index).type= TYPE_REGULAR;
    value.fields.at (index).accessible= ACCESSIBLE_ALWAYS;
    return *this;
  }
  bootstrap_contract& hidden (int index) {
    value.fields.at (index).type= TYPE_REGULAR;
    value.fields.at (index).accessible= ACCESSIBLE_HIDDEN;
    return *this;
  }
  bootstrap_contract& locals (int index, string name, string content) {
    auto& field= value.fields.at (index);
    field.environment= athena::node::frozen_tree (
      athena::contracts::field_environment_write (tree (field.environment), name, content));
    return *this;
  }
  bootstrap_contract& attribute (std::string key, string text) {
    value.attributes.insert_or_assign (std::move (key), athena::node::frozen_tree (tree (text)));
    return *this;
  }
  bootstrap_contract& name (string text) { return attribute ("name", text); }
  bootstrap_contract& long_name (string text) { return attribute ("long-name", text); }
  bootstrap_contract& name (int index, string text) {
    return attribute ("name-" + std::to_string (index), text);
  }
  bootstrap_contract& long_name (int index, string text) {
    return attribute ("long-name-" + std::to_string (index), text);
  }
};

#define BIFORM   field_mapping::split
#define DETAILED field_mapping::detailed

#define regular(i) type (i, TYPE_REGULAR)
#define adhoc(i) type (i, TYPE_ADHOC)
#define raw(i) type (i, TYPE_RAW)
#define macro(i) type (i, TYPE_MACRO)
#define argument(i) type (i, TYPE_ARGUMENT)
#define variable(i) type (i, TYPE_VARIABLE)
#define binding(i) type (i, TYPE_BINDING)
#define boolean(i) type (i, TYPE_BOOLEAN)
#define integer(i) type (i, TYPE_INTEGER)
#define string_type(i) type (i, TYPE_STRING)
#define numeric(i) type (i, TYPE_NUMERIC)
#define length(i) type (i, TYPE_LENGTH)
#define code(i) type (i, TYPE_CODE)
#define url_type(i) type (i, TYPE_URL)
#define identifier(i) type (i, TYPE_IDENTIFIER)
#define color_type(i) type (i, TYPE_COLOR)
#define graphical(i) type (i, TYPE_GRAPHICAL)
#define constraint(i) type (i, TYPE_CONSTRAINT)
#define graphical_id(i) type (i, TYPE_GRAPHICAL_ID)
#define point_type(i) type (i, TYPE_POINT)
#define effect(i) type (i, TYPE_EFFECT)
#define duration(i) type (i, TYPE_DURATION)

#define returns_adhoc() type (TYPE_ADHOC)
#define returns_boolean() type (TYPE_BOOLEAN)
#define returns_integer() type (TYPE_INTEGER)
#define returns_string() type (TYPE_STRING)
#define returns_numeric() type (TYPE_NUMERIC)
#define returns_length() type (TYPE_LENGTH)
#define returns_url() type (TYPE_URL)
#define returns_identifier() type (TYPE_IDENTIFIER)
#define returns_duration() type (TYPE_DURATION)
#define returns_color() type (TYPE_COLOR)
#define returns_graphical() type (TYPE_GRAPHICAL)
#define returns_constraint() type (TYPE_CONSTRAINT)
#define returns_effect() type (TYPE_EFFECT)

static bootstrap_contract
fixed (int arity, int extra=0, field_mapping mapping= field_mapping::uniform) {
  return bootstrap_contract ({arity, extra, arity_kind::fixed, mapping});
}

static bootstrap_contract
options (int arity, int extra, field_mapping mapping= field_mapping::uniform) {
  return bootstrap_contract ({arity, extra, arity_kind::optional, mapping});
}

static bootstrap_contract
repeat (int arity, int extra, field_mapping mapping= field_mapping::uniform) {
  return bootstrap_contract ({arity, extra, arity_kind::repeated_suffix, mapping});
}

static bootstrap_contract
var_repeat (int arity, int extra, field_mapping mapping= field_mapping::uniform) {
  return bootstrap_contract ({extra, arity, arity_kind::repeated_prefix, mapping});
}

static bootstrap_contract
native_contract (tree_label label) {
  using namespace athena::contracts;
  const auto* schema= native_fields (label);
  ASSERT (schema != nullptr, "Missing native node field contract");
  const int base= schema->editing_minimum;
  const int extra= schema->repeat ? schema->repeat : schema->optional;
  const auto mode= schema->repeat ?
    (schema->repeat_first ? arity_kind::repeated_prefix : arity_kind::repeated_suffix) :
    (schema->optional ? arity_kind::optional : arity_kind::fixed);
  bootstrap_contract result ({base, extra, mode, field_mapping::detailed});
  if (schema->inner_border) result.inner_border ();
  result.name (string (schema->display_name.data (), schema->display_name.size ()));
  for (int i=0; i<int(result.value.fields.size ()); ++i) {
    const auto& field= *schema->child (i, base + extra);
    auto& child= result.value.fields[i];
    child.type= field.value == field_value::atom ? TYPE_STRING : TYPE_REGULAR;
    child.accessible= field.editable ? ACCESSIBLE_ALWAYS : ACCESSIBLE_NEVER;
    child.block= field.role == field_role::inline_content ? BLOCK_REQUIRE_INLINE :
      field.role == field_role::body ? BLOCK_REQUIRE_BLOCK : BLOCK_REQUIRE_NONE;
    tree environment (ATTR);
    for (const auto& [key, value]: field.environment)
      environment << string (key.data (), key.size ()) <<
                   string (value.data (), value.size ());
    child.environment= athena::node::frozen_tree (environment);
    result.name (i, string (field.name.data (), field.name.size ()));
  }
  return result;
}

static void
init (tree_label l, string name, const bootstrap_contract& contract) {
  labels.emplace (std::string (name.data (), N(name)), (int) l);
  make_tree_label (l, name);
  baseline.replace (l, contract.value);
}

static void
init_var (string var, int tp, string vname= "") {
  tree_label l= make_tree_label (var);
  auto contract= fixed (0) -> var_parameter () -> type (tp);
  if (vname != "") contract.name (vname);
  baseline.replace (l, std::move (contract.value));
}

} // namespace

void
initialize_builtins () {
  static std::once_flag initialized;
  std::call_once (initialized, [] {
    init_standard_tree_labels ();
#include "standard_tree_labels.inc"
  });
}

const contract_scope& builtin_contracts () {
  initialize_builtins ();
  return baseline;
}

bool builtin_label (string name) {
  initialize_builtins ();
  return labels.find (std::string_view (name.data (), N(name))) != labels.end ();
}

hashmap<string,int>& builtin_codes () {
  static thread_local hashmap<string,int> local= [] {
    initialize_builtins ();
    hashmap<string,int> result (UNKNOWN);
    for (const auto& [name, label]: labels)
      result (string (name.data (), name.size ()))= label;
    return result;
  } ();
  return local;
}

} // namespace athena::contracts
