/******************************************************************************
* MODULE     : QTMDocumentHistory.hpp
* DESCRIPTION: Native document-history scheduling and UI integration
* COPYRIGHT  : (C) 2026 ATHENA contributors
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
******************************************************************************/

#ifndef QTMDOCUMENTHISTORY_HPP
#define QTMDOCUMENTHISTORY_HPP

#include "string.hpp"
#include "url.hpp"

#include <cstdint>

class tm_buffer_rep;
using tm_buffer= tm_buffer_rep*;

void qtm_document_history_initialize ();
bool qtm_document_history_manual_save_requested ();
void qtm_document_history_manual_request (tm_buffer buffer);
void qtm_document_history_snapshot_received (
  tm_buffer buffer, string bytes, std::uint64_t generation,
  std::uint64_t trigger_code, bool success);

void qtm_document_history_show (url document= url_none ());
bool qtm_document_history_open_version (url document, std::int64_t version_id);
bool qtm_document_history_restore_version (url document, std::int64_t version_id);

#endif // QTMDOCUMENTHISTORY_HPP
