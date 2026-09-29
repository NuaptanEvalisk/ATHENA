/******************************************************************************
* MODULE     : document_persistence.cpp
* DESCRIPTION: Native document persistence policy and realtime-save contract
* COPYRIGHT  : (C) 2026 ATHENA contributors
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
******************************************************************************/

#include "document_persistence.hpp"

#include "buffer_actor.hpp"
#include "buffer_name_catalog.hpp"
#include "buffer_state.hpp"
#include "file.hpp"
#include "new_buffer.hpp"
#include "scheme.hpp"
#include "scheme_execution_context.hpp"
#include "tm_buffer.hpp"
#include "tm_window.hpp"
#include "vault.hpp"

#include <algorithm>
#include <cstdlib>
#include <string>

namespace {

std::string
native_text (string value) {
  return std::string (value.data (), static_cast<std::size_t> (N(value)));
}

bool
realtime_suffix (url name) {
  string extension= suffix (name);
  return extension == "ath" || extension == "tm";
}

bool
published_pause_state (url name, bool& paused) {
  auto records= published_buffer_metadata ();
  auto found= records.find (native_text (as_string (name)));
  if (found == records.end ()) return false;
  paused= found->second.realtime_save_paused;
  return true;
}

athena_view_id
realtime_view (tm_buffer buffer) {
  if (buffer == nullptr || N(buffer->vws) == 0) return ATHENA_NO_VIEW;
  return buffer->vws[0]->runtime_id;
}

athena_blob_id
current_vault_payload () {
  if (!vault_active ()) return ATHENA_NO_BLOB;
  string root= as_string (concretize (vault_get_root ()), URL_SYSTEM);
  return N(root) == 0 ? ATHENA_NO_BLOB : actor_text_from_string (std::move (root));
}

bool
invoke_realtime_save (tm_buffer buffer) {
  if (buffer == nullptr || buffer->actor == nullptr) return false;
  athena_blob_id vault_payload= current_vault_payload ();
  actor_command_record result;
  const bool invoked= buffer->actor->invoke (
    actor_command_kind::realtime_save_buffer, realtime_view (buffer),
    vault_payload, ATHENA_NO_BLOB, &result, SCHEME_CAPABILITY_BUFFER);
  if (!invoked && vault_payload != ATHENA_NO_BLOB)
    (void) actor_text_registry::instance ().discard (vault_payload);
  return invoked && result.argument[0] == 0;
}

} // namespace

athena_document_save_mode
athena_current_document_save_mode () {
  string value= get_preference ("document save mode", "realtime");
  if (value == "autosave") return athena_document_save_mode::autosave;
  if (value == "manual") return athena_document_save_mode::manual;
  return athena_document_save_mode::realtime;
}

int
athena_realtime_save_interval_ms () {
  std::string raw= native_text (
    get_preference ("realtime save interval", "3"));
  char* end= nullptr;
  const double seconds= std::strtod (raw.c_str (), &end);
  if (end == raw.c_str () || *end != '\0' || !(seconds > 0.0)) return 3000;
  // 500 ms is intentionally supported; reject pathological configuration
  // values below it so a hand-edited preference cannot create a save storm.
  const double milliseconds= std::max (500.0, seconds * 1000.0);
  return static_cast<int> (std::min (milliseconds, 3600000.0));
}

bool
athena_realtime_save_eligible (url name) {
  if (is_none (name) || is_rooted_web (name) || is_rooted_tmfs (name) ||
      is_scratch (name) || !realtime_suffix (name))
    return false;

  const SchemeExecutionContext* context= current_scheme_execution_context ();
  if (context != nullptr && context->actor != nullptr &&
      context->actor->current_buffer_url () == name) {
    buffer_document_state* state= context->actor->current_state ();
    return state != nullptr && !state->read_only;
  }

  if (context == nullptr) {
    tm_buffer buffer= concrete_buffer (name);
    return buffer != nullptr && !buffer->buf->read_only;
  }

  // Foreign-buffer Scheme readers use the immutable published catalog and the
  // path checks above. A read-only buffer cannot report modified=true, so
  // treating it as eligible here cannot hide an actual unsaved state.
  return published_buffer_actor_id (native_text (as_string (name))) !=
         ATHENA_NO_ACTOR;
}

