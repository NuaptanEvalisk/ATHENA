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
#include "new_buffer.hpp"
#include "QTMVaultPreviewBuilder.hpp"
#include "ATHENA/Data/enunciation_model.hpp"
#include "ATHENA/Data/heading_word_count.hpp"
#include "node_metadata.hpp"
#include <algorithm>
#include <functional>
#include <map>
#include <set>

tree
vault_link_source_body (url file) {
  const auto buffers= get_all_buffers ();
  for (int i=0; i<N(buffers); ++i)
    if (concretize (buffers[i]) == concretize (file))
      return get_buffer_body (buffers[i]);
  return import_body (file);
}

static QString source_excerpt (const tree& value) {
  QString text;
  std::function<void (const tree&)> visit= [&] (const tree& part) {
    if (text.size () >= 120) return;
    if (is_atomic (part)) text += to_qstring (part->label).left (120-text.size ()) + " ";
    else for (int i=0; i<N(part) && text.size () < 120; ++i) visit (part[i]);
  };
  visit (value);
  return text.simplified ();
}

std::vector<VaultSourceTarget>
vault_source_targets (const tree& body) {
  std::vector<VaultSourceTarget> result;
  const auto& registry= athena::enunciation::standard_registry ();
  std::function<void (const tree&, path)> visit= [&] (const tree& value, path where) {
    const auto id= athena::node::id (value);
    if (athena::node::valid_id (id)) {
      QString kind, title;
      if (athena::enunciation::is_canonical (value)) {
        kind= QString::fromStdString (registry.kind_name (value));
        title= QString::fromStdString (registry.display_name (value));
        const auto* metadata= athena::node::get (value);
        const auto name= metadata->properties.find ("name");
        if (name != metadata->properties.end ())
          if (const auto* rich= std::get_if<athena::node::rich_text> (&name->second.data))
            title += ": " + source_excerpt (rich->content);
      }
      else if (athena_heading_level (value) > 0) {
        kind= "heading";
        title= to_qstring (athena_heading_title (value));
      }
      else if (is_nil (where)) title= "Whole document";
      if (title.isEmpty ()) title= is_atomic (value) ? "Paragraph" :
        to_qstring (as_string (L(value)));
      QString excerpt= source_excerpt (value);
      if (excerpt.size () > 120) excerpt= excerpt.left (117) + "...";
      if (!excerpt.isEmpty ()) title += ": " + excerpt;
      result.push_back ({QString::fromStdString (id), title, kind, where});
    }
    if (is_compound (value))
      for (int i=0; i<N(value); ++i) visit (value[i], where * i);
  };
  visit (body, path ());
  return result;
}

bool
vault_source_selection (const tree& body, const QStringList& requested,
                        std::vector<VaultSourceTarget>& selected, QString& error) {
  selected.clear ();
  error.clear ();
  const auto targets= vault_source_targets (body);
  std::map<QString, std::vector<const VaultSourceTarget*>> by_id;
  for (const auto& target: targets) by_id[target.uuid].push_back (&target);
  std::set<QString> seen;
  for (const auto& id: requested) {
    if (!seen.insert (id).second) continue;
    const auto found= by_id.find (id);
    if (found == by_id.end () || found->second.size () != 1) {
      error= found == by_id.end () ? "The selected source object no longer exists." :
                                    "The selected source UUID is duplicated.";
      selected.clear ();
      return false;
    }
    const auto& target= *found->second.front ();
    for (const auto& prior: selected)
      if (prior.where <= target.where || target.where <= prior.where) {
        error= "Select either a parent object or its children, not both.";
        selected.clear ();
        return false;
      }
    selected.push_back (target);
  }
  if (!selected.empty ()) return true;
  error= "Select a source object first.";
  return false;
}

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
