// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#include "application_lifecycle.hpp"
#include "document_save_frontend.hpp"
#include "document_persistence.hpp"
#include "buffer_name_catalog.hpp"
#include "new_buffer.hpp"
#include "new_window.hpp"
#include "new_view.hpp"
#include "tm_window.hpp"
#include "server.hpp"
#include "file.hpp"
#include "web_files.hpp"
#include "boot.hpp"
#include "scheme_execution_context.hpp"
#include "editor.hpp"
#include "QTMNativeDialogs.hpp"
#include "convert.hpp"
#include "guile_tm.hpp"
#include "object.hpp"
#include "native_interfaces.hpp"
#include "analyze.hpp"
#include "buffer_actor.hpp"
#include "document_commands.hpp"
#include "view_fit.hpp"
#include "recent_files.hpp"
#include "GoogleCloudTodo.hpp"
#include "QTMCompoundDocument.hpp"
#include "Data/Convert/Generic/conversion_registry.hpp"
#include "Data/Convert/Generic/conversion_graph.hpp"
#include <cstdlib>
#include <QApplication>
#include <QFileDialog>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>
#include <QDesktopServices>
#include <QUrl>
#include <functional>
#include <memory>
#include <vector>
#include <algorithm>

namespace {
struct conversion_continuation {
  athena_scheme_handle_id handle;
  explicit conversion_continuation (object next):
    handle (scheme_command_handle_acquire (object_to_tmscm (next))) {}
  ~conversion_continuation () { scheme_command_handle_release (handle); }
};

std::string encode (url name) {
  string text= as_string (name);
  return std::string (text.data (), N(text));
}

athena_actor_id current_actor () {
  const auto* context= current_scheme_execution_context ();
  if (context && context->actor_id != ATHENA_NO_ACTOR) return context->actor_id;
  return published_buffer_actor_id (encode (get_current_buffer_safe ()));
}

void on_gui (std::function<void()> action) {
  const auto* context= current_scheme_execution_context ();
  if (context && context->editor && context->editor->ui_endpoint &&
      context->editor->ui_endpoint->viewport ().attached) {
    context->editor->ui_endpoint->publish_continuation (std::move (action));
    return;
  }
  QMetaObject::invokeMethod (QCoreApplication::instance (), std::move (action), Qt::QueuedConnection);
}

void on_document_gui (std::function<void()> action) {
  if (current_scheme_execution_context () == nullptr) action ();
  else on_gui (std::move (action));
}

url actor_buffer (athena_actor_id actor) {
  if (actor == ATHENA_NO_ACTOR) return url_none ();
  array<url> buffers= get_all_buffers ();
  for (int i=0; i<N(buffers); ++i)
    if (published_buffer_actor_id (encode (buffers[i])) == actor) return buffers[i];
  return url_none ();
}

bool window_exists (url window) {
  array<url> windows= windows_list ();
  for (int i=0; i<N(windows); ++i) if (windows[i] == window) return true;
  return false;
}

struct load_options {
  bool background= false, new_window= false, strict= false;
  explicit load_options (object options) {
    for (; !is_null (options); options= cdr (options)) {
      object option= car (options);
      if (option == keyword_object ("background")) background= true;
      if (option == keyword_object ("new-window")) new_window= true;
      if (option == keyword_object ("strict")) strict= true;
    }
  }
};

void schedule_loaded_cloud_sync (athena_actor_id actor, athena_view_id view) {
  QTimer::singleShot (1000, QCoreApplication::instance (), [actor, view] {
    auto continuation= actor_continuation_registry::instance ().store ([actor, view] {
      const auto* context= current_scheme_execution_context ();
      if (context->editor && context->editor->idle_time () < 1000) {
        on_gui ([actor, view] { schedule_loaded_cloud_sync (actor, view); });
        return;
      }
      google_cloud_todo_sync_buffer (context->actor->current_buffer_url (), false);
    });
    if (!buffer_actor::submit_to (actor, actor_command_kind::run_native_continuation, view,
          ATHENA_NO_BLOB, ATHENA_NO_BLOB, SCHEME_CAPABILITY_BUFFER | SCHEME_CAPABILITY_GLOBAL,
          continuation)) actor_continuation_registry::instance ().discard (continuation);
  });
}

void open_loaded_document (url name, load_options options) {
  if (!options.background) {
    if (options.new_window) new_buffer_in_new_window (name, get_buffer_tree (name), "");
    else switch_to_buffer (name);
  }
  view_schedule_fit_width ();
  buffer_notify_recent (name);
  (void) get_recent_view (name);
  const auto source= published_buffer_source (encode (name));
  schedule_loaded_cloud_sync (source.first, source.second);
  if (is_rooted_tmfs (name)) {
    object master= call ("tmfs-master", object (name));
    if (master != object (false)) {
      const url target= is_string (master) ? url (as_string (master)) : as_url (master);
      if (target != name) set_master_buffer (name, target);
    }
  }
}

void load_document_permissions (url name, load_options options,
                                std::function<void(url)> completed= {}) {
  tree filename= compound ("verbatim", as_string (name, URL_SYSTEM));
  const bool file= is_of_type (name, "f");
  if (!file && !is_of_type (name, "c")) {
    get_server ()->set_message (compound ("concat", "The file ", filename,
      " cannot be loaded or created"), "Load file");
    return;
  }
  if (file && !is_of_type (name, "r")) {
    get_server ()->set_message (compound ("concat", "You do not have read access to ", filename), "Load file");
    return;
  }
  if (!is_nil (concrete_buffer (name))) {
    open_loaded_document (name, options);
    if (completed) completed (name);
    return;
  }
  const bool present= exists (name);
  if (present ? buffer_load (name) : buffer_create_source (name)) {
    get_server ()->set_message (compound ("concat", present ? "Could not load " : "Could not create ", filename), "Load file");
    return;
  }
  open_loaded_document (name, options);
  if (!present) {
    const auto source= published_buffer_source (encode (name));
    auto continuation= actor_continuation_registry::instance ().store ([] { document_set_default_style (); });
    if (!buffer_actor::submit_to (source.first, actor_command_kind::run_native_continuation,
          source.second, ATHENA_NO_BLOB, ATHENA_NO_BLOB,
          SCHEME_CAPABILITY_BUFFER | SCHEME_CAPABILITY_GLOBAL, continuation))
      actor_continuation_registry::instance ().discard (continuation);
    get_server ()->set_message (compound ("concat", "Could not load ", filename,
      ". Created new document"), "Load file");
  }
  if (completed) completed (name);
}

url recovery_source (url name) {
  if (is_rooted_web (name)) return url_none ();
  if (is_rooted_tmfs (name) && call ("tmfs-autosave", object (name), object ("~")) == object (false))
    return url_none ();
  url autosave= glue (name, "~");
  if (exists (name) && exists (autosave) && as_bool (call ("url-newer?", object (autosave), object (name))))
    return autosave;
  return url_none ();
}

void load_document_gui (url name, load_options options,
                         std::function<void(url)> completed= {}) {
  const url search= url ("$ATHENA_FILE_PATH") * name;
  if (!exists (name) && exists (search)) name= resolve (search, "f");
  if (!is_rooted (name)) {
    url current= get_current_buffer_safe ();
    name= is_none (current) ? url_pwd () * name : relative (current, name);
  }
  if (locase_all (suffix (name)) == "avd") { compound_document_open (name); return; }
  const url proposal= options.strict ? url_none () : recovery_source (name);
  if (is_none (proposal)) { load_document_permissions (name, options, completed); return; }
  if (headless_mode) {
    get_server ()->set_message ("Interactive confirmation required", "Load more recent autosave file?");
    return;
  }
  const std::string source= encode (name);
  auto* dialog= new QMessageBox (QMessageBox::Question, QObject::tr ("Load file"),
    QObject::tr ("Load more recent autosave file?"), QMessageBox::Yes | QMessageBox::No,
    QApplication::activeWindow ());
  dialog->setAttribute (Qt::WA_DeleteOnClose);
  dialog->setDefaultButton (QMessageBox::Yes);
  QObject::connect (dialog, &QDialog::finished, dialog, [source, options, completed] (int answer) {
    const url name (source.c_str ());
    if (answer != QMessageBox::Yes) { load_document_permissions (name, options, completed); return; }
    const url autosave= recovery_source (name);
    if (is_none (autosave) || buffer_import (name, autosave, file_format (name)))
      get_server ()->set_message ("Could not load autosave file", "Load file");
    else {
      open_loaded_document (name, options);
      pretend_buffer_modified (name);
      if (completed) completed (name);
    }
  });
  dialog->open ();
}

void browse_document_gui (url name, std::function<void(url)> completed) {
  if (is_rooted (name, "mailto")) { application_open_external (name); return; }
  if (locase_all (suffix (name)) == "avd") { compound_document_open (name); return; }
  if (!is_nil (concrete_buffer (name))) {
    switch_to_buffer (name);
    if (completed) completed (name);
    return;
  }
  const string format= conversion_format_from_suffix (locase_all (suffix (name)));
  if (is_rooted_web (name) || is_rooted_tmfs (name) || file_format (name) == "texmacs") {
    load_document_gui (name, load_options (null_object ()), completed);
    return;
  }
  if (conversion_format_image (format)) { application_open_external (name); return; }
  if (!is_of_type (name, "f")) {
    load_document_gui (name, load_options (null_object ()), completed);
    return;
  }
  if (headless_mode) {
    get_server ()->set_message ("Interactive confirmation required", "Open file");
    return;
  }
  array<string> choices;
  if (format != "generic" && conversion_graph_search (format * "-document", "texmacs-tree") != tree ("#f"))
    choices << string ("Convert to ATHENA document") << string ("convert");
  choices << string ("Edit as plain text") << string ("plain")
          << string ("Open with system application") << string ("system");
  const string answer= qtm_linked_file_choice_dialog (as_string (name, URL_SYSTEM), choices);
  if (answer == "system") application_open_external (name);
  else if (answer == "convert") {
    const url target ("tmfs://import/" * format * "/" * as_string (call ("url->tmfs-string", object (name))));
    load_document_gui (target, load_options (null_object ()), completed);
  }
  else if (answer == "plain") {
    if (buffer_import (name, name, "verbatim"))
      get_server ()->set_message (compound ("concat", "Could not open ",
        compound ("verbatim", as_string (name, URL_SYSTEM)), " as plain text"), "Open file");
    else {
      open_loaded_document (name, load_options (null_object ()));
      if (completed) completed (name);
    }
  }
}

void revert_document (const std::string& source) {
  const url name (source.c_str ());
  if (is_nil (concrete_buffer (name))) {
    load_document_gui (name, load_options (null_object ()));
    return;
  }
  if (name != get_current_buffer_safe ()) switch_to_buffer (name);
  web_cache_invalidate (name);
  tree document= import_tree (name, file_format (name));
  if (document == tree ("error"))
    get_server ()->set_message ("Error: file not found", "Revert buffer");
  else set_buffer_tree (name, document);
}

void request_revert (object arguments, bool confirm) {
  const url name= is_null (arguments) ? get_current_buffer () :
    (is_string (car (arguments)) ? url_system (as_string (car (arguments))) : as_url (car (arguments)));
  const std::string source= encode (name);
  on_gui ([source, confirm] {
    const url name (source.c_str ());
    if (!confirm || is_nil (concrete_buffer (name)) || !buffer_modified (name)) {
      revert_document (source);
      return;
    }
    if (headless_mode) {
      get_server ()->set_message ("Interactive confirmation required", "Revert buffer");
      return;
    }
    auto* dialog= new QMessageBox (QMessageBox::Question, QObject::tr ("Revert buffer"),
      QObject::tr ("Buffer has been modified. Really revert?"),
      QMessageBox::Yes | QMessageBox::No, QApplication::activeWindow ());
    dialog->setAttribute (Qt::WA_DeleteOnClose);
    dialog->setDefaultButton (QMessageBox::No);
    QObject::connect (dialog, &QDialog::finished, dialog, [source] (int answer) {
      if (answer == QMessageBox::Yes) revert_document (source);
    });
    dialog->open ();
  });
}

bool needs_confirmation (url name) {
  return !is_none (name) && athena_buffer_user_modified (name) &&
         !(is_scratch (name) && is_empty (get_buffer_body (name)));
}

bool close_embedded (url name) {
  if (!is_embedded_buffer (name)) return false;
  path windows= window_search (name);
  for (; !is_nil (windows); windows= windows->next) window_delete (windows->item);
  return true;
}

void after_save (athena_actor_id actor, std::function<void()> next) {
  url name= actor_buffer (actor);
  if (is_none (name)) return;
  if (!athena_buffer_user_modified (name)) { next (); return; }
  switch_to_buffer (name);
  document_save_before_close (name, [next] { on_gui (next); });
}

void ask_close (athena_actor_id actor, std::function<void()> close) {
  url name= actor_buffer (actor);
  if (is_none (name)) return;
  if (!needs_confirmation (name)) { close (); return; }
  if (headless_mode) {
    get_server ()->set_message ("The document has unsaved changes", "Close document");
    return;
  }
  auto* dialog= new QMessageBox (QMessageBox::Question, QObject::tr ("Close document"),
    QObject::tr ("The document has unsaved changes. What would you like to do?"),
    QMessageBox::NoButton, QApplication::activeWindow ());
  dialog->setAttribute (Qt::WA_DeleteOnClose);
  auto* save= dialog->addButton (QObject::tr ("Save and Close"), QMessageBox::AcceptRole);
  auto* discard= dialog->addButton (QObject::tr ("Close Without Saving"), QMessageBox::DestructiveRole);
  auto* cancel= dialog->addButton (QMessageBox::Cancel);
  dialog->setDefaultButton (cancel);
  QObject::connect (dialog, &QDialog::finished, dialog, [dialog, save, discard, actor, close] (int) {
    if (dialog->clickedButton () == save) after_save (actor, close);
    else if (dialog->clickedButton () == discard) close ();
  });
  dialog->open ();
}

void close_window (std::string window, athena_actor_id actor) {
  url name (window.c_str ());
  if (!window_exists (name)) return;
  // A tab may have been retargeted while the save dialog was open.
  if (published_buffer_actor_id (encode (window_to_buffer (name))) != actor) return;
  if (!athena_flush_realtime_buffer (window_to_buffer (name))) return;
  kill_window (name);
  QTimer::singleShot (100, QCoreApplication::instance (), [actor] {
    url buffer= actor_buffer (actor);
    if (!is_none (buffer) && N(buffer_to_windows (buffer)) == 0)
      kill_buffer_by_actor_id (actor);
  });
}

struct quit_request {
  bool restart;
  std::vector<athena_actor_id> selected;
  size_t next= 0;
};
std::weak_ptr<quit_request> pending_quit;

void finish_application (bool restart) {
  if (restart) {
    if (!get_server ()->restart ())
      qtm_info_dialog ("Could not restart ATHENA", "Restart ATHENA");
  }
  else get_server ()->quit ();
}

void save_for_quit (std::shared_ptr<quit_request> request) {
  while (request->next < request->selected.size ()) {
    auto actor= request->selected[request->next++];
    url name= actor_buffer (actor);
    if (is_none (name) || !athena_buffer_user_modified (name)) continue;
    after_save (actor, [request] { save_for_quit (request); });
    return;
  }
  finish_application (request->restart);
}

void request_quit (bool restart) {
  if (!pending_quit.expired ()) return;
  auto request= std::make_shared<quit_request> ();
  request->restart= restart;
  pending_quit= request;
  array<string> candidates;
  array<url> buffers= get_all_buffers ();
  for (int i=0; i<N(buffers); ++i)
    if (!is_aux_buffer (buffers[i]) && !is_rooted_tmfs (buffers[i]) &&
        needs_confirmation (buffers[i])) candidates << as_string (buffers[i]);
  if (N(candidates) == 0) { finish_application (restart); return; }
  if (headless_mode) {
    get_server ()->set_message ("Documents have unsaved changes", "Exit ATHENA");
    return;
  }
  switch_to_buffer (url (candidates[0]));
  qtm_unsaved_buffers_async (candidates, restart, [request] (std::vector<std::string> result) {
    if (result[0] == "discard") finish_application (request->restart);
    else if (result[0] == "save") {
      for (size_t i=1; i<result.size (); ++i)
        request->selected.push_back (published_buffer_actor_id (result[i]));
      save_for_quit (request);
    }
  });
}
}

