
/******************************************************************************
* MODULE     : native_interfaces.cpp
* DESCRIPTION: Native operations exposed through generated Scheme bindings
* COPYRIGHT  : (C) 1999-2011  Joris van der Hoeven and Massimiliano Gubinelli
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "scheme.hpp"

#include "promise.hpp"
#include "tree.hpp"
#include "drd_mode.hpp"
#include "tree_search.hpp"
#include "modification.hpp"
#include "patch.hpp"
#include "new_document.hpp"
#include "actor_ui_bridge.hpp"
#include "scheme_execution_context.hpp"

#include "boxes.hpp"
#include "editor.hpp"
#include "universal.hpp"
#include "convert.hpp"
#include "file.hpp"
#include "gui.hpp"
#include "gui_text.hpp"
#include "vault.hpp"
#include "namespaces.hpp"
#include "ATHENA/Data/vault_backup.hpp"
#include "ATHENA/Data/vaultfile_json.hpp"
#include "ATHENA/Data/artifact_title_filter.hpp"
#include "ATHENA/Data/artifact_document.hpp"
#include "ATHENA/Data/artifact_radioactive_links.hpp"
#include "ATHENA/Data/global_transformation.hpp"
#include "ATHENA/Data/materials.hpp"
#include "ATHENA/Data/materials_document.hpp"
#include "ATHENA/Data/materials_engine.hpp"
#include "ATHENA/Data/namespace_ontology.hpp"
#include "ATHENA/Data/transclusion_cache.hpp"
#include "QTMMainTabWindow.hpp"
#include "QTMVaultChooser.hpp"
#include "QTMQuickSwitcher.hpp"
#include "QTMBufferSwitcher.hpp"
#include "QTMVaultExplorer.hpp"
#include "QTMVaultBackupViewer.hpp"
#include "QTMGlobalSearch.hpp"
#include "QTMATHENADiff.hpp"
#include "QTMOutlinePane.hpp"
#include "QTMErrorMessagesPane.hpp"
#include "QTMCommandPalette.hpp"
#include "QTMCustomStylesManager.hpp"
#include "QTMNamespaceManager.hpp"
#include "QTMNamespaceExplorer.hpp"
#include "QTMNeighborhoodsPane.hpp"
#include "QTMNamespaceNewFile.hpp"
#include "QTMNamespaceExport.hpp"
#include "QTMWebsitesManager.hpp"
#include "QTMReverseHierarchyGraph.hpp"
#include "QTMFormulaAstViewer.hpp"
#include "QTMDocumentSearchBar.hpp"
#include "QTMArtifactsPane.hpp"
#include "QTMMaterialsManager.hpp"
#include "QTMMaterialCitationDialog.hpp"
#include "QTMVaultInfoModel.hpp"
#include "QTMVaultBackupDispatcher.hpp"
#include "QTMVaultMaintenanceDialog.hpp"
#include "QTMAbout.hpp"
#include "QTMAnchorConfirmation.hpp"
#include "QTMESCSymbolPicker.hpp"
#include "QTMHandwritingSymbolPane.hpp"
#include "QTMFontSelector.hpp"
#include "QTMVaultFontConfigurator.hpp"
#include "QTMCodexCompletion.hpp"
#include "QTMPreferencesDialog.hpp"
#include "QTMPagePropertiesPane.hpp"
#include "QTMParagraphPropertiesPane.hpp"
#include "QTMMetadataPropertiesPane.hpp"
#include "QTMGoogleTasksPane.hpp"
#include "GoogleCloudTodo.hpp"
#include "ATHENA/Data/image_background.hpp"
#include "boot.hpp"
#include "qt_widget.hpp"
#include "qt_utilities.hpp"
#include "message.hpp"
#include "locale.hpp"
#include "iterator.hpp"
#include "Freetype/tt_tools.hpp"
#include "Sqlite3/sqlite3.hpp"
#include "Gnutls/gnutls.hpp"

#include <DockWidget.h>
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPointer>
#include <QProcess>
#include <QProgressDialog>
#include <QPushButton>
#include <QSizePolicy>
#include <QSpacerItem>
#include <QVBoxLayout>
#include <QWidget>
#include <map>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>

#include "native_interfaces.hpp"
#include "new_buffer.hpp"
#include "tm_window.hpp"

namespace fs= std::filesystem;

void
gui_set_cursor_color (string value) {
  gui_cursor_color= named_color (value);
  windows_refresh ("");
}

void
gui_set_selection_color (string value) {
  gui_selection_color= named_color (value);
  windows_refresh ("");
}

void
gui_set_focus_color (string value) {
  gui_focus_color= named_color (value);
  windows_refresh ("");
}

void
gui_set_focus_border_width (string value) {
  gui_focus_border_width= max (as_int (value), 1);
  windows_refresh ("");
}

string original_path;

struct ads_tool_pane_rep {
  widget                    win;
  QPointer<ads::CDockWidget> dock;
  command                   close;
  bool                      closing;

  ads_tool_pane_rep (widget win2, ads::CDockWidget* dock2, command close2)
    : win (win2), dock (dock2), close (close2), closing (false) {}
};

static std::map<std::string, ads_tool_pane_rep*> ads_tool_panes;

static std::string
ads_tool_key (string id) {
  return to_qstring (id).toStdString ();
}

static void
ads_destroy_tool_pane (const std::string& key, bool run_close) {
  std::map<std::string, ads_tool_pane_rep*>::iterator it=
    ads_tool_panes.find (key);
  if (it == ads_tool_panes.end ()) return;

  ads_tool_pane_rep* pane= it->second;
  if (pane->closing) return;
  pane->closing= true;

  if (run_close && !is_nil (pane->close)) pane->close ();

  if (pane->dock) {
    QTMMainTabWindow* win= QTMMainTabWindow::topTabWindow ();
    if (win != nullptr && win->dockManager () != nullptr)
      win->dockManager ()->removeDockWidget (pane->dock);
  }

  widget doomed= pane->win;
  if (!is_nil (doomed)) {
    send_destroy (doomed);
    destroy_window_widget (doomed);
  }

  ads_tool_panes.erase (it);
  delete pane;
}

void
ads_close_tool_pane (string id) {
  ads_destroy_tool_pane (ads_tool_key (id), false);
}

void
ads_show_tool_pane (widget wid, string id, string title, command close,
                    bool floating) {
  std::string key= ads_tool_key (id);
  ads_destroy_tool_pane (key, false);

  widget pane_window= plain_window_widget (wid, id, command ());
  qt_widget qtw= concrete (pane_window);
  QWidget* pane_widget= qtw->qwid;
  if (pane_widget == nullptr) return;

  QTMMainTabWindow* win= QTMMainTabWindow::topTabWindow ();
  if (win == nullptr || win->dockManager () == nullptr) {
    pane_widget->setWindowTitle (to_qstring (title));
    pane_widget->show ();
    pane_widget->raise ();
    pane_widget->setFocus ();
    ads_tool_panes[key]= new ads_tool_pane_rep (pane_window, nullptr, close);
    return;
  }

  ads::CDockWidget* dock= new ads::CDockWidget (to_qstring (title));
  dock->setObjectName (QString ("athena-tool-pane-%1")
                       .arg (QString::fromStdString (key)));
  dock->resize (960, 340);
  dock->setWidget (pane_widget);
  dock->setFeature (ads::CDockWidget::DockWidgetDeleteOnClose, false);
  dock->setFeature (ads::CDockWidget::CustomCloseHandling, true);

  ads_tool_panes[key]= new ads_tool_pane_rep (pane_window, dock, close);
  QObject::connect (dock, &ads::CDockWidget::closeRequested, [key] () {
    ads_destroy_tool_pane (key, true);
  });
  QObject::connect (dock, &QObject::destroyed, [key] () {
    std::map<std::string, ads_tool_pane_rep*>::iterator it=
      ads_tool_panes.find (key);
    if (it != ads_tool_panes.end () && !it->second->closing)
      ads_destroy_tool_pane (key, true);
  });

  if (floating)
  {
    win->dockManager ()->addDockWidgetFloating (dock);
    win->scheduleAdsLayoutRestore (dock);
  }
  else {
    win->showAdsDockWidget (dock, ads::BottomDockWidgetArea);
    win->showAdsDockWidget (dock, ads::BottomDockWidgetArea);
  }
  dock->toggleView (true);
  dock->show ();
  dock->raise ();
  pane_widget->setFocus ();
}

string
get_original_path () {
  return original_path;
}

string
texmacs_version (string which) {
  if (which == "tgz") return TM_DEVEL;
  if (which == "rpm") return TM_DEVEL_RELEASE;
  if (which == "stgz") return TM_STABLE;
  if (which == "srpm") return TM_STABLE_RELEASE;
  if (which == "devel") return TM_DEVEL;
  if (which == "stable") return TM_STABLE;
  if (which == "devel-release") return TM_DEVEL_RELEASE;
  if (which == "stable-release") return TM_STABLE_RELEASE;
  if (which == "revision") return ATHENA_REVISION;
  return ATHENA_VERSION;
}

string
image_remove_background_current (object arg1) {
  if (!is_string (arg1))
    return string ("Remove background expects an image path.");

  string file_name= as_string (arg1);
  url image= relative (get_current_editor ()->get_name (),
                       url_unix (file_name));
  string error;
  if (!image_remove_white_background_png (image, error))
    return string (error);
  return string ("");
}

void
ads_restore_visible_panes () {
  QTMMainTabWindow* win= QTMMainTabWindow::topTabWindow ();
  if (win != nullptr) win->restoreAdsVisiblePanes ();
  return;
}

void
win32_display (string s) {
  cout << s;
  cout.flush ();
}

void
tm_output (string s) {
  cout << s;
}

void
tm_errput (string s) {
  cerr << s;
}

void
cpp_error () {
  // FAILED ("an error occurred");
  *((volatile int*)nullptr) = 0xDEADBEEF;
}

array<int>
get_bounding_rectangle (tree t) {
  editor ed= get_current_editor ();
  rectangle wr= ed -> get_window_extents ();
  path p= reverse (obtain_ip (t));
  selection sel= ed->search_selection (p * start (t), p * end (t));
  SI sz= ed->get_pixel_size ();
  double sf= ((double) sz) / 256.0;
  rectangle r (0, 0, 0, 0);
  if (!is_nil (sel->rs)) {
    rectangle selr= least_upper_bound (sel->rs) / sf;
    r= translate (selr, wr->x1, wr->y2);
  }
  array<int> ret;
  ret << ((int) r->x1) << ((int) r->y1) << ((int) r->x2) << ((int) r->y2);
  //ret << (r->x1/PIXEL) << (r->y1/PIXEL) << (r->x2/PIXEL) << (r->y2/PIXEL);
  return ret;
}

bool
supports_native_pdf () {
#ifdef PDF_RENDERER
  return true;
#else
  return false;
#endif
}

bool
supports_ghostscript () {
#ifdef USE_GS
  return true;
#else
  return false;
#endif
}

bool
is_busy_versioning () {
  return busy_versioning;
}

array<SI>
get_screen_size () {
  array<SI> r;
  SI w, h;
  gui_root_extents (w, h);
  r << w << h;
  return r;
}

/******************************************************************************
* Redirections
******************************************************************************/

