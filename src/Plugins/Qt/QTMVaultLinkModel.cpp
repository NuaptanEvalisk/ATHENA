/******************************************************************************
* MODULE     : QTMVaultLinkModel.cpp
* DESCRIPTION: Vault link file model helpers
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "QTMVaultLinkModel.hpp"
#include "link.hpp"
#include "qt_utilities.hpp"
#include "tm_buffer.hpp"
#include "vault.hpp"
#include <algorithm>

QString
strip_known_extension (QString s) {
  if (s.endsWith (".ath")) s.chop (4);
  else if (s.endsWith (".tm")) s.chop (3);
  return s;
}

bool
is_autosave_document_path (const QString& relPath) {
  return relPath.endsWith (".ath~", Qt::CaseInsensitive) ||
         relPath.endsWith (".tm~", Qt::CaseInsensitive);
}

QString
current_vault_relative_document () {
  if (!vault_active ()) return QString ();
  url current= get_current_buffer_safe ();
  if (is_none (current)) return QString ();
  url root= vault_get_root ();
  if (!descends (current, root)) return QString ();

  string suf= suffix (current);
  if (suf != "ath" && suf != "tm") return QString ();
  QString relPath= to_qstring (
    as_unix_string (delta (root * url (""), current)));
  if (is_autosave_document_path (relPath)) return QString ();
  return relPath;
}

QString
file_display_stem (const QString& relPath) {
  return strip_known_extension (relPath.section ('/', -1));
}

std::vector<WikilinkFileEntry>
load_vault_link_files () {
  std::vector<WikilinkFileEntry> files;
  url root= vault_get_root ();
  QString currentRelPath= current_vault_relative_document ();
  array<url> all= vault_get_all_files ();
  for (int i=0; i<N(all); i++) {
    string suf= suffix (all[i]);
    if (suf != "ath" && suf != "tm") continue;
    url rel= delta (root * url (""), all[i]);
    QString relPath= to_qstring (as_unix_string (rel));
    if (is_autosave_document_path (relPath)) continue;
    WikilinkFileEntry e;
    e.file= all[i];
    e.relPath= relPath;
    e.stem= file_display_stem (relPath);
    e.searchPath= from_qstring (strip_known_extension (relPath));
    e.searchStem= from_qstring (e.stem);
    e.mtime= vault_get_mtime (all[i]);
    e.isCurrent= relPath == currentRelPath;
    files.push_back (e);
  }
  std::sort (files.begin (), files.end (),
             [] (const WikilinkFileEntry& a,
                 const WikilinkFileEntry& b) {
               if (a.isCurrent != b.isCurrent) return a.isCurrent;
               if (a.mtime != b.mtime) return a.mtime > b.mtime;
               return a.relPath < b.relPath;
             });
  return files;
}
