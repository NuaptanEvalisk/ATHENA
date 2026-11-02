/******************************************************************************
* MODULE     : vault_frontend.cpp
* DESCRIPTION: Native vault startup and welcome-page policy
*******************************************************************************/

#include "ATHENA/Data/vault_frontend.hpp"

#include "ATHENA/Data/new_buffer.hpp"
#include "ATHENA/Data/recent_files.hpp"
#include "ATHENA/Data/vault.hpp"
#include "ATHENA/Data/vaultfile_json.hpp"
#include "ATHENA/Data/vault_database_layout.hpp"
#include "ATHENA/Data/vault_safe_rename.hpp"
#include "QTMVaultExplorer.hpp"
#include "QTMNativeDialogs.hpp"
#include "analyze.hpp"
#include "scheme.hpp"
#include "tm_configure.hpp"
#include "url.hpp"
#include "boot.hpp"
#include "server.hpp"
#include "native_interfaces.hpp"
#include "guile_tm.hpp"
#include "buffer_actor.hpp"
#include "qt_utilities.hpp"
#include "file.hpp"

#include <QCoreApplication>
#include <QMetaObject>
#include <QTimer>
#include <QApplication>
#include <QFileDialog>
#include <QInputDialog>
#include <QScopedValueRollback>

#include <filesystem>
#include <string>

namespace fs= std::filesystem;

