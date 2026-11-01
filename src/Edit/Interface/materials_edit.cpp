// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#include "materials_edit.hpp"
#include "native_interfaces.hpp"
#include "editor.hpp"
#include "tree_traverse.hpp"
#include "new_buffer.hpp"
#include "ATHENA/Data/materials_document.hpp"
#include "Subsystems/Qt/QTMMaterialCitationDialog.hpp"
#include "guile_tm.hpp"
#include "object.hpp"
#include <memory>
#include <set>

namespace {
string citation_style () {
  editor ed= get_current_editor ();
  return ed->defined_in_init ("materials-csl-style")?
    ed->get_init_string ("materials-csl-style"):
    get_preference ("materials csl style", "springer-mathphys");
}

tree focused_references () {
  editor ed= get_current_editor ();
  const path root= ed->the_buffer_path ();
  path p= path_up (ed->the_path ());
  while (root <= p) {
    tree t= ed->the_subtree (p);
    if (is_compound (t, "referenced-materials", 3)) return t;
    if (p == root) break;
    p= path_up (p);
  }
  return UNINIT;
}

// Retain an attached node without copying owner-local trees through Qt.
struct reference_target {
  athena_scheme_handle_id handle;
  explicit reference_target (tree node):
    handle (scheme_command_handle_acquire (object_to_tmscm (object (node)))) {}
  ~reference_target () { scheme_command_handle_release (handle); }
  tree get () const {
    return as_tree (tmscm_to_object (scheme_command_handle_value (handle)));
  }
};
}

void materials_update_current_document () {
  editor ed= get_current_editor ();
  if (!vault_active ()) { ed->set_message ("No active vault", "Materials"); return; }
  string style= citation_style ();
  std::string error;
  tree updated= athena_materials_update_for_file (
    ed->the_buffer (), get_current_buffer_safe (),
    std::string (style.data (), N(style)), error);
  if (!error.empty ()) {
    ed->set_message (string (error.data (), error.size ()), "Materials error");
    return;
  }
  tree body= ed->the_buffer ();
  tree_assign (body, updated);
  ed->set_message ("Updated Material citations and referenced list", "Materials");
}

void materials_set_document_style (string style) {
  editor ed= get_current_editor ();
  ed->init_env ("materials-csl-style", style);
  if (vault_active ()) materials_update_current_document ();
  else ed->set_message ("Citation style: " * style, "Materials");
}

void materials_use_default_style () {
  editor ed= get_current_editor ();
  ed->init_default ("materials-csl-style");
  if (vault_active ()) materials_update_current_document ();
  else ed->set_message ("Citation style follows Preferences", "Materials");
}

void materials_insert_citation () {
  if (!vault_active ()) {
    get_current_editor ()->set_message (
      "Load a vault before inserting a Material citation", "Materials");
    return;
  }
  qtm_material_choose_citation_async (citation_style (), [] (tree result) {
    if (!is_compound (result) || L(result) != TUPLE || N(result) < 3) return;
    tree rendered= result[2] == ""? result[1]: compound ("hlink", result[1], result[2]);
    get_current_editor ()->insert_tree (compound ("material-citation", result[0], rendered));
  });
}

void materials_insert_references () {
  if (!vault_active ()) {
    get_current_editor ()->set_message (
      "Load a vault before inserting referenced Materials", "Materials");
    return;
  }
  qtm_material_choose_references_async ([] (tree manual) {
    if (is_atomic (manual) || L(manual) != TUPLE) return;
    get_current_editor ()->insert_tree (compound (
      "referenced-materials", "", manual, tree (DOCUMENT, "")));
    materials_update_current_document ();
  });
}

void materials_append_references () {
  tree node= focused_references ();
  if (node == UNINIT) return;
  auto target= std::make_shared<reference_target> (node);
  qtm_material_choose_references_async ([target] (tree chosen) {
    tree node= target->get ();
    if (!ip_attached (obtain_ip (node)) || is_atomic (chosen) || L(chosen) != TUPLE) return;
    tree all (TUPLE);
    std::set<std::string> seen;
    auto append= [&] (tree ids) {
      if (is_atomic (ids) || L(ids) != TUPLE) return;
      for (int i=0; i<N(ids); ++i) {
        string id= as_string (ids[i]);
        if (seen.emplace (id.data (), N(id)).second) all << ids[i];
      }
    };
    append (node[1]); append (chosen);
    tree_assign (node[1], all);
    materials_update_current_document ();
  });
}

void materials_set_reference_style (string style) {
  tree node= focused_references ();
  if (node == UNINIT) return;
  tree_assign (node[0], tree (style));
  materials_update_current_document ();
}

string materials_page_title (string, object) { return "Material"; }
bool materials_page_permission (string, string kind) { return kind == "read"; }