void application_close_buffer () {
  auto actor= current_actor ();
  on_gui ([actor] {
    url name= actor_buffer (actor);
    if (is_none (name) || close_embedded (name)) return;
    ask_close (actor, [actor] { kill_buffer_by_actor_id (actor); });
  });
}

void application_close_window (object arguments) {
  const bool explicit_window= !is_null (arguments);
  url window= explicit_window? (is_url (car (arguments))? as_url (car (arguments)):
                               url (as_string (car (arguments)))): get_current_window ();
  std::string encoded= encode (window);
  auto actor= current_actor ();
  on_gui ([encoded, actor, explicit_window] {
    if (!explicit_window) {
      url buffer= actor_buffer (actor);
      if (!is_none (buffer) && close_embedded (buffer)) return;
    }
    url window (encoded.c_str ());
    if (!window_exists (window)) return;
    auto target= published_buffer_actor_id (encode (window_to_buffer (window)));
    if (target == ATHENA_NO_ACTOR) { kill_window (window); return; }
    ask_close (target, [encoded, target] { close_window (encoded, target); });
  });
}

void application_quit () { on_gui ([] { request_quit (false); }); }

object application_url_modified (url source) {
  return is_rooted_tmfs (source) ? call ("tmfs-date", object (source)) : object (last_modified (source));
}