namespace {

bool vault_preferences_active= false;
bool vault_preferences_reloading= false;

void reload_preferences (url file) {
  QScopedValueRollback<bool> loading (vault_preferences_reloading, true);
  load_user_preferences (file);
  application_preferences_notify_all ();
}

void deactivate_preferences () {
  if (!vault_preferences_active) return;
  save_user_preferences ();
  vault_preferences_active= false;
  reload_preferences ("$ATHENA_HOME_PATH/system/preferences.json");
}

bool
preference_on (string name) {
  return get_preference (name, "off") == "on";
}

void
show_message (string message, string title) {
  qtm_info_dialog (message, title);
}

void
load_buffer (url target) {
  try { (void) call ("load-buffer", object (target)); }
  catch (...) {}
}

fs::path
native_path (url value) {
  string path= as_string (concretize (value), URL_SYSTEM);
  return fs::path (std::string (path.data (), N(path)));
}

void activate_preferences (url directory, AthenaVaultfileInfo info) {
  if (info.preferences_path.empty ()) {
    info.preferences_path= "vprefs.json";
    std::string error;
    if (!athena_vaultfile_write (native_path (directory), info, error)) {
      show_message (string (error.c_str ()), "Vaultfile");
      return;
    }
  }
  url file= directory * url_unix (string (info.preferences_path.c_str ()));
  if (suffix (file) != "json") {
    std_warning << "Vault preferences must use a .json path: "
                << string (info.preferences_path.c_str ()) << LF;
    return;
  }
  if (!exists (file)) dump_user_preferences (file);
  vault_preferences_active= true;
  reload_preferences (file);
}

void report_root_namespace () {
  string error= athena_vault_validate_root_namespace ();
  if (error != "") show_message (error, "Vault");
}

void load_directory (url directory) {
  if (!athena_vaultfile_present (native_path (directory))) {
    vault_create_dialog_native (directory);
    return;
  }
  AthenaVaultfileInfo info;
  std::string error;
  if (!athena_vaultfile_read (native_path (directory), info, error)) {
    show_message (string (error.c_str ()), "Vault");
    return;
  }
  const bool take_preferences= preference_on ("vault take preferences with vault");
  if (vault_preferences_active) save_user_preferences ();
  string failure= vault_load (directory, string (info.name.c_str ()),
    string (info.map_path.c_str ()), string (info.namespace_db_path.c_str ()));
  if (failure != "") { show_message (failure, "Vault"); return; }
  // Loading may canonicalize the database and preference locations.
  if (!athena_vaultfile_read (native_path (directory), info, error)) {
    show_message (string (error.c_str ()), "Vaultfile");
    return;
  }
  deactivate_preferences ();
  if (take_preferences) activate_preferences (directory, info);
  vault_add_recent (directory);
  get_server ()->set_message ("Loaded vault: " * string (info.name.c_str ()), "Vault");
  report_root_namespace ();
}

void sync_preferences_mode () {
  if (vault_preferences_reloading) return;
  if (!preference_on ("vault take preferences with vault")) deactivate_preferences ();
  else if (vault_active () && !vault_preferences_active) {
    AthenaVaultfileInfo info;
    std::string error;
    url directory= vault_get_root ();
    if (athena_vaultfile_read (native_path (directory), info, error))
      activate_preferences (directory, info);
    else show_message (string (error.c_str ()), "Vaultfile");
  }
}

void on_gui_directory (url directory, void (*action) (url)) {
  if (!current_scheme_execution_context ()) { action (directory); return; }
  const string name= as_string (directory);
  std::string transfer (name.data (), N(name));
  qt_post_to_main_thread ([transfer= std::move (transfer), action] {
    action (url (string (transfer.c_str ())));
  });
}

void create_directory_dialog (url directory) {
  auto* dialog= new QInputDialog (QApplication::activeWindow ());
  dialog->setAttribute (Qt::WA_DeleteOnClose);
  dialog->setWindowTitle (QObject::tr ("New Vault"));
  dialog->setLabelText (QObject::tr ("Vault name"));
  QObject::connect (dialog, &QInputDialog::textValueSelected, dialog,
    [directory] (const QString& name) {
      AthenaVaultfileInfo info;
      info.name= name.toUtf8 ().toStdString ();
      std::string error;
      if (!athena_vaultfile_write (native_path (directory), info, error)) {
        show_message (string (error.c_str ()), "Vaultfile");
        return;
      }
      load_directory (directory);
    });
  dialog->open ();
}

void open_vault_dialog () {
  auto* dialog= new QFileDialog (QApplication::activeWindow (), QObject::tr ("Load Vault"));
  dialog->setAttribute (Qt::WA_DeleteOnClose);
  dialog->setFileMode (QFileDialog::Directory);
  dialog->setOption (QFileDialog::ShowDirsOnly);
#if ATHENA_PLATFORM_IPADOS
  dialog->setDirectory (qEnvironmentVariable ("ATHENA_VAULTS_PATH"));
#endif
  QObject::connect (dialog, &QFileDialog::fileSelected, dialog,
    [] (const QString& directory) { load_directory (url_system (string (directory.toUtf8 ().constData ()))); });
  dialog->open ();
}

void unload_vault () {
  if (!vault_active ()) {
    get_server ()->set_message ("No active vault to unload", "Vault");
    return;
  }
  deactivate_preferences ();
  vault_close ();
  get_server ()->set_message ("Unloaded vault", "Vault");
}

bool
read_current_vaultfile (AthenaVaultfileInfo& info) {
  if (!vault_active ()) return false;
  std::string error;
  return athena_vaultfile_read (native_path (vault_get_root ()), info, error);
}

url
welcome_target (const std::string& value) {
  std::string target= value.empty () ? "tmfs://welcome/home" : value;
  if (target.rfind ("tmfs://", 0) == 0 || target.rfind ("file://", 0) == 0)
    return url (target.c_str ());
  if (vault_active ())
    return vault_get_root () * url_unix (string (target.c_str ()));
  return url (target.c_str ());
}

array<string>
recent_file_system_paths () {
  array<string> out;
  try {
    array<url> values= recent_file_list (15);
    for (int i=0; i<N(values); ++i)
      out << as_string (values[i], URL_SYSTEM);
  }
  catch (...) {}
  return out;
}

tree
action_node (string label, string command) {
  return compound ("action", tree (label), tree (command));
}

tree
item_node (tree content) {
  return compound ("concat", compound ("item"), content);
}

tree
enumerate_nodes (const array<tree>& lines) {
  tree document (DOCUMENT);
  for (int i=0; i<N(lines); ++i) document << lines[i];
  return compound ("enumerate", document);
}

tree
welcome_body () {
  array<string> recent_vaults= vault_get_recent ();
  array<string> recent_files= recent_file_system_paths ();

  tree body (DOCUMENT);
  tree centered (DOCUMENT);
  centered << compound (
    "with", "font-size", "2.4", "font-series", "bold",
    compound ("concat", "Welcome to ", compound ("ATHENA")));
  centered << compound (
    "with", "font-size", "1.15", "color", "#555555",
    tree ("Advanced typesetting and hypertext for mathematical notes and archives."));
  body << compound ("with", "par-mode", "center", centered);
  body << compound ("vspace", "1.5fn");

  body << compound ("section*", "Start here");
  array<tree> start;
  start << item_node (action_node ("Open a blank document", "(new-document)"));
  if (N(recent_vaults) > 0) {
    url latest (recent_vaults[0]);
    string label= "Load latest vault (" * as_string (tail (latest), URL_SYSTEM) * ")";
    string command= "(vault-load-latest-action " * scm_quote (recent_vaults[0]) * ")";
    start << item_node (action_node (label, command));
  }
  body << enumerate_nodes (start);


  body << compound ("vspace", "1fn") << compound ("section*", "Recent Vaults");
  array<tree> vault_lines;
  if (N(recent_vaults) == 0)
    vault_lines << item_node (tree ("No recent vaults yet."));
  for (int i=0; i<N(recent_vaults); ++i) {
    url current (recent_vaults[i]);
    string label= as_string (tail (current), URL_SYSTEM);
    string command= "(vault-load-latest-action " * scm_quote (recent_vaults[i]) * ")";
    vault_lines << item_node (compound (
      "concat", action_node (label, command), "  ",
      compound ("with", "font-size", "0.85", "color", "grey",
                tree (recent_vaults[i]))));
  }
  body << enumerate_nodes (vault_lines);

  body << compound ("vspace", "1fn") << compound ("section*", "Recent Files");
  array<tree> file_lines;
  if (N(recent_files) == 0)
    file_lines << item_node (tree ("No recent files yet."));
  for (int i=0; i<N(recent_files); ++i) {
    string command= "(load-buffer (system->url " * scm_quote (recent_files[i]) * "))";
    file_lines << item_node (action_node (recent_files[i], command));
  }
  body << enumerate_nodes (file_lines);

  body << compound ("vspace", "2fn");
  tree footer (DOCUMENT);
  footer << tree ("ATHENA is a fork based on GNU TeXmacs.")
         << compound ("concat", "Copyright ", compound ("copyright"),
                      " 1999-2026 Joris van der Hoeven and others.")
         << compound ("concat", "Copyright ", compound ("copyright"),
                      " 2026 Nuaptan F. Evalisk.")
         << tree ("Released under the GNU General Public License version 3 or later.");
  body << compound (
    "with", "font-size", "0.8", "color", "grey",
    compound ("with", "par-mode", "center", footer));
  return compound ("with", "font", "pagella", "font-family", "rm", body);
}

void
schedule_explorer_after_startup () {
  if (!preference_on ("vault explorer show on startup")) return;
  QCoreApplication* app= QCoreApplication::instance ();
  if (app == nullptr) return;
  QMetaObject::invokeMethod (app, [] {
    QTimer::singleShot (100, [] {
      if (vault_active ()) vault_show_explorer_and_track_native ();
    });
  }, Qt::QueuedConnection);
}

} // namespace

