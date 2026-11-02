// Copyright (C) 1999 Joris van der Hoeven, 2026 ATHENA contributors.
// GPL-3.0-or-later.
#include "interactive_commands.hpp"
#include "guile_tm.hpp"
#include "file.hpp"
#include "editor.hpp"
#include "server.hpp"
#include "new_buffer.hpp"
#include "analyze.hpp"
#include "qt_utilities.hpp"
#include "tm_configure.hpp"
#include "sys_utils.hpp"
#include "boot.hpp"
#include "native_interfaces.hpp"
#include <QCoreApplication>
#include <QTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <stdexcept>
#include <map>
#include <mutex>
#include <vector>

namespace {
bool is_pair (object value) { return tmscm_is_pair (object_to_tmscm (value)); }
object procedure_name (object function) {
  if (is_symbol (function)) return function;
  if (is_string (function)) return symbol_object (as_string (function));
  if (!is_procedure (function)) return object (false);
  object name= call ("procedure-name", function);
  return name != object (false) ? name : call ("%athena-procedure-name", function);
}
std::mutex history_mutex;
std::once_flag history_loaded;
std::map<std::string, std::string> history;
bool history_modified= false;
std::string bytes (string value) { return std::string (value.data (), N(value)); }
void load_history () {
  std::call_once (history_loaded, [] {
    const url source ("$ATHENA_HOME_PATH/system/interactive.scm");
    if (!exists (source)) return;
    string text;
    if (load_string (source, text, false)) return;
    for (object entries= string_to_object (text); is_pair (entries); entries= cdr (entries)) {
      object entry= car (entries);
      history[bytes (as_symbol (car (entry)))]= bytes (object_to_string (cdr (entry)));
    }
  });
}
object get_history (object name) {
  const auto found= history.find (bytes (as_symbol (name)));
  return found == history.end () ? null_object () : string_to_object (string (found->second.c_str ()));
}
object property (object function, object key) { return call ("property", function, key); }
object field_property (object function, const char* property_name, object field) {
  return property (function, list_object (keyword_object (property_name), field));
}
object field_descriptor (object function, object field) {
  object annotation= field_property (function, "argument", field);
  string label= upcase_first (as_symbol (field)), type= "string";
  if (is_pair (annotation)) {
    if (is_null (cdr (annotation)) && is_string (car (annotation))) label= as_string (car (annotation));
    else if (is_pair (cdr (annotation))) {
      object first= car (annotation), second= car (cdr (annotation));
      if (is_string (second)) label= as_string (second);
      if (is_string (first)) type= as_string (first);
      else if (is_symbol (first)) type= as_symbol (first);
    }
  }
  object initial= field_property (function, "default", field);
  object proposals= field_property (function, "proposals", field);
  object choices= is_procedure (initial) ? list_object (call (initial)) :
    is_procedure (proposals) ? call (proposals) : null_object ();
  return cons (object (label), cons (object (type), choices));
}
object compute_arguments (object function) {
  object fields= property (function, keyword_object ("arguments"));
  array<object> result;
  if (fields == object (false)) {
    object source= call ("procedure-source", function);
    if (is_pair (source) && car (source) == symbol_object ("lambda") &&
        is_pair (cdr (source)) && is_list (car (cdr (source))))
      for (object names= car (cdr (source)); is_pair (names); names= cdr (names))
        result << object (upcase_first (as_symbol (car (names))));
  }
  else {
    for (; is_pair (fields); fields= cdr (fields)) result << field_descriptor (function, car (fields));
    object synopsis= property (function, keyword_object ("synopsis*"));
    if (N(result)>0 && is_pair (synopsis) && is_null (cdr (synopsis)) && is_string (car (synopsis)))
      result[0]= cons (car (synopsis), cdr (result[0]));
  }
  return as_list_object (result);
}
}

