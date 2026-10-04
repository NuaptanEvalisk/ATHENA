
/******************************************************************************
* MODULE     : preferences.cpp
* DESCRIPTION: User preferences for TeXmacs
* COPYRIGHT  : (C) 2012  Joris van der Hoeven
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "boot.hpp"
#include "basic.hpp"
#include "file.hpp"
#include "sys_utils.hpp"
#include "analyze.hpp"
#include "merge_sort.hpp"
#include "iterator.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QSet>
#include <QString>

#include <algorithm>
#include <mutex>
#include <shared_mutex>

/******************************************************************************
* Changing the user preferences
******************************************************************************/

bool user_prefs_modified= false;
hashmap<string,string> user_prefs ("");
hashmap<string,string> user_prefs_default ("");
hashmap<string,bool> user_prefs_string_default (true);
hashmap<string,string> user_prefs_callback ("");
url user_prefs_file= "$ATHENA_HOME_PATH/system/preferences.json";
void notify_preference (string var);

static std::shared_mutex user_preferences_mutex;
static std::once_flag builtin_preferences_once;
static std::vector<user_preference_ui_definition> builtin_preference_ui;

static QString
to_qstring (string s) {
  return QString::fromUtf8 (as_charp (s), N(s));
}

static string
from_qstring (const QString& s) {
  QByteArray bytes= s.toUtf8 ();
  return string (bytes.constData ());
}

static string
default_paper_type () {
  string psize= get_env ("PAPERSIZE");
  if (psize != "") return psize;
  return "a4";
}

static string
preference_callback_name (const QString& id) {
  struct callback_definition { const char* id; const char* procedure; };
  static const callback_definition callbacks[]= {
    {"autosave", "notify-autosave"},
    {"bidirectional-navigation", "notify-bidirectional-navigation"},
    {"converter-option", "converter-set-option"},
    {"cpp-pref", "notify-cpp-pref"},
    {"cursor-color", "notify-cursor-color"},
    {"debug-backtrace", "notify-debug-backtrace"},
    {"doc-collect-preference", "notify-doc-collect-preference"},
    {"doc-update-times", "notify-doc-update-times"},
    {"document-background-color", "notify-document-background-color"},
    {"enunciation-color", "notify-enunciation-color"},
    {"enunciation-rendering", "notify-enunciation-rendering"},
    {"external-navigation", "notify-external-navigation"},
    {"focus-border-width", "notify-focus-border-width"},
    {"focus-color", "notify-focus-color"},
    {"fold-table-of-contents", "notify-fold-table-of-contents"},
    {"fortran-pref", "notify-fortran-pref"},
    {"header", "notify-header"},
    {"highlight-brackets", "notify-highlight-brackets"},
    {"icon-bar", "notify-icon-bar"},
    {"julia-syntax", "notify-julia-syntax"},
    {"labels-mode", "notify-labels-mode"},
    {"latex-command", "notify-latex-command"},
    {"link-color", "notify-link-color"},
    {"link-pages", "notify-link-pages"},
    {"look-and-feel", "notify-look-and-feel"},
    {"new-page-breaking", "notify-new-page-breaking"},
    {"paper-type", "notify-paper-type"},
    {"preview-command", "notify-preview-command"},
    {"printer-dpi", "notify-printer-dpi"},
    {"printing-command", "notify-printing-command"},
    {"prog-auto-close-brackets", "notify-prog-auto-close-brackets"},
    {"python-syntax", "notify-python-syntax"},
    {"remote-control", "notify-remote-control"},
    {"restart", "notify-restart"},
    {"scheme-syntax", "notify-scheme-syntax"},
    {"security", "notify-security"},
    {"select-brackets", "notify-select-brackets"},
    {"selection-color", "notify-selection-color"},
    {"status-bar", "notify-status-bar"},
    {"tool", "notify-tool"},
    {"toolbar-presentation", "notify-toolbar-presentation"},
    {"vault-explorer-track", "notify-vault-explorer-track"},
    {"vault-preferences-mode", "notify-vault-preferences-mode"},
    {"zoom-factor", "notify-zoom-factor"}
  };
  for (const auto& callback: callbacks)
    if (id == callback.id) return callback.procedure;
  return "";
}

static string
preference_catalog_default (const QJsonObject& entry) {
  const QString provider= entry.value ("default_provider").toString ();
  if (provider == "printing-command") return get_printing_default ();
  if (provider == "paper-type") return default_paper_type ();
  return from_qstring (entry.value ("default").toString ());
}

