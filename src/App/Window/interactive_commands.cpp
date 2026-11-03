// Copyright (C) 1999 Joris van der Hoeven, 2026 ATHENA contributors.
// GPL-3.0-or-later.
#include "App/Window/interactive_commands.hpp"
#include "file.hpp"
#include "convert.hpp"
#include "Editor/editor.hpp"
#include "App/Runtime/server.hpp"
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
#include "App/Runtime/buffer_actor.hpp"
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
#include <algorithm>

namespace {
QJsonObject command_properties;
std::mutex history_mutex;
std::once_flag history_loaded;
std::map<std::string, interactive_history_rows> history;
bool history_modified= false;
std::string bytes (string value) { return std::string (value.data (), N(value)); }
void load_history () {
  std::call_once (history_loaded, [] {
    const url current ("$ATHENA_HOME_PATH/system/interactive.json");
    string text;
    if (exists (current)) {
      if (load_string (current, text, false))
        throw std::runtime_error ("Could not read interactive history");
      QJsonParseError error;
      const auto document= QJsonDocument::fromJson (QByteArray (text.data (), N(text)), &error);
      const auto root= document.object ();
      if (error.error != QJsonParseError::NoError || root["version"].toInt () != 1 ||
          !root["commands"].isObject ())
        throw std::runtime_error ("Invalid interactive history");
      const auto commands= root["commands"].toObject ();
      for (auto command= commands.begin (); command != commands.end (); ++command) {
        if (!command.value ().isArray ()) throw std::runtime_error ("Invalid history rows");
        interactive_history_rows rows;
        for (const auto& entry: command.value ().toArray ()) {
          if (!entry.isObject ()) throw std::runtime_error ("Invalid history row");
          interactive_history_row row;
          const auto fields= entry.toObject ();
          for (auto field= fields.begin (); field != fields.end (); ++field) {
            if (!field.value ().isString ()) throw std::runtime_error ("Invalid history value");
            row[field.key ().toStdString ()]= field.value ().toString ().toStdString ();
          }
          rows.push_back (std::move (row));
        }
        history.emplace (command.key ().toStdString (), std::move (rows));
      }
      return;
    }

    // Legacy history is data, not code. Import only string and URL answers;
    // obsolete non-text values remain available in the untouched source file.
    const url legacy ("$ATHENA_HOME_PATH/system/interactive.scm");
    if (!exists (legacy) || load_string (legacy, text, false)) return;
    const tree entries= string_to_scheme_tree (text);
    for (int i=0; i<N(entries); ++i) {
      const tree entry= entries[i];
      if (!is_tuple (entry) || N(entry) != 3 || !is_atomic (entry[0]) ||
          entry[1] != "." || !is_tuple (entry[2])) continue;
      interactive_history_rows rows;
      for (int j=0; j<N(entry[2]); ++j) {
        const tree fields= entry[2][j];
        interactive_history_row row;
        for (int k=0; k<N(fields); ++k) {
          const tree field= fields[k];
          if (!is_tuple (field) || N(field) != 3 || !is_atomic (field[0]) ||
              !is_quoted (field[0]->label) || !is_atomic (field[2]) ||
              !is_quoted (field[2]->label)) continue;
          if (field[1] != "." && field[1] != "system->url" &&
              field[1] != "string->url") continue;
          row[bytes (scm_unquote (field[0]->label))]= bytes (scm_unquote (field[2]->label));
        }
        if (!row.empty ()) rows.push_back (std::move (row));
      }
      history[bytes (entry[0]->label)]= std::move (rows);
    }
    history_modified= true;
  });
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

void interactive_learn (string name, interactive_history_row values) {
  load_history ();
  std::lock_guard<std::mutex> guard (history_mutex);
  auto& rows= history[bytes (name)];
  rows.erase (std::remove (rows.begin (), rows.end (), values), rows.end ());
  rows.insert (rows.begin (), std::move (values));
  history_modified= true;
}
interactive_history_rows interactive_learned (string name) {
  load_history ();
  std::lock_guard<std::mutex> guard (history_mutex);
  const auto found= history.find (bytes (name));
  return found == history.end () ? interactive_history_rows () : found->second;
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
  QJsonObject commands;
  for (const auto& entry: history) {
    QJsonArray rows;
    for (const auto& row: entry.second) {
      QJsonObject fields;
      for (const auto& field: row)
        fields[QString::fromStdString (field.first)]= QString::fromStdString (field.second);
      rows.append (fields);
    }
    commands[QString::fromStdString (entry.first)]= rows;
  }
  QJsonObject root;
  root["version"]= 1;
  root["commands"]= commands;
  const QByteArray data= QJsonDocument (root).toJson (QJsonDocument::Compact);
  if (!save_string ("$ATHENA_HOME_PATH/system/interactive.json",
                   string (data.constData (), data.size ())))
    history_modified= false;
}
void interactive_command (string function, array<string> labels) {
  const auto* context= current_execution_context ();
  ASSERT (context && context->editor, "interactive command requires a BufferActor");
  const auto actor= context->actor_id;
  const auto view= context->view_id;
  const QJsonObject descriptor= command_properties.value (to_qstring (function)).toObject ();
  const QJsonArray arguments= descriptor.value ("arguments").toArray ();
  if (N(labels) == 0 && arguments.isEmpty ())
    throw std::invalid_argument ("Missing interactive argument metadata: " + bytes (function));

  const auto learned= interactive_learned (function);
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
      command_value initial;
      const string provider= from_qstring (argument["default"].toString ());
      if (!native_dispatch_call (provider, command_arguments (), initial))
        throw std::invalid_argument ("Unknown native argument default: " + bytes (provider));
      if (const auto* value= std::get_if<string> (&initial)) field.proposals << *value;
      else if (const auto* value= std::get_if<url> (&initial)) field.proposals << as_string (*value);
    }
    for (const auto& row: learned) {
      const auto found= row.find (std::to_string (i));
      if (found == row.end ()) continue;
      const string proposal (found->second.data (), found->second.size ());
      bool present= false;
      for (int j=0; j<N(field.proposals); ++j)
        present= present || field.proposals[j] == proposal;
      if (!present) field.proposals << proposal;
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
          if (current_execution_context ()->editor == nullptr) return;
          command_arguments arguments;
          interactive_history_row learned;
          for (const auto& answer: answers)
            arguments << command_value (string (answer.data (), answer.size ()));
          for (std::size_t i=0; i<answers.size (); ++i)
            learned[std::to_string (i)]= passwords[i] ? std::string () : answers[i];
          const string function (command.data (), command.size ());
          interactive_learn (function, std::move (learned));
          command_value result;
          if (!native_dispatch_call (function, arguments, result))
            throw std::invalid_argument ("Unknown native interactive command: " + command);
        });
      if (!buffer_actor::submit_to (actor, actor_command_kind::run_native_continuation,
            view, ATHENA_NO_BLOB, ATHENA_NO_BLOB,
            EXECUTION_CAPABILITY_BUFFER | EXECUTION_CAPABILITY_UI |
              EXECUTION_CAPABILITY_GLOBAL, continuation))
        actor_continuation_registry::instance ().discard (continuation);
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