void
cout_buffer () {
  cout.buffer ();
}

string
cout_unbuffer () {
  return cout.unbuffer ();
}


tree
coerce_string_tree (string s) {
  return s;
}

string
coerce_tree_string (tree t) {
  return as_string (t);
}

tree
tree_ref (tree t, int i) {
  return t[i];
}

tree
tree_set (tree t, int i, tree u) {
  t[i]= u;
  return u;
}

tree
tree_range (tree t, int i, int j) {
  return t(i,j);
}

tree
tree_append (tree t1, tree t2) {
  return t1 * t2;
}

bool
tree_active (tree t) {
  path ip= obtain_ip (t);
  return is_nil (ip) || last_item (ip) != DETACHED;
}

tree
tree_child_insert (tree t, int pos, tree x) {
  //cout << "t= " << t << "\n";
  //cout << "x= " << x << "\n";
  int i, n= N(t);
  tree r (t, n+1);
  for (i=0; i<pos; i++) r[i]= t[i];
  r[pos]= x;
  for (i=pos; i<n; i++) r[i+1]= t[i];
  return r;
}

/******************************************************************************
* Document modification routines
******************************************************************************/


tree
tree_assign (tree r, tree t) {
  path ip= copy (obtain_ip (r));
  if (ip_attached (ip)) {
    assign (reverse (ip), copy (t));
    return subtree (current_document_tree (), reverse (ip));
  }
  else {
    assign (r, copy (t));
    return r;
  }
}

