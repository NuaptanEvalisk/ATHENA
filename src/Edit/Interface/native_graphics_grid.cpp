// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#include "native_graphics_grid.hpp"
#include "edit_graphics.hpp"
#include "Typeset/Env/local_environment.hpp"
#include "convert.hpp"
#include "server.hpp"
#include <algorithm>
#include <map>
#include <optional>
#include <stdexcept>

namespace {

const char* const grid_properties[] {
  "gr-grid", "gr-grid-old", "gr-edit-grid", "gr-edit-grid-old",
  "gr-grid-aspect", "gr-edit-grid-aspect", "gr-grid-aspect-props",
  "gr-as-visual-grid", "gr-frame"
};

string grid_key (bool visual) { return visual ? "gr-grid" : "gr-edit-grid"; }
string aspect_key (bool visual) { return visual ? "gr-grid-aspect" : "gr-edit-grid-aspect"; }

string grid_type (tree value) {
  return is_tuple (value) && N(value) > 0 && is_atomic (value[0]) ?
    value[0]->label : string ("");
}

struct grid_parameters {
  string type= "empty";
  tree center= tree (_POINT, "0", "0");
  tree step, angular, base= "10";

  grid_parameters (tree value, bool visual):
    step (visual ? "1" : "0.1"), angular (visual ? "24" : "240") {
    const string name= grid_type (value);
    if (name == "empty") return;
    if (name != "cartesian" && name != "polar" && name != "logarithmic") return;
    type= name;
    if (name == "logarithmic") step= "1";
    if (name == "cartesian") {
      if (N(value) == 2) step= value[1];
      else if (N(value) == 3) { center= value[1]; step= value[2]; }
    }
    else {
      if (N(value) >= 2) step= value[1];
      if (N(value) == 4) { center= value[1]; step= value[2]; }
      if (N(value) >= 3) {
        if (name == "polar") angular= value[N(value)-1];
        else base= value[N(value)-1];
      }
    }
  }

  tree value () const {
    if (type == "empty") return tuple ("empty");
    if (type == "cartesian") return tuple (type, center, step);
    if (type == "polar") return tuple (type, center, step, angular);
    if (type == "logarithmic") return tuple (type, center, step, base);
    throw std::runtime_error ("Unknown graphics grid type");
  }
};

bool aspect_shape (tree value) {
  if (!is_tuple (value) || N(value) < 2) return false;
  for (int i=0; i<N(value); ++i)
    if (!is_tuple (value[i]) || N(value[i]) != 2 || !is_atomic (value[i][0]))
      return false;
  return true;
}

tree sorted_aspect (tree value, tree fallback) {
  if (!aspect_shape (value)) value= fallback;
  if (!aspect_shape (value)) throw std::runtime_error ("Invalid graphics grid aspect");
  std::vector<tree> items;
  for (int i=0; i<N(value); ++i) {
    if (value[i][0] != "axes" && !is_double (value[i][0]->label))
      throw std::runtime_error ("Invalid graphics grid subdivision");
    items.push_back (value[i]);
  }
  std::stable_sort (items.begin (), items.end (), [] (tree a, tree b) {
    if (a[0] == "axes") return b[0] != "axes";
    if (b[0] == "axes") return false;
    return as_double (a[0]->label) < as_double (b[0]->label);
  });
  tree result (TUPLE);
  for (const auto& item: items) result << copy (item);
  return result;
}

class grid_plan {
  edit_graphics_rep& editor;
  path graphics;
  std::map<string, tree> values, authored, initial;
  std::map<string, std::optional<tree>> changes;

  void write (string name, tree value) {
    values.insert_or_assign (name, value);
    authored.insert_or_assign (name, value);
    changes.insert_or_assign (name, std::move (value));
  }

  void erase (string name) {
    if (authored.at (name) != tree (UNINIT)) changes.insert_or_assign (name, std::nullopt);
  }

  void put_grid (bool visual, const grid_parameters& parameters) {
    const string key= grid_key (visual), old= key * "-old";
    const tree value= parameters.value ();
    if (grid_type (values.at (key)) == "empty" &&
        grid_type (values.at (old)) == parameters.type)
      write (key, values.at (old));
    else {
      write (key, value);
      if (parameters.type != "empty") write (old, value);
    }
  }

  tree aspect (bool visual) const {
    const string key= aspect_key (visual);
    return sorted_aspect (values.at (key), initial.at (key));
  }

  tree stored_aspect () const {
    return sorted_aspect (values.at ("gr-grid-aspect-props"), aspect (true));
  }

  tree default_subdivision (bool visual) const {
    const tree value= sorted_aspect (initial.at (aspect_key (visual)), tree (UNINIT));
    if (N(value) < 3) throw std::runtime_error ("Grid defaults have no subdivision");
    return value[2][0];
  }

