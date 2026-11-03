// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#include "App/Frontend/document_save_frontend.hpp"
#include "App/Frontend/application_lifecycle.hpp"
#include "App/Frontend/new_buffer.hpp"
#include "App/Frontend/new_view.hpp"
#include "App/Frontend/recent_files.hpp"
#include "native_interfaces.hpp"
#include "generic_editor_commands.hpp"
#include "Editor/editor.hpp"
#include "App/Runtime/server.hpp"
#include "file.hpp"
#include "convert.hpp"
#include "App/Runtime/actor_transport.hpp"
#include "App/Runtime/buffer_actor.hpp"
#include "App/Runtime/buffer_name_catalog.hpp"
#include "App/Runtime/tm_data.hpp"
#include "App/Runtime/execution_context.hpp"
#include "GoogleCloudTodo.hpp"
#include "Document/Persistence/document_persistence.hpp"
#include "Document/Model/buffer_export.hpp"
#include "analyze.hpp"
#include "QTMDocumentHistory.hpp"
#include "boot.hpp"
#include <QApplication>
#include <QFileDialog>
#include <QMessageBox>
#include <QPointer>
#include <QTimer>
#include <limits>
#include <memory>
#include <functional>
#include <stdexcept>

namespace {
void remove_autosave (url name) {
  url saved= glue (name, "~");
  if (!exists (saved)) return;
  if (!is_rooted_tmfs (saved)) remove (saved);
}

void queue_cloud_sync (url name) {
  const auto* context= current_execution_context ();
  buffer_actor* actor= context == nullptr? nullptr: context->actor;
  athena_view_id view= context == nullptr? ATHENA_NO_VIEW: context->view_id;
  if (actor == nullptr) {
    tm_buffer buffer= concrete_buffer (name);
    if (is_nil (buffer)) return;
    actor= buffer->actor;
    view= ATHENA_NO_VIEW;
  }
  // Keep post-save synchronization out of the saving command. The actor owns
  // document extraction; the queued continuation carries only a URL string.
  string encoded= as_string (name);
  std::string source (encoded.data (), N(encoded));
  auto id= actor_continuation_registry::instance ().store ([source] {
    google_cloud_todo_sync_buffer (url (source.c_str ()), true);
  });
  auto ticket= buffer_actor::submit_to (
    actor->id (), actor_command_kind::run_native_continuation,
    view, ATHENA_NO_BLOB, ATHENA_NO_BLOB,
    EXECUTION_CAPABILITY_BUFFER | EXECUTION_CAPABILITY_GLOBAL, id);
  if (!ticket) actor_continuation_registry::instance ().discard (id);
}
}

void document_save_preserve_viewport (url name) {
  const auto* context= current_execution_context ();
  if (context != nullptr && context->editor != nullptr &&
      context->actor != nullptr && context->actor->current_buffer_url () == name)
    context->editor->notify_change (THE_FREEZE);
}

void document_save_post (url name) {
  queue_cloud_sync (name);
  athena_vault_backup_dispatch_realtime (name);
}

bool document_save_now (url name) {
  document_save_preserve_viewport (name);
  tree filename= compound ("verbatim", utf8_to_cork (as_string (name, URL_SYSTEM)));
  if (buffer_save (name)) {
    pretend_buffer_modified (name);
    get_server ()->set_message (compound ("concat", "Could not save ", filename), "Save file");
    return false;
  }
  if (suffix (name) == "ats") {
    get_server ()->style_clear_cache ();
    generic_focus_parameters_cache_clear ();
  }
  remove_autosave (name);
  buffer_notify_recent (name);
  get_server ()->set_message (compound ("concat", "Saved ", filename), "Save file");
  document_save_post (name);
  return true;
}

bool document_save_as_now (url destination, url source) {
  const bool scratch= is_scratch (source);
  rename_buffer (source, destination);
  pretend_buffer_modified (destination);
  // Complete cleanup before invoking a callback that may close the buffer.
  if (!document_save_now (destination)) return false;
  if (scratch && source != destination && exists (source)) remove (source);
  return true;
}

