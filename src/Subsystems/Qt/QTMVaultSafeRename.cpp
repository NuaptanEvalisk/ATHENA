/******************************************************************************
* MODULE     : QTMVaultSafeRename.cpp
* DESCRIPTION: Qt confirmation frontend for safe Vault renaming
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************/

#include "QTMVaultSafeRename.hpp"

#include "ATHENA/Data/vault_safe_rename.hpp"

#include <QApplication>
#include <QMessageBox>
#include <QStringList>

#include <filesystem>

bool
qtm_safe_rename_vault_item (QWidget* parent, const QString& source,
                            const QString& target) {
  QApplication::setOverrideCursor (Qt::WaitCursor);
  VaultSafeRenamePlan plan;
  std::string error;
  bool planned= vault_safe_rename_plan (
    std::filesystem::path (source.toStdString ()),
    std::filesystem::path (target.toStdString ()), plan, error);
  QApplication::restoreOverrideCursor ();
  if (!planned) {
    QMessageBox::warning (parent, "Safe rename",
                          QString::fromStdString (error));
    return false;
  }

  QStringList summary;
  summary << "Source: " + source
          << "Target: " + target
          << "Operation: " + QString (plan.is_directory ?
                                       "Rename directory" : "Rename file")
          << QString ("Filesystem entries affected: %1")
               .arg ((qulonglong) plan.filesystem_entries)
          << QString ("map.sqlite rows to update: %1")
               .arg ((qulonglong) plan.map_rows)
          << QString ("Candidate documents found by fast scan: %1")
               .arg ((qulonglong) plan.candidate_documents)
          << QString ("Documents requiring structural rewrite: %1")
               .arg ((qulonglong) plan.rewritten_documents)
          << QString ("Path references to rewrite: %1")
               .arg ((qulonglong) plan.rewritten_references)
          << QString ("Open buffers following the rename: %1")
               .arg ((qulonglong) plan.affected_open_buffers);
  if (!plan.modified_buffers.empty ()) {
    summary << "" << "The following affected buffers are modified:";
    for (const std::string& path: plan.modified_buffers)
      summary << "  " + QString::fromStdString (path);
    summary << "Save them before retrying the rename.";
    QMessageBox::warning (parent, "Safe rename", summary.join ('\n'));
    return false;
  }
  summary << "" << "Wikilink and transclusion hints will not be changed."
          << "RAG and recent-file data will not be changed."
          << "" << "Proceed with these operations?";
  QMessageBox box (QMessageBox::Question, "Safe rename",
                   summary.join ('\n'), QMessageBox::Yes | QMessageBox::No,
                   parent);
  box.setDefaultButton (QMessageBox::No);
  if (box.exec () != QMessageBox::Yes) return false;

  QApplication::setOverrideCursor (Qt::WaitCursor);
  bool executed= vault_safe_rename_execute (plan, error);
  QApplication::restoreOverrideCursor ();
  if (!executed) {
    QMessageBox::warning (parent, "Safe rename",
                          QString::fromStdString (error));
    return false;
  }
  return true;
}
