/******************************************************************************
* MODULE     : QTMDocumentPersistence.hpp
* DESCRIPTION: Qt scheduling/chrome for native document persistence
* COPYRIGHT  : (C) 2026 ATHENA contributors
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
******************************************************************************/

#ifndef QTMDOCUMENTPERSISTENCE_HPP
#define QTMDOCUMENTPERSISTENCE_HPP

#include <functional>

class tm_buffer_rep;
using tm_buffer= tm_buffer_rep*;

void qtm_document_persistence_initialize ();
void qtm_document_persistence_preferences_changed ();
// GUI entry/callback; actor waits run off the UI thread. Manual-save documents
// are not written by a scene lifecycle event.
void qtm_document_persistence_flush_realtime_async (
  std::function<void(bool)> completed);

// UI-thread projection of actor-owned realtime state.
void qtm_document_persistence_realtime_state (
  tm_buffer buffer, bool paused, bool completed, bool success);

// Convert the raw editor dirty bit into the user-visible modified decoration.
bool qtm_document_persistence_show_modified (tm_buffer buffer, bool raw_modified);

#endif // QTMDOCUMENTPERSISTENCE_HPP
