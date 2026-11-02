// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#include "user_shortcuts.hpp"
#include "native_editor_actions.hpp"
#include "editor.hpp"
#include "server.hpp"
#include "file.hpp"
#include "QTMNativeDialogs.hpp"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonParseError>
#include <QMap>
#include <mutex>
#include <stdexcept>

namespace {
QString qs (string s) {
  return QString::fromUtf8 (s.data (), N (s));
}
string shortcut_text (QString s) {
  auto b= s.toUtf8 ();
  return string (b.data (), b.size ());
}
using entries= QMap<QString, QJsonObject>;
std::mutex mutex;
entries shortcuts;
QString filename () {
  return qs (concretize (url ("$ATHENA_HOME_PATH/system/shortcuts.json")));
}
QJsonObject parse_action (const QString& text) {
  QJsonParseError parse;
  auto document= QJsonDocument::fromJson (text.toUtf8 (), &parse);
  QString error;
  if (parse.error != QJsonParseError::NoError || !document.isObject ())
    throw std::invalid_argument ("Shortcut command must be a JSON editor action");
  if (!native_editor_action_validate (document.object (), &error))
    throw std::invalid_argument (error.toStdString ());
  return document.object ();
}
string encode (QJsonObject action) {
  return shortcut_text (
      QString::fromUtf8 (QJsonDocument (action).toJson (QJsonDocument::Compact)));
}
void publish (const entries& next) {
  QJsonArray bindings;
  for (auto i= next.begin (); i != next.end (); ++i)
    bindings.append (QJsonObject{{"key", i.key ()}, {"action", i.value ()}});
  const auto data=
      QJsonDocument (QJsonObject{{"version", 1}, {"bindings", bindings}}).toJson ();
  QDir ().mkpath (QFileInfo (filename ()).absolutePath ());
  QSaveFile file (filename ());
  if (!file.open (QIODevice::WriteOnly) || file.write (data) != data.size () ||
      !file.commit ())
    throw std::runtime_error (
        ("Cannot save keyboard shortcuts: " + file.errorString ()).toStdString ());
  shortcuts= next;
}
} // namespace

bool user_shortcut_action_valid (const QString& text, QString* error) {
  try {
    parse_action (text);
    return true;
  } catch (const std::exception& e) {
    if (error)
      *error= QString::fromUtf8 (e.what ());
    return false;
  }
}
void user_shortcuts_initialize () {
  QFile file (filename ());
  if (!file.exists ())
    return;
  if (!file.open (QIODevice::ReadOnly))
    throw std::runtime_error (file.errorString ().toStdString ());
  QJsonParseError error;
  auto doc= QJsonDocument::fromJson (file.readAll (), &error);
  auto root= doc.object ();
  if (error.error != QJsonParseError::NoError || root["version"].toInt () != 1 ||
      !root["bindings"].isArray ())
    throw std::invalid_argument ("Invalid shortcuts.json schema");
  entries next;
  for (auto value : root["bindings"].toArray ()) {
    auto entry= value.toObject ();
    auto key= entry["key"].toString ();
    QString failure;
    if (key.isEmpty () || next.contains (key) || !entry["action"].isObject () ||
        !native_editor_action_validate (entry["action"].toObject (), &failure))
      throw std::invalid_argument (
          ("Invalid shortcut: " + key + " " + failure).toStdString ());
    next.insert (key, entry["action"].toObject ());
  }
  std::lock_guard<std::mutex> lock (mutex);
  shortcuts= std::move (next);
}
array<string> user_shortcuts_list () {
  std::lock_guard<std::mutex> lock (mutex);
  array<string> out;
  for (auto i= shortcuts.begin (); i != shortcuts.end (); ++i)
    out << shortcut_text (i.key ());
  return out;
}
string user_shortcuts_get (string key) {
  std::lock_guard<std::mutex> lock (mutex);
  auto i= shortcuts.find (qs (key));
  return i == shortcuts.end () ? string ("") : encode (i.value ());
}
bool user_shortcuts_has (string command) {
  auto action= parse_action (qs (command));
  std::lock_guard<std::mutex> lock (mutex);
  for (const auto& value : shortcuts)
    if (value == action)
      return true;
  return false;
}
void user_shortcuts_set (string key, string command) {
  if (key == "")
    throw std::invalid_argument ("Shortcut key must not be empty");
  auto action= parse_action (qs (command));
  std::lock_guard<std::mutex> lock (mutex);
  auto next= shortcuts;
  next[qs (key)]= action;
  publish (next);
}
void user_shortcuts_remove (string key) {
  std::lock_guard<std::mutex> lock (mutex);
  auto next= shortcuts;
  next.remove (qs (key));
  publish (next);
}
void user_shortcuts_open_native (string key, string command) {
  array<string> values;
  {
    std::lock_guard<std::mutex> lock (mutex);
    for (auto i= shortcuts.begin (); i != shortcuts.end (); ++i)
      values << shortcut_text (i.key ()) << encode (i.value ());
  }
  auto result= qtm_shortcut_editor_dialog (key, command, values);
  if (N (result) == 0)
    return;
  entries next;
  for (int i= 1; i + 1 < N (result); i+= 2)
    next.insert (qs (result[i]), parse_action (qs (result[i + 1])));
  try {
    std::lock_guard<std::mutex> lock (mutex);
    publish (next);
  } catch (const std::exception& error) {
    qtm_info_dialog (string (error.what ()), "Keyboard shortcuts");
  }
}
void user_shortcuts_open (object options) {
  auto args= as_array_object (options);
  user_shortcuts_open_native (N (args) > 0 ? as_string (args[0]) : string (""),
                              N (args) > 1 ? as_string (args[1]) : string (""));
}
std::optional<native_key_binding> user_shortcuts_find (string key) {
  bool matched= false, prefix= false;
  {
    std::lock_guard<std::mutex> lock (mutex);
    for (auto i= shortcuts.begin (); i != shortcuts.end (); ++i) {
      const string rewritten= get_server ()->kbd_pre_rewrite (shortcut_text (i.key ()));
      if (rewritten == key) {
        matched= true;
        break;
      }
      if (N (rewritten) > N (key) && rewritten (0, N (key) + 1) == key * " ")
        prefix= true;
    }
  }
  if (matched) {
    const std::string stable (key.data (), N(key));
    return native_key_binding {[stable] {
      user_shortcuts_run (string (stable.data (), stable.size ()));
    }, "", ""};
  }
  if (prefix) return native_key_binding {{}, std::string (key.data (), N(key)), ""};
  return std::nullopt;
}
void user_shortcuts_run (string key) {
  QJsonObject action;
  {
    std::lock_guard<std::mutex> lock (mutex);
    for (auto i= shortcuts.begin (); i != shortcuts.end (); ++i)
      if (get_server ()->kbd_pre_rewrite (shortcut_text (i.key ())) == key) {
        action= i.value ();
        break;
      }
  }
  if (!action.isEmpty ())
    native_editor_action_execute (get_current_editor (), action);
}
string user_shortcuts_inverse (tree source) {
  // Macro insertion labels use the same source form as the shipped keymaps.
  if (!is_tuple (source, "make", 1) || !is_tuple (source[1], "quote", 1) ||
      !is_atomic (source[1][1]))
    return "";
  QJsonObject action{{"op", "make"}, {"tag", qs (source[1][1]->label)}};
  std::lock_guard<std::mutex> lock (mutex);
  for (auto i= shortcuts.begin (); i != shortcuts.end (); ++i)
    if (i.value () == action)
      return get_server ()->kbd_pre_rewrite (shortcut_text (i.key ()));
  return "";
}
