/******************************************************************************
* MODULE     : QTMVaultInfoModel.hpp
* DESCRIPTION: Qt-side model for ATHENA Vaultfile metadata
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef QTMVAULTINFOMODEL_HPP
#define QTMVAULTINFOMODEL_HPP

#include <QString>
#include <QStringList>
#include <QVector>

struct QTMBackupDispatcher {
  QString destination;
  QString trigger;
};

struct QTMVaultfileInfo {
  QString name;
  QString mapPath;
  QString preferencesPath;
  QString namespaceDbPath;
  QString startupPage;
  QString oneTimeStartupPage;
  QString maintenanceSummaryPath;
  QString ragIndexPath;
  QString websitesPath;
  QString rootNamespace;
  QString materialsDbPath;
  QString materialsDirectory;
  QString artifactTitleFilterPath;
};

bool    qtm_vault_info_available ();
QString qtm_vault_root_path ();
QString qtm_clean_vault_relative_path (const QString& path);
QString qtm_clean_vault_target (const QString& target);
bool    qtm_valid_vault_relative_path (const QString& path);
bool    qtm_valid_optional_vault_relative_path (const QString& path);
bool    qtm_valid_optional_vault_target (const QString& target);
QString qtm_vault_relative_from_selected_path (const QString& selected);
bool    qtm_vaultfile_read (QTMVaultfileInfo& info, QString* error= nullptr);
bool    qtm_vaultfile_write (const QTMVaultfileInfo& info,
                             QString* error= nullptr);
bool    qtm_backup_dispatchers_read (QVector<QTMBackupDispatcher>& dispatchers,
                                     QString* error= nullptr);
bool    qtm_backup_dispatchers_write (
  const QVector<QTMBackupDispatcher>& dispatchers, QString* error= nullptr);
bool    qtm_artifact_title_filter_read (QStringList& entries,
                                        QString* error= nullptr);
bool    qtm_artifact_title_filter_write (const QStringList& entries,
                                         QString* error= nullptr);

#endif
