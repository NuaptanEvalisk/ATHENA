/******************************************************************************
* MODULE     : reference_markup.cpp
* DESCRIPTION: Native typed and grouped reference markup
* COPYRIGHT  : (C) 2020 Joris van der Hoeven, 2026 The ATHENA developers
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "reference_markup.hpp"
#include "analyze.hpp"
#include "editor.hpp"

namespace {
tree reference_list (const array<tree>& items) {
  if (N(items) == 0) return tree ("");
  if (N(items) == 1) return items[0];
  tree result (CONCAT);
  for (int i=0; i<N(items); ++i) {
    if (i == N(items)-1) {
      if (N(items) == 2) result << tree (" ");
      result << compound ("localize", "and") << compound ("nbsp");
    }
    result << items[i];
    if (i < N(items)-2) result << tree (", ");
  }
  return result;
}

string reference_type (string name) {
  name= locase_all (name);
  int first= find_non_alpha (name, 0, true);
  int last= find_non_alpha (name, N(name), false);
  editor ed= get_current_editor ();
  if (first >= 0 && ed->defined_at_init (name(0, first) * "-ref"))
    return name(0, first);
  if (last >= 0 && ed->defined_at_init (name(last+1, N(name)) * "-ref"))
    return name(last+1, N(name));
  return "unknown";
}
}

tree reference_typed_markup (tree name, tree wrapper, tree arguments) {
  string title= is_atomic (name) ? name->label : string ("");
  string macro= is_atomic (wrapper) ? wrapper->label : string ("identity");
  array<tree> items;
  for (int i=0; i<N(arguments); ++i) {
    tree item (REFERENCE, arguments[i]);
    if (macro != "identity") item= compound (macro, item);
    items << item;
  }
  if (N(items) == 0) return tree ("");
  if (N(items) > 1 && title != "")
    title= ends (title, "y") ? title(0, N(title)-1) * "ies" : title * "s";
  tree body= reference_list (items);
  if (title == "") return body;
  tree result (CONCAT, compound ("localize", title), compound ("nbsp"));
  if (is_func (body, CONCAT)) result << A(body);
  else result << body;
  return result;
}

tree reference_typed_markup (tree name, tree arguments) {
  return reference_typed_markup (name, tree ("identity"), arguments);
}

tree reference_smart_markup (tree arguments) {
  array<string> types;
  array<tree> groups;
  for (int i=0; i<N(arguments); ++i) {
    string type= reference_type (as_string (arguments[i]));
    int group=0;
    while (group < N(types) && types[group] != type) ++group;
    if (group == N(types)) {
      types << type;
      groups << compound (type * "-ref");
    }
    groups[group] << arguments[i];
  }
  return reference_list (groups);
}