tree
tree_insert (tree r, int pos, tree t) {
  path ip= copy (obtain_ip (r));
  if (ip_attached (ip)) {
    insert (reverse (path (pos, ip)), copy (t));
    return subtree (current_document_tree (), reverse (ip));
  }
  else {
    insert (r, pos, copy (t));
    return r;
  }
}

tree
tree_remove (tree r, int pos, int nr) {
  path ip= copy (obtain_ip (r));
  if (ip_attached (ip)) {
    remove (reverse (path (pos, ip)), nr);
    return subtree (current_document_tree (), reverse (ip));
  }
  else {
    remove (r, pos, nr);
    return r;
  }
}

tree
tree_split (tree r, int pos, int at) {
  path ip= copy (obtain_ip (r));
  if (ip_attached (ip)) {
    split (reverse (path (at, pos, ip)));
    return subtree (current_document_tree (), reverse (ip));
  }
  else {
    split (r, pos, at);
    return r;
  }
}

tree
tree_join (tree r, int pos) {
  path ip= copy (obtain_ip (r));
  if (ip_attached (ip)) {
    join (reverse (path (pos, ip)));
    return subtree (current_document_tree (), reverse (ip));
  }
  else {
    join (r, pos);
    return r;
  }
}

tree
tree_assign_node (tree r, tree_label op) {
  path ip= copy (obtain_ip (r));
  if (ip_attached (ip)) {
    assign_node (reverse (ip), op);
    return subtree (current_document_tree (), reverse (ip));
  }
  else {
    assign_node (r, op);
    return r;
  }
}

tree
tree_insert_node (tree r, int pos, tree t) {
  path ip= copy (obtain_ip (r));
  if (ip_attached (ip)) {
    insert_node (reverse (path (pos, ip)), copy (t));
    return subtree (current_document_tree (), reverse (ip));
  }
  else {
    insert_node (r, pos, copy (t));
    return r;
  }
}

tree
tree_remove_node (tree r, int pos) {
  path ip= copy (obtain_ip (r));
  if (ip_attached (ip)) {
    remove_node (reverse (path (pos, ip)));
    return subtree (current_document_tree (), reverse (ip));
  }
  else {
    remove_node (r, pos);
    return r;
  }
}


url url_concat (url u1, url u2) { return u1 * u2; }
url url_or (url u1, url u2) { return u1 | u2; }
void string_save (string s, url u) { (void) save_string (u, s); }
string string_load (url u) {
  string s; (void) load_string (u, s, false); return s; }
void string_append_to_file (string s, url u) { (void) append_string (u, s); }
url url_ref (url u, int i) { return u[i]; }


tree
var_apply (tree& t, modification m) {
  apply (t, copy (m));
  return t;
}

tree
var_clean_apply (tree& t, modification m) {
  return clean_apply (t, copy (m));
}


