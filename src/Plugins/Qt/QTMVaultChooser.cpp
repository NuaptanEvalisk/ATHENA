/******************************************************************************
* MODULE     : QTMVaultChooser.cpp
* DESCRIPTION: Qt vault link chooser facade
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "QTMVaultChooser.hpp"
#include "QTMVaultLinkFocus.hpp"
#include "QTMVaultTransclusionWizard.hpp"
#include "QTMVaultWikilinkWizard.hpp"
#include "vault.hpp"
#include <QApplication>

tree
vault_choose_link (bool transcludeMode) {
  if (!vault_active ()) return UNINIT;
  TeXmacsFocusSnapshot focusSnapshot= capture_texmacs_focus_snapshot ();
  tree result= transcludeMode ?
    qtm_vault_choose_transclusion (QApplication::activeWindow ()) :
    qtm_vault_choose_wikilink (QApplication::activeWindow ());
  restore_texmacs_focus_snapshot (focusSnapshot, true);
  restore_texmacs_focus_snapshot_later (focusSnapshot);
  return result;
}