void vault_create_dialog_native (url directory) { on_gui_directory (directory, create_directory_dialog); }
void vault_load_directory_native (url directory) { on_gui_directory (directory, load_directory); }
void vault_open_dialog_native () { athena_dispatch_ui (open_vault_dialog); }
void vault_unload_native () { athena_dispatch_ui (unload_vault); }
void vault_notify_preferences_mode (string, string) { athena_dispatch_ui (sync_preferences_mode); }

void
vault_track_current_buffer_if_enabled () {
  if (!preference_on ("vault explorer track current file") || !vault_active ())
    return;
  url current= get_current_buffer_safe ();
  if (!is_none (current)) vault_explorer_track_file (current);
}

void
vault_show_explorer_and_track_native () {
  vault_show_explorer ();
  vault_track_current_buffer_if_enabled ();
}

void
vault_load_latest_action_native (string path) {
  vault_load_directory_native (url (path));
}

void
go_to_system_welcome_page_native () {
  load_buffer (url ("tmfs://welcome/home"));
}

void
go_to_welcome_page_native () {
  AthenaVaultfileInfo info;
  if (read_current_vaultfile (info)) load_buffer (welcome_target (info.startup_page));
  else go_to_system_welcome_page_native ();
}

void
go_to_vault_initial_page_native () {
  AthenaVaultfileInfo info;
  if (!read_current_vaultfile (info) || info.one_time_startup_page.empty ()) {
    go_to_welcome_page_native ();
    return;
  }
  std::string target= info.one_time_startup_page;
  info.one_time_startup_page.clear ();
  std::string error;
  (void) athena_vaultfile_write (native_path (vault_get_root ()), info, error);
  load_buffer (welcome_target (target));
}

void
vault_startup_open_initial_buffer_native () {
  if (!preference_on ("vault auto load last")) {
    if (preference_on ("vault welcome page")) go_to_welcome_page_native ();
    return;
  }
  array<string> recent= vault_get_recent ();
  if (N(recent) == 0) return;
  string latest= recent[0];
  url dir (latest);
  if (!athena_vaultfile_present (native_path (dir))) {
    if (preference_on ("vault report missing last"))
      show_message ("Last vault is unavailable:\n" * latest, "Vault unavailable");
    return;
  }
  // Relocate before load-vault-dir opens databases or starts background workers.
  try {
    const auto root= native_path (dir);
    const bool enabled= athena_vault_canonical_database_preference (root,
      preference_on ("vault canonical database positions"),
      preference_on ("vault take preferences with vault"));
    std::string error;
    if (enabled && !athena_vault_database_layout_pending (root)) {
      AthenaVaultfileInfo info;
      if (!athena_vaultfile_read (root, info, error) ||
          !vault_safe_rename_recover (root, info.map_path, error))
        throw std::runtime_error (error);
    }
    if (!athena_vault_canonicalize_databases (root, enabled, error))
      throw std::runtime_error (error);
  }
  catch (const std::exception& e) {
    std_error << "Vault database relocation failed: " << e.what () << LF;
    show_message (string (e.what ()), "Vault database relocation failed");
    return;
  }
  vault_load_latest_action_native (latest);
  schedule_explorer_after_startup ();
  if (preference_on ("vault welcome page")) go_to_vault_initial_page_native ();
}

tree
vault_welcome_page_native () {
  tree doc (DOCUMENT);
  doc << compound ("style", tuple ("generic"))
      << compound ("body", compound ("document", welcome_body ()))
      << compound ("initial",
           compound ("collection",
             compound ("associate", "font", "pagella"),
             compound ("associate", "font-base-size", "10"),
             compound ("associate", "font-family", "rm"),
             compound ("associate", "page-medium", "automatic")));
  return doc;
}