void interactive_properties_initialize () {
  string data;
  if (load_string ("$ATHENA_PATH/misc/command-properties.json", data, true))
    throw std::runtime_error ("Could not load command properties");
  QJsonParseError error;
  QJsonDocument document= QJsonDocument::fromJson (QByteArray (data.data (), N(data)), &error);
  if (error.error != QJsonParseError::NoError || !document.isObject ())
    throw std::runtime_error ("Invalid command-properties.json");
  const auto text= [] (const QString& value) { return string (value.toUtf8 ().constData ()); };
  const QJsonObject commands= document.object ();
  for (auto it= commands.begin (); it != commands.end (); ++it) {
    const object name= symbol_object (text (it.key ()));
    const QJsonObject descriptor= it.value ().toObject ();
    const auto put= [&] (object key, object value) {
      call ("property-set!", name, key, value, null_object ());
    };
    if (descriptor.contains ("interactive"))
      put (keyword_object ("interactive"), list_object (object (descriptor["interactive"].toBool ())));
    if (descriptor.contains ("synopsis"))
      put (keyword_object ("synopsis"), list_object (object (text (descriptor["synopsis"].toString ()))));
    if (descriptor.contains ("check-mark")) {
      const QJsonArray mark= descriptor["check-mark"].toArray ();
      put (keyword_object ("check-mark"), list_object (object (text (mark[0].toString ())),
        eval (symbol_object (text (mark[1].toString ())))));
    }
    if (descriptor.contains ("applicable"))
      put (keyword_object ("applicable"), list_object (
        eval (symbol_object (text (descriptor["applicable"].toString ())))));
    if (!descriptor.contains ("arguments")) continue;
    const QJsonArray arguments= descriptor["arguments"].toArray ();
    object formals= descriptor.contains ("rest") ?
      symbol_object (text (descriptor["rest"].toString ())) : null_object ();
    for (int i= arguments.size ()-1; i>=0; --i)
      formals= cons (symbol_object (text (arguments[i].toObject ()["name"].toString ())), formals);
    put (keyword_object ("arguments"), formals);
    for (const QJsonValue& entry: arguments) {
      const QJsonObject argument= entry.toObject ();
      const object field= symbol_object (text (argument["name"].toString ()));
      object label= list_object (object (text (argument["label"].toString ())));
      if (argument.contains ("type"))
        label= cons (symbol_object (text (argument["type"].toString ())), label);
      put (list_object (keyword_object ("argument"), field), label);
      if (argument.contains ("default"))
        put (list_object (keyword_object ("default"), field),
             eval (symbol_object (text (argument["default"].toString ()))));
    }
  }
}

bool interactive_page_format_applicable (object arguments) {
  (void) arguments;
  return !get_current_editor ()->selection_active_any ();
}