patch
branch_patch (array<patch> a) {
  return patch (true, a);
}

tree
var_clean_apply (tree t, patch p) {
  return clean_apply (copy (p), t);
}

tree
var_apply (tree& t, patch p) {
  apply (copy (p), t);
  return t;
}


void
athena_dispatch_ui (void (*function) ()) {
  const SchemeExecutionContext* context= current_scheme_execution_context ();
  if (context != nullptr && context->editor != nullptr &&
      context->view_id != ATHENA_NO_VIEW) {
    athena_resource_id action= actor_ui_register_action (function);
    (void) context->editor->publish_ui (
      actor_command_kind::ui_global_action, action);
    return;
  }
  function ();
}



void
athena_native_info_dialog (string arg1, string arg2) {
  if (headless_mode) return;

  string message= arg1;
  string title  = arg2;

  QMessageBox msg_box (QApplication::activeWindow ());
  msg_box.setWindowTitle (to_qstring (title));
  msg_box.setText (to_qstring (message));
  msg_box.setTextFormat (Qt::PlainText);
  msg_box.setIcon (QMessageBox::Information);
  msg_box.setStandardButtons (QMessageBox::Ok);
  msg_box.setMinimumWidth (560);
  for (QLabel* label: msg_box.findChildren<QLabel*> ())
    label->setWordWrap (true);
  if (QGridLayout* layout = qobject_cast<QGridLayout*> (msg_box.layout ())) {
    QSpacerItem* spacer =
      new QSpacerItem (520, 0, QSizePolicy::Minimum, QSizePolicy::Expanding);
    layout->addItem (spacer, layout->rowCount (), 0, 1,
                     layout->columnCount ());
  }
  msg_box.exec ();

  return;
}

void
athena_native_anchor_enunciations_confirm (
    string wraps, string dead, string headings, string notes, object callback) {
  if (headless_mode) {
    (void) call (callback, object (false));
    return;
  }
  // Bind before crossing to Qt. Share the command holder, not its non-atomic
  // reference counter; completion resumes on the requesting BufferActor.
  auto completion= std::make_shared<command> (as_command (callback));
  qt_anchor_enunciations_confirm (
    to_qstring (wraps), to_qstring (dead), to_qstring (headings),
    to_qstring (notes), [completion] (bool accepted) {
      (*completion) (list_object (object (accepted)));
    });
}

array<string>
athena_native_font_selector (string arg1, string arg2, string arg3, string arg4, string arg5) {
  if (headless_mode) return array<string> ();

  array<string> result=
    native_font_selector_dialog (arg1,
                                 arg2,
                                 arg3,
                                 arg4,
                                 arg5);
  return array<string> (result);
}

bool
athena_native_preferences_openP () {
  return bool (qtm_preferences_dialog_open ());
}

int
athena_native_preferences_export_privacy () {
  return int (qtm_preferences_export_privacy_dialog ());
}

array<string>
athena_native_preferences_export_metadata () {
  array<string> result;
  const QStringList metadata= qtm_preferences_export_metadata ();
  for (const QString& field: metadata) {
    const QByteArray utf8= field.toUtf8 ();
    result << string (utf8.constData (), utf8.size ());
  }
  return array<string> (result);
}

string
athena_escape_symbol_picker () {
  if (headless_mode) return string ("");
  return string (escape_symbol_picker_dialog ());
}

void
athena_google_cloud_todo_sync_buffer (url arg1) {
  google_cloud_todo_sync_buffer (arg1, true);
  return;
}

void
athena_google_cloud_todo_sync_open_buffers () {
  google_cloud_todo_sync_open_buffers (false);
  return;
}

void
athena_codex_initialize_models (string arg1, string arg2) {
  if (!headless_mode)
    qtm_codex_initialize_models (arg1,
                                 arg2);
  return;
}

array<string>
athena_codex_completion_options (string arg1, string arg2) {
  if (headless_mode) return array<string> ();
  return array<string> (
    qtm_codex_completion_options (arg1,
                                  arg2));
}

void
athena_codex_run_completion_async (string arg1, string arg2, string arg3, string arg4, string arg5, string arg6, string arg7, string arg8, array<string> arg9, command arg10) {
  QString bridge= to_qstring (arg1);
  QString home= to_qstring (arg2);
  QString input= to_qstring (arg3);
  QString output= to_qstring (arg4);
  QString model= to_qstring (arg5);
  QString effort= to_qstring (arg6);
  QString serviceTier= to_qstring (arg7);
  QString webSearch= to_qstring (arg8);
  array<string> imagePaths= arg9;
  command callback= arg10;

  QProcess* process= new QProcess (QApplication::instance ());
  process->setProcessChannelMode (QProcess::MergedChannels);
  auto completed= std::make_shared<bool> (false);
  auto complete= [process, callback, completed] () {
    if (*completed) return;
    *completed= true;
    QByteArray diagnostics= process->readAll ();
    if (!diagnostics.isEmpty () &&
        (process->exitStatus () != QProcess::NormalExit ||
         process->exitCode () != 0))
      std_warning << "Codex completion bridge: "
                  << from_qstring (QString::fromUtf8 (diagnostics)) << LF;
    eval (callback);
    process->deleteLater ();
  };
  QObject::connect (
    process, qOverload<int,QProcess::ExitStatus> (&QProcess::finished),
    process, [complete] (int, QProcess::ExitStatus) { complete (); });
  QObject::connect (
    process, &QProcess::errorOccurred, process,
    [complete] (QProcess::ProcessError error) {
      if (error == QProcess::FailedToStart) complete ();
    });
  QStringList arguments {"--one-shot", "--codex-home", home,
                         "--input", input, "--output", output};
  if (!model.isEmpty ()) arguments << "--model" << model;
  if (!effort.isEmpty ()) arguments << "--effort" << effort;
  if (!serviceTier.isEmpty ())
    arguments << "--service-tier" << serviceTier;
  if (webSearch == "live") arguments << "--web-search";
  else if (webSearch == "disabled") arguments << "--no-web-search";
  for (int i= 0; i < N(imagePaths); ++i)
    arguments << "--image" << to_qstring (imagePaths[i]);
  process->start (bridge, arguments);
  return;
}

