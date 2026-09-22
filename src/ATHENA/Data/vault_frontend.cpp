/******************************************************************************
* MODULE     : vault_frontend.cpp
* DESCRIPTION: Native vault startup and welcome-page policy
*******************************************************************************/

#include "ATHENA/Data/vault_frontend.hpp"

#include "ATHENA/Data/new_buffer.hpp"
#include "ATHENA/Data/vault.hpp"
#include "ATHENA/Data/vaultfile_json.hpp"
#include "QTMVaultExplorer.hpp"
#include "analyze.hpp"
#include "scheme.hpp"
#include "tm_configure.hpp"
#include "url.hpp"

#include <QCoreApplication>
#include <QMetaObject>
#include <QTimer>

#include <filesystem>
#include <string>

namespace fs= std::filesystem;

namespace {

bool
preference_on (string name) {
  return get_preference (name, "off") == "on";
}

void
show_message (string message, string title) {
  try { (void) call ("show-message", object (message), object (title)); }
  catch (...) {}
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
    scheme_tree values= as_scheme_tree (
      eval ("(map url->system (recent-file-list 15))"));
    if (!is_tuple (values)) return out;
    for (int i=0; i<N(values); ++i)
      if (is_atomic (values[i]) && is_quoted (values[i]->label))
        out << scm_unquote (values[i]->label);
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
  centered << compound ("vspace", "0.6fn");
  centered << compound (
    "concat",
    action_node ("Welcome", "(load-help-article \"about/welcome/new-welcome\")"),
    "    ",
    action_node ("Get Started", "(load-help-article \"about/welcome/start\")"),
    "    ", action_node ("Manual", "(load-help-buffer \"main/man-manual\")"));
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

  body << compound ("vspace", "1fn") << compound ("section*", "Learn ATHENA");
  array<tree> learn;
  learn << item_node (compound (
    "concat", action_node ("Welcome", "(load-help-article \"about/welcome/new-welcome\")"),
    " explains the project and its interface."));
  learn << item_node (compound (
    "concat", action_node ("Get Started", "(load-help-article \"about/welcome/start\")"),
    " introduces the basic editing workflow."));
  learn << item_node (compound (
    "concat", action_node ("Manual", "(load-help-buffer \"main/man-manual\")"),
    " opens the complete help manual."));
  body << enumerate_nodes (learn);

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
  try { (void) call ("load-vault-dir", object (url (path))); }
  catch (...) {}
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