static void
invalid_preference_catalog (const QString& reason) {
  failed_error << "Invalid ATHENA preference catalog: "
               << from_qstring (reason) << LF;
  FAILED ("invalid preference catalog");
}

static void
ensure_builtin_user_preferences () {
  std::call_once (builtin_preferences_once, [] {
    string source;
    const url catalog_path= "$ATHENA_PATH/misc/preferences/catalog.json";
    if (load_string (catalog_path, source, false))
      invalid_preference_catalog ("could not read " + to_qstring (as_string (catalog_path)));

    QJsonParseError parse_error;
    const QJsonDocument document= QJsonDocument::fromJson (
      QByteArray (as_charp (source), N(source)), &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject ())
      invalid_preference_catalog (parse_error.errorString ());
    const QJsonObject root= document.object ();
    if (root.value ("format").toString () != "athena-preference-catalog" ||
        root.value ("version").toInt () != 1 ||
        !root.value ("preferences").isArray ())
      invalid_preference_catalog ("unsupported format or version");

    const QSet<QString> types {
      "string", "scheme-object", "boolean", "choice", "color"};
    const QSet<QString> controls {
      "toggle", "text", "password", "choice", "color", "optional-color",
      "native"};
    const QSet<QString> native_providers {
      "delegation-server-list", "directory-path", "dynamic-choice", "file-path",
      "font-profile", "mirrored-toggle", "preferred-fonts", "remote-task-list",
      "search-worker-count", "enunciation-preset"};
    QSet<QString> keys;
    std::vector<user_preference_ui_definition> ui;
    std::unique_lock<std::shared_mutex> guard (user_preferences_mutex);
    for (const QJsonValue& value: root.value ("preferences").toArray ()) {
      if (!value.isObject ()) invalid_preference_catalog ("preference entry is not an object");
      const QJsonObject entry= value.toObject ();
      const QString key= entry.value ("key").toString ();
      const QString type= entry.value ("type").toString ();
      const QString scope= entry.value ("scope").toString ();
      if (key.isEmpty () || keys.contains (key))
        invalid_preference_catalog ("duplicate or empty key: " + key);
      if (!types.contains (type) || scope != "active")
        invalid_preference_catalog ("invalid type or scope for " + key);
      const bool has_default= entry.value ("default").isString ();
      const QString default_provider= entry.value ("default_provider").toString ();
      if (has_default == !default_provider.isEmpty ())
        invalid_preference_catalog ("invalid default declaration for " + key);
      if (!default_provider.isEmpty () && default_provider != "printing-command" &&
          default_provider != "paper-type")
        invalid_preference_catalog ("unknown default provider for " + key);
      if (type == "boolean" && has_default) {
        const QString def= entry.value ("default").toString ();
        if (def != "on" && def != "off")
          invalid_preference_catalog ("invalid boolean default for " + key);
      }

      const QString callback_id= entry.value ("callback").toString ();
      const string callback= callback_id.isEmpty ()? string (""):
                             preference_callback_name (callback_id);
      if (!callback_id.isEmpty () && callback == "")
        invalid_preference_catalog ("unknown callback id for " + key);

      const string native_key= from_qstring (key);
      user_prefs_default (native_key)= preference_catalog_default (entry);
      user_prefs_string_default (native_key)= type != "scheme-object";
      if (callback != "") user_prefs_callback (native_key)= callback;
      keys.insert (key);

      if (!entry.value ("ui").isObject ()) continue;
      const QJsonObject visual= entry.value ("ui").toObject ();
      user_preference_ui_definition definition;
      definition.key= native_key;
      definition.type= from_qstring (type);
      definition.scope= from_qstring (scope);
      definition.category= from_qstring (visual.value ("category").toString ());
      definition.tab= from_qstring (visual.value ("tab").toString ());
      definition.section= from_qstring (visual.value ("section").toString ());
      definition.label= from_qstring (visual.value ("label").toString ());
      definition.control= from_qstring (visual.value ("control").toString ());
      definition.provider= from_qstring (visual.value ("provider").toString ());
      definition.help= from_qstring (visual.value ("help").toString ());
      definition.unit= from_qstring (visual.value ("unit").toString ());
      definition.category_order= visual.value ("category_order").toInt (-1);
      definition.tab_order= visual.value ("tab_order").toInt (-1);
      definition.section_order= visual.value ("section_order").toInt (-1);
      definition.order= visual.value ("order").toInt (-1);
      definition.restart= visual.value ("restart").toBool (false);
      if (definition.category == "" || definition.tab == "" ||
          definition.section == "" || definition.label == "" ||
          !controls.contains (to_qstring (definition.control)))
        invalid_preference_catalog ("invalid UI declaration for " + key);
      if (definition.category_order < 0 || definition.tab_order < 0 ||
          definition.section_order < 0 || definition.order < 0)
        invalid_preference_catalog ("invalid UI order for " + key);
      if (definition.control == "native" &&
          !native_providers.contains (to_qstring (definition.provider)))
        invalid_preference_catalog ("unknown native control provider for " + key);
      if (visual.value ("choices").isArray ()) {
        for (const QJsonValue& choice_value: visual.value ("choices").toArray ()) {
          const QJsonObject choice= choice_value.toObject ();
          const QString choice_value_string= choice.value ("value").toString ();
          const QString choice_label= choice.value ("label").toString ();
          if (choice_value_string.isNull () || choice_label.isNull ())
            invalid_preference_catalog ("invalid choice for " + key);
          definition.choices.push_back (
            {from_qstring (choice_value_string), from_qstring (choice_label)});
        }
      }
      if (definition.control == "choice" && definition.choices.empty ())
        invalid_preference_catalog ("choice control has no choices for " + key);
      if (definition.control == "choice" && has_default) {
        const string def= from_qstring (entry.value ("default").toString ());
        bool found= false;
        for (const auto& choice: definition.choices)
          if (choice.value == def) { found= true; break; }
        if (!found) invalid_preference_catalog ("choice default is not offered for " + key);
      }
      ui.push_back (std::move (definition));
    }
    std::stable_sort (
      ui.begin (), ui.end (),
      [] (const user_preference_ui_definition& a,
          const user_preference_ui_definition& b) {
        if (a.category_order != b.category_order)
          return a.category_order < b.category_order;
        if (a.tab_order != b.tab_order) return a.tab_order < b.tab_order;
        if (a.section_order != b.section_order)
          return a.section_order < b.section_order;
        if (a.order != b.order) return a.order < b.order;
        return a.key < b.key;
      });
    builtin_preference_ui= std::move (ui);
  });
}