int
athena_vault_rewrite_anchor_references (string arg1, string arg2) {
  size_t changed= vault_rewrite_anchor_references (arg1,
                                                    arg2);
  return int ((int) changed);
}

tree
athena_vault_maintenance_setup (url arg1) {
  string root= concretize (arg1);
  return tree (qtm_vault_maintenance_setup (root));
}

static std::filesystem::path
native_vault_root_path (url arg) {
  string s= concretize (arg);
  return std::filesystem::path (std::string (as_charp (s), N(s)));
}

static array<string>
native_vaultfile_fields_to_array (const std::vector<std::string>& fields) {
  array<string> out;
  for (const std::string& field: fields)
    out << string (field.c_str (), field.size ());
  return out;
}

static tree
native_utf8_text (const std::string& value) {
  return tree (utf8_to_cork (string (value.data (), value.size ())));
}

static std::string
native_cork_to_utf8 (string value) {
  string converted= cork_to_utf8 (value);
  return std::string (as_charp (converted), (size_t) N(converted));
}

static tree
native_integer_text (std::int64_t value) {
  return tree (std::to_string (value).c_str ());
}

static MaterialsStore*
native_materials_store (const char* routine) {
  MaterialsStore* store= vault_get_materials_store ();
  if (store == nullptr)
    FAILED (c_string (string (routine) * ": no active Materials database"));
  return store;
}

static tree
native_material_record_tree (const MaterialRecord& material) {
  tree fields (TUPLE);
  for (const MaterialField& field: material.fields)
    fields << compound ("material-field", native_utf8_text (field.name),
                        native_utf8_text (field.value),
                        native_utf8_text (field.language),
                        native_integer_text (field.ordinal));
  tree creators (TUPLE);
  for (const MaterialCreator& creator: material.creators)
    creators << compound ("material-creator", native_utf8_text (creator.role),
                          native_utf8_text (creator.given),
                          native_utf8_text (creator.family),
                          native_utf8_text (creator.literal),
                          native_utf8_text (creator.suffix),
                          native_integer_text (creator.ordinal));
  tree identifiers (TUPLE);
  for (const MaterialIdentifier& identifier: material.identifiers)
    identifiers << compound ("material-identifier",
                              native_utf8_text (identifier.scheme),
                              native_utf8_text (identifier.value),
                              native_utf8_text (identifier.normalized_value));
  tree tags (TUPLE);
  for (const std::string& tag: material.tags) tags << native_utf8_text (tag);

  array<tree> children;
  children << native_utf8_text (material.uuid)
           << native_utf8_text (material.item_type)
           << native_utf8_text (material.review_state)
           << native_integer_text (material.revision)
           << native_integer_text (material.created_at)
           << native_integer_text (material.updated_at)
           << fields << creators << identifiers << tags;
  return compound ("material-record", children);
}

object
athena_material_resolve_uuid (string arg1) {
  MaterialsStore* store= native_materials_store ("material-resolve-uuid");
  std::string error;
  std::string resolved= store->resolve_uuid (native_cork_to_utf8 (arg1), error);
  if (!error.empty ())
    FAILED (c_string ("material-resolve-uuid: " * string (error.c_str ())));
  if (resolved.empty ()) return object (false);
  return object (utf8_to_cork (
    string (resolved.data (), resolved.size ())));
}

object
athena_material_find_by_identifier (string arg1, string arg2) {
  MaterialsStore* store= native_materials_store ("material-find-by-identifier");
  std::string error;
  std::optional<std::string> found= store->material_for_identifier (
    native_cork_to_utf8 (arg1), native_cork_to_utf8 (arg2), error);
  if (!error.empty ())
    FAILED (c_string ("material-find-by-identifier: " *
                      string (error.c_str ())));
  if (!found) return object (false);
  return object (utf8_to_cork (
    string (found->data (), found->size ())));
}

object
athena_material_get (string arg1) {
  MaterialsStore* store= native_materials_store ("material-get");
  std::string error;
  std::optional<MaterialRecord> material=
    store->get (native_cork_to_utf8 (arg1), error);
  if (!error.empty ())
    FAILED (c_string ("material-get: " * string (error.c_str ())));
  if (!material) return object (false);
  return object (native_material_record_tree (*material));
}

