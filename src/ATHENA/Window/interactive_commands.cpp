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
#include "document_save_frontend.hpp"
#include "native_dispatch.hpp"
#include "QTMNativeDialogs.hpp"
#include "buffer_actor.hpp"
#include <QCoreApplication>
#include <QTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <stdexcept>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

namespace {
bool is_pair (object value) { return tmscm_is_pair (object_to_tmscm (value)); }
QJsonObject command_properties;
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
object get_history (string name) {
  const auto found= history.find (bytes (name));
  return found == history.end () ? null_object () : string_to_object (string (found->second.c_str ()));
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
  command_properties= document.object ();
}

void interactive_learn (string name, object values) {
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
  history[bytes (name)]= bytes (object_to_string (as_list_object (rows)));
  history_modified= true;
}
object interactive_learned (string name) {
  load_history ();
  std::lock_guard<std::mutex> guard (history_mutex);
  return get_history (name);
}
void interactive_forget (string name) {
  load_history ();
  std::lock_guard<std::mutex> guard (history_mutex);
  history.erase (bytes (name));
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
void interactive_command (string function, array<string> labels) {
  const auto* context= current_scheme_execution_context ();
  ASSERT (context && context->editor, "interactive command requires a BufferActor");
  const auto actor= context->actor_id;
  const auto view= context->view_id;
  const QJsonObject descriptor= command_properties.value (to_qstring (function)).toObject ();
  const QJsonArray arguments= descriptor.value ("arguments").toArray ();
  if (N(labels) == 0 && arguments.isEmpty ())
    throw std::invalid_argument ("Missing interactive argument metadata: " + bytes (function));

  const object learned= interactive_learned (function);
  std::vector<QTMInteractiveField> fields;
  std::vector<bool> passwords;
  const int count= N(labels) ? N(labels) : arguments.size ();
  for (int i=0; i<count; ++i) {
    const QJsonObject argument= N(labels) ? QJsonObject () : arguments[i].toObject ();
    QTMInteractiveField field;
    field.prompt= N(labels) ? labels[i] : from_qstring (argument["label"].toString ());
    field.type= from_qstring (argument.value ("type").toString ("string"));
    if (!ends (field.prompt, ":") && !ends (field.prompt, "?")) field.prompt << ":";
    if (argument.contains ("default")) {
      object initial;
      const string provider= from_qstring (argument["default"].toString ());
      if (!native_dispatch_call (provider, array<object> (), initial))
        throw std::invalid_argument ("Unknown native argument default: " + bytes (provider));
      if (is_string (initial)) field.proposals << as_string (initial);
      else if (is_url (initial)) field.proposals << as_string (as_url (initial));
    }
    for (object rows= learned; is_pair (rows); rows= cdr (rows)) {
      for (object row= car (rows); is_pair (row); row= cdr (row)) {
        if (car (car (row)) != object (as_string (i))) continue;
        const object value= cdr (car (row));
        if (!is_string (value)) break;
        const string proposal= as_string (value);
        bool present= false;
        for (int j=0; j<N(field.proposals); ++j)
          present= present || field.proposals[j] == proposal;
        if (!present) field.proposals << proposal;
        break;
      }
    }
    passwords.push_back (field.type == "password");
    fields.push_back (std::move (field));
  }
  const std::string command= bytes (function);
  const string title= ends (fields.front ().prompt, "?") ? "Question" : "Enter data";
  qtm_interactive_form_async (title, fields,
    [actor, view, command, passwords] (std::vector<std::string> answers) {
      if (answers.size () != passwords.size ()) return;
      const auto continuation= actor_continuation_registry::instance ().store (
        [command, passwords, answers= std::move (answers)] {
          if (current_scheme_execution_context ()->editor == nullptr) return;
          array<object> arguments;
          object learned= null_object ();
          for (const auto& answer: answers)
            arguments << object (string (answer.data (), answer.size ()));
          for (int i=N(arguments)-1; i>=0; --i)
            learned= cons (cons (object (as_string (i)),
              passwords[i] ? object ("") : arguments[i]), learned);
          const string function (command.data (), command.size ());
          interactive_learn (function, learned);
          object result;
          if (!native_dispatch_call (function, arguments, result))
            throw std::invalid_argument ("Unknown native interactive command: " + command);
        });
      if (!buffer_actor::submit_to (actor, actor_command_kind::run_native_continuation,
            view, ATHENA_NO_BLOB, ATHENA_NO_BLOB,
            SCHEME_CAPABILITY_BUFFER | SCHEME_CAPABILITY_UI |
              SCHEME_CAPABILITY_GLOBAL, continuation))
        actor_continuation_registry::instance ().discard (continuation);
    });
}

void interactive_schedule_idle (int milliseconds, object continuation) {
  struct retained_callback {
    athena_scheme_handle_id handle;
    explicit retained_callback (object value):
      handle (scheme_command_handle_acquire (object_to_tmscm (value))) {}
    ~retained_callback () { scheme_command_handle_release (handle); }
  };
  auto callback= std::make_shared<retained_callback> (continuation);
  qt_schedule_editor_idle (milliseconds, [callback] {
    call (tmscm_to_object (scheme_command_handle_value (callback->handle)));
  });
}
void interactive_startup () {
  qt_schedule_editor_idle (10000, document_autosave_schedule);
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