static bool
has_suffix (string s, string suf) {
  return N(s) >= N(suf) && s (N(s) - N(suf), N(s)) == suf;
}

static url
with_json_suffix (url u) {
  string s= as_string (u);
  if (has_suffix (s, ".json")) return u;
  if (has_suffix (s, ".scm")) return url (s (0, N(s) - 4) * ".json");
  return url (s * ".json");
}

bool
has_user_preference (string var) {
  ensure_builtin_user_preferences ();
  std::shared_lock<std::shared_mutex> guard (user_preferences_mutex);
  return user_prefs->contains (var);
}

void
register_user_preference (string var, string def, bool string_def) {
  ensure_builtin_user_preferences ();
  std::unique_lock<std::shared_mutex> guard (user_preferences_mutex);
  if (!user_prefs_default->contains (var)) {
    user_prefs_default (var)= def;
    user_prefs_string_default (var)= string_def;
  }
}

void
register_user_preference_callback (string var, string callback) {
  ensure_builtin_user_preferences ();
  std::unique_lock<std::shared_mutex> guard (user_preferences_mutex);
  if (callback == "") user_prefs_callback->reset (var);
  else user_prefs_callback (var)= callback;
}

bool
user_preference_default_is_string (string var) {
  ensure_builtin_user_preferences ();
  std::shared_lock<std::shared_mutex> guard (user_preferences_mutex);
  if (user_prefs_string_default->contains (var))
    return user_prefs_string_default[var];
  return true;
}

bool
get_user_preference_ui_definition (
  string var, user_preference_ui_definition& definition) {
  ensure_builtin_user_preferences ();
  std::shared_lock<std::shared_mutex> guard (user_preferences_mutex);
  for (const auto& candidate: builtin_preference_ui)
    if (candidate.key == var) {
      definition= candidate;
      return true;
    }
  return false;
}

std::vector<user_preference_ui_definition>
get_user_preference_ui_definitions () {
  ensure_builtin_user_preferences ();
  std::shared_lock<std::shared_mutex> guard (user_preferences_mutex);
  return builtin_preference_ui;
}

string
get_user_preference_callback (string var) {
  ensure_builtin_user_preferences ();
  std::shared_lock<std::shared_mutex> guard (user_preferences_mutex);
  if (user_prefs_callback->contains (var)) return user_prefs_callback[var];
  return "";
}

array<string>
get_user_preference_names () {
  ensure_builtin_user_preferences ();
  std::shared_lock<std::shared_mutex> guard (user_preferences_mutex);
  iterator<string> it= iterate (user_prefs_default);
  array<string> a;
  while (it->busy ())
    a << it->next ();
  merge_sort (a);
  return a;
}

