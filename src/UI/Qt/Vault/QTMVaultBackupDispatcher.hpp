/******************************************************************************
* MODULE     : QTMVaultBackupDispatcher.hpp
* DESCRIPTION: Asynchronous vault backup dispatch scheduling
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
******************************************************************************/

#ifndef QTMVAULTBACKUPDISPATCHER_HPP
#define QTMVAULTBACKUPDISPATCHER_HPP

class QEvent;
class QString;

void qtm_vault_backup_dispatcher_initialize ();
void qtm_vault_backup_dispatcher_note_activity (const QEvent* event);
void qtm_vault_backup_dispatch_realtime (const QString& saved_file);

#endif // QTMVAULTBACKUPDISPATCHER_HPP
