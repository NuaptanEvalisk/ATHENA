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
#include "path.hpp"
#include "tree.hpp"
#include "ATHENA/Data/node_location.hpp"
#include <QString>
#include <QStringList>
#include <Qt>
#include <vector>
#include <functional>
class QObject;

// GUI-owned asynchronous lifetime; cancelled with owner destruction. No file
// hint participates in identity selection, including after external renames.
QObject* vault_resolve_source_ids (QObject* owner, const QStringList& ids,
  std::function<void (athena::node_location::snapshot, QString)> completed,
  bool content= true);
tree vault_source_preview (const athena::node_location::result&);

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

struct VaultSourceTarget {
  QString uuid, title, kind;
  path where;
};

// Owner-local source snapshots; preview transformations must happen afterwards.
tree vault_link_source_body (url file);
std::vector<VaultSourceTarget> vault_source_targets (const tree& body);
bool vault_source_selection (const tree& body, const QStringList& requested,
                             std::vector<VaultSourceTarget>& selected,
                             QString& error);

#endif // QTMVAULTLINKMODEL_HPP
