/******************************************************************************
* MODULE     : document_persistence.hpp
* DESCRIPTION: Native document persistence policy and realtime-save contract
* COPYRIGHT  : (C) 2026 ATHENA contributors
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
******************************************************************************/

#ifndef DOCUMENT_PERSISTENCE_HPP
#define DOCUMENT_PERSISTENCE_HPP

#include "url.hpp"

enum class athena_document_save_mode {
  realtime,
  autosave,
  manual
};

athena_document_save_mode athena_current_document_save_mode ();
int athena_realtime_save_interval_ms ();

bool athena_realtime_save_eligible (url name);
bool athena_realtime_save_paused (url name);
bool athena_realtime_save_active (url name);
bool athena_set_realtime_save_paused (url name, bool paused);
bool athena_toggle_realtime_save_current_buffer ();

// User-visible modified state differs from actor-internal dirty state while
// Realtime save is active. The latter remains authoritative for persistence.
bool athena_buffer_user_modified (url name);
bool athena_buffer_user_menu_modified (url name);

bool athena_current_buffer_realtime_save_eligible ();
bool athena_current_buffer_realtime_save_paused ();
bool athena_current_buffer_realtime_save_active ();

// Synchronous safety boundary used before a realtime buffer is destroyed or
// the process exits. Returns false when canonical persistence failed.
bool athena_flush_realtime_buffer (url name);
bool athena_flush_all_realtime_buffers ();

#endif // DOCUMENT_PERSISTENCE_HPP