bool application_url_newer (url left, url right) {
  if (!is_rooted_tmfs (left) && !is_rooted_tmfs (right)) return is_newer (left, right);
  object a= application_url_modified (left), b= application_url_modified (right);
  return a != object (false) && b != object (false) && as_double (a) > as_double (b);
}

void application_url_remove (url source) {
  if (is_rooted_tmfs (source)) call ("tmfs-remove", object (source));
  else remove (source);
}

object application_url_autosave (url source, string suffix) {
  if (is_rooted_tmfs (source)) return call ("tmfs-autosave", object (source), object (suffix));
  return is_scratch (source) || is_of_type (source, "fw") ||
         (!exists (source) && is_of_type (source, "c")) ? object (glue (source, suffix)) : object (false);
}

object application_url_wrap (url source) {
  return is_rooted_tmfs (source) ? call ("tmfs-wrap", object (source)) : object (false);
}

object application_buffer_last_save (url source) {
  object wrapped= application_url_wrap (source);
  if (wrapped == object (false)) return object (get_last_save_buffer (source));
  const url base= is_string (wrapped) ? url (as_string (wrapped)) : as_url (wrapped);
  return published_buffer_actor_id (encode (base)) != ATHENA_NO_ACTOR ?
    application_buffer_last_save (base) : application_url_modified (base);
}

