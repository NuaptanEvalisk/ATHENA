/******************************************************************************
* MODULE     : QTMZoteroImporter.hpp
* DESCRIPTION: Zotero Local API bulk import UI for ATHENA Materials
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef QTMZOTEROIMPORTER_HPP
#define QTMZOTEROIMPORTER_HPP

class MaterialsStore;
class QWidget;

struct QTMZoteroImportResult {
  int added= 0;
  int already_imported= 0;
  int identifier_duplicates= 0;
  int hash_reconciled= 0;
  int hash_conflicts_kept_both= 0;
  int attachments_copied= 0;
  int attachments_already_present= 0;
  int attachments_unavailable= 0;
  int failed_items= 0;
  bool cancelled= false;
};

bool qtm_import_zotero_library (QWidget* parent, MaterialsStore& store,
                                QTMZoteroImportResult& result);

#endif // QTMZOTEROIMPORTER_HPP
