// Native implementations for generated Scheme bindings.
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef ATHENA_NATIVE_INTERFACES_HPP
#define ATHENA_NATIVE_INTERFACES_HPP

#include "scheme.hpp"
#include "modification.hpp"
#include "patch.hpp"

void athena_dispatch_ui (void (*function) ());
void gui_set_cursor_color (string value);
void gui_set_selection_color (string value);
void gui_set_focus_color (string value);
void gui_set_focus_border_width (string value);
extern string original_path;
string get_original_path ();
string texmacs_version (string which);
string image_remove_background_current (object image);
void ads_restore_visible_panes ();
void ads_close_tool_pane (string id);
void ads_show_tool_pane (object wid, string id, string title, object close, bool floating);
void win32_display (string s);
void tm_output (string s);
void tm_errput (string s);
void cpp_error ();
array<int> get_bounding_rectangle (tree t);
bool supports_native_pdf ();
bool supports_ghostscript ();
bool is_busy_versioning ();
array<SI> get_screen_size ();
void cout_buffer ();
string cout_unbuffer ();
tree coerce_string_tree (string s);
string coerce_tree_string (tree t);
tree tree_ref (tree t, int i);
tree tree_set (tree t, int i, tree u);
tree tree_range (tree t, int i, int j);
tree tree_append (tree t1, tree t2);
bool tree_active (tree t);
tree tree_child_insert (tree t, int pos, tree x);
tree tree_assign (tree r, tree t);
tree tree_insert (tree r, int pos, tree t);
tree tree_remove (tree r, int pos, int nr);
tree tree_split (tree r, int pos, int at);
tree tree_join (tree r, int pos);
tree tree_assign_node (tree r, tree_label op);
tree tree_insert_node (tree r, int pos, tree t);
tree tree_remove_node (tree r, int pos);
url url_concat (url u1, url u2);
url url_or (url u1, url u2);
void string_save (string s, url u);
string string_load (url u);
void string_append_to_file (string s, url u);
url url_ref (url u, int i);
tree var_apply (tree& t, modification m);
tree var_clean_apply (tree& t, modification m);
patch branch_patch (array<patch> a);
tree var_clean_apply (tree t, patch p);
tree var_apply (tree& t, patch p);

void
athena_native_info_dialog (string arg1, string arg2);

void
athena_native_anchor_enunciations_confirm (
  string wraps, string dead, string headings, string notes, object callback);

array<string>
athena_native_font_selector (string arg1, string arg2, string arg3, string arg4, string arg5);

bool
athena_native_preferences_openP ();

int
athena_native_preferences_export_privacy ();

array<string>
athena_native_preferences_export_metadata ();

string
athena_escape_symbol_picker ();

void
athena_google_cloud_todo_sync_buffer (url arg1);

void
athena_google_cloud_todo_sync_open_buffers ();

void
athena_codex_initialize_models (string arg1, string arg2);

array<string>
athena_codex_completion_options (string arg1, string arg2);

void
athena_codex_run_completion_async (string arg1, string arg2, string arg3, string arg4, string arg5, string arg6, string arg7, string arg8, array<string> arg9, command arg10);

object
athena_artifact_resolve_uuid (string arg1);

tree
athena_material_choose_citation (string arg1);

object
athena_material_resolve_uuid (string arg1);

object
athena_material_find_by_identifier (string arg1, string arg2);

object
athena_material_get (string arg1);

tree
athena_materials_search (string arg1, int arg2);

tree
athena_materials_list (int arg1, int arg2);

tree
athena_materials_update_document (tree arg1, string arg2);

tree
athena_materials_update_document_auto (tree arg1);

tree
athena_materials_csl_styles ();

tree
athena_material_info_page (string arg1);

tree
athena_global_transformation_run (object arg1, string arg2);

tree
athena_reverse_hierarchy_graph_render (tree arg1);

string
athena_vault_validate_root_namespace ();

string
athena_namespace_new_file_wizard ();

string
athena_namespace_create_file_with_optional_initializer (string arg1);

int
athena_vault_rewrite_anchor_references (string arg1, string arg2);

tree
athena_vault_maintenance_setup (url arg1);

bool
athena_vaultfile_presentP (url arg1);

array<string>
athena_vaultfile_read (url arg1);

array<string>
athena_artifact_title_filter_read (url arg1);

string
athena_vaultfile_write (url arg1, array<string> arg2);

string
athena_vaultfile_ensure_json (url arg1);

void
athena_vault_backup_dispatch_realtime (url arg1);

#endif