bool
user_preference_is_sensitive (string var) {
  string key= locase_all (var);
  if (key == "rag mcp bearer token" ||
      key == "google oauth client id" ||
      key == "google oauth client secret")
    return true;

  static const char* markers[]= {
    "access key", "api key", "apikey", "authentication",
    "authorization", "bearer", "client id", "client secret", "cookie",
    "credential", "oauth", "password", "passwd", "private key",
    "refresh token", "secret", "session key", "token"
  };
  for (const char* marker: markers)
    if (search_forwards (string (marker), key) >= 0) return true;
  return false;
}

array<string>
get_user_preference_callback_names () {
  ensure_builtin_user_preferences ();
  std::shared_lock<std::shared_mutex> guard (user_preferences_mutex);
  iterator<string> it= iterate (user_prefs_callback);
  array<string> a;
  while (it->busy ())
    a << it->next ();
  merge_sort (a);
  return a;
}

void
set_user_preference (string var, string val) {
  ensure_builtin_user_preferences ();
  {
    std::unique_lock<std::shared_mutex> guard (user_preferences_mutex);
    if (val == "default") user_prefs->reset (var);
    else user_prefs (var)= val;
    user_prefs_modified= true;
  }
  notify_preference (var);
}

void
reset_user_preference (string var) {
  ensure_builtin_user_preferences ();
  {
    std::unique_lock<std::shared_mutex> guard (user_preferences_mutex);
    user_prefs->reset (var);
    user_prefs_modified= true;
  }
  notify_preference (var);
}

string
get_user_preference (string var, string val) {
  ensure_builtin_user_preferences ();
  std::shared_lock<std::shared_mutex> guard (user_preferences_mutex);
  if (user_prefs->contains (var)) return user_prefs[var];
  if (user_prefs_default->contains (var)) return user_prefs_default[var];
  else return val;
}

namespace {

array<string>
parse_color_preference (string raw) {
  array<string> out;
  QByteArray bytes (as_charp (raw), N (raw));
  QJsonParseError error;
  QJsonDocument doc= QJsonDocument::fromJson (bytes, &error);
  if (error.error == QJsonParseError::NoError && doc.isArray ()) {
    QSet<QString> seen;
    for (const QJsonValue& value: doc.array ()) {
      if (!value.isString ()) continue;
      QString text= value.toString ();
      if (text.isEmpty () || seen.contains (text)) continue;
      seen.insert (text);
      out << from_qstring (text);
      if (N (out) >= 8) break;
    }
    return out;
  }

  // Compatibility for preferences written by the former Scheme list
  // serializer. Color history contains strings only, so reject every other
  // datum instead of invoking the Scheme reader from native UI code.
  std::string legacy (bytes.constData (), static_cast<std::size_t> (bytes.size ()));
  std::size_t i= 0;
  auto skip_space= [&] {
    while (i < legacy.size () &&
           (legacy[i] == ' ' || legacy[i] == '\t' ||
            legacy[i] == '\r' || legacy[i] == '\n')) ++i;
  };
  skip_space ();
  if (i >= legacy.size () || legacy[i++] != '(') return out;
  QSet<QString> seen;
  for (;;) {
    skip_space ();
    if (i >= legacy.size ()) return array<string> ();
    if (legacy[i] == ')') {
      ++i;
      skip_space ();
      return i == legacy.size () ? out : array<string> ();
    }
    if (legacy[i++] != '"') return array<string> ();
    std::string value;
    bool closed= false;
    while (i < legacy.size ()) {
      char c= legacy[i++];
      if (c == '"') {
        closed= true;
        break;
      }
      if (c != '\\') {
        value.push_back (c);
        continue;
      }
      if (i >= legacy.size ()) return array<string> ();
      char escaped= legacy[i++];
      if (escaped == 'n') value.push_back ('\n');
      else if (escaped == 'r') value.push_back ('\r');
      else if (escaped == 't') value.push_back ('\t');
      else value.push_back (escaped);
    }
    if (!closed) return array<string> ();
    QString text= QString::fromUtf8 (value.data (), static_cast<int> (value.size ()));
    if (!text.isEmpty () && !seen.contains (text)) {
      seen.insert (text);
      out << from_qstring (text);
      if (N (out) >= 8) return out;
    }
  }
}

string
serialize_color_preference (const array<string>& colors) {
  QJsonArray values;
  QSet<QString> seen;
  for (int i= 0; i < N (colors) && values.size () < 8; ++i) {
    QString text= to_qstring (colors[i]);
    if (text.isEmpty () || seen.contains (text)) continue;
    seen.insert (text);
    values.append (text);
  }
  QByteArray bytes= QJsonDocument (values).toJson (QJsonDocument::Compact);
  return string (bytes.constData (), bytes.size ());
}

void
store_color_preference (string key, const array<string>& colors) {
  set_user_preference (key, serialize_color_preference (colors));
  save_user_preferences ();
}

} // namespace

