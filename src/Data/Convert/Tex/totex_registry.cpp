/******************************************************************************
* MODULE     : totex_registry.cpp
* DESCRIPTION: Resolved native registry data for LaTeX export
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
*******************************************************************************/

#include "totex_internal.hpp"

namespace {

using namespace latex_export_internal;

// Native LaTeX registry data.  This snapshot was imported from the legacy
// Scheme DRD/smart tables during the native converter migration and is now the
// runtime/source-of-truth database for both LaTeX export and import metadata.
static const char registry_blob[]=
#include "totex_registry_data.inc"
;

bool
true_stree (scheme_tree value) {
  return is_atomic (value) && !is_quoted (value->label) &&
         value->label == "#t";
}

int
integer_stree (scheme_tree value, int fallback) {
  if (!is_atomic (value) || is_quoted (value->label) ||
      !is_int (value->label)) return fallback;
  return as_int (value->label);
}

scheme_tree
field_value (scheme_tree root, string name) {
  if (!stree_list (root)) return tree ("#f");
  for (int i=1; i<N(root); ++i)
    if (head_is (root[i], name) && N(root[i]) > 1) return root[i][1];
  return tree ("#f");
}

LatexRegistryInfo
parse_info (scheme_tree value) {
  LatexRegistryInfo info;
  if (!stree_list (value) || N(value) < 5) return info;
  info.present= true;
  info.arity= integer_stree (value[0], -1);
  info.option= true_stree (value[1]);
  info.needs= true_stree (value[2]);
  info.body= value[3];
  info.preamble= value[4];
  return info;
}

struct RegistryState {
  hashmap<string,LatexRegistryInfo> commands;
  hashmap<string,LatexRegistryInfo> environments;

  RegistryState ():
    commands (LatexRegistryInfo ()),
    environments (LatexRegistryInfo ()) {}
};

void
load_info_table (scheme_tree values,
                 hashmap<string,LatexRegistryInfo>& target) {
  if (!stree_list (values)) return;
  for (int i=0; i<N(values); ++i) {
    scheme_tree entry= values[i];
    if (!stree_list (entry) || N(entry) < 2 || !string_atom (entry[0]))
      continue;
    target (atom_text (entry[0]))= parse_info (entry[1]);
  }
}

void
load_state (scheme_tree value, RegistryState& state) {
  if (!stree_list (value) || N(value) < 2) return;
  load_info_table (value[0], state.commands);
  load_info_table (value[1], state.environments);
}

struct RegistryData {
  RegistryState base;
  RegistryState article;
  RegistryState letter;
  RegistryState amsthm;
  RegistryState article_amsthm;
  RegistryState letter_amsthm;
  hashmap<string,array<string>> needs;
  hashmap<string,bool> symbols;
  hashmap<string,bool> operators;
  hashmap<string,string> import_types;
  hashmap<string,int> import_arities;
  hashmap<string,string> paper_opts;
  hashmap<string,string> paper_types;
  array<string> tag_names;
  array<string> symbol_names;
  array<string> import_names;