static tree
native_material_hits_tree (const std::vector<MaterialSearchHit>& hits) {
  tree result (TUPLE);
  for (const MaterialSearchHit& hit: hits) {
    array<tree> values;
    values << native_utf8_text (hit.uuid)
           << native_utf8_text (hit.item_type)
           << native_utf8_text (hit.title)
           << native_utf8_text (hit.creators)
           << native_utf8_text (hit.issued)
           << native_utf8_text (hit.review_state)
           << tree (as_string (hit.rank));
    result << compound ("material-search-hit", values);
  }
  return result;
}

tree
athena_materials_search (string arg1, int arg2) {
  MaterialsStore* store= native_materials_store ("materials-search");
  std::string error;
  int limit= std::max (1, std::min (arg2, 1000));
  std::vector<MaterialSearchHit> hits=
    store->search (native_cork_to_utf8 (arg1), limit, error);
  if (!error.empty ())
    FAILED (c_string ("materials-search: " * string (error.c_str ())));
  return tree (native_material_hits_tree (hits));
}

tree
athena_materials_list (int arg1, int arg2) {
  MaterialsStore* store= native_materials_store ("materials-list");
  std::string error;
  int limit= std::max (1, std::min (arg1, 1000));
  int offset= std::max (0, arg2);
  std::vector<MaterialSearchHit> hits= store->list (limit, offset, error);
  if (!error.empty ())
    FAILED (c_string ("materials-list: " * string (error.c_str ())));
  return tree (native_material_hits_tree (hits));
}

tree
athena_material_choose_citation (string arg1) {
  string style= arg1;
  return tree (qtm_material_choose_citation (
    std::string (as_charp (style), N(style))));
}

tree
athena_materials_update_document (tree arg1, string arg2) {
  std::string error;
  string style= arg2;
  tree updated= athena_materials_update_document (
    arg1, std::string (as_charp (style), N(style)), error);
  tree result (TUPLE);
  result << tree (error.empty () ? "ok" : "error");
  result << (error.empty () ? updated : tree (error.c_str ()));
  return tree (result);
}

tree
athena_materials_update_document_auto (tree arg1) {
  tree document= arg1;
  string preference= get_preference ("materials csl style",
                                     "springer-mathphys");
  std::string fallback (as_charp (preference), (size_t) N(preference));
  std::string style=
    athena_materials_document_citation_style (document, fallback);
  std::string error;
  tree updated= athena_materials_update_document (document, style, error);
  tree result (TUPLE);
  result << tree (error.empty () ? "ok" : "error");
  result << (error.empty () ? updated : native_utf8_text (error));
  return tree (result);
}

tree
athena_materials_csl_styles () {
  std::vector<MaterialCslStyle> styles;
  std::string error;
  tree result (TUPLE);
  if (!athena_materials_list_csl_styles (styles, error)) {
    FAILED (c_string ("could not list CSL styles: " *
                      string (error.c_str ())));
    return tree (result);
  }
  for (const MaterialCslStyle& style: styles)
    result << compound ("tuple", tree (style.name.c_str ()),
                        tree (style.title.c_str ()));
  return tree (result);
}

tree
athena_material_info_page (string arg1) {
  string name= arg1;
  return tree (athena_material_info_page (
    std::string (as_charp (name), N(name))));
}

namespace {

class GlobalTransformationProgress final: public QProgressDialog {
public:
  explicit GlobalTransformationProgress (const QString& title,
                                          QWidget* parent):
    QProgressDialog (QString (), "Cancel", 0, 1, parent) {
    setWindowTitle (title);
    setWindowModality (Qt::ApplicationModal);
    setMinimumDuration (0);
    setAutoClose (false);
    setAutoReset (false);
    setFixedWidth (620);
    QLabel* label= findChild<QLabel*> ();
    if (label != nullptr) {
      label->setWordWrap (true);
      label->setMinimumWidth (0);
      label->setSizePolicy (QSizePolicy::Ignored, QSizePolicy::Preferred);
    }
  }

  void set_progress_text (const QString& value) {
    setLabelText (value);
    ensurePolished ();
    int required= std::max (sizeHint ().height (), minimumSizeHint ().height ());
    if (required > monotonic_height) {
      monotonic_height= required;
      setMinimumHeight (monotonic_height);
    }
    if (height () < monotonic_height) resize (width (), monotonic_height);
  }

private:
  int monotonic_height= 0;
};

fs::path
native_normalized_path (const fs::path& path) {
  std::error_code ec;
  fs::path result= fs::weakly_canonical (path, ec);
  if (!ec) return result.lexically_normal ();
  result= fs::absolute (path, ec);
  return (ec ? path : result).lexically_normal ();
}

bool
native_path_at_or_below (const fs::path& path, const fs::path& root) {
  fs::path relative= path.lexically_relative (root);
  if (relative.empty ()) return path == root;
  auto first= relative.begin ();
  return first != relative.end () && *first != "..";
}

tree
native_global_transformation_result (
    const char* status, const AthenaGlobalTransformationPlan& plan,
    const std::string& message) {
  tree changed (TUPLE);
  for (const AthenaGlobalTransformationRewrite& rewrite: plan.rewrites)
    changed << native_utf8_text (rewrite.relative_path.generic_u8string ());
  tree result (TUPLE);
  result << tree (status)
         << native_integer_text ((std::int64_t) plan.scanned)
         << native_integer_text ((std::int64_t) plan.rewrites.size ())
         << native_utf8_text (message)
         << native_utf8_text (plan.backup_root.empty ()
                             ? std::string ()
                             : plan.backup_root.string ())
         << changed;
  return result;
}

} // namespace

