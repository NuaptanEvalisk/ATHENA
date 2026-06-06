/******************************************************************************
* MODULE     : QTMVaultLinkModel.hpp
* DESCRIPTION: Vault link file model helpers
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef QTMVAULTLINKMODEL_HPP
#define QTMVAULTLINKMODEL_HPP

#include "string.hpp"
#include "url.hpp"
#include <QString>
#include <Qt>
#include <vector>

enum WikilinkItemRole {
  WikilinkPayloadRole= Qt::UserRole,
  WikilinkIndexRole,
  WikilinkCompletionRole
};

struct WikilinkFileEntry {
  url     file;
  QString relPath;
  QString stem;
  string  searchPath;
  string  searchStem;
  int     mtime;
  bool    isCurrent;
};

QString strip_known_extension (QString s);
bool is_autosave_document_path (const QString& relPath);
QString current_vault_relative_document ();
QString file_display_stem (const QString& relPath);
std::vector<WikilinkFileEntry> load_vault_link_files ();

#endif // QTMVAULTLINKMODEL_HPP
