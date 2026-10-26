
/******************************************************************************
* MODULE     : drd_std.cpp
* DESCRIPTION: standard drd for TeXmacs; most other drd's inherit from it
* COPYRIGHT  : (C) 2003  Joris van der Hoeven
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "drd_std.hpp"
#include "vars.hpp"
#include "Data/NodeContracts/native_fields.hpp"
#include <mutex>

drd_info std_drd ("tm");
hashmap<string,int> STD_CODE (UNKNOWN);

static thread_local drd_info* thread_drd= nullptr;
static std::recursive_mutex std_drd_mutex;

static drd_info
clone_standard_drd () {
  std::lock_guard<std::recursive_mutex> guard (std_drd_mutex);
  init_std_drd ();
  drd_info local ("tm-thread");
  local->contracts= athena::contracts::contract_scope (std_drd->contracts.snapshot ());
  return local;
}

drd_info&
standard_drd_for_thread () {
  static thread_local drd_info local= clone_standard_drd ();
  return local;
}

hashmap<string,int>&
standard_codes_for_thread () {
  static thread_local hashmap<string,int> local= [] {
    std::lock_guard<std::recursive_mutex> guard (std_drd_mutex);
    init_std_drd ();
    return copy (STD_CODE);
  } ();
  return local;
}

drd_info&
current_drd () noexcept {
  return thread_drd == nullptr ? standard_drd_for_thread () : *thread_drd;
}

drd_info*
swap_current_drd (drd_info* drd) noexcept {
  drd_info* previous= thread_drd;
  thread_drd= drd;
  return previous;
}

#define BIFORM   CHILD_BIFORM
#define DETAILED CHILD_DETAILED

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

static tag_info
fixed (int arity, int extra=0, int child_mode= CHILD_UNIFORM) {
  return tag_info (arity, extra, ARITY_NORMAL, child_mode, true);
}

static tag_info
options (int arity, int extra, int child_mode= CHILD_UNIFORM) {
  return tag_info (arity, extra, ARITY_OPTIONS, child_mode, true);
}

static tag_info
repeat (int arity, int extra, int child_mode= CHILD_UNIFORM) {
  return tag_info (arity, extra, ARITY_REPEAT, child_mode, true);
}

static tag_info
var_repeat (int arity, int extra, int child_mode= CHILD_UNIFORM) {
  return tag_info (extra, arity, ARITY_VAR_REPEAT, child_mode, true);
}

static tag_info
native_contract (tree_label label) {
  using namespace athena::contracts;
  const auto* schema= native_fields (label);
  ASSERT (schema != nullptr, "Missing native node field contract");
  const int base= schema->editing_minimum;
  const int extra= schema->repeat ? schema->repeat : schema->optional;
  const int mode= schema->repeat ?
    (schema->repeat_first ? ARITY_VAR_REPEAT : ARITY_REPEAT) :
    (schema->optional ? ARITY_OPTIONS : ARITY_NORMAL);
  tag_info result (base, extra, mode, CHILD_DETAILED, true);
  if (schema->inner_border) result= result->inner_border ();
  result= result->name (string (schema->display_name.data (), schema->display_name.size ()));
  // Expand just the descriptor prototype, not the actual repeated source.
  // The old positional facade then maps each physical child to this prototype.
  for (int i=0; i<N(result->ci); ++i) {
    const auto& field= *schema->child (i, base + extra);
    auto& child= result->ci[i];
    child.type= field.value == field_value::atom ? TYPE_STRING : TYPE_REGULAR;
    child.accessible= field.editable ? ACCESSIBLE_ALWAYS : ACCESSIBLE_NEVER;
    child.block= field.role == field_role::inline_content ? BLOCK_REQUIRE_INLINE :
      field.role == field_role::body ? BLOCK_REQUIRE_BLOCK : BLOCK_REQUIRE_NONE;
    child.env= tree (ATTR);
    for (const auto& [key, value]: field.environment)
      child.env << string (key.data (), key.size ()) <<
                   string (value.data (), value.size ());
    result->set_attribute ("name-" * as_string (i),
      string (field.name.data (), field.name.size ()));
  }
  return result;
}

static void
init (tree_label l, string name, tag_info ti) {
  STD_CODE(name)= (int) l;
  make_tree_label (l, name);
  std_drd->contracts.replace (l, freeze_descriptor (ti));
}

static void
init_var (string var, int tp, string vname= "") {
  tree_label l= make_tree_label (var);
  tag_info ti= fixed (0) -> var_parameter () -> type (tp);
  if (vname != "") ti= ti->name (vname);
  std_drd->contracts.replace (l, freeze_descriptor (ti));
}

static bool std_drd_initialized= false;

void
init_std_drd () {
  std::lock_guard<std::recursive_mutex> guard (std_drd_mutex);
  if (std_drd_initialized) return;
  init_standard_tree_labels ();
  std_drd_initialized=true;

#include "standard_tree_labels.inc"
}
