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
#include "boot.hpp"
#include "scheme_execution_context.hpp"
#include "editor.hpp"
#include "QTMNativeDialogs.hpp"
#include <QApplication>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>
#include <functional>
#include <memory>
#include <vector>

namespace {
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
  if (context && context->editor && context->editor->ui_endpoint) {
    context->editor->ui_endpoint->publish_continuation (std::move (action));
    return;
  }
  QMetaObject::invokeMethod (QCoreApplication::instance (), std::move (action), Qt::QueuedConnection);
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
void application_restart () { on_gui ([] { request_quit (true); }); }
void application_new_window () { open_document_window (false); }
