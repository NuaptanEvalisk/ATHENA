// Native source editing, macro lookup and parameter/theme analysis.
// Copyright (C) 1999-2013 Joris van der Hoeven, 2026 ATHENA contributors.
// GPL-3.0-or-later.
#include "insertion_commands.hpp"
#include "source_commands.hpp"
#include "editor.hpp"
#include "native_interfaces.hpp"
#include "document_commands.hpp"
#include "document_style_commands.hpp"
#include "format_commands.hpp"
#include "generic_editor_commands.hpp"
#include "ATHENA/Data/application_lifecycle.hpp"
#include "ATHENA/Data/compound_edit_batch.hpp"
#include "buffer_name_catalog.hpp"
#include "new_buffer.hpp"
#include "new_view.hpp"
#include "qt_utilities.hpp"
#include "Data/Convert/Xml/athena_document_xml.hpp"
#include "new_style.hpp"
#include "file.hpp"
#include "analyze.hpp"
#include "tree_traverse.hpp"
#include <map>
#include <set>
#include <vector>
#include <algorithm>

namespace {
using names= std::set<string>;
std::vector<object> objects (object value) {
  auto a= as_array_object (value);
  std::vector<object> out;
  for (int i= 0; i < N (a); ++i)
    out.push_back (a[i]);
  return out;
}
string name_of (object x) {
  return is_symbol (x) ? as_symbol (x) : as_string (x);
}
bool tag (tree t, const char* name) {
  return is_compound (t, name);
}
tree definition (string name) {
  return get_current_editor ()->get_init_value (name);
}
bool macro (tree t) {
  return is_func (t, MACRO) || is_func (t, XMACRO);
}
bool containers (tree t) {
  return is_document (t) || is_concat (t) || is_func (t, SURROUND) || is_func (t, WITH);
}
tree explicit_compound (tree t) {
  if (is_func (t, COMPOUND) && N (t) > 0 && is_atomic (t[0])) {
    tree out (as_tree_label (t[0]->label));
    for (int i= 1; i < N (t); ++i)
      out << t[i];
    return out;
  }
  return t;
}
void collect_parameters (string, names&, names&);
void collect_sub (tree, names&, names&);
void assigned (string name, names& values, names& visited);
void assigned_sub (tree t, names& values, names& visited) {
  if (is_atomic (t))
    return;
  t= explicit_compound (t);
  if (is_func (t, WITH) && N (t) % 2 == 1) {
    assigned_sub (t[N (t) - 1], values, visited);
    for (int i= N (t) - 3; i >= 0; i-= 2) {
      names used, seen;
      collect_sub (t[i], used, seen);
      collect_sub (t[i + 1], used, seen);
      if (is_atomic (t[i]))
        values.insert (t[i]->label);
      for (auto s : used)
        values.erase (s);
    }
  } else if (N (t) == 1)
    assigned (as_string (L (t)), values, visited);
}
void assigned (string name, names& values, names& visited) {
  if (!visited.insert (name).second)
    return;
  tree def= definition (name);
  if (macro (def) && N (def))
    assigned_sub (def[N (def) - 1], values, visited);
}
void collect_sub (tree t, names& values, names& visited) {
  if (is_atomic (t))
    return;
  t= explicit_compound (t);
  if ((is_func (t, VALUE) || is_func (t, QUOTE_VALUE)) && N (t) && is_atomic (t[0])) {
    values.insert (t[0]->label);
    return;
  }
  if (is_func (t, WITH) && N (t) % 2 == 1) {
    names local;
    for (int i= 0; i < N (t); ++i)
      collect_sub (t[i], local, visited);
    for (int i= 0; i + 1 < N (t); i+= 2)
      if (is_atomic (t[i]))
        local.erase (t[i]->label);
    values.insert (local.begin (), local.end ());
    return;
  }
  string name= as_string (L (t));
  if (generic_tree_label_parameter (L (t)))
    values.insert (name);
  names remove;
  if (is_extension (t)) {
    names seen;
    assigned (name, remove, seen);
    for (auto s : values)
      remove.erase (s);
  }
  collect_parameters (name, values, visited);
  for (int i= 0; i < N (t); ++i)
    collect_sub (t[i], values, visited);
  for (auto s : remove)
    values.erase (s);
}
void collect_parameters (string name, names& values, names& visited) {
  if (!visited.insert (name).second)
    return;
  auto standard= format_standard_parameters (name);
  if (standard) {
    for (int i=0; i<N(*standard); ++i)
      values.insert ((*standard)[i]);
    return;
  }
  tree def= definition (name);
  if (is_func (def, UNINIT))
    return;
  if (macro (def))
    collect_sub (def, values, visited);
  else
    values.insert (name);
}
using option_map= std::map<string, std::vector<string>>;
void append_unique (std::vector<string>& out, const std::vector<string>& values) {
  for (auto s : values)
    if (std::find (out.begin (), out.end (), s) == out.end ())
      out.push_back (s);
}
std::vector<string> collect_options (string, option_map&);
std::vector<string> options_sub (tree t, option_map& cache) {
  if (is_atomic (t))
    return {};
  t= explicit_compound (t);
  auto out= collect_options (as_string (L (t)), cache);
  for (int i= 0; i < N (t); ++i)
    append_unique (out, options_sub (t[i], cache));
  return out;
}
std::vector<string> collect_options (string name, option_map& cache) {
  auto found= cache.find (name);
  if (found != cache.end ())
    return found->second;
  cache[name]= {};
  auto options= format_standard_options (name);
  auto out= std::move (options.packages);
  tree def= definition (name);
  if (options.recurse && macro (def))
    append_unique (out, options_sub (def, cache));
  cache[name]= out;
  return out;
}
tree find_definition (tree t, string name, bool preamble_only) {
  if ((tag (t, "show-preamble") || tag (t, "hide-preamble")) && N (t) == 1)
    return find_definition (t[0], name, false);
  if (!preamble_only && is_func (t, ASSIGN, 2) && is_atomic (t[0]) &&
      t[0]->label == name)
    return t;
  if (containers (t))
    for (int j= 0; j < N (t); ++j) {
      int i= preamble_only ? j : N (t) - 1 - j;
      tree found= find_definition (t[i], name, preamble_only);
      if (is_func (found, ASSIGN, 2))
        return found;
    }
  return tree ();
}
tree style_body (string name) {
  string file= (ends (name, ".ats") || ends (name, ".ts")) ? name : name * ".ats";
  url local= relative (get_current_editor ()->get_name (), file);
  return load_style_tree (exists (local) ? as_string (local) : name);
}
string style_definition (tree t, string name, string current, names& visited) {
  if (is_func (t, ASSIGN, 2) && is_atomic (t[0]) && t[0]->label == name)
    return current;
  if (is_func (t, USE_PACKAGE))
    for (int i= N (t) - 1; i >= 0; --i)
      if (is_atomic (t[i]) && visited.insert (t[i]->label).second) {
        string result=
            style_definition (style_body (t[i]->label), name, t[i]->label, visited);
        if (result != "")
          return result;
      }
  if (containers (t))
    for (int i= N (t) - 1; i >= 0; --i) {
      string result= style_definition (t[i], name, current, visited);
      if (result != "")
        return result;
    }
  return "";
}
string macro_package (string name) {
  auto styles= document_get_style_list ();
  names visited;
  for (int i= N (styles) - 1; i >= 0; --i) {
    string pack= styles[i];
    if (!visited.insert (pack).second)
      continue;
    string found= style_definition (style_body (pack), name, pack, visited);
    if (found != "")
      return found;
  }
  return "";
}
void position_definition (editor_rep& ed, tree def) {
  tree body= def[1];
  if (is_func (body, MACRO) && N (body))
    body= body[N (body) - 1];
  ed.go_to_start (reverse (obtain_ip (body)));
}
void list_macros (tree t, names& out) {
  if (is_func (t, ASSIGN, 2) && is_atomic (t[0]) && macro (t[1]))
    out.insert (t[0]->label);
  else if (containers (t))
    for (int i= 0; i < N (t); ++i)
      list_macros (t[i], out);
}
} // namespace

