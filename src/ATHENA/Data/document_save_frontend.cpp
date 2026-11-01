// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#include "document_save_frontend.hpp"
#include "new_buffer.hpp"
#include "recent_files.hpp"
#include "native_interfaces.hpp"
#include "generic_editor_commands.hpp"
#include "editor.hpp"
#include "server.hpp"
#include "file.hpp"
#include "convert.hpp"
#include "actor_transport.hpp"
#include "buffer_actor.hpp"
#include "tm_data.hpp"
#include "scheme_execution_context.hpp"
#include "GoogleCloudTodo.hpp"
#include "guile_tm.hpp"
#include "object.hpp"

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
  if (is_rooted_tmfs (saved)) call ("tmfs-remove", object (saved));
  else remove (saved);
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

void document_save_as_now (url destination, url source, object options) {
  const bool scratch= is_scratch (source);
  rename_buffer (source, destination);
  pretend_buffer_modified (destination);
  // Complete cleanup before invoking a callback that may close the buffer.
  if (!document_save_now (destination, null_object ())) return;
  if (scratch && source != destination && exists (source)) remove (source);
  saved_callbacks (options);
}