tree
athena_global_transformation_run (object arg1, string arg2) {
  AthenaGlobalTransformationPlan plan;
  if (!vault_active ())
    return tree (native_global_transformation_result (
      "error", plan, "Load a Vault before running a global transformation"));
  if (headless_mode)
    return tree (native_global_transformation_result (
      "error", plan, "Global transformations require the interactive editor"));

  std::string name= native_cork_to_utf8 (arg2);
  string root_string= concretize (vault_get_root ());
  fs::path root= native_normalized_path (
    fs::path (std::string (as_charp (root_string), (size_t) N(root_string))));
  std::unordered_map<std::string,url> open_buffers;
  QStringList modified;
  array<url> buffers= get_all_buffers ();
  for (int i=0; i<N(buffers); ++i) {
    if (is_rooted_tmfs (buffers[i]) || is_rooted_web (buffers[i])) continue;
    string concrete= concretize (buffers[i]);
    fs::path path= native_normalized_path (fs::path (
      std::string (as_charp (concrete), (size_t) N(concrete))));
    if (!native_path_at_or_below (path, root) || path.extension () != ".ath")
      continue;
    open_buffers[path.string ()]= buffers[i];
    if (buffer_modified (buffers[i]))
      modified << QString::fromStdString (
        path.lexically_relative (root).generic_u8string ());
  }
  if (!modified.isEmpty ()) {
    QStringList shown= modified.mid (0, 12);
    if (modified.size () > shown.size ())
      shown << QString ("... and %1 more").arg (modified.size () - shown.size ());
    QMessageBox::warning (
      qApp->activeWindow (), "Run global transformation",
      QString ("Save the open Vault documents before continuing:\n\n%1")
        .arg (shown.join ("\n")));
    return tree (native_global_transformation_result (
      "error", plan, "Open Vault documents have unsaved changes"));
  }

  GlobalTransformationProgress progress (
    QString::fromStdString (name.empty () ? "Run global transformation" : name),
    qApp->activeWindow ());
  progress.set_progress_text ("Scanning Vault documents...");
  progress.show ();
  QApplication::processEvents ();

  std::string error;
  auto transform= [&] (const std::string& relative, const tree& input,
                        tree& output, std::string& callback_error) {
    object result= call (
      arg1, object (utf8_to_cork (
        string (relative.data (), relative.size ()))), object (copy (input)));
    if (is_bool (result) && !as_bool (result)) {
      output= input;
      return true;
    }
    if (!is_tree (result)) {
      callback_error=
        "Transformer must return #f for no change or a complete document tree";
      return false;
    }
    output= as_tree (result);
    return true;
  };
  auto report_progress= [&] (size_t current, size_t total,
                              const std::string& relative) {
    progress.setRange (0, std::max (1, (int) total));
    progress.setValue ((int) current);
    if (!relative.empty ())
      progress.set_progress_text (
        QString ("Transforming %1 of %2: %3")
          .arg (current + 1).arg (total)
          .arg (QString::fromStdString (relative)));
    QApplication::processEvents ();
    return !progress.wasCanceled ();
  };

  if (!athena_global_transformation_prepare (
        root, transform, report_progress, plan, error)) {
    progress.close ();
    if (error == "cancelled")
      return tree (native_global_transformation_result (
        "cancelled", plan, "Transformation cancelled; no files were changed"));
    QMessageBox::critical (qApp->activeWindow (), "Run global transformation",
                           QString::fromStdString (error));
    return tree (native_global_transformation_result (
      "error", plan, error));
  }
  progress.close ();

  if (plan.rewrites.empty ())
    return tree (native_global_transformation_result (
      "ok", plan, "The transformation did not change any documents"));

  QString question=
    QString ("%1 inspected %2 document(s) and will change %3.\n\n"
             "Apply the transformation? A backup of every changed document "
             "will be retained in the Vault.")
      .arg (QString::fromStdString (name.empty () ? "The transformation" : name))
      .arg ((qulonglong) plan.scanned)
      .arg ((qulonglong) plan.rewrites.size ());
  if (QMessageBox::question (
        qApp->activeWindow (), "Run global transformation", question,
        QMessageBox::Apply | QMessageBox::Cancel,
        QMessageBox::Cancel) != QMessageBox::Apply)
    return tree (native_global_transformation_result (
      "cancelled", plan, "Transformation cancelled; no files were changed"));

  GlobalTransformationProgress commit_progress (
    "Run global transformation", qApp->activeWindow ());
  commit_progress.setCancelButton (nullptr);
  commit_progress.setRange (0, 0);
  commit_progress.set_progress_text (
    "Backing up and installing transformed documents...");
  commit_progress.show ();
  QApplication::processEvents ();
  if (!athena_global_transformation_commit (plan, error)) {
    commit_progress.close ();
    QMessageBox::critical (qApp->activeWindow (), "Run global transformation",
                           QString::fromStdString (error));
    return tree (native_global_transformation_result (
      "error", plan, error));
  }
  commit_progress.close ();

  for (const AthenaGlobalTransformationRewrite& rewrite: plan.rewrites) {
    auto open= open_buffers.find (native_normalized_path (rewrite.path).string ());
    if (open != open_buffers.end ())
      set_buffer_tree (open->second, rewrite.transformed);
  }
  athena_clear_transclusion_caches ();
  athena_namespace_ontology_invalidate (true);
  athena_artifact_radioactive_invalidate ();

  return tree (native_global_transformation_result (
    "ok", plan,
    "Transformed " + std::to_string (plan.rewrites.size ()) + " document(s)"));
}