  void set_aspect (string type, tree subdivision, bool visual) {
    if (visual) {
      tree full= stored_aspect ();
      if (type == "units-only") {
        write ("gr-grid-aspect-props", full);
        write ("gr-grid-aspect", tuple (full[0], full[1]));
      }
      else if (type == "detailed") {
        if (subdivision == "") subdivision= default_subdivision (true);
        if (N(full) == 2) {
          const tree defaults= sorted_aspect (initial.at ("gr-grid-aspect"), tree (UNINIT));
          if (N(defaults) < 3) throw std::runtime_error ("Grid defaults have no subdivision");
          full << copy (defaults[2]);
        }
        full[2][0]= copy (subdivision);
        write ("gr-grid-aspect", full);
        write ("gr-grid-aspect-props", full);
      }
      else throw std::runtime_error ("Unknown graphics grid aspect mode");
    }
    else {
      if (subdivision == "") subdivision= default_subdivision (false);
      write ("gr-edit-grid-aspect", tuple (
        tuple ("axes", "none"), tuple ("1", "none"), tuple (subdivision, "none")));
      if (type != "update") write ("gr-as-visual-grid", "off");
    }
  }

  void synchronize () {
    const grid_parameters visual (values.at ("gr-grid"), true);
    if (visual.type != "logarithmic") {
      const tree shown= aspect (true), full= stored_aspect ();
      const tree subdivision= N(shown) > 2 ? full[N(full)-1][0] : tree ("1");
      set_aspect ("update", subdivision, false);
    }
    put_grid (false, visual);
  }

  bool linked () const { return values.at ("gr-as-visual-grid") != "off"; }
  void synchronize_if_linked () { if (linked ()) synchronize (); }

  bool defaulted () const {
    for (const auto* name: grid_properties)
      if (string (name) != "gr-as-visual-grid" && string (name) != "gr-frame" &&
          authored.at (name) != tree (UNINIT)) return false;
    return true;
  }

  bool enabled (bool visual) const {
    const tree value= authored.at (grid_key (visual));
    return value != tree (UNINIT) && grid_type (value) != "empty";
  }

  void toggle (bool visual) {
    const string key= grid_key (visual), old= key * "-old";
    const tree value= authored.at (key), saved= authored.at (old);
    if (saved != tree (UNINIT))
      write (key, value == tree (UNINIT) || grid_type (value) == "empty" ? saved : tuple ("empty"));
    else if (value != tree (UNINIT)) {
      write (key, tuple ("empty"));
      write (old, value);
    }
  }

  void set_type (string type, bool visual) {
    if (!visual && (type == "default" || type == "grid-change")) { synchronize (); return; }
    grid_parameters grid (values.at (grid_key (visual)), visual);
    if (type == "logarithmic" && grid.type == "empty") grid.step= "1";
    if (visual && type == "polar" && grid.type != "polar") {
      const tree shown= aspect (true);
      set_aspect ("detailed", N(shown) > 2 ? shown[2][0] : tree (""), true);
      grid.angular= "24";
    }
    grid.type= type;
    if (!visual) write ("gr-as-visual-grid", "off");
    put_grid (visual, grid);
    if (visual) synchronize_if_linked ();
  }

public:
  explicit grid_plan (edit_graphics_rep& owner): editor (owner) {
    graphics= editor.native_graphics_canvas_path ();
    if (is_nil (graphics)) throw std::runtime_error ("No graphics canvas selected");
    const auto context= editor.cursor_context_at (graphics * 0);
    if (!context) throw std::runtime_error ("Graphics canvas is awaiting layout");
    for (const auto* key: grid_properties) {
      values.emplace (key, context->effective_value (key));
      authored.emplace (key, editor.native_ink_property (graphics, key, tree (UNINIT)));
    }
    initial.emplace ("gr-grid-aspect", editor.get_init_value ("gr-grid-aspect"));
    initial.emplace ("gr-edit-grid-aspect", editor.get_init_value ("gr-edit-grid-aspect"));
  }

  tree query (string property, bool visual) const {
    if (property == "linked") return bool_as_tree (linked ());
    if (property == "defaulted") return bool_as_tree (defaulted ());
    if (property == "enabled") return bool_as_tree (enabled (visual));
    if (property == "subunits") return bool_as_tree (N(aspect (visual)) > 2);
    if (property == "aspect") return aspect (visual);
    if (property == "aspect-props") return stored_aspect ();
    if (property == "default-aspect") return sorted_aspect (initial.at (aspect_key (visual)), tree (UNINIT));
    const grid_parameters grid (values.at (grid_key (visual)), visual);
    if (property == "type") return grid.type;
    if (property == "center") return grid.center;
    if (property == "step") return grid.step;
    if (property == "astep") return grid.angular;
    if (property == "base") return grid.base;
    throw std::runtime_error ("Unknown graphics grid query");
  }

