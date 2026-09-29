/******************************************************************************
* MODULE     : QTMDocumentPersistence.cpp
* DESCRIPTION: Qt scheduling/chrome for native document persistence
* COPYRIGHT  : (C) 2026 ATHENA contributors
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
******************************************************************************/

#include "QTMDocumentPersistence.hpp"

#include "ATHENA/Data/document_persistence.hpp"
#include "ATHENA/Data/new_buffer.hpp"
#include "ATHENA/actor_transport.hpp"
#include "ATHENA/buffer_actor.hpp"
#include "ATHENA/tm_buffer.hpp"
#include "ATHENA/tm_window.hpp"
#include "file.hpp"
#include "vault.hpp"

#include <QApplication>
#include <QObject>
#include <QTimer>

namespace {

athena_view_id
first_view (tm_buffer buffer) {
  if (buffer == nullptr || N(buffer->vws) == 0) return ATHENA_NO_VIEW;
  return buffer->vws[0]->runtime_id;
}

athena_blob_id
vault_payload () {
  if (!vault_active ()) return ATHENA_NO_BLOB;
  string root= as_string (concretize (vault_get_root ()), URL_SYSTEM);
  return N(root) == 0 ? ATHENA_NO_BLOB : actor_text_from_string (std::move (root));
}

void
set_buffer_chrome (tm_buffer buffer) {
  if (buffer == nullptr) return;
  const bool active= athena_current_document_save_mode () ==
                       athena_document_save_mode::realtime &&
                     athena_realtime_save_eligible (buffer->buf->name) &&
                     !buffer->buf->realtime_save_paused;
  const bool show_pause=
    athena_current_document_save_mode () == athena_document_save_mode::realtime &&
    buffer->buf->realtime_save_paused;
  array<url> windows= buffer_to_windows (buffer->buf->name);
  for (int i=0; i<N(windows); ++i) {
    tm_window window= concrete_window (windows[i]);
    if (window == nullptr) continue;
    window->set_realtime_save_paused (show_pause);
    window->set_modified (buffer->buf->menu_modified && !active);
  }
}

void
remove_stale_autosave (url name) {
  url autosave (as_string (name) * "~");
  if (exists (autosave)) remove (autosave);
}

class DocumentPersistenceManager: public QObject {
public:
  explicit DocumentPersistenceManager (QObject* parent): QObject (parent) {
    timer.setSingleShot (true);
    connect (&timer, &QTimer::timeout, this, [this] () {
      tick ();
      arm ();
    });
    arm ();
  }

  void preferencesChanged () {
    refreshAllChrome ();
    timer.stop ();
    arm ();
  }

  void stateChanged (tm_buffer buffer, bool paused,
                     bool completed, bool success) {
    if (buffer == nullptr) return;
    publish_buffer_realtime_save_paused (buffer, paused);
    if (completed) {
      buffer->buf->realtime_save_queued= false;
      if (success) remove_stale_autosave (buffer->buf->name);
    }
    set_buffer_chrome (buffer);
    if (completed && !success)
      std_warning << "Realtime save failed and was paused for "
                  << buffer->buf->name << LF;
  }

  void refreshAllChrome () {
    array<url> names= get_all_buffers ();
    for (int i=0; i<N(names); ++i)
      set_buffer_chrome (concrete_buffer (names[i]));
  }

private:
  void arm () {
    int interval= athena_current_document_save_mode () ==
                    athena_document_save_mode::realtime
                    ? athena_realtime_save_interval_ms ()
                    : 1000;
    timer.start (interval);
  }

  void tick () {
    if (athena_current_document_save_mode () !=
        athena_document_save_mode::realtime)
      return;

    array<url> names= get_all_buffers ();
    for (int i=0; i<N(names); ++i) {
      tm_buffer buffer= concrete_buffer (names[i]);
      if (buffer == nullptr || buffer->actor == nullptr || N(buffer->vws) == 0)
        continue;
      if (!buffer->buf->menu_modified || buffer->buf->realtime_save_queued ||
          !athena_realtime_save_active (names[i]))
        continue;

      // menu_modified is the UI owner's delayed dirty signal x. At most one
      // realtime save command may be outstanding per buffer; edits that happen
      // after that save will publish x=1 again after ui_mark_buffer_saved.
      buffer->buf->realtime_save_queued= true;
      athena_blob_id payload= vault_payload ();
      actor_command_ticket ticket= buffer->actor->try_submit (
        actor_command_kind::realtime_save_buffer, first_view (buffer),
        payload, ATHENA_NO_BLOB, SCHEME_CAPABILITY_BUFFER);
      if (!ticket) {
        buffer->buf->realtime_save_queued= false;
        if (payload != ATHENA_NO_BLOB)
          (void) actor_text_registry::instance ().discard (payload);
      }
    }
  }

  QTimer timer;
};

DocumentPersistenceManager*&
manager_storage () {
  static DocumentPersistenceManager* instance= nullptr;
  return instance;
}

DocumentPersistenceManager*
manager (bool create) {
  DocumentPersistenceManager*& instance= manager_storage ();
  if (create && instance == nullptr && qApp != nullptr) {
    instance= new DocumentPersistenceManager (qApp);
  }
  return instance;
}

} // namespace

void
qtm_document_persistence_initialize () {
  (void) manager (true);
}

void
qtm_document_persistence_preferences_changed () {
  DocumentPersistenceManager* instance= manager (true);
  if (instance != nullptr) instance->preferencesChanged ();
}

void
qtm_document_persistence_realtime_state (
    tm_buffer buffer, bool paused, bool completed, bool success) {
  DocumentPersistenceManager* instance= manager (true);
  if (instance != nullptr)
    instance->stateChanged (buffer, paused, completed, success);
}

bool
qtm_document_persistence_show_modified (tm_buffer buffer, bool raw_modified) {
  return raw_modified &&
    (buffer == nullptr || !athena_realtime_save_active (buffer->buf->name));
}