string application_propose_filename () {
  const url current= get_current_buffer ();
  if (!is_scratch (current)) return as_string (current, URL_UNIX);
#ifdef OS_WIN32
  return "";
#else
  return as_string (url_pwd (), URL_UNIX) * "/";
#endif
}

array<url> application_sorted_buffers () {
  std::vector<std::pair<double,url>> candidates;
  const array<url> buffers= get_all_buffers ();
  for (int i=0; i<N(buffers); ++i) {
    const url name= buffers[i];
    const string text= as_string (name, URL_UNIX);
    if (!is_rooted_tmfs (name) || starts (text, "tmfs://part/") || starts (text, "tmfs://help/"))
      candidates.emplace_back (last_visited (name), name);
  }
  std::stable_sort (candidates.begin (), candidates.end (),
    [] (const auto& a, const auto& b) { return a.first > b.first; });
  array<url> result;
  for (const auto& entry: candidates) result << entry.second;
  return result;
}

void application_open_auxiliary (string name, tree body, object master) {
  const url base= is_null (master) ? get_master_buffer (get_current_buffer ()) :
    (is_string (car (master)) ? url (as_string (car (master))) : as_url (car (master)));
  athena_open_auxiliary_document (name, body, base);
}

namespace {
void open_document_dialog (bool window) {
  const string proposed= application_propose_filename ();
  const std::string initial (proposed.data (), N(proposed));
  on_gui ([initial, window] {
    const QString selected= QFileDialog::getOpenFileName (QApplication::activeWindow (),
      QObject::tr ("Load file"), QString::fromUtf8 (initial.c_str ()));
    if (selected.isEmpty ()) return;
    const url source= url_system (string (selected.toUtf8 ().constData ()));
    if (window) application_load_document_window (source, null_object ());
    else application_load_document (source, null_object ());
  });
}
}
void application_open_document_dialog () { open_document_dialog (false); }
void application_open_window_dialog () { open_document_dialog (true); }
void application_open_document_url (url source) {
  if (!is_none (source)) application_load_document (source, null_object ());
}
void application_open_window_url (url source) {
  if (!is_none (source)) application_load_document_window (source, null_object ());
}
void application_revert_buffer (object arguments) { request_revert (arguments, true); }
void application_revert_buffer_now (object arguments) { request_revert (arguments, false); }