void interactive_learn (object function, object values) {
  const object name= procedure_name (function);
  if (!is_symbol (name)) return;
  array<object> normalized;
  for (; is_pair (values); values= cdr (values)) {
    object entry= car (values), value= cdr (entry);
    if (is_tree (value)) value= tree_to_stree (as_tree (value));
    else if (is_bool (value)) value= object (as_bool (value) ? "true" : "false");
    normalized << cons (car (entry), value);
  }
  const object row= as_list_object (normalized);
  load_history ();
  std::lock_guard<std::mutex> guard (history_mutex);
  array<object> rows;
  rows << row;
  for (object old= get_history (name); is_pair (old); old= cdr (old))
    if (car (old) != row) rows << car (old);
  history[bytes (as_symbol (name))]= bytes (object_to_string (as_list_object (rows)));
  history_modified= true;
}
object interactive_learned (object function) {
  const object name= procedure_name (function);
  load_history ();
  std::lock_guard<std::mutex> guard (history_mutex);
  return get_history (name);
}
void interactive_forget (object function) {
  const object name= procedure_name (function);
  load_history ();
  std::lock_guard<std::mutex> guard (history_mutex);
  history.erase (bytes (as_symbol (name)));
  history_modified= true;
}
void interactive_history_save () {
  load_history ();
  std::lock_guard<std::mutex> guard (history_mutex);
  if (!history_modified) return;
  string text= "(\n";
  for (const auto& entry: history)
    text << "(" << object_to_string (symbol_object (string (entry.first.c_str ())))
         << " . " << string (entry.second.c_str ()) << ")\n";
  text << ")\n";
  if (!save_string ("$ATHENA_HOME_PATH/system/interactive.scm", text)) history_modified= false;
}
void interactive_command (object function, object arguments) {
  call ("lazy-define-force", function);
  if (is_null (arguments)) arguments= compute_arguments (function);
  const object learned= interactive_learned (function);
  array<object> descriptors;
  for (int i=0; is_pair (arguments); ++i, arguments= cdr (arguments)) {
    object descriptor= car (arguments);
    if (is_string (descriptor)) descriptor= list_object (descriptor, object ("string"));
    string label= as_string (car (descriptor));
    if (!ends (label, ":") && !ends (label, "?")) label << ":";
    object choices= cdr (cdr (descriptor));
    array<object> proposals;
    for (object p= choices; is_pair (p); p= cdr (p)) proposals << car (p);
    if (N(proposals)<=1) {
      for (object rows= learned; is_pair (rows); rows= cdr (rows)) {
        object value (false);
        for (object row= car (rows); is_pair (row); row= cdr (row))
          if (car (car (row)) == object (as_string (i))) { value= cdr (car (row)); break; }
        bool explicit_choice= false;
        for (object p= choices; is_pair (p); p= cdr (p)) explicit_choice= explicit_choice || car (p) == value;
        if (!explicit_choice) proposals << value;
      }
    }
    descriptors << cons (object (label), cons (car (cdr (descriptor)), as_list_object (proposals)));
  }
  get_server ()->interactive (function, as_scheme_tree (as_list_object (descriptors)));
}

void interactive_schedule_idle (int milliseconds, object continuation) {
  object body= cons (symbol_object ("native-idle-step"), list_object (object (milliseconds),
    object (double (texmacs_time ())), list_object (symbol_object ("quote"), continuation)));
  exec_delayed_pause (eval (list_object (symbol_object ("lambda"), null_object (), body)));
}
object interactive_idle_step (int milliseconds, double start, object continuation) {
  int remaining= headless_mode ? milliseconds-int (texmacs_time ()-start) :
    milliseconds-get_current_editor ()->idle_time ();
  if (remaining>0) return object (remaining);
  call (continuation);
  return object (true);
}
void interactive_startup () {
  interactive_schedule_idle (10000, eval (symbol_object ("autosave-delayed")));
  if (headless_mode) return;
  const char* messages[]= {"Welcome to ATHENA", "ATHENA is a fork of GNU TeXmacs",
    "ATHENA falls under the GNU general public license", "ATHENA comes without any form of legal warranty",
    "More information about ATHENA can be found in the Help->About menu", ""};
  const int delays[]= {0, 5000, 7500, 10000, 12500, 15000};
  for (int i=0; i<6; ++i) {
    const std::string message= messages[i];
    QTimer::singleShot (delays[i], QCoreApplication::instance (), [message] {
      get_server ()->set_message (string (message.c_str ()), message.empty () ? string ("") : "ATHENA " * texmacs_version (""));
    });
  }
}

void interactive_choose_file (object action, string title, string type, object options) {
  string prompt= starts (title, "Save") ? "Save as:" :
    starts (title, "Export") ? "Export as:" :
    title == "Select database" ? "Selected database:" : "";
  url base= is_pair (options) && is_pair (cdr (options)) ?
    as_url (cadr (options)) : get_master_buffer (get_current_buffer ());
  if (is_pair (options)) {
    prompt= as_string (car (options));
  }
  get_server ()->choose_file (action, title, type, prompt, base);
}