void source_make_latex () {
  auto ed= get_current_editor ();
  if (ed->selection_active_any ()) insertion_make (as_tree_label ("latex"));
  else ed->make_compound (LATEX);
  ed->set_message ("Type a latex command followed by return", "latex");
}
void source_activate_compound () {
  auto ed= get_current_editor ();
  path p= ed->search_upwards (COMPOUND);
  if (is_nil (p))
    return;
  tree t= ed->the_subtree (p);
  if (!is_func (ed->the_subtree (path_up (p)), INACTIVE))
    tree_insert_node (t, 0, tree (INACTIVE));
  ed->activate ();
}
bool source_enter (tree t) {
  auto ed= get_current_editor ();
  if (is_func (t, HYBRID))
    ed->activate_hybrid (false);
  else if (is_func (t, COMPOUND))
    source_activate_compound ();
  else if (is_func (t, LATEX))
    ed->activate_latex ();
  else if (is_func (t, SYMBOL))
    ed->activate_symbol ();
  else if (is_func (t, INACTIVE))
    ed->activate ();
  else
    return false;
  return true;
}
bool source_variant (tree t, bool forward) {
  auto ed= get_current_editor ();
  if (is_func (t, HYBRID)) {
    ed->activate_hybrid (true);
    return true;
  }
  if (is_func (t, INACTIVE) || is_tuple (t) || is_func (t, ATTR) || ed->in_source ()) {
    ed->insert_argument (forward);
    return true;
  }
  return false;
}
void source_inactive_toggle (tree t) {
  auto ed= get_current_editor ();
  path ip= obtain_ip (t);
  if (!ip_attached (ip))
    return;
  path p= reverse (ip);
  bool inactive= is_func (t, INACTIVE) ||
                 (!is_nil (p) && is_func (ed->the_subtree (path_up (p)), INACTIVE));
  if (inactive) {
    if (!source_enter (t))
      ed->activate ();
  } else {
    tree original= t;
    tree_insert_node (t, 0, tree (INACTIVE));
    generic_notify_disactivated (original);
  }
}
void source_remove_unary_document () {
  auto ed= get_current_editor ();
  path p= ed->search_upwards (DOCUMENT);
  if (N (p) > 1 && is_func (ed->the_subtree (p), DOCUMENT, 1))
    tree_remove_node (ed->the_subtree (p), 0);
}
std::vector<string> source_search_options (string label) {
  option_map cache;
  return collect_options (label, cache);
}
array<string> source_search_parameters (string name) {
  if (name == "reference" || name == "pageref" || name == "eqref" ||
      name == "smart-ref" || name == "hlink")
    name= "locus";
  names values, visited;
  collect_parameters (name, values, visited);
  array<string> result;
  for (const auto& value: values) result << value;
  return result;
}
std::optional<array<string>> source_theme_members (string theme) {
  tree def= definition ("with-" * theme);
  if (!is_func (def, MACRO, 2) || !is_func (def[1], WITH))
    return std::nullopt;
  tree body= def[1];
  array<string> out;
  for (int i= 0; i + 1 < N (body); i+= 2) {
    if (!is_atomic (body[i]) || !is_func (body[i + 1], VALUE, 1) ||
        !is_atomic (body[i + 1][0]) ||
        body[i + 1][0]->label != theme * "-" * body[i]->label)
      return std::nullopt;
    out << body[i]->label;
  }
  return out;
}
std::optional<string> source_member_theme (string variable) {
  for (int i= N (variable) - 1; i >= 0; --i)
    if (variable[i] == '-') {
      const auto members= source_theme_members (variable (0, i));
      if (members)
        for (int j=0; j<N(*members); ++j)
          if ((*members)[j] == variable (i + 1, N (variable)))
            return variable (0, i);
    }
  return std::nullopt;
}
array<string> source_search_themes (string label) {
  array<string> out;
  names seen;
  const auto parameters= source_search_parameters (label);
  for (int i=0; i<N(parameters); ++i) {
    const auto theme= source_member_theme (parameters[i]);
    if (theme && seen.insert (*theme).second) out << *theme;
  }
  return out;
}
bool source_has_macro (object label) {
  string name= name_of (label);
  return is_func (find_definition (get_current_editor ()->the_buffer (), name, true),
                  ASSIGN, 2) ||
         macro_package (name) != "";
}
tree source_get_definition (object label) {
  string name= name_of (label);
  tree found= find_definition (get_current_editor ()->the_buffer (), name, true);
  return is_func (found, ASSIGN, 2) ? found : tree (ASSIGN, name, definition (name));
}
void source_edit_package (string name) {
  application_browse_document (document_url_resolve_package (name),
                               std::function<void (url)> ());
}
void source_edit_style () {
  auto styles= document_get_style_list ();
  if (N (styles))
    source_edit_package (styles[0]);
}
void source_edit_macro (object label) {
  string name= name_of (label);
  auto ed= get_current_editor ();
  tree found= find_definition (ed->the_buffer (), name, true);
  if (is_func (found, ASSIGN, 2)) {
    document_buffer_show_preamble ();
    found= find_definition (ed->the_buffer (), name, true);
    position_definition (*ed.operator->(), found);
    return;
  }
  string pack= macro_package (name);
  if (pack == "")
    return;
  const std::string target (name.data (), N (name));
  application_browse_document (
      document_url_resolve_package (pack), [target] (url loaded) {
        string name= as_string (loaded);
        auto owner= published_buffer_source (std::string (name.data (), N (name)));
        athena::avd::submit_source_task (
            owner.first, owner.second,
            [target] (editor_rep& destination) {
              tree def= find_definition (destination.the_buffer (),
                                         string (target.c_str ()), false);
              if (is_func (def, ASSIGN, 2))
                position_definition (destination, def);
            },
            [] (std::string error) {
              if (!error.empty ())
                std_warning << "Macro source: " << string (error.c_str ()) << LF;
            });
      });
}
void source_edit_focus_macro () {
  auto ed= get_current_editor ();
  tree t= ed->the_subtree (ed->focus_get ());
  string name= is_atomic (t) ? string ("string") : as_string (L (t));
  if (is_func (t, COMPOUND) && N (t) && is_atomic (t[0]))
    name= t[0]->label;
  source_edit_macro (object (name));
}
array<string> source_personal_macros () {
  array<string> result;
  names preamble;
  list_macros (document_buffer_get_preamble (), preamble);
  int n= 0;
  for (auto name : preamble) {
    if (n++ == 20)
      break;
    result << string ("") << name << name;
  }
  auto styles= document_get_style_list ();
  for (int i= 0; i < N (styles); ++i) {
    string pack= styles[i];
    if (i == 0) {
      url builtin= complete (
          url ("$ATHENA_PATH/styles") * url_wildcard () * url (pack * ".ats"), "fr");
      url legacy= complete (
          url ("$ATHENA_PATH/styles") * url_wildcard () * url (pack * ".ts"), "fr");
      if (!is_none (builtin) || !is_none (legacy))
        continue;
    } else if (hidden_package (pack))
      continue;
    names macros;
    list_macros (style_body (pack), macros);
    n= 0;
    for (auto name : macros) {
      if (n++ == 30)
        break;
      result << pack << name << name;
    }
  }
  return result;
}
tree source_render_edit_macro (tree t) {
  if (N (t) < 2)
    return "";
  tree name= t[0], body= t[N (t) - 1];
  bool unnamed= is_atomic (name) && name->label == "";
  if (unnamed)
    name=
        tree (CONCAT, tree (WITH, "color", "red", "font-shape", "italic", "enter-name"),
              name);
  if (unnamed && is_atomic (body) && body->label == "")
    body=
        tree (CONCAT, tree (WITH, "color", "red", "font-shape", "italic", "enter-body"),
              body);
  tree args= compound ("inline-tag", name);
  for (int i= 1; i < N (t) - 1; ++i)
    args << compound ("src-arg", t[i]);
  return tree (WITH, "par-first", "0em", "par-par-sep", "0.5em",
               tree (DOCUMENT, tree (CONCAT, args, " ", compound ("math", "≔")), body));
}
void source_extract_style (bool style) {
  auto ed= get_current_editor ();
  string name= basename (ed->get_name ());
  tree title= compound (
      "active*",
      tree (DOCUMENT,
            compound (
                "src-title",
                tree (DOCUMENT,
                      compound (style ? "src-style-file" : "src-package",
                                name * "-macros", "1.0"),
                      compound ("src-purpose",
                                style ? "Automatically generated style file."
                                      : "Automatically generated style package.")))));
  tree body (DOCUMENT, title), packages (USE_PACKAGE);
  auto styles= document_get_style_list ();
  for (int i= style ? 0 : 1; i < N (styles); ++i)
    packages << tree (styles[i]);
  if (N (packages))
    body << packages;
  tree initial= ed->get_init_all ();
  std::map<string, tree> parameters;
  for (int i= 0; i < N (initial); ++i)
    if (is_func (initial[i], ASSOCIATE, 2) && is_atomic (initial[i][0])) {
      string key= initial[i][0]->label;
      if (key != "zoom-factor" && key != "sfactor" && key != "preamble" &&
          key != "page-screen-width" && key != "page-screen-height")
        parameters.emplace (key, initial[i][1]);
    }
  auto comment= [] (const char* text) {
    return compound ("active*",
                     tree (DOCUMENT, compound ("src-comment", tree (DOCUMENT, text))));
  };
  if (!parameters.empty ()) {
    body << comment ("Style parameters.");
    for (auto& value : parameters)
      body << tree (ASSIGN, value.first, value.second);
  }
  tree preamble= document_buffer_get_preamble ();
  if (is_document (preamble) && N (preamble)) {
    body << comment ("Macro definitions.");
    for (int i= 0; i < N (preamble); ++i)
      body << preamble[i];
  }
  tree document (DOCUMENT, compound ("style", tree (TUPLE, "source")),
                 compound ("body", body));
  const std::string encoded=
      athena::document::write_xml_v2 (tree_duplicate_source (document));
  qt_post_to_main_thread ([encoded] {
    url target= make_new_source_buffer ();
    set_buffer_tree (target, athena::document::read_xml_v2 (encoded));
    switch_to_buffer (target);
  });
}