namespace {
struct save_request {
  athena_actor_id actor;
  athena_view_id view;
  ExecutionCapabilitySet capabilities;
  std::string source, destination;
  bool overwrite= false;
  std::function<void()> completed;
};
using save_task= std::shared_ptr<save_request>;

std::string encode_url (url name) {
  string text= as_string (name);
  return std::string (text.data (), N(text));
}

bool open_buffer (url name) {
  array<url> buffers= get_all_buffers ();
  for (int i=0; i<N(buffers); ++i) if (buffers[i] == name) return true;
  return false;
}

save_task make_save_request (url source, bool overwrite= false) {
  const auto* context= current_execution_context ();
  auto request= std::make_shared<save_request> ();
  request->source= encode_url (source);
  if (context && context->actor && context->actor->current_buffer_url () == source) {
    request->actor= context->actor_id;
    request->view= context->view_id;
  }
  else {
    auto owner= published_buffer_source (request->source);
    request->actor= owner.first;
    request->view= owner.second;
  }
  if (request->actor == ATHENA_NO_ACTOR)
    throw std::runtime_error ("The buffer to save no longer exists");
  request->capabilities= EXECUTION_CAPABILITY_BUFFER | EXECUTION_CAPABILITY_GLOBAL;
  request->overwrite= overwrite;
  return request;
}


void resume_save (save_task request, std::function<void()> next) {
  auto id= actor_continuation_registry::instance ().store ([request, next] {
    const auto* context= current_execution_context ();
    if (!context || !context->actor ||
        (request->view != ATHENA_NO_VIEW && !context->editor) ||
        context->actor->current_buffer_url () != url (request->source.c_str ())) return;
    next ();
  });
  auto ticket= buffer_actor::submit_to (
    request->actor, actor_command_kind::run_native_continuation, request->view,
    ATHENA_NO_BLOB, ATHENA_NO_BLOB, request->capabilities, id);
  if (!ticket) actor_continuation_registry::instance ().discard (id);
}

void start_save (save_task request, std::function<void()> next) {
  const auto* context= current_execution_context ();
  if (context && context->actor_id == request->actor) next ();
  else resume_save (request, std::move (next));
}

void confirm_save (save_task request, QString prompt, std::function<void()> next,
                   QString title= QObject::tr ("Save file")) {
  if (headless_mode) {
    get_server ()->set_message ("Interactive confirmation required", string (title.toUtf8 ().constData ()));
    return;
  }
  QMetaObject::invokeMethod (QCoreApplication::instance (), [request, prompt, next, title] {
    auto* dialog= new QMessageBox (QMessageBox::Question, title,
      prompt, QMessageBox::Yes | QMessageBox::No, QApplication::activeWindow ());
    dialog->setAttribute (Qt::WA_DeleteOnClose);
    dialog->setDefaultButton (QMessageBox::No);
    QObject::connect (dialog, &QDialog::finished, dialog, [request, next] (int answer) {
      if (answer == QMessageBox::Yes) resume_save (request, next);
    });
    dialog->open ();
  }, Qt::QueuedConnection);
}

void save_as_permissions (save_task request);

void choose_save_destination (save_task request) {
  if (headless_mode) {
    get_server ()->set_message ("Choose a destination with Save As", "Save file");
    return;
  }
  url source (request->source.c_str ());
  string path= is_scratch (source)? string (""): as_string (source, URL_SYSTEM);
  QString initial= QString::fromUtf8 (path.data (), N(path));
  QMetaObject::invokeMethod (QCoreApplication::instance (), [request, initial] {
    auto* dialog= new QFileDialog (QApplication::activeWindow (), QObject::tr ("Save ATHENA file"));
    dialog->setAttribute (Qt::WA_DeleteOnClose);
    dialog->setAcceptMode (QFileDialog::AcceptSave);
    dialog->setFileMode (QFileDialog::AnyFile);
    dialog->setNameFilter (QObject::tr ("ATHENA documents (*.ath);;ATHENA styles (*.ats);;All files (*)"));
    dialog->setDefaultSuffix ("ath");
    if (!initial.isEmpty ()) dialog->selectFile (initial);
    QObject::connect (dialog, &QFileDialog::fileSelected, dialog, [request] (const QString& path) {
      QByteArray bytes= path.toUtf8 ();
      std::string selected (bytes.constData (), bytes.size ());
      resume_save (request, [request, selected] {
        request->destination= encode_url (url_system (selected.c_str ()));
        // QFileDialog already confirmed overwriting an existing file.
        request->overwrite= true;
        save_as_permissions (request);
      });
    });
    dialog->open ();
  }, Qt::QueuedConnection);
}

void save_as_other_buffer (save_task request) {
  url destination (request->destination.c_str ());
  auto save= [request] {
    if (document_save_as_now (url (request->destination.c_str ()),
                             url (request->source.c_str ())) &&
        request->completed) request->completed ();
  };
  if (request->destination != request->source && open_buffer (destination)) {
    string name= as_string (destination, URL_SYSTEM);
    confirm_save (request, QObject::tr ("The file %1 is being edited. Discard edits?")
      .arg (QString::fromUtf8 (name.data (), N(name))), save);
  }
  else save ();
}

void save_as_permissions (save_task request) {
  url destination (request->destination.c_str ());
  if (document_cannot_write (destination, "Save file")) return;
  if (is_of_type (destination, "f") && !request->overwrite)
    confirm_save (request, QObject::tr ("File already exists. Really overwrite?"),
                  [request] { save_as_other_buffer (request); });
  else save_as_other_buffer (request);
}

void save_faithful (save_task request) {
  url source (request->source.c_str ());
  string ext= suffix (source);
  auto save= [request] {
    if (document_save_now (url (request->source.c_str ())) &&
        request->completed) request->completed ();
  };
  if (ext == "ath" || ext == "ats" || ext == "tm" || ext == "ts" ||
      ext == "tp" || ext == "stm" || ext == "scm" || ext == "") save ();
  else confirm_save (request, QObject::tr ("Save requires data conversion. Really proceed?"), save);
}

void save_permissions (save_task request) {
  url source (request->source.c_str ());
  document_save_preserve_viewport (source);
  if (is_scratch (source)) { choose_save_destination (request); return; }
  if (!open_buffer (source)) {
    get_server ()->set_message ("The buffer no longer exists", "Save file");
    return;
  }
  if (!athena_buffer_user_modified (source)) {
    get_server ()->set_message ("No changes need to be saved", "Save file");
    document_save_post (source);
    if (request->completed) request->completed ();
    return;
  }
  if (document_cannot_write (source, "Save file")) return;
  if (!is_rooted_tmfs (source) && is_of_type (source, "fr") &&
      last_modified (source) > get_last_save_buffer (source))
    confirm_save (request, QObject::tr ("The file has changed on disk. Really save?"),
                  [request] { save_faithful (request); });
  else save_faithful (request);
}
}