static AthenaVaultfileInfo
native_vaultfile_info_from_array (array<string> fields) {
  std::vector<std::string> values;
  for (int i=0; i<N(fields); i++)
    values.push_back (std::string (as_charp (fields[i]), N(fields[i])));
  return athena_vaultfile_from_fields (values);
}

bool
athena_vaultfile_presentP (url arg1) {
  return bool (athena_vaultfile_present (native_vault_root_path (arg1)));
}

array<string>
athena_vaultfile_read (url arg1) {
  AthenaVaultfileInfo info;
  std::string error;
  if (!athena_vaultfile_read (native_vault_root_path (arg1), info, error))
    return array<string> (array<string> ());
  return array<string> (
    native_vaultfile_fields_to_array (athena_vaultfile_to_fields (info)));
}

array<string>
athena_artifact_title_filter_read (url arg1) {
  url root_url= arg1;
  AthenaArtifactTitleFilter filter=
    is_none (root_url) ? athena_artifact_title_filter_defaults ()
                       : AthenaArtifactTitleFilter ();
  std::string error;
  if (!is_none (root_url) && !athena_artifact_title_filter_read (
        native_vault_root_path (arg1), filter, error))
    return array<string> (array<string> ());
  return array<string> (
    native_vaultfile_fields_to_array (filter.entries));
}

string
athena_vaultfile_write (url arg1, array<string> arg2) {
  std::string error;
  std::filesystem::path root= native_vault_root_path (arg1);
  array<string> fields= arg2;
  AthenaVaultfileInfo info= native_vaultfile_info_from_array (fields);
  AthenaVaultfileInfo previous;
  std::string read_error;
  if (athena_vaultfile_present (root) &&
      athena_vaultfile_read (root, previous, read_error))
    {
      info.backup_dispatchers= previous.backup_dispatchers;
      // Preserve newer Vaultfile fields when an older Scheme caller writes the
      // positional record without knowing about them.
      if (N(fields) < 14) info.materials_db_path= previous.materials_db_path;
      if (N(fields) < 15)
        info.materials_directory= previous.materials_directory;
      if (N(fields) < 16)
        info.artifact_title_filter_path=
          previous.artifact_title_filter_path;
    }
  if (athena_vaultfile_write (root, info, error))
    return string ("");
  return string (string (error.c_str (), error.size ()));
}

string
athena_vaultfile_ensure_json (url arg1) {
  std::string error;
  if (athena_vaultfile_ensure_json (native_vault_root_path (arg1), error))
    return string ("");
  return string (string (error.c_str (), error.size ()));
}

void
athena_vault_backup_dispatch_realtime (url arg1) {
  url saved_file= arg1;
  string saved_path= as_string (concretize (saved_file), URL_SYSTEM);
  const SchemeExecutionContext* context= current_scheme_execution_context ();
  if (context != nullptr && context->editor != nullptr &&
      context->view_id != ATHENA_NO_VIEW) {
    (void) context->editor->publish_ui_text (
      actor_command_kind::ui_vault_backup_dispatch_realtime,
      std::move (saved_path));
    return;
  }
  qtm_vault_backup_dispatch_realtime (to_qstring (saved_path));
  return;
}

tree
athena_reverse_hierarchy_graph_render (tree arg1) {
  tree t= arg1;
  string size= is_atomic (t) ? t->label : tree_as_string (t);
  return tree (reverse_hierarchy_graph_render (size));
}

string
athena_vault_validate_root_namespace () {
  QTMVaultfileInfo info;
  if (!qtm_vaultfile_read (info)) return string ("");
  if (info.rootNamespace.isEmpty ()) return string ("");

  std::shared_ptr<const athena_namespace_definition> ns;
  string root= from_qstring (info.rootNamespace);
  if (athena_namespace_get (root, ns)) return string ("");
  return string (
    "Root namespace in Vaultfile.json is not a valid namespace: " * root);
}

string
athena_namespace_new_file_wizard () {
  if (headless_mode) return string ("");
  return string (namespace_new_file_wizard ());
}

string
athena_namespace_create_file_with_optional_initializer (string arg1) {
  if (headless_mode) return string ("Headless mode has no native namespace initializer chooser.");
  string error;
  if (namespace_create_file_with_optional_initializer (arg1,
                                                      error))
    return string ("");
  return string (error);
}

object
athena_artifact_resolve_uuid (string arg1) {
  url file;
  path source_path;
  if (!artifacts_resolve_uuid (
        arg1, file, source_path))
    return object (false);
  return list_object (object (file), object (source_path));
}
