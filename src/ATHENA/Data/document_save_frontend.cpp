// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#include "document_save_frontend.hpp"
#include "application_lifecycle.hpp"
#include "new_buffer.hpp"
#include "new_view.hpp"
#include "recent_files.hpp"
#include "native_interfaces.hpp"
#include "generic_editor_commands.hpp"
#include "editor.hpp"
#include "server.hpp"
#include "file.hpp"
#include "convert.hpp"
#include "actor_transport.hpp"
#include "buffer_actor.hpp"
#include "buffer_name_catalog.hpp"
#include "tm_data.hpp"
#include "scheme_execution_context.hpp"
#include "GoogleCloudTodo.hpp"
#include "guile_tm.hpp"
#include "object.hpp"
#include "document_persistence.hpp"
#include "buffer_export.hpp"
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
void saved_callbacks (object options) {
  for (; !is_null (options); options= cdr (options)) {
    object option= car (options);
    if (tmscm_is_pair (object_to_tmscm (option)) &&
        car (option) == symbol_object ("on-saved"))
      call (cdr (option));
  }
}

void remove_autosave (url name) {
  url saved= glue (name, "~");
  if (!exists (saved)) return;
  if (!is_rooted_tmfs (saved)) remove (saved);
}

void queue_cloud_sync (url name) {
  const auto* context= current_scheme_execution_context ();
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
    SCHEME_CAPABILITY_BUFFER | SCHEME_CAPABILITY_GLOBAL, id);
  if (!ticket) actor_continuation_registry::instance ().discard (id);
}
}

void document_save_preserve_viewport (url name) {
  const auto* context= current_scheme_execution_context ();
  if (context != nullptr && context->editor != nullptr &&
      context->actor != nullptr && context->actor->current_buffer_url () == name)
    context->editor->notify_change (THE_FREEZE);
}

void document_save_post (url name, object options) {
  queue_cloud_sync (name);
  athena_vault_backup_dispatch_realtime (name);
  // Save/close callers must advance only after a successful save, including
  // the no-changes case. Failure and cancellation never run these callbacks.
  saved_callbacks (options);
}

bool document_save_now (url name, object options) {
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
  document_save_post (name, options);
  return true;
}

bool document_save_as_now (url destination, url source, object options) {
  const bool scratch= is_scratch (source);
  rename_buffer (source, destination);
  pretend_buffer_modified (destination);
  // Complete cleanup before invoking a callback that may close the buffer.
  if (!document_save_now (destination, null_object ())) return false;
  if (scratch && source != destination && exists (source)) remove (source);
  saved_callbacks (options);
  return true;
}