bool document_cannot_write (url name, string action) {
  tree filename= compound ("verbatim", as_string (name, URL_SYSTEM));
  if (!is_of_type (name, "f") && !is_of_type (name, "c")) {
    get_server ()->set_message (compound ("concat", "The file ", filename, " cannot be created"), action);
    return true;
  }
  if (is_of_type (name, "f") && !is_of_type (name, "w")) {
    get_server ()->set_message (compound ("concat", "You do not have write access for ", filename), action);
    return true;
  }
  return false;
}

void document_save () {
  auto request= make_save_request (get_current_buffer_safe ());
  start_save (request, [request] { save_permissions (request); });
}

void document_save (url source, bool overwrite) {
  auto request= make_save_request (source, overwrite);
  start_save (request, [request] { save_permissions (request); });
}

namespace {
void start_manual_save (save_task request) {
  start_save (request, [request] {
    qtm_document_history_manual_save_requested ();
    if (!athena_current_buffer_realtime_save_active ()) save_permissions (request);
  });
}
}

void document_save_manual () {
  start_manual_save (make_save_request (get_current_buffer_safe ()));
}

void document_save_manual (url source) {
  start_manual_save (make_save_request (source));
}

void document_save_as (url destination) {
  auto request= make_save_request (get_current_buffer_safe ());
  request->destination= encode_url (destination);
  start_save (request, [request] { save_as_permissions (request); });
}