void application_load_document (url source, object options) {
  application_load_document (source, options, {});
}

void application_load_document (url source, object options, std::function<void(url)> completed) {
  const std::string name= encode (source);
  const load_options settings (options);
  on_document_gui ([name, settings, completed] {
    load_document_gui (url (name.c_str ()), settings, completed);
  });
}

void application_load_document_window (url source, object options) {
  const std::string name= encode (source);
  load_options settings (options);
  settings.new_window= true;
  on_document_gui ([name, settings] {
    url target (name.c_str ());
    if (N(buffer_to_windows (target)) != 0) switch_to_buffer (target);
    else load_document_gui (target, settings);
  });
}

void application_open_loaded_document (url source, object options) {
  const std::string name= encode (source);
  const load_options settings (options);
  on_document_gui ([name, settings] { open_loaded_document (url (name.c_str ()), settings); });
}

void application_browse_document (url source, object after_open) {
  std::function<void(url)> completed;
  if (!is_null (after_open) && car (after_open) != object (false)) {
    auto retained= std::make_shared<conversion_continuation> (car (after_open));
    completed= [retained] (url target) {
      exec_buffer (target, tmscm_to_object (scheme_command_handle_value (retained->handle)));
    };
  }
  application_browse_document (source, std::move (completed));
}