  RegistryData ():
    needs (array<string> ()), symbols (false), operators (false),
    import_types ("undefined"), import_arities (0),
    paper_opts ("undefined"), paper_types ("undefined") {
    scheme_tree root= string_to_scheme_tree (string (registry_blob));
    load_state (field_value (root, "base"), base);
    load_state (field_value (root, "article"), article);
    load_state (field_value (root, "letter"), letter);
    load_state (field_value (root, "amsthm"), amsthm);
    load_state (field_value (root, "article-amsthm"), article_amsthm);
    load_state (field_value (root, "letter-amsthm"), letter_amsthm);

    scheme_tree need_values= field_value (root, "needs");
    if (stree_list (need_values))
      for (int i=0; i<N(need_values); ++i) {
        scheme_tree entry= need_values[i];
        if (!stree_list (entry) || N(entry) < 2 || !string_atom (entry[0]) ||
            !stree_list (entry[1])) continue;
        array<string> packages;
        for (int j=0; j<N(entry[1]); ++j)
          if (string_atom (entry[1][j])) packages << atom_text (entry[1][j]);
        needs (atom_text (entry[0]))= packages;
      }

    scheme_tree tags= field_value (root, "tags");
    if (stree_list (tags))
      for (int i=0; i<N(tags); ++i)
        if (string_atom (tags[i])) tag_names << atom_text (tags[i]);

    scheme_tree import_values= field_value (root, "import-tags");
    if (stree_list (import_values))
      for (int i=0; i<N(import_values); ++i) {
        scheme_tree entry= import_values[i];
        if (!stree_list (entry) || N(entry) < 3 ||
            !string_atom (entry[0]) || !string_atom (entry[1])) continue;
        string name= atom_text (entry[0]);
        import_names << name;
        import_types (name)= atom_text (entry[1]);
        import_arities (name)= integer_stree (entry[2], 0);
      }

    scheme_tree paper_opt_values= field_value (root, "paper-opts");
    if (stree_list (paper_opt_values))
      for (int i=0; i<N(paper_opt_values); ++i) {
        scheme_tree entry= paper_opt_values[i];
        if (!stree_list (entry) || N(entry) < 2 ||
            !string_atom (entry[0]) || !string_atom (entry[1])) continue;
        string source= atom_text (entry[0]);
        string option= atom_text (entry[1]);
        // Historical latex-paper-opts queried the second DRD column and
        // returned the first match.  Keep that reverse lookup, including
        // first-entry wins for duplicate values such as "left".
        if (paper_opts[option] == "undefined") paper_opts (option)= source;
      }

    scheme_tree paper_type_values= field_value (root, "paper-types");
    if (stree_list (paper_type_values))
      for (int i=0; i<N(paper_type_values); ++i) {
        scheme_tree entry= paper_type_values[i];
        if (!stree_list (entry) || N(entry) < 2 ||
            !string_atom (entry[0]) || !string_atom (entry[1])) continue;
        string source= atom_text (entry[0]);
        string option= atom_text (entry[1]);
        if (paper_types[option] == "undefined") paper_types (option)= source;
      }

    scheme_tree symbol_values= field_value (root, "symbols");
    if (stree_list (symbol_values))
      for (int i=0; i<N(symbol_values); ++i)
        if (string_atom (symbol_values[i])) {
          string name= atom_text (symbol_values[i]);
          symbol_names << name;
          symbols (name)= true;
        }

    scheme_tree operator_values= field_value (root, "operators");
    if (stree_list (operator_values))
      for (int i=0; i<N(operator_values); ++i)
        if (string_atom (operator_values[i])) operators (atom_text (operator_values[i]))= true;
  }
};

RegistryData&
registry_data () {
  static RegistryData data;
  return data;
}

string
normalize_import_name (string name, bool& was_end) {
  was_end= false;
  if (starts (name, "\\")) name= name (1, N(name));
  // Scheme's latex-resolve maps the empty command name to the one-space
  // symbol before consulting the DRD.  Preserve that historical sentinel.
  if (name == "") name= " ";
  if (starts (name, "end-")) {
    name= "begin-" * name (4, N(name));
    was_end= true;
  }
  return name;
}

bool
depends_on (string package) {
  object deps= latex_export_latex_dependencies ();
  if (!is_list (deps)) return false;
  array<object> values= as_array_object (deps);
  for (int i=0; i<N(values); ++i)
    if (is_string (values[i]) && as_string (values[i]) == package) return true;
  return false;
}

RegistryState&
active_delta (RegistryData& data) {
  string style= latex_export_latex_style ();
  bool amsthm_enabled= depends_on ("amsthm");
  if (style == "article")
    return amsthm_enabled ? data.article_amsthm : data.article;
  if (style == "letter")
    return amsthm_enabled ? data.letter_amsthm : data.letter;
  return amsthm_enabled ? data.amsthm : data.base;
}

LatexRegistryInfo
merged_info (hashmap<string,LatexRegistryInfo>& base,
             hashmap<string,LatexRegistryInfo>& delta,
             string name) {
  LatexRegistryInfo result= base[name];
  LatexRegistryInfo override= delta[name];
  if (override.present) return override;
  return result;
}

} // namespace

string
latex_native_paper_opts (string cmd) {
  return registry_data ().paper_opts[cmd];
}

string
latex_native_paper_type (string cmd) {
  return registry_data ().paper_types[cmd];
}

string
latex_native_type (string cmd) {
  bool was_end= false;
  string name= normalize_import_name (cmd, was_end);
  (void) was_end;
  return registry_data ().import_types[name];
}

int
latex_native_arity (string cmd) {
  bool was_end= false;
  string name= normalize_import_name (cmd, was_end);
  if (was_end) return 0;
  return registry_data ().import_arities[name];
}

array<string>
latex_native_tags () {
  return registry_data ().import_names;
}

namespace latex_export_internal {

LatexRegistryInfo
registry_command_info (string name) {
  RegistryData& data= registry_data ();
  RegistryState& delta= active_delta (data);
  return merged_info (data.base.commands, delta.commands, name);
}

LatexRegistryInfo
registry_environment_info (string name) {
  RegistryData& data= registry_data ();
  RegistryState& delta= active_delta (data);
  return merged_info (data.base.environments, delta.environments, name);
}

array<string>
registry_command_needs (string name) {
  return registry_data ().needs[name];
}

bool
registry_symbol_known (string name) {
  return registry_data ().symbols[name];
}

bool
registry_operator_known (string name) {
  return registry_data ().operators[name];
}

bool
registry_latex_name_known (string name) {
  if (starts (name, "\\")) name= name (1, N(name));
  if (starts (name, "end-")) name= "begin-" * name (4, N(name));
  array<string> names= registry_data ().tag_names;
  for (int i=0; i<N(names); ++i)
    if (names[i] == name) return true;
  return false;
}

array<string>
registry_tag_names () {
  return registry_data ().tag_names;
}

array<string>
registry_symbol_names () {
  return registry_data ().symbol_names;
}

} // namespace latex_export_internal
