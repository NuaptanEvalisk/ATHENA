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

#include "convert.hpp"
#include "vault.hpp"

#include <QDir>
#include <QFile>
#include <QTextStream>

static QString
to_qstring_vault_info (string s) {
  return QString::fromUtf8 (as_charp (s), N(s));
}

static string
from_qstring_vault_info (const QString& s) {
  QByteArray bytes= s.toUtf8 ();
  return string (bytes.constData ());
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

static QString
qtm_vaultfile_path () {
  QString root= qtm_vault_root_path ();
  if (root.isEmpty ()) return QString ();
  return QDir (root).filePath ("Vaultfile");
}

static QStringList
qtm_vaultfile_quoted_strings (const QString& text) {
  QStringList out;
  bool in= false;
  bool esc= false;
  QString cur;
  for (QChar c: text) {
    if (!in) {
      if (c == '"') {
        in= true;
        cur.clear ();
      }
      continue;
    }
    if (esc) {
      cur.append (c);
      esc= false;
      continue;
    }
    if (c == '\\') {
      esc= true;
      continue;
    }
    if (c == '"') {
      out << cur;
      in= false;
      continue;
    }
    cur.append (c);
  }
  return out;
}

static QString
qtm_scheme_quote_qstring (const QString& text) {
  QString out= "\"";
  for (QChar c: text) {
    if (c == '\\' || c == '"') out.append ('\\');
    out.append (c);
  }
  out.append ('"');
  return out;
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
  info.mapPath= "map.tmdb";
  info.preferencesPath= "";
  info.namespaceDbPath= "ns.sqlite";
  info.startupPage= "";
  info.oneTimeStartupPage= "";

  QFile file (qtm_vaultfile_path ());
  if (!file.open (QIODevice::ReadOnly | QIODevice::Text)) return true;
  QTextStream in (&file);
  QStringList fields= qtm_vaultfile_quoted_strings (in.readAll ());
  if (fields.size () >= 1) info.name= fields[0];
  if (fields.size () >= 2) info.mapPath= fields[1];
  if (fields.size () >= 3) info.preferencesPath= fields[2];
  if (fields.size () >= 4 && !fields[3].isEmpty ())
    info.namespaceDbPath= fields[3];
  if (fields.size () >= 5) info.startupPage= fields[4];
  if (fields.size () >= 6) info.oneTimeStartupPage= fields[5];

  info.mapPath= qtm_clean_vault_relative_path (info.mapPath);
  info.preferencesPath= qtm_clean_vault_relative_path (info.preferencesPath);
  info.namespaceDbPath= qtm_clean_vault_relative_path (info.namespaceDbPath);
  info.startupPage= qtm_clean_vault_target (info.startupPage);
  info.oneTimeStartupPage= qtm_clean_vault_target (info.oneTimeStartupPage);
  if (info.mapPath.isEmpty ()) info.mapPath= "map.tmdb";
  if (info.namespaceDbPath.isEmpty ()) info.namespaceDbPath= "ns.sqlite";
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

  if (out.name.trimmed ().isEmpty ()) {
    if (error != nullptr) *error= "Vault name cannot be empty.";
    return false;
  }
  if (!qtm_valid_vault_relative_path (out.mapPath) ||
      !qtm_valid_optional_vault_relative_path (out.preferencesPath) ||
      !qtm_valid_vault_relative_path (out.namespaceDbPath) ||
      !qtm_valid_optional_vault_target (out.startupPage) ||
      !qtm_valid_optional_vault_target (out.oneTimeStartupPage)) {
    if (error != nullptr)
      *error= "Vaultfile paths must be relative paths inside the vault, "
              "tmfs:// links, or file:// links, without ./ or ../ prefixes.";
    return false;
  }

  QFile file (qtm_vaultfile_path ());
  if (!file.open (QIODevice::WriteOnly | QIODevice::Text)) {
    if (error != nullptr) *error= "Could not write " + qtm_vaultfile_path ();
    return false;
  }
  QTextStream stream (&file);
  stream << "(" << qtm_scheme_quote_qstring (out.name.trimmed ())
         << " " << qtm_scheme_quote_qstring (out.mapPath)
         << " " << qtm_scheme_quote_qstring (out.preferencesPath)
         << " " << qtm_scheme_quote_qstring (out.namespaceDbPath)
         << " " << qtm_scheme_quote_qstring (out.startupPage)
         << " " << qtm_scheme_quote_qstring (out.oneTimeStartupPage)
         << ")\n";
  file.close ();

  vault_load (vault_get_root (), from_qstring_vault_info (out.name.trimmed ()),
              from_qstring_vault_info (out.mapPath),
              from_qstring_vault_info (out.namespaceDbPath));
  return true;
}