void application_browse_document (url source, std::function<void(url)> completed) {
  const url current= get_current_buffer_safe ();
  if (!is_rooted (source) && !is_none (current)) source= relative (current, source);
  const std::string name= encode (source);
  on_document_gui ([name, completed] { browse_document_gui (url (name.c_str ()), completed); });
}

void application_import_document (url source, string format, object options) {
  const std::string encoded= encode (source), selected (format.data (), N(format));
  auto retained= std::make_shared<conversion_continuation> (options);
  on_gui ([encoded, selected, retained] {
    url source (encoded.c_str ());
    const url search= url ("$ATHENA_FILE_PATH") * source;
    if (!exists (source) && exists (search)) source= resolve (search, "f");
    tree filename= compound ("verbatim", as_string (source, URL_SYSTEM));
    if (!is_of_type (source, "f")) {
      get_server ()->set_message (compound ("concat", "The file ", filename, " does not exist"), "Import file");
      return;
    }
    if (!is_of_type (source, "r")) {
      get_server ()->set_message (compound ("concat", "You do not have read access to ", filename), "Import file");
      return;
    }
    const string format (selected.c_str ());
    url target= source;
    if (format != file_format (source))
      target= url ("tmfs://import/" * format * "/" * as_string (call ("url->tmfs-string", object (source))));
    load_options settings (tmscm_to_object (scheme_command_handle_value (retained->handle)));
    const bool latex= format == "latex" || format == "tex" || format == "latex-document";
    if (latex) system_wait ("Importing LaTeX", "please wait");
    try { load_document_gui (target, settings); }
    catch (...) {
      if (latex) system_wait ("", "");
      throw;
    }
    if (latex) system_wait ("", "");
  });
}

void application_command_line_convert (string from, string to, object next) {
  const std::string source (from.data (), N(from)), target (to.data (), N(to));
  auto continuation= std::make_shared<conversion_continuation> (next);
  on_gui ([source, target, continuation] {
    const url name (source.c_str ()), destination (target.c_str ());
    if (buffer_load (name)) {
      cerr << "ATHENA: could not load " << source.c_str () << "\n";
      std::exit (1);
    }
    const string format= file_format (destination);
    if (buffer_export (name, destination, format == "generic" ? string ("verbatim") : format)) {
      cerr << "ATHENA: could not export " << source.c_str () << " to " << target.c_str () << "\n";
      std::exit (1);
    }
    object next= tmscm_to_object (scheme_command_handle_value (continuation->handle));
    if (!exec_buffer (name, next)) call (next);
  });
}
void application_restart () { on_gui ([] { request_quit (true); }); }
void application_new_window () { open_document_window (false); }

void application_open_external (url target) {
  if (!is_rooted (target)) {
    const url source= get_current_buffer_safe ();
    target= is_none (source) ? url_pwd () * target : relative (source, target);
  }
  if (is_rooted (target, "doi")) target= url ("https://doi.org") * unroot (target);
  const bool local= is_rooted (target, "default") || is_rooted (target, "file");
  const string text= local ? concretize (target) : as_string (target, URL_SYSTEM);
  const std::string encoded (text.data (), N(text));
  on_gui ([encoded, local] {
    const QString text= QString::fromUtf8 (encoded.data (), encoded.size ());
    const QUrl target= local ? QUrl::fromLocalFile (text) : QUrl (text);
    if (!QDesktopServices::openUrl (target))
      get_server ()->set_message (compound ("concat", "Could not open externally: ",
        compound ("verbatim", string (encoded.c_str ()))), "Open file");
  });
}