array<string>
color_picker_recent_colors () {
  return parse_color_preference (get_user_preference ("recent text colors", "[]"));
}

array<string>
color_picker_saved_colors () {
  return parse_color_preference (get_user_preference ("saved text colors", "[]"));
}

void
color_picker_remember_color (string color) {
  if (color == "") return;
  array<string> current= color_picker_recent_colors ();
  array<string> next;
  next << color;
  for (int i= 0; i < N (current) && N (next) < 8; ++i)
    if (current[i] != color) next << current[i];
  store_color_preference ("recent text colors", next);
}

void
color_picker_set_saved_colors (array<string> colors) {
  store_color_preference ("saved text colors", colors);
}

/******************************************************************************
* Loading and saving user preferences
******************************************************************************/

static hashmap<string,string>
read_json_user_preferences (url prefs_file, bool& ok) {
  ok= false;
  hashmap<string,string> prefs ("");
  string s;
  if (load_string (prefs_file, s, false)) return prefs;

  QJsonParseError error;
  QJsonDocument doc= QJsonDocument::fromJson (QByteArray (as_charp (s), N(s)),
                                              &error);
  if (error.error != QJsonParseError::NoError || !doc.isObject ()) {
    std_error << "Invalid preferences JSON in " << prefs_file << LF;
    return prefs;
  }

  QJsonObject root= doc.object ();
  if (root.value ("format").toString () != "athena-preferences" ||
      root.value ("version").toInt () != 1 ||
      !root.value ("preferences").isObject ()) {
    std_error << "Unsupported preferences JSON in " << prefs_file << LF;
    return prefs;
  }

  QJsonObject obj= root.value ("preferences").toObject ();
  for (QJsonObject::const_iterator it= obj.constBegin ();
       it != obj.constEnd (); ++it) {
    if (!it.value ().isString ()) continue;
    prefs (from_qstring (it.key ()))= from_qstring (it.value ().toString ());
  }

  ok= true;
  return prefs;
}

static void
write_json_user_preferences (
  url prefs_file, const hashmap<string,string>& preferences) {
  iterator<string> it= iterate (preferences);
  QJsonObject prefs;
  while (it->busy ()) {
    string key= it->next ();
    prefs.insert (to_qstring (key), to_qstring (preferences[key]));
  }

  QJsonObject root;
  root.insert ("format", "athena-preferences");
  root.insert ("version", 1);
  root.insert ("preferences", prefs);

  QJsonDocument doc (root);
  QByteArray bytes= doc.toJson (QJsonDocument::Indented);
  if (save_string (prefs_file, string (bytes.constData ())))
    std_warning << "The user preferences could not be saved\n";
}

static hashmap<string,string>
read_user_preferences (url prefs_file, url& canonical_file) {
  bool json_ok= false;
  canonical_file= with_json_suffix (prefs_file);
  return read_json_user_preferences (canonical_file, json_ok);
}

static void
write_user_preferences (
  url prefs_file, const hashmap<string,string>& preferences) {
  write_json_user_preferences (with_json_suffix (prefs_file), preferences);
}

void
load_user_preferences () {
  load_user_preferences ("$ATHENA_HOME_PATH/system/preferences.json");
}

void
load_user_preferences (url prefs_file) {
  ensure_builtin_user_preferences ();
  save_user_preferences ();
  url canonical_file;
  hashmap<string,string> preferences=
    read_user_preferences (prefs_file, canonical_file);
  {
    std::unique_lock<std::shared_mutex> guard (user_preferences_mutex);
    user_prefs= std::move (preferences);
    user_prefs_file= std::move (canonical_file);
    user_prefs_modified= false;
  }
}

void
dump_user_preferences (url prefs_file) {
  ensure_builtin_user_preferences ();
  std::shared_lock<std::shared_mutex> guard (user_preferences_mutex);
  write_user_preferences (prefs_file, user_prefs);
}

void
save_user_preferences () {
  ensure_builtin_user_preferences ();
  std::unique_lock<std::shared_mutex> guard (user_preferences_mutex);
  if (!user_prefs_modified) return;
  write_user_preferences (user_prefs_file, user_prefs);
  user_prefs_modified= false;
}
