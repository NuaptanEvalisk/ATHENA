/******************************************************************************
* MODULE     : QTMVaultInfoModel.cpp
* DESCRIPTION: Qt-side model for ATHENA Vaultfile metadata
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "QTMVaultInfoModel.hpp"

#include "ATHENA/Data/vaultfile_json.hpp"
#include "convert.hpp"
#include "vault.hpp"

#include <QDir>
#include <filesystem>

static QString
to_qstring_vault_info (string s) {
  return QString::fromUtf8 (as_charp (s), N(s));
}

static string
from_qstring_vault_info (const QString& s) {
  QByteArray bytes= s.toUtf8 ();
  return string (bytes.constData ());
}

static std::string
qtm_utf8_std_string (const QString& s) {
  QByteArray bytes= s.toUtf8 ();
  return std::string (bytes.constData (), (size_t) bytes.size ());
}

bool
qtm_vault_info_available () {
  return vault_active ();
}

QString
qtm_vault_root_path () {
  if (!qtm_vault_info_available ()) return QString ();
  return to_qstring_vault_info (concretize (vault_get_root ()));
}

QString
qtm_clean_vault_relative_path (const QString& path) {
  QString p= QDir::fromNativeSeparators (path.trimmed ());
  while (p.startsWith ("./")) p.remove (0, 2);
  p= QDir::cleanPath (p);
  while (p.startsWith ("./")) p.remove (0, 2);
  if (p == ".") return QString ();
  return p;
}

QString
qtm_clean_vault_target (const QString& target) {
  QString t= QDir::fromNativeSeparators (target.trimmed ());
  if (t.startsWith ("tmfs://") || t.startsWith ("file://")) return t;
  return qtm_clean_vault_relative_path (t);
}

bool
qtm_valid_vault_relative_path (const QString& path) {
  if (path.isEmpty ()) return false;
  if (QDir::isAbsolutePath (path)) return false;
  return path != ".." && !path.startsWith ("../");
}

bool
qtm_valid_optional_vault_relative_path (const QString& path) {
  return path.isEmpty () || qtm_valid_vault_relative_path (path);
}

bool
qtm_valid_optional_vault_target (const QString& target) {
  if (target.isEmpty ()) return true;
  if (target.startsWith ("tmfs://") || target.startsWith ("file://"))
    return true;
  return qtm_valid_vault_relative_path (target);
}

QString
qtm_vault_relative_from_selected_path (const QString& selected) {
  QString root= qtm_vault_root_path ();
  if (root.isEmpty () || selected.isEmpty ()) return QString ();
  QString path= QDir::fromNativeSeparators (selected);
  QString rel= QDir (root).relativeFilePath (path);
  return qtm_clean_vault_relative_path (rel);
}

bool
qtm_vaultfile_read (QTMVaultfileInfo& info, QString* error) {
  if (!qtm_vault_info_available ()) {
    if (error != nullptr) *error= "No active vault.";
    return false;
  }
  info.name= to_qstring_vault_info (vault_get_name ());
  info.mapPath= "map.sqlite";
  info.preferencesPath= "";
  info.namespaceDbPath= "ns.sqlite";
  info.startupPage= "";
  info.oneTimeStartupPage= "";
  info.maintenanceSummaryPath= "";
  info.ragIndexPath= "rag.sqlite";
  info.websitesPath= "websites.json";
  info.rootNamespace= "";

  std::string read_error;
  AthenaVaultfileInfo vault_info;
  if (!athena_vaultfile_read (
        std::filesystem::path (qtm_utf8_std_string (qtm_vault_root_path ())),
        vault_info, read_error)) {
    if (error != nullptr) *error= QString::fromStdString (read_error);
    return false;
  }

  info.name= QString::fromStdString (vault_info.name);
  info.mapPath= QString::fromStdString (vault_info.map_path);
  info.preferencesPath= QString::fromStdString (vault_info.preferences_path);
  info.namespaceDbPath= QString::fromStdString (vault_info.namespace_db_path);
  info.startupPage= QString::fromStdString (vault_info.startup_page);
  info.oneTimeStartupPage=
    QString::fromStdString (vault_info.one_time_startup_page);
  info.maintenanceSummaryPath=
    QString::fromStdString (vault_info.maintenance_summary_path);
  info.ragIndexPath= QString::fromStdString (vault_info.rag_index_path);
  info.websitesPath= QString::fromStdString (vault_info.websites_path);
  info.rootNamespace= QString::fromStdString (vault_info.root_namespace);

  info.mapPath= qtm_clean_vault_relative_path (info.mapPath);
  info.preferencesPath= qtm_clean_vault_relative_path (info.preferencesPath);
  info.namespaceDbPath= qtm_clean_vault_relative_path (info.namespaceDbPath);
  info.startupPage= qtm_clean_vault_target (info.startupPage);
  info.oneTimeStartupPage= qtm_clean_vault_target (info.oneTimeStartupPage);
  info.maintenanceSummaryPath=
    qtm_clean_vault_relative_path (info.maintenanceSummaryPath);
  info.ragIndexPath= qtm_clean_vault_relative_path (info.ragIndexPath);
  info.websitesPath= qtm_clean_vault_relative_path (info.websitesPath);
  info.rootNamespace= info.rootNamespace.trimmed ();
  if (info.mapPath.isEmpty ()) info.mapPath= "map.sqlite";
  if (info.namespaceDbPath.isEmpty ()) info.namespaceDbPath= "ns.sqlite";
  if (info.ragIndexPath.isEmpty ()) info.ragIndexPath= "rag.sqlite";
  if (info.websitesPath.isEmpty ()) info.websitesPath= "websites.json";
  return true;
}

bool
qtm_vaultfile_write (const QTMVaultfileInfo& info, QString* error) {
  if (!qtm_vault_info_available ()) {
    if (error != nullptr) *error= "No active vault.";
    return false;
  }
  QTMVaultfileInfo out= info;
  out.mapPath= qtm_clean_vault_relative_path (out.mapPath);
  out.preferencesPath= qtm_clean_vault_relative_path (out.preferencesPath);
  out.namespaceDbPath= qtm_clean_vault_relative_path (out.namespaceDbPath);
  out.startupPage= qtm_clean_vault_target (out.startupPage);
  out.oneTimeStartupPage= qtm_clean_vault_target (out.oneTimeStartupPage);
  out.maintenanceSummaryPath=
    qtm_clean_vault_relative_path (out.maintenanceSummaryPath);
  out.ragIndexPath= qtm_clean_vault_relative_path (out.ragIndexPath);
  out.websitesPath= qtm_clean_vault_relative_path (out.websitesPath);
  out.rootNamespace= out.rootNamespace.trimmed ();
  if (out.ragIndexPath.isEmpty ()) out.ragIndexPath= "rag.sqlite";
  if (out.websitesPath.isEmpty ()) out.websitesPath= "websites.json";

  if (out.name.trimmed ().isEmpty ()) {
    if (error != nullptr) *error= "Vault name cannot be empty.";
    return false;
  }
  if (!out.mapPath.endsWith (".sqlite", Qt::CaseInsensitive)) {
    if (error != nullptr) *error= "Vault map database must be a .sqlite file.";
    return false;
  }
  if (!qtm_valid_vault_relative_path (out.mapPath) ||
      !qtm_valid_optional_vault_relative_path (out.preferencesPath) ||
      !qtm_valid_vault_relative_path (out.namespaceDbPath) ||
      !qtm_valid_optional_vault_target (out.startupPage) ||
      !qtm_valid_optional_vault_target (out.oneTimeStartupPage) ||
      !qtm_valid_optional_vault_relative_path (out.maintenanceSummaryPath) ||
      !qtm_valid_vault_relative_path (out.ragIndexPath) ||
      !qtm_valid_vault_relative_path (out.websitesPath)) {
    if (error != nullptr)
      *error= "Vaultfile paths must be relative paths inside the vault, "
              "tmfs:// links, or file:// links, without ./ or ../ prefixes.";
    return false;
  }

  AthenaVaultfileInfo previous_info;
  std::string read_error;
  if (!athena_vaultfile_read (
        std::filesystem::path (qtm_utf8_std_string (qtm_vault_root_path ())),
        previous_info, read_error)) {
    if (error != nullptr) *error= QString::fromStdString (read_error);
    return false;
  }

  AthenaVaultfileInfo vault_info;
  vault_info.name= qtm_utf8_std_string (out.name.trimmed ());
  vault_info.map_path= qtm_utf8_std_string (out.mapPath);
  vault_info.preferences_path= qtm_utf8_std_string (out.preferencesPath);
  vault_info.namespace_db_path= qtm_utf8_std_string (out.namespaceDbPath);
  vault_info.startup_page= qtm_utf8_std_string (out.startupPage);
  vault_info.one_time_startup_page=
    qtm_utf8_std_string (out.oneTimeStartupPage);
  vault_info.maintenance_summary_path=
    qtm_utf8_std_string (out.maintenanceSummaryPath);
  vault_info.rag_index_path= qtm_utf8_std_string (out.ragIndexPath);
  vault_info.websites_path= qtm_utf8_std_string (out.websitesPath);
  vault_info.root_namespace= qtm_utf8_std_string (out.rootNamespace);

  std::string write_error;
  if (!athena_vaultfile_write (
        std::filesystem::path (qtm_utf8_std_string (qtm_vault_root_path ())),
        vault_info, write_error)) {
    if (error != nullptr) *error= QString::fromStdString (write_error);
    return false;
  }

  string load_error= vault_load (
    vault_get_root (), from_qstring_vault_info (out.name.trimmed ()),
    from_qstring_vault_info (out.mapPath),
    from_qstring_vault_info (out.namespaceDbPath));
  if (load_error != "") {
    std::string rollback_error;
    athena_vaultfile_write (
      std::filesystem::path (qtm_utf8_std_string (qtm_vault_root_path ())),
      previous_info, rollback_error);
    if (error != nullptr) *error= to_qstring_vault_info (load_error);
    return false;
  }
  return true;
}
