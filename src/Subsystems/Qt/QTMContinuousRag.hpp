/******************************************************************************
* MODULE     : QTMContinuousRag.hpp
* DESCRIPTION: Realtime Continuous RAG coordinator on the Qt owner thread
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef QTMCONTINUOUSRAG_HPP
#define QTMCONTINUOUSRAG_HPP

#include <QString>
#include <cstdint>

void qtm_continuous_rag_start ();
void qtm_continuous_rag_saved (const QString& saved_file,
                               const QString& vault_root,
                               const QString& storage_revision,
                               std::uint64_t save_sequence);
void qtm_continuous_rag_shutdown ();

#endif // QTMCONTINUOUSRAG_HPP