bool
athena_realtime_save_paused (url name) {
  const SchemeExecutionContext* context= current_scheme_execution_context ();
  if (context != nullptr && context->actor != nullptr &&
      context->actor->current_buffer_url () == name) {
    buffer_document_state* state= context->actor->current_state ();
    return state != nullptr && state->realtime_save_paused;
  }

  if (context == nullptr) {
    tm_buffer buffer= concrete_buffer (name);
    return buffer != nullptr && buffer->buf->realtime_save_paused;
  }

  bool paused= false;
  return published_pause_state (name, paused) && paused;
}

bool
athena_realtime_save_active (url name) {
  return athena_current_document_save_mode () ==
           athena_document_save_mode::realtime &&
         athena_realtime_save_eligible (name) &&
         !athena_realtime_save_paused (name);
}

bool
athena_set_realtime_save_paused (url name, bool paused) {
  const SchemeExecutionContext* context= current_scheme_execution_context ();
  if (context != nullptr && context->actor != nullptr &&
      context->actor->current_buffer_url () == name) {
    return context->actor->invoke (
      actor_command_kind::set_realtime_save_paused, context->view_id,
      ATHENA_NO_BLOB, ATHENA_NO_BLOB, nullptr, SCHEME_CAPABILITY_BUFFER,
      paused ? 1 : 0);
  }

  tm_buffer buffer= concrete_buffer (name);
  if (buffer == nullptr || buffer->actor == nullptr) return false;
  return buffer->actor->invoke (
    actor_command_kind::set_realtime_save_paused, realtime_view (buffer),
    ATHENA_NO_BLOB, ATHENA_NO_BLOB, nullptr, SCHEME_CAPABILITY_BUFFER,
    paused ? 1 : 0);
}

bool
athena_toggle_realtime_save_current_buffer () {
  url name= get_current_buffer_safe ();
  if (!athena_realtime_save_eligible (name) ||
      athena_current_document_save_mode () != athena_document_save_mode::realtime)
    return false;
  return athena_set_realtime_save_paused (
    name, !athena_realtime_save_paused (name));
}

bool
athena_buffer_user_modified (url name) {
  return buffer_modified (name) && !athena_realtime_save_active (name);
}

bool
athena_buffer_user_menu_modified (url name) {
  return buffer_menu_modified (name) && !athena_realtime_save_active (name);
}

bool
athena_current_buffer_realtime_save_eligible () {
  return athena_current_document_save_mode () ==
           athena_document_save_mode::realtime &&
         athena_realtime_save_eligible (get_current_buffer_safe ());
}

bool
athena_current_buffer_realtime_save_paused () {
  url name= get_current_buffer_safe ();
  return athena_current_buffer_realtime_save_eligible () &&
         athena_realtime_save_paused (name);
}

bool
athena_current_buffer_realtime_save_active () {
  return athena_realtime_save_active (get_current_buffer_safe ());
}

bool
athena_flush_realtime_buffer (url name) {
  if (!athena_realtime_save_active (name)) return true;
  tm_buffer buffer= concrete_buffer (name);
  if (buffer == nullptr) return true;
  const bool saved= invoke_realtime_save (buffer);
  if (saved && current_scheme_execution_context () == nullptr) {
    publish_buffer_menu_modified (buffer, false);
    buffer->buf->realtime_save_queued= false;
    buffer->buf->last_save= last_modified (name);
  }
  return saved;
}

bool
athena_flush_all_realtime_buffers () {
  if (current_scheme_execution_context () != nullptr) return false;
  array<url> buffers= get_all_buffers ();
  bool ok= true;
  for (int i=0; i<N(buffers); ++i) {
    tm_buffer buffer= concrete_buffer (buffers[i]);
    if (buffer == nullptr || !athena_realtime_save_active (buffers[i])) continue;
    if (!athena_flush_realtime_buffer (buffers[i])) ok= false;
  }
  return ok;
}