void document_save_dialog () {
  auto request= make_save_request (get_current_buffer_safe ());
  start_save (request, [request] { choose_save_destination (request); });
}


bool document_autosave_enabled (url source) {
  return get_preference ("document save mode") == "autosave" &&
    !is_rooted_web (source) && !is_rooted_tmfs (source) && !is_scratch (source) &&
    exists (source) && is_of_type (source, "fw");
}

void document_autosave (url source) {
  if (!document_autosave_enabled (source)) return;
  auto request= make_save_request (source);
  start_save (request, [request] {
    const url source (request->source.c_str ());
    if (!document_autosave_enabled (source) || !buffer_modified_since_autosave (source)) return;
    const string format= file_format (source);
    tree filename= compound ("verbatim", as_string (source, URL_SYSTEM));
    if (format != "texmacs" && format != "stm") {
      get_server ()->set_message (compound ("concat", "Warning: ", filename, " not auto-saved"), "Auto-save file");
      return;
    }
    if (buffer_export (source, glue (source, "~"), format))
      get_server ()->set_message (compound ("concat", "Failed to auto-save ", filename), "Auto-save file");
    else {
      pretend_buffer_autosaved (source);
      get_server ()->set_message (compound ("concat", "Auto-saved ", filename), "Auto-save file", true);
    }
  });
}

namespace {
QPointer<QTimer> autosave_timer;

void autosave_all_gui () {
  const array<url> buffers= get_all_buffers ();
  for (int i=0; i<N(buffers); ++i) document_autosave (buffers[i]);
}

void autosave_schedule_gui () {
  if (autosave_timer) autosave_timer->stop ();
  if (get_preference ("document save mode") != "autosave") return;
  const string value= get_preference ("autosave");
  bool valid= false;
  qint64 seconds= QString::fromUtf8 (value.data (), N(value)).toLongLong (&valid);
  if (!valid) seconds= 120;
  if (seconds <= 0) return;
  if (!autosave_timer) {
    autosave_timer= new QTimer (QCoreApplication::instance ());
    autosave_timer->setSingleShot (true);
    QObject::connect (autosave_timer, &QTimer::timeout, [] {
      autosave_all_gui ();
      autosave_schedule_gui ();
    });
  }
  const qint64 maximum= std::numeric_limits<int>::max () / 1000;
  autosave_timer->start (int (std::min (seconds, maximum) * 1000));
}

void autosave_now_gui () { autosave_all_gui (); autosave_schedule_gui (); }
void notify_autosave_gui () {
  if (!is_none (get_current_view_safe ())) autosave_schedule_gui ();
}
}

void document_autosave_all () { athena_dispatch_ui (autosave_all_gui); }
void document_autosave_now () { athena_dispatch_ui (autosave_now_gui); }
void document_autosave_schedule () { athena_dispatch_ui (autosave_schedule_gui); }
void document_notify_autosave (string, string) { athena_dispatch_ui (notify_autosave_gui); }

void document_save_before_close (url name, std::function<void()> completion) {
  auto request= make_save_request (name);
  request->completed= std::move (completion);
  start_save (request, [request] {
    qtm_document_history_manual_save_requested ();
    url source (request->source.c_str ());
    if (athena_realtime_save_active (source)) {
      if (athena_flush_realtime_buffer (source)) request->completed ();
    }
    else save_permissions (request);
  });
}
