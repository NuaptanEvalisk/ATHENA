
/******************************************************************************
* MODULE     : glue.cpp
* DESCRIPTION: Glue for linking TeXmacs commands to scheme
* COPYRIGHT  : (C) 1999-2011  Joris van der Hoeven and Massimiliano Gubinelli
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "glue.hpp"
#include "scheme.hpp"

#include "promise.hpp"
#include "tree.hpp"
#include "drd_mode.hpp"
#include "tree_search.hpp"
#include "modification.hpp"
#include "patch.hpp"
#include "new_document.hpp"

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
#include "Database/database.hpp"
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

namespace fs= std::filesystem;

tmscm 
blackboxP (tmscm t) {
  bool b= tmscm_is_blackbox (t);
  return bool_to_tmscm (b);
}

/******************************************************************************
* Miscellaneous routines for use by glue only
******************************************************************************/

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
    win->dockManager ()->addDockWidgetFloating (dock);
  else {
    win->showAdsDockWidget (dock, ads::BottomDockWidgetArea);
    win->restoreAdsLayoutState ();
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

tmscm
image_remove_background_current (tmscm arg1) {
  if (!tmscm_is_string (arg1))
    return string_to_tmscm ("Remove background expects an image path.");

  string file_name= tmscm_to_string (arg1);
  url image= relative (get_current_editor ()->get_name (),
                       url_unix (file_name));
  string error;
  if (!image_remove_white_background_png (image, error))
    return string_to_tmscm (error);
  return string_to_tmscm ("");
}

tmscm
ads_restore_visible_panes () {
  QTMMainTabWindow* win= QTMMainTabWindow::topTabWindow ();
  if (win != nullptr) win->restoreAdsVisiblePanes ();
  return TMSCM_UNSPECIFIED;
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

/******************************************************************************
* Basic assertions
******************************************************************************/

#define TMSCM_ASSERT_STRING(s,arg,rout) \
TMSCM_ASSERT (tmscm_is_string (s), s, arg, rout)
#define TMSCM_ASSERT_BOOL(flag,arg,rout) \
TMSCM_ASSERT (tmscm_is_bool (flag), flag, arg, rout)
#define TMSCM_ASSERT_INT(i,arg,rout) \
TMSCM_ASSERT (tmscm_is_int (i), i, arg, rout);
#define TMSCM_ASSERT_UINT(i,arg,rout) \
TMSCM_ASSERT (tmscm_is_int (i) && scm_positive_p (i), i, arg, rout);
#define TMSCM_ASSERT_DOUBLE(i,arg,rout) \
  TMSCM_ASSERT (tmscm_is_double (i), i, arg, rout);
//TMSCM_ASSERT (SCM_REALP (i), i, arg, rout);
#define TMSCM_ASSERT_URL(u,arg,rout) \
TMSCM_ASSERT (tmscm_is_url (u) || tmscm_is_string (u), u, arg, rout)
#define TMSCM_ASSERT_MODIFICATION(m,arg,rout) \
TMSCM_ASSERT (tmscm_is_modification (m), m, arg, rout)
#define TMSCM_ASSERT_PATCH(p,arg,rout) \
TMSCM_ASSERT (tmscm_is_patch (p), p, arg, rout)
#define TMSCM_ASSERT_BLACKBOX(t,arg,rout) \
TMSCM_ASSERT (tmscm_is_blackbox (t), t, arg, rout)
#define TMSCM_ASSERT_SYMBOL(s,arg,rout) \
  TMSCM_ASSERT (tmscm_is_symbol (s), s, arg, rout)
//TMSCM_ASSERT (SCM_NFALSEP (tmscm_symbol_p (s)), s, arg, rout)

#define TMSCM_ASSERT_OBJECT(a,b,c)
// no check

/******************************************************************************
* Tree labels
******************************************************************************/

#define TMSCM_ASSERT_TREE_LABEL(p,arg,rout) TMSCM_ASSERT_SYMBOL(p,arg,rout)

tmscm 
tree_label_to_tmscm (tree_label l) {
  string s= as_string (l);
  return symbol_to_tmscm (s);
}

tree_label
tmscm_to_tree_label (tmscm p) {
  string s= tmscm_to_symbol (p);
  return make_tree_label (s);
}

/******************************************************************************
* Trees
******************************************************************************/

#define TMSCM_ASSERT_TREE(t,arg,rout) TMSCM_ASSERT (tmscm_is_tree (t), t, arg, rout)


bool
tmscm_is_tree (tmscm u) {
  return (tmscm_is_blackbox (u) && 
         (type_box (tmscm_to_blackbox(u)) == type_helper<tree>::id));
}

tmscm 
tree_to_tmscm (tree o) {
  return blackbox_to_tmscm (close_box<tree> (o));
}

tree
tmscm_to_tree (tmscm obj) {
  return open_box<tree>(tmscm_to_blackbox (obj));
}

tmscm 
treeP (tmscm t) {
  bool b= tmscm_is_blackbox (t) && 
          (type_box (tmscm_to_blackbox(t)) == type_helper<tree>::id);
  return bool_to_tmscm (b);
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

/******************************************************************************
* Scheme trees
******************************************************************************/

#define TMSCM_ASSERT_SCHEME_TREE(p,arg,rout)

tmscm 
scheme_tree_to_tmscm (scheme_tree t) {
  if (is_atomic (t)) {
    string s= t->label;
    if (s == "#t") return tmscm_true ();
    if (s == "#f") return tmscm_false ();
    if (is_int (s)) return int_to_tmscm (as_int (s));
    if (is_quoted (s))
      return string_to_tmscm (scm_unquote (s));
    //if ((N(s)>=2) && (s[0]=='\42') && (s[N(s)-1]=='\42'))
    //return string_to_tmscm (s (1, N(s)-1));
    if (N(s) >= 1 && s[0] == '\'') return symbol_to_tmscm (s (1, N(s)));
    return symbol_to_tmscm (s);
  }
  else {
    int i;
    tmscm p= tmscm_null ();
    for (i=N(t)-1; i>=0; i--)
      p= tmscm_cons (scheme_tree_to_tmscm (t[i]), p);
    return p;
  }
}

scheme_tree
tmscm_to_scheme_tree (tmscm p) {
  if (tmscm_is_list (p)) {
    tree t (TUPLE);
    while (!tmscm_is_null (p)) {
      t << tmscm_to_scheme_tree (tmscm_car (p));
      p= tmscm_cdr (p);
    }
    return t;
  }
  if (tmscm_is_symbol (p)) return tmscm_to_symbol (p);
  if (tmscm_is_string (p)) return scm_quote (tmscm_to_string (p));
  //if (tmscm_is_string (p)) return "\"" * tmscm_to_string (p) * "\"";
  if (tmscm_is_int (p)) return as_string ((int) tmscm_to_int (p));
  if (tmscm_is_bool (p)) return (tmscm_to_bool (p)? string ("#t"): string ("#f"));
  if (tmscm_is_tree (p)) return tree_to_scheme_tree (tmscm_to_tree (p));
  return "?";
}

/******************************************************************************
* Content
******************************************************************************/

bool
tmscm_is_content (tmscm p) {
  if (tmscm_is_string (p) || tmscm_is_tree (p)) return true;
  else if (!tmscm_is_pair (p) || !tmscm_is_symbol (tmscm_car (p))) return false;
  else {
    for (p= tmscm_cdr (p); tmscm_is_pair (p); p= tmscm_cdr (p))
      if (!tmscm_is_content (tmscm_car (p))) return false;
    return tmscm_is_null (p);
  }
}

#define content tree
#define TMSCM_ASSERT_CONTENT(p,arg,rout) \
   TMSCM_ASSERT (tmscm_is_content (p), p, arg, rout)
#define content_to_tmscm tree_to_tmscm

tree
tmscm_to_content (tmscm p) {
  if (tmscm_is_string (p)) return tmscm_to_string (p);
  if (tmscm_is_tree (p)) return tmscm_to_tree (p);
  if (tmscm_is_pair (p)) {
    if (!tmscm_is_symbol (tmscm_car (p))) return "?";
    tree t (make_tree_label (tmscm_to_symbol (tmscm_car (p))));
    p= tmscm_cdr (p);
    while (!tmscm_is_null (p)) {
      t << tmscm_to_content (tmscm_car (p));
      p= tmscm_cdr (p);
    }
    return t;
  }
  return "?";
}

tmscm 
contentP (tmscm t) {
  bool b= tmscm_is_content (t);
  return bool_to_tmscm (b);
}

/******************************************************************************
* Paths
******************************************************************************/

bool
tmscm_is_path (tmscm p) {
  if (tmscm_is_null (p)) return true;
  else return tmscm_is_int (tmscm_car (p)) && tmscm_is_path (tmscm_cdr (p));
}

#define TMSCM_ASSERT_PATH(p,arg,rout) \
TMSCM_ASSERT (tmscm_is_path (p), p, arg, rout)

tmscm 
path_to_tmscm (path p) {
  if (is_nil (p)) return tmscm_null ();
  else return tmscm_cons (int_to_tmscm (p->item), path_to_tmscm (p->next));
}

path
tmscm_to_path (tmscm p) {
  if (tmscm_is_null (p)) return path ();
  else return path ((int) tmscm_to_int (tmscm_car (p)), 
                          tmscm_to_path (tmscm_cdr (p)));
}


/******************************************************************************
* Observers
******************************************************************************/

#define TMSCM_ASSERT_OBSERVER(o,arg,rout) \
TMSCM_ASSERT (tmscm_is_observer (o), o, arg, rout)


bool
tmscm_is_observer (tmscm o) {
  return (tmscm_is_blackbox (o) &&
         (type_box (tmscm_to_blackbox(o)) == type_helper<observer>::id));
}

tmscm 
observer_to_tmscm (observer o) {
  return blackbox_to_tmscm (close_box<observer> (o));
}

static observer
tmscm_to_observer (tmscm obj) {
  return open_box<observer>(tmscm_to_blackbox (obj));
}

tmscm 
observerP (tmscm t) {
  bool b= tmscm_is_blackbox (t) && 
  (type_box (tmscm_to_blackbox(t)) == type_helper<observer>::id);
  return bool_to_tmscm (b);
}


/******************************************************************************
* Widgets
******************************************************************************/

#define TMSCM_ASSERT_WIDGET(o,arg,rout) \
TMSCM_ASSERT (tmscm_is_widget (o), o, arg, rout)

bool
tmscm_is_widget (tmscm u) {
  return (tmscm_is_blackbox (u) &&
         (type_box (tmscm_to_blackbox(u)) == type_helper<widget>::id));
}


static tmscm 
widget_to_tmscm (widget o) {
  return blackbox_to_tmscm (close_box<widget> (o));
}

widget
tmscm_to_widget (tmscm o) {
  return open_box<widget> (tmscm_to_blackbox (o));
}

/******************************************************************************
* Commands
******************************************************************************/

#define TMSCM_ASSERT_COMMAND(o,arg,rout) \
TMSCM_ASSERT (tmscm_is_command (o), o, arg, rout)

bool
tmscm_is_command (tmscm u) {
  return (tmscm_is_blackbox (u) && 
      (type_box (tmscm_to_blackbox(u)) == type_helper<command>::id));
}

static tmscm 
command_to_tmscm (command o) {
  return blackbox_to_tmscm (close_box<command> (o));
}

command
tmscm_to_command (tmscm o) {
  return open_box<command> (tmscm_to_blackbox (o));
}

/******************************************************************************
*  Widget Factory
******************************************************************************/

typedef promise<widget> promise_widget;

#define TMSCM_ASSERT_PROMISE_WIDGET(o,arg,rout) \
TMSCM_ASSERT (tmscm_is_promise_widget (o), o, arg, rout)

bool
tmscm_is_promise_widget (tmscm u) {
  return (tmscm_is_blackbox (u) && 
         (type_box (tmscm_to_blackbox(u)) == type_helper<promise_widget>::id));
}

static tmscm 
promise_widget_to_tmscm (promise_widget o) {
  return blackbox_to_tmscm (close_box<promise_widget> (o));
}

static promise_widget
tmscm_to_promise_widget (tmscm o) {
  return open_box<promise_widget> (tmscm_to_blackbox (o));
}

/******************************************************************************
* Urls
******************************************************************************/

bool
tmscm_is_url (tmscm u) {
  return (tmscm_is_blackbox (u)
              && (type_box (tmscm_to_blackbox(u)) == type_helper<url>::id))
         || (tmscm_is_string(u));
}

tmscm 
url_to_tmscm (url u) {
  return blackbox_to_tmscm (close_box<url> (u));
}

url
tmscm_to_url (tmscm obj) {
  if (tmscm_is_string (obj))
#ifdef OS_MINGW
    return url_system (tmscm_to_string (obj));
#else
  return tmscm_to_string (obj);
#endif
  return open_box<url> (tmscm_to_blackbox (obj));
}

tmscm 
urlP (tmscm t) {
  bool b= tmscm_is_url (t);
  return bool_to_tmscm (b);
}

url url_concat (url u1, url u2) { return u1 * u2; }
url url_or (url u1, url u2) { return u1 | u2; }
void string_save (string s, url u) { (void) save_string (u, s); }
string string_load (url u) {
  string s; (void) load_string (u, s, false); return s; }
void string_append_to_file (string s, url u) { (void) append_string (u, s); }
url url_ref (url u, int i) { return u[i]; }

/******************************************************************************
* Modification
******************************************************************************/

bool
tmscm_is_modification (tmscm m) {
  return (tmscm_is_blackbox (m) &&
	  (type_box (tmscm_to_blackbox(m)) == type_helper<modification>::id))
    || (tmscm_is_string (m));
}

tmscm 
modification_to_tmscm (modification m) {
  return blackbox_to_tmscm (close_box<modification> (m));
}

modification
tmscm_to_modification (tmscm obj) {
  return open_box<modification> (tmscm_to_blackbox (obj));
}

tmscm 
modificationP (tmscm t) {
  bool b= tmscm_is_modification (t);
  return bool_to_tmscm (b);
}

tree
var_apply (tree& t, modification m) {
  apply (t, copy (m));
  return t;
}

tree
var_clean_apply (tree& t, modification m) {
  return clean_apply (t, copy (m));
}

/******************************************************************************
* Patch
******************************************************************************/

bool
tmscm_is_patch (tmscm p) {
  return (tmscm_is_blackbox (p) &&
	  (type_box (tmscm_to_blackbox(p)) == type_helper<patch>::id))
    || (tmscm_is_string (p));
}

tmscm 
patch_to_tmscm (patch p) {
  return blackbox_to_tmscm (close_box<patch> (p));
}

patch
tmscm_to_patch (tmscm obj) {
  return open_box<patch> (tmscm_to_blackbox (obj));
}

tmscm 
patchP (tmscm t) {
  bool b= tmscm_is_patch (t);
  return bool_to_tmscm (b);
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

/******************************************************************************
* Table types
******************************************************************************/

typedef hashmap<string,string> table_string_string;

bool
tmscm_is_table_string_string (tmscm p) {
  if (tmscm_is_null (p)) return true;
  else if (!tmscm_is_pair (p)) return false;
  else {
    tmscm f= tmscm_car (p);
    return tmscm_is_pair (f) &&
    tmscm_is_string (tmscm_car (f)) &&
    tmscm_is_string (tmscm_cdr (f)) &&
    tmscm_is_table_string_string (tmscm_cdr (p));
  }
}

#define TMSCM_ASSERT_TABLE_STRING_STRING(p,arg,rout) \
TMSCM_ASSERT (tmscm_is_table_string_string (p), p, arg, rout)

tmscm 
table_string_string_to_tmscm (hashmap<string,string> t) {
  tmscm p= tmscm_null ();
  iterator<string> it= iterate (t);
  while (it->busy ()) {
    string s= it->next ();
    tmscm n= tmscm_cons (string_to_tmscm (s), string_to_tmscm (t[s]));
    p= tmscm_cons (n, p);
  }
  return p;
}

hashmap<string,string>
tmscm_to_table_string_string (tmscm p) {
  hashmap<string,string> t;
  while (!tmscm_is_null (p)) {
    tmscm n= tmscm_car (p);
    t (tmscm_to_string (tmscm_car (n)))= tmscm_to_string (tmscm_cdr (n));
    p= tmscm_cdr (p);
  }
  return t;
}

#define tmscm_is_solution tmscm_is_table_string_string
#define TMSCM_ASSERT_SOLUTION(p,arg,rout) \
TMSCM_ASSERT (tmscm_is_solution(p), p, arg, rout)
#define solution_to_tmscm table_string_string_to_tmscm
#define tmscm_to_solution tmscm_to_table_string_string

/******************************************************************************
* Several array types
******************************************************************************/

typedef array<int> array_int;
typedef array<SI> array_SI;
typedef array<string> array_string;
typedef array<tree> array_tree;
typedef array<url> array_url;
typedef array<patch> array_patch;
typedef array<path> array_path;
typedef array<widget> array_widget;
typedef array<double> array_double;
typedef array<array<double> > array_array_double;
typedef array<array<array<double> > > array_array_array_double;

static bool
tmscm_is_array_int (tmscm p) {
  if (tmscm_is_null (p)) return true;
  else return tmscm_is_pair (p) &&
    tmscm_is_int (tmscm_car (p)) &&
    tmscm_is_array_int (tmscm_cdr (p));
}

#define TMSCM_ASSERT_ARRAY_INT(p,arg,rout) \
TMSCM_ASSERT (tmscm_is_array_int (p), p, arg, rout)

/* static */ tmscm 
array_int_to_tmscm (array<int> a) {
  int i, n= N(a);
  tmscm p= tmscm_null ();
  for (i=n-1; i>=0; i--) p= tmscm_cons (int_to_tmscm (a[i]), p);
  return p;
}

/* static */ array<int>
tmscm_to_array_int (tmscm p) {
  array<int> a;
  while (!tmscm_is_null (p)) {
    a << ((int) tmscm_to_int (tmscm_car (p)));
    p= tmscm_cdr (p);
  }
  return a;
}

// FIXME: we also should introduce separate converters for SI
#define tmscm_is_SI tmscm_is_int
#define tmscm_to_SI tmscm_to_int
#define SI_to_tmscm int_to_tmscm

/* NOTE: not yet needed
static bool
tmscm_is_array_SI (tmscm p) {
  if (tmscm_is_null (p)) return true;
  else return tmscm_is_pair (p) &&
    tmscm_is_SI (tmscm_car (p)) &&
    tmscm_is_array_SI (tmscm_cdr (p));
}

#define TMSCM_ASSERT_ARRAY_SI(p,arg,rout) \
TMSCM_ASSERT (tmscm_is_array_SI (p), p, arg, rout)
*/

/* static */ tmscm 
array_SI_to_tmscm (array<SI> a) {
  int i, n= N(a);
  tmscm p= tmscm_null ();
  for (i=n-1; i>=0; i--) p= tmscm_cons (SI_to_tmscm (a[i]), p);
  return p;
}

/* static */ array<SI>
tmscm_to_array_SI (tmscm p) {
  array<SI> a;
  while (!tmscm_is_null (p)) {
    a << ((SI) tmscm_to_SI (tmscm_car (p)));
    p= tmscm_cdr (p);
  }
  return a;
}

static bool
tmscm_is_array_string (tmscm p) {
  if (tmscm_is_null (p)) return true;
  else return tmscm_is_pair (p) && 
    tmscm_is_string (tmscm_car (p)) &&
    tmscm_is_array_string (tmscm_cdr (p));
}


/* static */ bool
tmscm_is_array_double (tmscm p) {
  if (tmscm_is_null (p)) return true;
  else return tmscm_is_pair (p) &&
    tmscm_is_double (tmscm_car (p)) &&
    tmscm_is_array_double (tmscm_cdr (p));
}

#define TMSCM_ASSERT_ARRAY_DOUBLE(p,arg,rout) \
TMSCM_ASSERT (tmscm_is_array_double (p), p, arg, rout)

/* static */ tmscm 
array_double_to_tmscm (array<double> a) {
  int i, n= N(a);
  tmscm p= tmscm_null();
  for (i=n-1; i>=0; i--) p= tmscm_cons (double_to_tmscm (a[i]), p);
  return p;
}

/* static */ array<double>
tmscm_to_array_double (tmscm p) {
  array<double> a;
  while (!tmscm_is_null (p)) {
    a << ((double) tmscm_to_double (tmscm_car (p)));
    p= tmscm_cdr (p);
  }
  return a;
}

static bool
tmscm_is_array_array_double (tmscm p) {
  if (tmscm_is_null (p)) return true;
  else return tmscm_is_pair (p) &&
    tmscm_is_array_double (tmscm_car (p)) &&
    tmscm_is_array_array_double (tmscm_cdr (p));
}

#define TMSCM_ASSERT_ARRAY_ARRAY_DOUBLE(p,arg,rout) \
TMSCM_ASSERT (tmscm_is_array_array_double (p), p, arg, rout)

/* static */ tmscm 
array_array_double_to_tmscm (array<array_double> a) {
  int i, n= N(a);
  tmscm p= tmscm_null ();
  for (i=n-1; i>=0; i--) p= tmscm_cons (array_double_to_tmscm (a[i]), p);
  return p;
}

/* static */ array<array_double>
tmscm_to_array_array_double (tmscm p) {
  array<array_double> a;
  while (!tmscm_is_null (p)) {
    a << ((array_double) tmscm_to_array_double (tmscm_car (p)));
    p= tmscm_cdr (p);
  }
  return a;
}

static bool
tmscm_is_array_array_array_double (tmscm p) {
  if (tmscm_is_null (p)) return true;
  else return tmscm_is_pair (p) &&
    tmscm_is_array_array_double (tmscm_car (p)) &&
    tmscm_is_array_array_array_double (tmscm_cdr (p));
}

#define TMSCM_ASSERT_ARRAY_ARRAY_ARRAY_DOUBLE(p,arg,rout) \
TMSCM_ASSERT (tmscm_is_array_array_array_double (p), p, arg, rout)

/* static */ tmscm 
array_array_array_double_to_tmscm (array<array_array_double> a) {
  int i, n= N(a);
  tmscm p= tmscm_null ();
  for (i=n-1; i>=0; i--) p= tmscm_cons (array_array_double_to_tmscm (a[i]), p);
  return p;
}

/* static */ array<array_array_double>
tmscm_to_array_array_array_double (tmscm p) {
  array<array_array_double> a;
  while (!tmscm_is_null (p)) {
    a << ((array_array_double) tmscm_to_array_array_double (tmscm_car (p)));
    p= tmscm_cdr (p);
  }
  return a;
}

void register_glyph (string s, array_array_array_double gl);
string recognize_glyph (array_array_array_double gl);



#define TMSCM_ASSERT_ARRAY_STRING(p,arg,rout) \
TMSCM_ASSERT (tmscm_is_array_string (p), p, arg, rout)

/* static */ tmscm 
array_string_to_tmscm (array<string> a) {
  int i, n= N(a);
  tmscm p= tmscm_null ();
  for (i=n-1; i>=0; i--) p= tmscm_cons (string_to_tmscm (a[i]), p);
  return p;
}

/* static */ array<string>
tmscm_to_array_string (tmscm p) {
  array<string> a;
  while (!tmscm_is_null (p)) {
    a << tmscm_to_string (tmscm_car (p));
    p= tmscm_cdr (p);
  }
  return a;
}

static bool
tmscm_is_array_tree (tmscm p) {
  if (tmscm_is_null (p)) return true;
  else return tmscm_is_pair (p) && 
    tmscm_is_tree (tmscm_car (p)) &&
    tmscm_is_array_tree (tmscm_cdr (p));
}

#define TMSCM_ASSERT_ARRAY_TREE(p,arg,rout) \
TMSCM_ASSERT (tmscm_is_array_tree (p), p, arg, rout)

/* static */ tmscm 
array_tree_to_tmscm (array<tree> a) {
  int i, n= N(a);
  tmscm p= tmscm_null ();
  for (i=n-1; i>=0; i--) p= tmscm_cons (tree_to_tmscm (a[i]), p);
  return p;
}

/* static */ array<tree>
tmscm_to_array_tree (tmscm p) {
  array<tree> a;
  while (!tmscm_is_null (p)) {
    a << tmscm_to_tree (tmscm_car (p));
    p= tmscm_cdr (p);
  }
  return a;
}

static bool
tmscm_is_array_widget (tmscm p) {
  if (tmscm_is_null (p)) return true;
  else return tmscm_is_pair (p) &&
    tmscm_is_widget (tmscm_car (p)) &&
    tmscm_is_array_widget (tmscm_cdr (p));
}

#define TMSCM_ASSERT_ARRAY_WIDGET(p,arg,rout) \
TMSCM_ASSERT (tmscm_is_array_widget (p), p, arg, rout)

/* static */ tmscm 
array_widget_to_tmscm (array<widget> a) {
  int i, n= N(a);
  tmscm p= tmscm_null ();
  for (i=n-1; i>=0; i--) p= tmscm_cons (widget_to_tmscm (a[i]), p);
  return p;
}

/* static */ array<widget>
tmscm_to_array_widget (tmscm p) {
  array<widget> a;
  while (!tmscm_is_null (p)) {
    a << tmscm_to_widget (tmscm_car (p));
    p= tmscm_cdr (p);
  }
  return a;
}

static bool
tmscm_is_array_url (tmscm p) {
  if (tmscm_is_null (p)) return true;
  else return tmscm_is_pair (p) &&
    tmscm_is_url (tmscm_car (p)) &&
    tmscm_is_array_url (tmscm_cdr (p));
}


#define TMSCM_ASSERT_ARRAY_URL(p,arg,rout) \
TMSCM_ASSERT (tmscm_is_array_url (p), p, arg, rout)

/* static */ tmscm 
array_url_to_tmscm (array<url> a) {
  int i, n= N(a);
  tmscm p= tmscm_null ();
  for (i=n-1; i>=0; i--) p= tmscm_cons (url_to_tmscm (a[i]), p);
  return p;
}

/* static */ array<url>
tmscm_to_array_url (tmscm p) {
  array<url> a;
  while (!tmscm_is_null (p)) {
    a << tmscm_to_url (tmscm_car (p));
    p= tmscm_cdr (p);
  }
  return a;
}

static bool
tmscm_is_array_patch (tmscm p) {
  if (tmscm_is_null (p)) return true;
  else return tmscm_is_pair (p) &&
    tmscm_is_patch (tmscm_car (p)) &&
    tmscm_is_array_patch (tmscm_cdr (p));
}


#define TMSCM_ASSERT_ARRAY_PATCH(p,arg,rout) \
TMSCM_ASSERT (tmscm_is_array_patch (p), p, arg, rout)

/* static */ tmscm 
array_patch_to_tmscm (array<patch> a) {
  int i, n= N(a);
  tmscm p= tmscm_null ();
  for (i=n-1; i>=0; i--) p= tmscm_cons (patch_to_tmscm (a[i]), p);
  return p;
}

/* static */ array<patch>
tmscm_to_array_patch (tmscm p) {
  array<patch> a;
  while (!tmscm_is_null (p)) {
    a << tmscm_to_patch (tmscm_car (p));
    p= tmscm_cdr (p);
  }
  return a;
}

static bool
tmscm_is_array_path (tmscm p) {
  if (tmscm_is_null (p)) return true;
  else return tmscm_is_pair (p) &&
    tmscm_is_path (tmscm_car (p)) &&
    tmscm_is_array_path (tmscm_cdr (p));
}

#define TMSCM_ASSERT_ARRAY_PATH(p,arg,rout) \
TMSCM_ASSERT (tmscm_is_array_path (p), p, arg, rout)

/* static */ tmscm 
array_path_to_tmscm (array<path> a) {
  int i, n= N(a);
  tmscm p= tmscm_null ();
  for (i=n-1; i>=0; i--) p= tmscm_cons (path_to_tmscm (a[i]), p);
  return p;
}

/* static */ array<path>
tmscm_to_array_path (tmscm p) {
  array<path> a;
  while (!tmscm_is_null (p)) {
    a << tmscm_to_path (tmscm_car (p));
    p= tmscm_cdr (p);
  }
  return a;
}

/******************************************************************************
* List types
******************************************************************************/

typedef list<string> list_string;

bool
tmscm_is_list_string (tmscm p) {
  if (tmscm_is_null (p)) return true;
  else return tmscm_is_pair (p) &&
    tmscm_is_string (tmscm_car (p)) &&
    tmscm_is_list_string (tmscm_cdr (p));
}

#define TMSCM_ASSERT_LIST_STRING(p,arg,rout) \
TMSCM_ASSERT (tmscm_is_list_string (p), p, arg, rout)

tmscm 
list_string_to_tmscm (list_string l) {
  if (is_nil (l)) return tmscm_null ();
  return tmscm_cons (string_to_tmscm (l->item),
           list_string_to_tmscm (l->next));
}

list_string
tmscm_to_list_string (tmscm p) {
  if (tmscm_is_null (p)) return list_string ();
  return list_string (tmscm_to_string (tmscm_car (p)),
            tmscm_to_list_string (tmscm_cdr (p)));
}

typedef list<tree> list_tree;

bool
tmscm_is_list_tree (tmscm p) {
  if (tmscm_is_null (p)) return true;
  else return tmscm_is_pair (p) &&
    tmscm_is_tree (tmscm_car (p)) &&
    tmscm_is_list_tree (tmscm_cdr (p));
}

#define TMSCM_ASSERT_LIST_TREE(p,arg,rout) \
TMSCM_ASSERT (tmscm_is_list_tree (p), p, arg, rout)

tmscm 
list_tree_to_tmscm (list_tree l) {
  if (is_nil (l)) return tmscm_null ();
  return tmscm_cons (tree_to_tmscm (l->item),
           list_tree_to_tmscm (l->next));
}

list_tree
tmscm_to_list_tree (tmscm p) {
  if (tmscm_is_null (p)) return list_tree ();
  return list_tree (tmscm_to_tree (tmscm_car (p)),
            tmscm_to_list_tree (tmscm_cdr (p)));
}

/******************************************************************************
* Gluing
******************************************************************************/

#include "server.hpp"
#include "tm_window.hpp"
#include "boot.hpp"
#include "connect.hpp"
#include "convert.hpp"
#include "file.hpp"
#include "image_files.hpp"
#include "web_files.hpp"
#include "sys_utils.hpp"
#include "analyze.hpp"
#include "wencoding.hpp"
#include "base64.hpp"
#include "tree_traverse.hpp"
#include "tree_analyze.hpp"
#include "tree_correct.hpp"
#include "tree_modify.hpp"
#include "tree_math_stats.hpp"
#include "tm_frame.hpp"
#include "Concat/concater.hpp"
#include "converter.hpp"
#include "tm_timer.hpp"
#include "Metafont/tex_files.hpp"
#include "Freetype/tt_file.hpp"
#include "LaTeX_Preview/latex_preview.hpp"
#include "link.hpp"
#include "dictionary.hpp"
#include "patch.hpp"
#include "packrat.hpp"
#include "new_style.hpp"
#include "persistent.hpp"

#include "Pdf/pdf_hummus_extract_attachment.hpp"
#include "Pdf/pdf_hummus_make_attachment.hpp"

#include "../Glue/glue_basic.cpp"
#include "../Glue/glue_editor.cpp"
#include "../Glue/glue_server.cpp"

tmscm
tmg_visual_buffer_switcher_choose (tmscm arg1) {
  TMSCM_ASSERT_ARRAY_STRING (arg1, TMSCM_ARG1,
                             "visual-buffer-switcher-choose");

  array_string in1= tmscm_to_array_string (arg1);
  string out= visual_buffer_switcher_choose (in1);
  return string_to_tmscm (out);
}

tmscm
tmg_heading_fold_toggle () {
  bool out= get_current_editor()->heading_fold_toggle ();
  return bool_to_tmscm (out);
}

tmscm
tmg_heading_fold_current () {
  bool out= get_current_editor()->heading_fold_current ();
  return bool_to_tmscm (out);
}

tmscm
tmg_heading_unfold_current () {
  bool out= get_current_editor()->heading_unfold_current ();
  return bool_to_tmscm (out);
}

tmscm
tmg_heading_fold_toggle_path (tmscm arg1) {
  TMSCM_ASSERT_STRING (arg1, TMSCM_ARG1, "heading-fold-toggle-path");

  string in1= tmscm_to_string (arg1);
  bool out= get_current_editor()->heading_fold_toggle_at (in1);
  return bool_to_tmscm (out);
}

tmscm
tmg_heading_word_count_path (tmscm arg1) {
  TMSCM_ASSERT_PATH (arg1, TMSCM_ARG1, "heading-word-count-path");

  path in1= tmscm_to_path (arg1);
  int out= get_current_editor()->heading_word_count_at (in1);
  return int_to_tmscm (out);
}

tmscm
tmg_heading_unfold_all () {
  get_current_editor()->heading_unfold_all ();
  return TMSCM_UNSPECIFIED;
}

tmscm
tmg_toc_fold_set_path (tmscm arg1, tmscm arg2) {
  TMSCM_ASSERT_PATH (arg1, TMSCM_ARG1, "toc-fold-set-path");
  TMSCM_ASSERT_BOOL (arg2, TMSCM_ARG2, "toc-fold-set-path");
  bool out= get_current_editor()->toc_fold_set_at (
    tmscm_to_path (arg1), tmscm_to_bool (arg2));
  return bool_to_tmscm (out);
}

tmscm
tmg_native_info_dialog (tmscm arg1, tmscm arg2) {
  TMSCM_ASSERT_STRING (arg1, TMSCM_ARG1, "native-info-dialog");
  TMSCM_ASSERT_STRING (arg2, TMSCM_ARG2, "native-info-dialog");

  if (headless_mode) return TMSCM_UNSPECIFIED;

  string message= tmscm_to_string (arg1);
  string title  = tmscm_to_string (arg2);

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

  return TMSCM_UNSPECIFIED;
}

tmscm
tmg_native_anchor_enunciations_confirm (tmscm arg1, tmscm arg2,
                                        tmscm arg3, tmscm arg4) {
  TMSCM_ASSERT_STRING (arg1, TMSCM_ARG1,
                       "native-anchor-enunciations-confirm");
  TMSCM_ASSERT_STRING (arg2, TMSCM_ARG2,
                       "native-anchor-enunciations-confirm");
  TMSCM_ASSERT_STRING (arg3, TMSCM_ARG3,
                       "native-anchor-enunciations-confirm");
  TMSCM_ASSERT_STRING (arg4, TMSCM_ARG4,
                       "native-anchor-enunciations-confirm");

  if (headless_mode) return bool_to_tmscm (false);

  QString wraps= to_qstring (tmscm_to_string (arg1));
  QString dead = to_qstring (tmscm_to_string (arg2));
  QString headings= to_qstring (tmscm_to_string (arg3));
  QString notes= to_qstring (tmscm_to_string (arg4));

  notes.replace ("<<<ATHENA-ANCHOR-ACTION>>>", "\n");
  notes.replace ("\\r\\n", "\n");
  notes.replace ("\\n", "\n");
  notes.replace ("\\t", "\t");
  notes.replace ("\r\n", "\n");
  notes.replace ('\r', '\n');

  QStringList items;
  QString current;
  for (int i=0; i<notes.size (); i++) {
    QChar c= notes.at (i);
    if (c == '\n' || c == '\t' || c.unicode () == 0x1e ||
        c.unicode () == 0x00af) {
      QString trimmed= current.trimmed ();
      if (!trimmed.isEmpty ()) items << trimmed;
      current.clear ();
    }
    else current.append (c);
  }
  QString trimmed= current.trimmed ();
  if (!trimmed.isEmpty ()) items << trimmed;

  if (items.size () <= 1 && !notes.trimmed ().isEmpty ()) {
    QString compact= notes.simplified ();
    QStringList split;
    int start= 0;
    for (int i=1; i<compact.size (); i++) {
      bool boundary=
        compact.mid (i).startsWith ("wrap ") ||
        compact.mid (i).startsWith ("anchor heading: ") ||
        compact.mid (i).startsWith ("remove dead anchors: ");
      if (boundary && compact.at (i - 1).isSpace ()) {
        QString item= compact.mid (start, i - start).trimmed ();
        if (!item.isEmpty ()) split << item;
        start= i;
      }
    }
    QString item= compact.mid (start).trimmed ();
    if (!item.isEmpty ()) split << item;
    if (split.size () > items.size ()) items= split;
  }

  QDialog dialog (QApplication::activeWindow ());
  dialog.setWindowTitle ("Anchor structures");
  dialog.resize (760, 480);

  QVBoxLayout* layout= new QVBoxLayout (&dialog);

  QLabel* intro= new QLabel (
    QString ("ATHENA will wrap %1 enunciation(s), add %2 heading anchor(s), "
             "and remove %3 dead anchor pair(s). Review the planned changes "
             "before applying them.")
      .arg (wraps, headings, dead),
    &dialog);
  intro->setWordWrap (true);
  layout->addWidget (intro);

  QLabel* list_label= new QLabel ("Planned actions:", &dialog);
  layout->addWidget (list_label);

  QListWidget* list= new QListWidget (&dialog);
  list->setAlternatingRowColors (true);
  list->setSelectionMode (QAbstractItemView::NoSelection);
  list->setWordWrap (true);
  list->setHorizontalScrollBarPolicy (Qt::ScrollBarAlwaysOff);
  list->setMinimumHeight (300);
  if (items.isEmpty ())
    list->addItem ("No individual action details are available.");
  else
    for (const QString& item: items)
      list->addItem (item);
  layout->addWidget (list, 1);

  QDialogButtonBox* buttons= new QDialogButtonBox (&dialog);
  QPushButton* apply= buttons->addButton ("Apply", QDialogButtonBox::AcceptRole);
  QPushButton* cancel= buttons->addButton (QDialogButtonBox::Cancel);
  cancel->setDefault (true);
  apply->setAutoDefault (false);
  QObject::connect (buttons, &QDialogButtonBox::accepted,
                    &dialog, &QDialog::accept);
  QObject::connect (buttons, &QDialogButtonBox::rejected,
                    &dialog, &QDialog::reject);
  layout->addWidget (buttons);

  return bool_to_tmscm (dialog.exec () == QDialog::Accepted);
}

tmscm
tmg_native_font_selector (tmscm arg1, tmscm arg2, tmscm arg3, tmscm arg4,
                          tmscm arg5) {
  TMSCM_ASSERT_STRING (arg1, TMSCM_ARG1, "native-font-selector");
  TMSCM_ASSERT_STRING (arg2, TMSCM_ARG2, "native-font-selector");
  TMSCM_ASSERT_STRING (arg3, TMSCM_ARG3, "native-font-selector");
  TMSCM_ASSERT_STRING (arg4, TMSCM_ARG4, "native-font-selector");
  TMSCM_ASSERT_STRING (arg5, TMSCM_ARG5, "native-font-selector");

  if (headless_mode) return tmscm_null ();

  array<string> result=
    native_font_selector_dialog (tmscm_to_string (arg1),
                                 tmscm_to_string (arg2),
                                 tmscm_to_string (arg3),
                                 tmscm_to_string (arg4),
                                 tmscm_to_string (arg5));
  return array_string_to_tmscm (result);
}

tmscm
tmg_native_open_preferences () {
  qtm_preferences_dialog_show ();
  return TMSCM_UNSPECIFIED;
}

tmscm
tmg_native_preferences_openP () {
  return bool_to_tmscm (qtm_preferences_dialog_open ());
}

tmscm
tmg_native_preferences_export_privacy () {
  return int_to_tmscm (qtm_preferences_export_privacy_dialog ());
}

tmscm
tmg_native_preferences_export_metadata () {
  array<string> result;
  const QStringList metadata= qtm_preferences_export_metadata ();
  for (const QString& field: metadata) {
    const QByteArray utf8= field.toUtf8 ();
    result << string (utf8.constData (), utf8.size ());
  }
  return array_string_to_tmscm (result);
}

tmscm
tmg_native_preference_sensitiveP (tmscm arg1) {
  TMSCM_ASSERT_STRING (arg1, TMSCM_ARG1, "native-preference-sensitive?");
  return bool_to_tmscm (
    user_preference_is_sensitive (tmscm_to_string (arg1)));
}

tmscm
tmg_native_open_page_setup () {
  qtm_page_setup_dialog_show ();
  return TMSCM_UNSPECIFIED;
}

tmscm
tmg_escape_symbol_picker () {
  if (headless_mode) return string_to_tmscm ("");
  return string_to_tmscm (escape_symbol_picker_dialog ());
}

tmscm
tmg_google_cloud_todo_sync_buffer (tmscm arg1) {
  TMSCM_ASSERT_URL (arg1, TMSCM_ARG1, "google-cloud-todo-sync-buffer");

  google_cloud_todo_sync_buffer (tmscm_to_url (arg1), true);
  return TMSCM_UNSPECIFIED;
}

tmscm
tmg_google_cloud_todo_sync_open_buffers () {
  google_cloud_todo_sync_open_buffers (false);
  return TMSCM_UNSPECIFIED;
}

tmscm
tmg_google_cloud_todo_push_item (tmscm arg1, tmscm arg2) {
  TMSCM_ASSERT_TREE (arg1, TMSCM_ARG1, "google-cloud-todo-push-item");
  TMSCM_ASSERT_BOOL (arg2, TMSCM_ARG2, "google-cloud-todo-push-item");

  google_cloud_todo_push_item (tmscm_to_tree (arg1), tmscm_to_bool (arg2));
  return TMSCM_UNSPECIFIED;
}

tmscm
tmg_codex_initialize_models (tmscm arg1, tmscm arg2) {
  TMSCM_ASSERT_STRING (arg1, TMSCM_ARG1, "codex-initialize-models");
  TMSCM_ASSERT_STRING (arg2, TMSCM_ARG2, "codex-initialize-models");
  if (!headless_mode)
    qtm_codex_initialize_models (tmscm_to_string (arg1),
                                 tmscm_to_string (arg2));
  return TMSCM_UNSPECIFIED;
}

tmscm
tmg_codex_completion_options (tmscm arg1, tmscm arg2) {
  TMSCM_ASSERT_STRING (arg1, TMSCM_ARG1, "codex-completion-options");
  TMSCM_ASSERT_STRING (arg2, TMSCM_ARG2, "codex-completion-options");
  if (headless_mode) return tmscm_null ();
  return array_string_to_tmscm (
    qtm_codex_completion_options (tmscm_to_string (arg1),
                                  tmscm_to_string (arg2)));
}

tmscm
tmg_codex_run_completion_async (tmscm arg1, tmscm arg2, tmscm arg3,
                                tmscm arg4, tmscm arg5, tmscm arg6,
                                tmscm arg7, tmscm arg8, tmscm arg9,
                                tmscm arg10) {
  TMSCM_ASSERT_STRING (arg1, TMSCM_ARG1, "codex-run-completion-async");
  TMSCM_ASSERT_STRING (arg2, TMSCM_ARG2, "codex-run-completion-async");
  TMSCM_ASSERT_STRING (arg3, TMSCM_ARG3, "codex-run-completion-async");
  TMSCM_ASSERT_STRING (arg4, TMSCM_ARG4, "codex-run-completion-async");
  TMSCM_ASSERT_STRING (arg5, TMSCM_ARG5, "codex-run-completion-async");
  TMSCM_ASSERT_STRING (arg6, TMSCM_ARG6, "codex-run-completion-async");
  TMSCM_ASSERT_STRING (arg7, TMSCM_ARG7, "codex-run-completion-async");
  TMSCM_ASSERT_STRING (arg8, TMSCM_ARG8, "codex-run-completion-async");
  TMSCM_ASSERT_ARRAY_STRING (arg9, TMSCM_ARG9,
                             "codex-run-completion-async");
  TMSCM_ASSERT_COMMAND (arg10, TMSCM_ARG10, "codex-run-completion-async");

  QString bridge= to_qstring (tmscm_to_string (arg1));
  QString home= to_qstring (tmscm_to_string (arg2));
  QString input= to_qstring (tmscm_to_string (arg3));
  QString output= to_qstring (tmscm_to_string (arg4));
  QString model= to_qstring (tmscm_to_string (arg5));
  QString effort= to_qstring (tmscm_to_string (arg6));
  QString serviceTier= to_qstring (tmscm_to_string (arg7));
  QString webSearch= to_qstring (tmscm_to_string (arg8));
  array<string> imagePaths= tmscm_to_array_string (arg9);
  command callback= tmscm_to_command (arg10);

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
  return TMSCM_UNSPECIFIED;
}

tmscm
tmg_vault_load_with_ns (tmscm arg1, tmscm arg2, tmscm arg3, tmscm arg4) {
  TMSCM_ASSERT_URL (arg1, TMSCM_ARG1, "vault-load-with-ns");
  TMSCM_ASSERT_STRING (arg2, TMSCM_ARG2, "vault-load-with-ns");
  TMSCM_ASSERT_STRING (arg3, TMSCM_ARG3, "vault-load-with-ns");
  TMSCM_ASSERT_STRING (arg4, TMSCM_ARG4, "vault-load-with-ns");

  string error= vault_load (tmscm_to_url (arg1), tmscm_to_string (arg2),
                            tmscm_to_string (arg3), tmscm_to_string (arg4));
  return string_to_tmscm (error);
}

tmscm
tmg_vault_rewrite_anchor_references (tmscm arg1, tmscm arg2) {
  TMSCM_ASSERT_STRING (arg1, TMSCM_ARG1,
                       "vault-rewrite-anchor-references");
  TMSCM_ASSERT_STRING (arg2, TMSCM_ARG2,
                       "vault-rewrite-anchor-references");
  size_t changed= vault_rewrite_anchor_references (tmscm_to_string (arg1),
                                                    tmscm_to_string (arg2));
  return int_to_tmscm ((int) changed);
}

tmscm
tmg_vault_maintenance_setup (tmscm arg1) {
  TMSCM_ASSERT_URL (arg1, TMSCM_ARG1, "vault-maintenance-setup");
  string root= concretize (tmscm_to_url (arg1));
  return tree_to_tmscm (qtm_vault_maintenance_setup (root));
}

static std::filesystem::path
tmg_vault_root_path (tmscm arg) {
  string s= concretize (tmscm_to_url (arg));
  return std::filesystem::path (std::string (as_charp (s), N(s)));
}

static array<string>
tmg_vaultfile_fields_to_array (const std::vector<std::string>& fields) {
  array<string> out;
  for (const std::string& field: fields)
    out << string (field.c_str (), field.size ());
  return out;
}

static tree
tmg_utf8_text (const std::string& value) {
  return tree (utf8_to_cork (string (value.data (), value.size ())));
}

static std::string
tmg_cork_to_utf8 (tmscm value) {
  string converted= cork_to_utf8 (tmscm_to_string (value));
  return std::string (as_charp (converted), (size_t) N(converted));
}

static tree
tmg_integer_text (std::int64_t value) {
  return tree (std::to_string (value).c_str ());
}

static MaterialsStore*
tmg_materials_store (const char* routine) {
  MaterialsStore* store= vault_get_materials_store ();
  if (store == nullptr)
    FAILED (c_string (string (routine) * ": no active Materials database"));
  return store;
}

static tree
tmg_material_record_tree (const MaterialRecord& material) {
  tree fields (TUPLE);
  for (const MaterialField& field: material.fields)
    fields << compound ("material-field", tmg_utf8_text (field.name),
                        tmg_utf8_text (field.value),
                        tmg_utf8_text (field.language),
                        tmg_integer_text (field.ordinal));
  tree creators (TUPLE);
  for (const MaterialCreator& creator: material.creators)
    creators << compound ("material-creator", tmg_utf8_text (creator.role),
                          tmg_utf8_text (creator.given),
                          tmg_utf8_text (creator.family),
                          tmg_utf8_text (creator.literal),
                          tmg_utf8_text (creator.suffix),
                          tmg_integer_text (creator.ordinal));
  tree identifiers (TUPLE);
  for (const MaterialIdentifier& identifier: material.identifiers)
    identifiers << compound ("material-identifier",
                              tmg_utf8_text (identifier.scheme),
                              tmg_utf8_text (identifier.value),
                              tmg_utf8_text (identifier.normalized_value));
  tree tags (TUPLE);
  for (const std::string& tag: material.tags) tags << tmg_utf8_text (tag);

  array<tree> children;
  children << tmg_utf8_text (material.uuid)
           << tmg_utf8_text (material.item_type)
           << tmg_utf8_text (material.review_state)
           << tmg_integer_text (material.revision)
           << tmg_integer_text (material.created_at)
           << tmg_integer_text (material.updated_at)
           << fields << creators << identifiers << tags;
  return compound ("material-record", children);
}

tmscm
tmg_material_resolve_uuid (tmscm arg1) {
  TMSCM_ASSERT_STRING (arg1, TMSCM_ARG1, "material-resolve-uuid");
  MaterialsStore* store= tmg_materials_store ("material-resolve-uuid");
  std::string error;
  std::string resolved= store->resolve_uuid (tmg_cork_to_utf8 (arg1), error);
  if (!error.empty ())
    FAILED (c_string ("material-resolve-uuid: " * string (error.c_str ())));
  if (resolved.empty ()) return bool_to_tmscm (false);
  return string_to_tmscm (utf8_to_cork (
    string (resolved.data (), resolved.size ())));
}

tmscm
tmg_material_find_by_identifier (tmscm arg1, tmscm arg2) {
  TMSCM_ASSERT_STRING (arg1, TMSCM_ARG1, "material-find-by-identifier");
  TMSCM_ASSERT_STRING (arg2, TMSCM_ARG2, "material-find-by-identifier");
  MaterialsStore* store= tmg_materials_store ("material-find-by-identifier");
  std::string error;
  std::optional<std::string> found= store->material_for_identifier (
    tmg_cork_to_utf8 (arg1), tmg_cork_to_utf8 (arg2), error);
  if (!error.empty ())
    FAILED (c_string ("material-find-by-identifier: " *
                      string (error.c_str ())));
  if (!found) return bool_to_tmscm (false);
  return string_to_tmscm (utf8_to_cork (
    string (found->data (), found->size ())));
}

tmscm
tmg_material_get (tmscm arg1) {
  TMSCM_ASSERT_STRING (arg1, TMSCM_ARG1, "material-get");
  MaterialsStore* store= tmg_materials_store ("material-get");
  std::string error;
  std::optional<MaterialRecord> material=
    store->get (tmg_cork_to_utf8 (arg1), error);
  if (!error.empty ())
    FAILED (c_string ("material-get: " * string (error.c_str ())));
  if (!material) return bool_to_tmscm (false);
  return tree_to_tmscm (tmg_material_record_tree (*material));
}

static tree
tmg_material_hits_tree (const std::vector<MaterialSearchHit>& hits) {
  tree result (TUPLE);
  for (const MaterialSearchHit& hit: hits) {
    array<tree> values;
    values << tmg_utf8_text (hit.uuid)
           << tmg_utf8_text (hit.item_type)
           << tmg_utf8_text (hit.title)
           << tmg_utf8_text (hit.creators)
           << tmg_utf8_text (hit.issued)
           << tmg_utf8_text (hit.review_state)
           << tree (as_string (hit.rank));
    result << compound ("material-search-hit", values);
  }
  return result;
}

tmscm
tmg_materials_search (tmscm arg1, tmscm arg2) {
  TMSCM_ASSERT_STRING (arg1, TMSCM_ARG1, "materials-search");
  TMSCM_ASSERT_INT (arg2, TMSCM_ARG2, "materials-search");
  MaterialsStore* store= tmg_materials_store ("materials-search");
  std::string error;
  int limit= std::max (1, std::min (tmscm_to_int (arg2), 1000));
  std::vector<MaterialSearchHit> hits=
    store->search (tmg_cork_to_utf8 (arg1), limit, error);
  if (!error.empty ())
    FAILED (c_string ("materials-search: " * string (error.c_str ())));
  return tree_to_tmscm (tmg_material_hits_tree (hits));
}

tmscm
tmg_materials_list (tmscm arg1, tmscm arg2) {
  TMSCM_ASSERT_INT (arg1, TMSCM_ARG1, "materials-list");
  TMSCM_ASSERT_INT (arg2, TMSCM_ARG2, "materials-list");
  MaterialsStore* store= tmg_materials_store ("materials-list");
  std::string error;
  int limit= std::max (1, std::min (tmscm_to_int (arg1), 1000));
  int offset= std::max (0, tmscm_to_int (arg2));
  std::vector<MaterialSearchHit> hits= store->list (limit, offset, error);
  if (!error.empty ())
    FAILED (c_string ("materials-list: " * string (error.c_str ())));
  return tree_to_tmscm (tmg_material_hits_tree (hits));
}

tmscm
tmg_material_choose_citation (tmscm arg1) {
  TMSCM_ASSERT_STRING (arg1, TMSCM_ARG1, "material-choose-citation");
  string style= tmscm_to_string (arg1);
  return tree_to_tmscm (qtm_material_choose_citation (
    std::string (as_charp (style), N(style))));
}

tmscm
tmg_material_choose_references () {
  return tree_to_tmscm (qtm_material_choose_references ());
}

tmscm
tmg_materials_update_document (tmscm arg1, tmscm arg2) {
  TMSCM_ASSERT_TREE (arg1, TMSCM_ARG1, "materials-update-document");
  TMSCM_ASSERT_STRING (arg2, TMSCM_ARG2, "materials-update-document");
  std::string error;
  string style= tmscm_to_string (arg2);
  tree updated= athena_materials_update_document (
    tmscm_to_tree (arg1), std::string (as_charp (style), N(style)), error);
  tree result (TUPLE);
  result << tree (error.empty () ? "ok" : "error");
  result << (error.empty () ? updated : tree (error.c_str ()));
  return tree_to_tmscm (result);
}

tmscm
tmg_materials_update_document_auto (tmscm arg1) {
  TMSCM_ASSERT_TREE (arg1, TMSCM_ARG1, "materials-update-document-auto");
  tree document= tmscm_to_tree (arg1);
  string preference= get_preference ("materials csl style",
                                     "springer-mathphys");
  std::string fallback (as_charp (preference), (size_t) N(preference));
  std::string style=
    athena_materials_document_citation_style (document, fallback);
  std::string error;
  tree updated= athena_materials_update_document (document, style, error);
  tree result (TUPLE);
  result << tree (error.empty () ? "ok" : "error");
  result << (error.empty () ? updated : tmg_utf8_text (error));
  return tree_to_tmscm (result);
}

tmscm
tmg_materials_csl_styles () {
  std::vector<MaterialCslStyle> styles;
  std::string error;
  tree result (TUPLE);
  if (!athena_materials_list_csl_styles (styles, error)) {
    FAILED (c_string ("could not list CSL styles: " *
                      string (error.c_str ())));
    return tree_to_tmscm (result);
  }
  for (const MaterialCslStyle& style: styles)
    result << compound ("tuple", tree (style.name.c_str ()),
                        tree (style.title.c_str ()));
  return tree_to_tmscm (result);
}

tmscm
tmg_material_info_page (tmscm arg1) {
  TMSCM_ASSERT_STRING (arg1, TMSCM_ARG1, "material-info-page");
  string name= tmscm_to_string (arg1);
  return tree_to_tmscm (athena_material_info_page (
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
tmg_normalized_path (const fs::path& path) {
  std::error_code ec;
  fs::path result= fs::weakly_canonical (path, ec);
  if (!ec) return result.lexically_normal ();
  result= fs::absolute (path, ec);
  return (ec ? path : result).lexically_normal ();
}

bool
tmg_path_at_or_below (const fs::path& path, const fs::path& root) {
  fs::path relative= path.lexically_relative (root);
  if (relative.empty ()) return path == root;
  auto first= relative.begin ();
  return first != relative.end () && *first != "..";
}

tree
tmg_global_transformation_result (
    const char* status, const AthenaGlobalTransformationPlan& plan,
    const std::string& message) {
  tree changed (TUPLE);
  for (const AthenaGlobalTransformationRewrite& rewrite: plan.rewrites)
    changed << tmg_utf8_text (rewrite.relative_path.generic_u8string ());
  tree result (TUPLE);
  result << tree (status)
         << tmg_integer_text ((std::int64_t) plan.scanned)
         << tmg_integer_text ((std::int64_t) plan.rewrites.size ())
         << tmg_utf8_text (message)
         << tmg_utf8_text (plan.backup_root.empty ()
                             ? std::string ()
                             : plan.backup_root.string ())
         << changed;
  return result;
}

} // namespace

tmscm
tmg_global_transformation_run (tmscm arg1, tmscm arg2) {
  TMSCM_ASSERT (scm_is_true (scm_procedure_p (arg1)), arg1, TMSCM_ARG1,
                "global-transformation-run");
  TMSCM_ASSERT_STRING (arg2, TMSCM_ARG2, "global-transformation-run");
  AthenaGlobalTransformationPlan plan;
  if (!vault_active ())
    return tree_to_tmscm (tmg_global_transformation_result (
      "error", plan, "Load a Vault before running a global transformation"));
  if (headless_mode)
    return tree_to_tmscm (tmg_global_transformation_result (
      "error", plan, "Global transformations require the interactive editor"));

  std::string name= tmg_cork_to_utf8 (arg2);
  string root_string= concretize (vault_get_root ());
  fs::path root= tmg_normalized_path (
    fs::path (std::string (as_charp (root_string), (size_t) N(root_string))));
  std::unordered_map<std::string,url> open_buffers;
  QStringList modified;
  array<url> buffers= get_all_buffers ();
  for (int i=0; i<N(buffers); ++i) {
    if (is_rooted_tmfs (buffers[i]) || is_rooted_web (buffers[i])) continue;
    string concrete= concretize (buffers[i]);
    fs::path path= tmg_normalized_path (fs::path (
      std::string (as_charp (concrete), (size_t) N(concrete))));
    if (!tmg_path_at_or_below (path, root) || path.extension () != ".ath")
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
    return tree_to_tmscm (tmg_global_transformation_result (
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
    tmscm result= call_scheme (
      arg1,
      string_to_tmscm (utf8_to_cork (
        string (relative.data (), relative.size ()))),
      tree_to_tmscm (copy (input)));
    if (scm_is_false (result)) {
      output= input;
      return true;
    }
    if (!tmscm_is_tree (result)) {
      callback_error=
        "Transformer must return #f for no change or a complete document tree";
      return false;
    }
    output= tmscm_to_tree (result);
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
      return tree_to_tmscm (tmg_global_transformation_result (
        "cancelled", plan, "Transformation cancelled; no files were changed"));
    QMessageBox::critical (qApp->activeWindow (), "Run global transformation",
                           QString::fromStdString (error));
    return tree_to_tmscm (tmg_global_transformation_result (
      "error", plan, error));
  }
  progress.close ();

  if (plan.rewrites.empty ())
    return tree_to_tmscm (tmg_global_transformation_result (
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
    return tree_to_tmscm (tmg_global_transformation_result (
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
    return tree_to_tmscm (tmg_global_transformation_result (
      "error", plan, error));
  }
  commit_progress.close ();

  for (const AthenaGlobalTransformationRewrite& rewrite: plan.rewrites) {
    auto open= open_buffers.find (tmg_normalized_path (rewrite.path).string ());
    if (open != open_buffers.end ())
      set_buffer_tree (open->second, rewrite.transformed);
  }
  athena_clear_transclusion_caches ();
  athena_namespace_ontology_invalidate (true);
  athena_artifact_radioactive_invalidate ();

  return tree_to_tmscm (tmg_global_transformation_result (
    "ok", plan,
    "Transformed " + std::to_string (plan.rewrites.size ()) + " document(s)"));
}

static AthenaVaultfileInfo
tmg_vaultfile_info_from_array (array<string> fields) {
  std::vector<std::string> values;
  for (int i=0; i<N(fields); i++)
    values.push_back (std::string (as_charp (fields[i]), N(fields[i])));
  return athena_vaultfile_from_fields (values);
}

tmscm
tmg_vaultfile_presentP (tmscm arg1) {
  TMSCM_ASSERT_URL (arg1, TMSCM_ARG1, "vaultfile-present?");

  return bool_to_tmscm (athena_vaultfile_present (tmg_vault_root_path (arg1)));
}

tmscm
tmg_vaultfile_read (tmscm arg1) {
  TMSCM_ASSERT_URL (arg1, TMSCM_ARG1, "vaultfile-read");

  AthenaVaultfileInfo info;
  std::string error;
  if (!athena_vaultfile_read (tmg_vault_root_path (arg1), info, error))
    return array_string_to_tmscm (array<string> ());
  return array_string_to_tmscm (
    tmg_vaultfile_fields_to_array (athena_vaultfile_to_fields (info)));
}

tmscm
tmg_artifact_title_filter_read (tmscm arg1) {
  TMSCM_ASSERT_URL (arg1, TMSCM_ARG1, "artifact-title-filter-read");

  url root_url= tmscm_to_url (arg1);
  AthenaArtifactTitleFilter filter=
    is_none (root_url) ? athena_artifact_title_filter_defaults ()
                       : AthenaArtifactTitleFilter ();
  std::string error;
  if (!is_none (root_url) && !athena_artifact_title_filter_read (
        tmg_vault_root_path (arg1), filter, error))
    return array_string_to_tmscm (array<string> ());
  return array_string_to_tmscm (
    tmg_vaultfile_fields_to_array (filter.entries));
}

tmscm
tmg_vaultfile_write (tmscm arg1, tmscm arg2) {
  TMSCM_ASSERT_URL (arg1, TMSCM_ARG1, "vaultfile-write");
  TMSCM_ASSERT_ARRAY_STRING (arg2, TMSCM_ARG2, "vaultfile-write");

  std::string error;
  std::filesystem::path root= tmg_vault_root_path (arg1);
  array<string> fields= tmscm_to_array_string (arg2);
  AthenaVaultfileInfo info= tmg_vaultfile_info_from_array (fields);
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
    return string_to_tmscm ("");
  return string_to_tmscm (string (error.c_str (), error.size ()));
}

tmscm
tmg_vaultfile_ensure_json (tmscm arg1) {
  TMSCM_ASSERT_URL (arg1, TMSCM_ARG1, "vaultfile-ensure-json");

  std::string error;
  if (athena_vaultfile_ensure_json (tmg_vault_root_path (arg1), error))
    return string_to_tmscm ("");
  return string_to_tmscm (string (error.c_str (), error.size ()));
}

static tmscm
tmg_vault_backup_dispatch_realtime (tmscm arg1) {
  TMSCM_ASSERT_URL (arg1, TMSCM_ARG1, "vault-backup-dispatch-realtime");
  url saved_file= tmscm_to_url (arg1);
  qtm_vault_backup_dispatch_realtime (
    to_qstring (as_string (concretize (saved_file), URL_SYSTEM)));
  return TMSCM_UNSPECIFIED;
}

tmscm
tmg_namespace_info_page (tmscm arg1) {
  TMSCM_ASSERT_STRING (arg1, TMSCM_ARG1, "namespace-info-page");

  return tree_to_tmscm (athena_namespace_info_page (tmscm_to_string (arg1)));
}

tmscm
tmg_reverse_hierarchy_graph_render (tmscm arg1) {
  TMSCM_ASSERT_CONTENT (arg1, TMSCM_ARG1, "reverse-hierarchy-graph-render");

  tree t= tmscm_to_content (arg1);
  string size= is_atomic (t) ? t->label : tree_as_string (t);
  return tree_to_tmscm (reverse_hierarchy_graph_render (size));
}

tmscm
tmg_direct_hierarchy_graph_show_namespace (tmscm arg1) {
  TMSCM_ASSERT_STRING (arg1, TMSCM_ARG1,
                       "direct-hierarchy-graph-show-namespace");
  direct_hierarchy_graph_show_namespace (tmscm_to_string (arg1));
  return TMSCM_UNSPECIFIED;
}

tmscm
tmg_vault_validate_root_namespace () {
  QTMVaultfileInfo info;
  if (!qtm_vaultfile_read (info)) return string_to_tmscm ("");
  if (info.rootNamespace.isEmpty ()) return string_to_tmscm ("");

  athena_namespace_definition ns;
  string root= from_qstring (info.rootNamespace);
  if (athena_namespace_get (root, ns)) return string_to_tmscm ("");
  return string_to_tmscm (
    "Root namespace in Vaultfile.json is not a valid namespace: " * root);
}

tmscm
tmg_namespace_new_file_wizard () {
  if (headless_mode) return string_to_tmscm ("");
  return string_to_tmscm (namespace_new_file_wizard ());
}

tmscm
tmg_namespace_create_file_with_optional_initializer (tmscm arg1) {
  TMSCM_ASSERT_STRING (arg1, TMSCM_ARG1,
                       "namespace-create-file-with-optional-initializer");
  if (headless_mode) return string_to_tmscm ("Headless mode has no native namespace initializer chooser.");
  string error;
  if (namespace_create_file_with_optional_initializer (tmscm_to_string (arg1),
                                                      error))
    return string_to_tmscm ("");
  return string_to_tmscm (error);
}

tmscm
tmg_document_search_open () {
  document_search_open ();
  return TMSCM_UNSPECIFIED;
}

tmscm
tmg_document_search_next (tmscm arg1) {
  TMSCM_ASSERT_BOOL (arg1, TMSCM_ARG1, "document-search-next");
  document_search_next (tmscm_to_bool (arg1));
  return TMSCM_UNSPECIFIED;
}

tmscm
tmg_document_search_close () {
  document_search_close ();
  return TMSCM_UNSPECIFIED;
}

tmscm
tmg_artifact_open_uuid (tmscm arg1) {
  TMSCM_ASSERT_STRING (arg1, TMSCM_ARG1, "artifact-open-uuid");
  return bool_to_tmscm (artifacts_open_uuid (tmscm_to_string (arg1)));
}

tmscm
tmg_artifact_resolve_uuid (tmscm arg1) {
  TMSCM_ASSERT_STRING (arg1, TMSCM_ARG1, "artifact-resolve-uuid");
  url file;
  path source_path;
  if (!artifacts_resolve_uuid (
        tmscm_to_string (arg1), file, source_path))
    return bool_to_tmscm (false);
  return tmscm_cons (
    url_to_tmscm (file),
    tmscm_cons (path_to_tmscm (source_path), tmscm_null ()));
}

tmscm
tmg_artifact_disambiguation_page (tmscm arg1) {
  TMSCM_ASSERT_STRING (arg1, TMSCM_ARG1,
                       "artifact-disambiguation-page");
  return tree_to_tmscm (
    athena_artifact_disambiguation_page (tmscm_to_string (arg1)));
}

template<void (*Function) ()>
static tmscm
tmg_void_nullary () {
  Function ();
  return TMSCM_UNSPECIFIED;
}

void
initialize_glue () {
  initialize_escape_symbol_picker_data ();

  tmscm_install_procedure ("tree?", treeP, 1, 0, 0);
  tmscm_install_procedure ("tm?", contentP, 1, 0, 0);
  tmscm_install_procedure ("observer?", observerP, 1, 0, 0);
  tmscm_install_procedure ("url?", urlP, 1, 0, 0);
  tmscm_install_procedure ("modification?", modificationP, 1, 0, 0);
  tmscm_install_procedure ("patch?", patchP, 1, 0, 0);
  tmscm_install_procedure ("blackbox?", blackboxP, 1, 0, 0);
  tmscm_install_procedure ("outline-pane-show",
                           tmg_void_nullary<outline_pane_show>, 0, 0, 0);
  tmscm_install_procedure ("neighborhoods-pane-show",
                           tmg_void_nullary<neighborhoods_pane_show>, 0, 0, 0);
  tmscm_install_procedure ("handwriting-symbol-pane-show",
                           tmg_void_nullary<handwriting_symbol_pane_show>,
                           0, 0, 0);
  tmscm_install_procedure ("error-messages-show",
                           tmg_void_nullary<error_messages_show>, 0, 0, 0);
  tmscm_install_procedure ("command-palette-show",
                           tmg_void_nullary<command_palette_show>, 0, 0, 0);
  tmscm_install_procedure ("custom-styles-manager-show",
                           tmg_void_nullary<custom_styles_manager_show>,
                           0, 0, 0);
  tmscm_install_procedure ("visual-buffer-switcher-show",
                           tmg_void_nullary<visual_buffer_switcher_show>,
                           0, 0, 0);
  tmscm_install_procedure ("visual-buffer-switcher-choose",
                           tmg_visual_buffer_switcher_choose, 1, 0, 0);
  tmscm_install_procedure ("heading-fold-toggle",
                           tmg_heading_fold_toggle, 0, 0, 0);
  tmscm_install_procedure ("heading-fold-current",
                           tmg_heading_fold_current, 0, 0, 0);
  tmscm_install_procedure ("heading-unfold-current",
                           tmg_heading_unfold_current, 0, 0, 0);
  tmscm_install_procedure ("heading-fold-toggle-path",
                           tmg_heading_fold_toggle_path, 1, 0, 0);
  tmscm_install_procedure ("heading-word-count-path",
                           tmg_heading_word_count_path, 1, 0, 0);
  tmscm_install_procedure ("heading-unfold-all",
                           tmg_heading_unfold_all, 0, 0, 0);
  tmscm_install_procedure ("toc-fold-set-path",
                           tmg_toc_fold_set_path, 2, 0, 0);
  tmscm_install_procedure ("native-info-dialog",
                           tmg_native_info_dialog, 2, 0, 0);
  tmscm_install_procedure ("native-anchor-enunciations-confirm",
                           tmg_native_anchor_enunciations_confirm, 4, 0, 0);
  tmscm_install_procedure ("native-font-selector",
                           tmg_native_font_selector, 5, 0, 0);
  tmscm_install_procedure ("configure-font-for-vault",
                           tmg_void_nullary<qtm_configure_font_for_vault>,
                           0, 0, 0);
  tmscm_install_procedure ("native-open-preferences",
                           tmg_native_open_preferences, 0, 0, 0);
  tmscm_install_procedure ("native-preferences-open?",
                           tmg_native_preferences_openP, 0, 0, 0);
  tmscm_install_procedure ("native-preferences-export-privacy",
                           tmg_native_preferences_export_privacy, 0, 0, 0);
  tmscm_install_procedure ("native-preferences-export-metadata",
                           tmg_native_preferences_export_metadata, 0, 0, 0);
  tmscm_install_procedure ("native-preference-sensitive?",
                           tmg_native_preference_sensitiveP, 1, 0, 0);
  tmscm_install_procedure ("native-open-page-setup",
                           tmg_native_open_page_setup, 0, 0, 0);
  tmscm_install_procedure ("page-properties-pane-show",
                           tmg_void_nullary<page_properties_pane_show>,
                           0, 0, 0);
  tmscm_install_procedure ("paragraph-properties-pane-show",
                           tmg_void_nullary<paragraph_properties_pane_show>,
                           0, 0, 0);
  tmscm_install_procedure ("metadata-properties-pane-show",
                           tmg_void_nullary<metadata_properties_pane_show>,
                           0, 0, 0);
  tmscm_install_procedure ("escape-symbol-picker",
                           tmg_escape_symbol_picker, 0, 0, 0);
  tmscm_install_procedure ("ads-restore-visible-panes",
                           ads_restore_visible_panes, 0, 0, 0);
  tmscm_install_procedure ("vault-backup-viewer-show",
                           tmg_void_nullary<vault_backup_viewer_show>, 0, 0, 0);
  tmscm_install_procedure ("google-tasks-show",
                           tmg_void_nullary<google_tasks_show>, 0, 0, 0);
  tmscm_install_procedure ("google-cloud-todo-sync-buffer",
                           tmg_google_cloud_todo_sync_buffer, 1, 0, 0);
  tmscm_install_procedure ("google-cloud-todo-sync-open-buffers",
                           tmg_google_cloud_todo_sync_open_buffers, 0, 0, 0);
  tmscm_install_procedure ("google-cloud-todo-push-item",
                           tmg_google_cloud_todo_push_item, 2, 0, 0);
  tmscm_install_procedure ("codex-initialize-models",
                           tmg_codex_initialize_models, 2, 0, 0);
  tmscm_install_procedure ("codex-completion-options",
                           tmg_codex_completion_options, 2, 0, 0);
  tmscm_install_procedure ("codex-run-completion-async",
                           tmg_codex_run_completion_async, 10, 0, 0);
  tmscm_install_procedure ("document-search-open",
                           tmg_document_search_open, 0, 0, 0);
  tmscm_install_procedure ("document-search-next",
                           tmg_document_search_next, 1, 0, 0);
  tmscm_install_procedure ("document-search-close",
                           tmg_document_search_close, 0, 0, 0);
  tmscm_install_procedure ("artifacts-pane-show",
                           tmg_void_nullary<artifacts_pane_show>, 0, 0, 0);
  tmscm_install_procedure ("artifacts-build-entire-vault",
                           tmg_void_nullary<artifacts_build_entire_vault>,
                           0, 0, 0);
  tmscm_install_procedure ("artifacts-build-current-document",
                           tmg_void_nullary<artifacts_build_current_document>,
                           0, 0, 0);
  tmscm_install_procedure ("artifact-open-uuid",
                           tmg_artifact_open_uuid, 1, 0, 0);
  tmscm_install_procedure ("artifact-resolve-uuid",
                           tmg_artifact_resolve_uuid, 1, 0, 0);
  tmscm_install_procedure ("artifact-disambiguation-page",
                           tmg_artifact_disambiguation_page, 1, 0, 0);
  tmscm_install_procedure ("materials-manager-show",
                           tmg_void_nullary<materials_manager_show>, 0, 0, 0);
  tmscm_install_procedure ("material-choose-citation",
                           tmg_material_choose_citation, 1, 0, 0);
  tmscm_install_procedure ("material-choose-references",
                           tmg_material_choose_references, 0, 0, 0);
  tmscm_install_procedure ("material-resolve-uuid",
                           tmg_material_resolve_uuid, 1, 0, 0);
  tmscm_install_procedure ("material-find-by-identifier",
                           tmg_material_find_by_identifier, 2, 0, 0);
  tmscm_install_procedure ("material-get",
                           tmg_material_get, 1, 0, 0);
  tmscm_install_procedure ("materials-search",
                           tmg_materials_search, 2, 0, 0);
  tmscm_install_procedure ("materials-list",
                           tmg_materials_list, 2, 0, 0);
  tmscm_install_procedure ("materials-update-document",
                           tmg_materials_update_document, 2, 0, 0);
  tmscm_install_procedure ("materials-update-document-auto",
                           tmg_materials_update_document_auto, 1, 0, 0);
  tmscm_install_procedure ("materials-csl-styles",
                           tmg_materials_csl_styles, 0, 0, 0);
  tmscm_install_procedure ("material-info-page",
                           tmg_material_info_page, 1, 0, 0);
  tmscm_install_procedure ("global-transformation-run",
                           tmg_global_transformation_run, 2, 0, 0);
  tmscm_install_procedure ("namespace-manager-show",
                           tmg_void_nullary<namespace_manager_show>, 0, 0, 0);
  tmscm_install_procedure ("namespace-explorer-show",
                           tmg_void_nullary<namespace_explorer_show>, 0, 0, 0);
  tmscm_install_procedure ("reverse-hierarchy-graph-show",
                           tmg_void_nullary<reverse_hierarchy_graph_show>,
                           0, 0, 0);
  tmscm_install_procedure ("reverse-hierarchy-graph-insert",
                           tmg_void_nullary<reverse_hierarchy_graph_insert>,
                           0, 0, 0);
  tmscm_install_procedure ("reverse-hierarchy-graph-render",
                           tmg_reverse_hierarchy_graph_render, 1, 0, 0);
  tmscm_install_procedure ("direct-hierarchy-graph-show",
                           tmg_void_nullary<direct_hierarchy_graph_show>,
                           0, 0, 0);
  tmscm_install_procedure ("direct-hierarchy-graph-show-namespace",
                           tmg_direct_hierarchy_graph_show_namespace, 1, 0, 0);
  tmscm_install_procedure ("global-hierarchy-graph-show",
                           tmg_void_nullary<global_hierarchy_graph_show>,
                           0, 0, 0);
  tmscm_install_procedure ("local-reference-graph-show",
                           tmg_void_nullary<local_reference_graph_show>,
                           0, 0, 0);
  tmscm_install_procedure ("reference-graph-show",
                           tmg_void_nullary<reference_graph_show>, 0, 0, 0);
  tmscm_install_procedure ("formula-ast-show",
                           tmg_void_nullary<formula_ast_show>, 0, 0, 0);
  tmscm_install_procedure ("vault-validate-root-namespace",
                           tmg_vault_validate_root_namespace, 0, 0, 0);
  tmscm_install_procedure ("namespace-info-page",
                           tmg_namespace_info_page, 1, 0, 0);
  tmscm_install_procedure ("namespace-new-file-wizard",
                           tmg_namespace_new_file_wizard, 0, 0, 0);
  tmscm_install_procedure (
    "namespace-create-file-with-optional-initializer",
    tmg_namespace_create_file_with_optional_initializer, 1, 0, 0);
  tmscm_install_procedure ("namespace-export-show",
                           tmg_void_nullary<namespace_export_show>, 0, 0, 0);
  tmscm_install_procedure ("websites-manager-show",
                           tmg_void_nullary<websites_manager_show>, 0, 0, 0);
  tmscm_install_procedure ("vault-load-with-ns",
                           tmg_vault_load_with_ns, 4, 0, 0);
  tmscm_install_procedure ("vault-rewrite-anchor-references",
                           tmg_vault_rewrite_anchor_references, 2, 0, 0);
  tmscm_install_procedure ("vault-maintenance-setup",
                           tmg_vault_maintenance_setup, 1, 0, 0);
  tmscm_install_procedure ("vaultfile-present?",
                           tmg_vaultfile_presentP, 1, 0, 0);
  tmscm_install_procedure ("vaultfile-read",
                           tmg_vaultfile_read, 1, 0, 0);
  tmscm_install_procedure ("artifact-title-filter-read",
                           tmg_artifact_title_filter_read, 1, 0, 0);
  tmscm_install_procedure ("vaultfile-write",
                           tmg_vaultfile_write, 2, 0, 0);
  tmscm_install_procedure ("vaultfile-ensure-json",
                           tmg_vaultfile_ensure_json, 1, 0, 0);
  tmscm_install_procedure ("vault-backup-dispatch-realtime",
                           tmg_vault_backup_dispatch_realtime, 1, 0, 0);
  tmscm_install_procedure ("image-remove-background",
                           image_remove_background_current, 1, 0, 0);
  
  initialize_glue_basic ();
  initialize_glue_editor ();
  initialize_glue_server ();
}