namespace {
struct save_request {
  athena_actor_id actor;
  athena_view_id view;
  SchemeCapabilitySet capabilities;
  athena_scheme_handle_id options= ATHENA_NO_SCHEME_HANDLE;
  std::string source, destination;
  bool overwrite= false;
  std::function<void()> completed;
  ~save_request () { scheme_command_handle_release (options); }
  object arguments () const {
    return tmscm_to_object (scheme_command_handle_value (options));
  }
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

save_task make_save_request (object arguments) {
  url source= get_current_buffer_safe ();
  if (!is_null (arguments) && is_url (car (arguments))) {
    source= as_url (car (arguments));
    arguments= cdr (arguments);
  }
  const auto* context= current_scheme_execution_context ();
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
  request->capabilities= SCHEME_CAPABILITY_BUFFER | SCHEME_CAPABILITY_GLOBAL;
  request->options= scheme_command_handle_acquire (object_to_tmscm (arguments));
  for (object opts= arguments; !is_null (opts); opts= cdr (opts))
    if (car (opts) == keyword_object ("overwrite")) request->overwrite= true;
  return request;
}

void resume_save (save_task request, std::function<void()> next) {
  auto id= actor_continuation_registry::instance ().store ([request, next] {
    const auto* context= current_scheme_execution_context ();
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
  const auto* context= current_scheme_execution_context ();
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
                             url (request->source.c_str ()), request->arguments ()) &&
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
    if (document_save_now (url (request->source.c_str ()), request->arguments ()) &&
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
    document_save_post (source, request->arguments ());
    if (request->completed) request->completed ();
    return;
  }
  if (document_cannot_write (source, "Save file")) return;
  object modified= is_rooted_tmfs (source)? object (false):
    object (last_modified (source));
  object saved= is_rooted_tmfs (source)? application_buffer_last_save (source):
    object (get_last_save_buffer (source));
  if (is_of_type (source, "fr") && is_int (modified) && is_int (saved) &&
      as_int (modified) > as_int (saved))
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

void document_save (object arguments) {
  auto request= make_save_request (arguments);
  start_save (request, [request] { save_permissions (request); });
}

void document_save_manual (object arguments) {
  auto request= make_save_request (arguments);
  start_save (request, [request] {
    qtm_document_history_manual_save_requested ();
    if (!athena_current_buffer_realtime_save_active ()) save_permissions (request);
  });
}

void document_save_as (object destination, object arguments) {
  auto request= make_save_request (arguments);
  request->destination= encode_url (is_url (destination)? as_url (destination):
                                   url_system (as_string (destination)));
  start_save (request, [request] { save_as_permissions (request); });
}

void document_save_dialog () {
  auto request= make_save_request (null_object ());
  start_save (request, [request] { choose_save_destination (request); });
}

void document_export_main (url source, object destination, string format, object options) {
  bool portable= false;
  if (is_list (destination) && !is_null (destination) && is_url (car (destination))) {
    object rest= cdr (destination);
    portable= !is_null (rest) && is_string (car (rest)) && as_string (car (rest)) == "on";
    destination= car (destination);
  }
  for (object opts= options; !is_null (opts); opts= cdr (opts))
    if (car (opts) == keyword_object ("latex-portable")) portable= true;
  const bool relative_text= is_string (destination);
  string target= relative_text ? replace (replace (as_string (destination), ":", "-"), ";", "-") :
                               as_string (as_url (destination));
  auto request= make_save_request (cons (object (source), options));
  request->destination= std::string (target.data (), N(target));
  const std::string fmt (format.data (), N(format));
  start_save (request, [request, relative_text, portable, fmt] {
    url destination= relative_text ? relative (get_master_buffer (url (request->source.c_str ())),
                                               url_system (request->destination.c_str ())):
                                    url (request->destination.c_str ());
    request->destination= encode_url (destination);
    if (document_cannot_write (destination, "Export file")) return;
    auto export_document= [request, portable, fmt] {
      auto settings= conversion_options_snapshot ();
      if (fmt == "latex" && portable) settings["texmacs->latex:portable"]= "on";
      url destination (request->destination.c_str ());
      const bool failed= buffer_export_request (url (request->source.c_str ()), destination,
                                                string (fmt.c_str ()), settings);
      get_server ()->set_message (compound ("concat", failed ? "Could not save " : "Exported to ",
        compound ("verbatim", as_string (destination, URL_SYSTEM))), "Export file");
    };
    if (is_of_type (destination, "f") && !request->overwrite)
      confirm_save (request, QObject::tr ("File already exists. Really overwrite?"),
                    export_document, QObject::tr ("Export file"));
    else export_document ();
  });
}

void document_export (object destination) {
  const url target= is_url (destination) ? as_url (destination) : url_system (as_string (destination));
  string format= file_format (target);
  if (format == "generic") format= "verbatim";
  document_export_main (get_current_buffer (), destination, format, list_object (keyword_object ("overwrite")));
}

bool document_autosave_enabled (url source) {
  return get_preference ("document save mode") == "autosave" &&
    !is_rooted_web (source) && !is_rooted_tmfs (source) && !is_scratch (source) &&
    exists (source) && is_of_type (source, "fw");
}

void document_autosave (url source) {
  if (!document_autosave_enabled (source)) return;
  auto request= make_save_request (list_object (object (source)));
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
  auto request= make_save_request (list_object (object (name)));
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