  void update (string operation, tree argument, bool visual) {
    if (operation == "type") set_type (as_string (argument), visual);
    else if (operation == "center" || operation == "step" || operation == "astep" || operation == "base") {
      grid_parameters grid (values.at (grid_key (visual)), visual);
      if (operation == "center") grid.center= argument;
      else if (operation == "step") grid.step= argument;
      else if (operation == "astep") grid.angular= argument;
      else grid.base= argument;
      if (!visual) write ("gr-as-visual-grid", "off");
      put_grid (visual, grid);
      if (visual) synchronize_if_linked ();
    }
    else if (operation == "aspect") {
      if (!is_tuple (argument) || N(argument) != 2) throw std::runtime_error ("Expected grid aspect mode and subdivision");
      set_aspect (as_string (argument[0]), argument[1], visual);
      if (visual) synchronize_if_linked ();
    }
    else if (operation == "aspect-properties") {
      const tree value= sorted_aspect (argument, tree (UNINIT));
      write ("gr-grid-aspect", value);
      write ("gr-grid-aspect-props", value);
      synchronize_if_linked ();
    }
    else if (operation == "color") {
      if (!is_tuple (argument) || N(argument) != 2) throw std::runtime_error ("Expected grid color component and value");
      const int index= argument[0] == "axes" ? 0 : argument[0] == "units" ? 1 : argument[0] == "subunits" ? 2 : -1;
      if (index < 0) throw std::runtime_error ("Unknown graphics grid color component");
      tree current= aspect (true), full= stored_aspect ();
      const tree defaults= sorted_aspect (initial.at ("gr-grid-aspect"), tree (UNINIT));
      if (argument[1] == "default" && N(defaults) <= index)
        throw std::runtime_error ("Grid defaults have no requested color component");
      if (N(full) <= index) {
        if (N(defaults) <= index) throw std::runtime_error ("Grid defaults have no requested color component");
        full << copy (defaults[index]);
      }
      const tree color= argument[1] == "default" ? defaults[index][1] : argument[1];
      full[index][1]= copy (color);
      write ("gr-grid-aspect-props", full);
      if (N(current) > index) { current[index][1]= copy (color); write ("gr-grid-aspect", current); }
    }
    else if (operation == "link") {
      write ("gr-as-visual-grid", linked () ? "off" : "on");
      synchronize_if_linked ();
    }
    else if (operation == "subunits") {
      set_aspect (N(aspect (true)) > 2 ? "units-only" : "detailed", "", true);
      synchronize_if_linked ();
    }
    else if (operation == "toggle") toggle (visual);
    else if (operation == "toggle-both") {
      if (defaulted ()) set_type ("cartesian", true);
      else { toggle (true); toggle (false); }
    }
    else if (operation == "reset") {
      erase ("gr-grid"); erase ("gr-grid-old");
      if (linked ()) {
        erase ("gr-edit-grid"); erase ("gr-edit-grid-old");
        erase ("gr-edit-grid-aspect"); erase ("gr-grid-aspect"); erase ("gr-grid-aspect-props");
      }
    }
    else if (operation == "notebook") {
      set_type ("cartesian", true);
      set_aspect ("detailed", "2", true);
      tree value= aspect (true);
      for (int i=0; i<N(value); ++i) value[i][1]= "#e0e0ff";
      write ("gr-grid-aspect", value); write ("gr-grid-aspect-props", value);
      synchronize_if_linked ();
      const tree frame= values.at ("gr-frame");
      if (!is_tuple (frame, "scale", 2)) throw std::runtime_error ("Invalid graphics coordinate frame");
      write ("gr-frame", tuple ("scale", "1cm", frame[2]));
    }
    else throw std::runtime_error ("Unknown graphics grid operation");
  }

  void commit () {
    if (changes.empty ()) return;
    std::vector<std::pair<string, tree>> assignments;
    for (const auto& [name, value]: changes)
      if (value) assignments.push_back ({name, *value});
    editor.start_editing ();
    for (const auto& [name, value]: changes)
      if (!value) editor.native_graphics_remove_property (name);
    if (!assignments.empty ())
      editor.native_drawing_set_graphics_properties (editor.native_graphics_canvas_path (), assignments);
    editor.end_editing ();
    editor.mark_native_ink_interaction_dirty ();
    editor.refresh_native_ink_interaction ();
    editor.refresh_native_drawing_properties_snapshot ();
    editor.publish_native_drawing_focus_refresh ();
    editor.invalidate_all ();
  }
};

edit_graphics_rep& current_graphics () {
  editor current= get_current_editor ();
  if (is_nil (current)) throw std::runtime_error ("No editor for graphics grid command");
  return dynamic_cast<edit_graphics_rep&> (*current.operator-> ());
}

} // namespace

scheme_tree native_graphics_grid_query (string property, bool visual) {
  const grid_plan state (current_graphics ());
  return tree_to_scheme_tree (state.query (property, visual));
}

void native_graphics_grid_command (string operation, scheme_tree argument, bool visual) {
  grid_plan plan (current_graphics ());
  plan.update (operation, scheme_tree_to_tree (argument), visual);
  plan.commit ();
}
