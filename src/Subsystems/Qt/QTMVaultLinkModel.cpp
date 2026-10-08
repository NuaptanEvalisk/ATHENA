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
#include "convert.hpp"
#include "named_symbol.hpp"
#include "drd_mode.hpp"
#include "utf8_edit.hpp"
#include "ATHENA/Data/node_reference.hpp"
#include "Data/Convert/Xml/athena_document_xml.hpp"
#include <QTimer>
#include <algorithm>
#include <functional>
#include <map>
#include <set>

QObject* vault_resolve_source_ids (QObject* owner, const QStringList& ids,
    std::function<void (athena::node_location::snapshot, QString)> completed) {
  auto context= vault_capture_context ();
  auto* timer= new QTimer (owner);
  std::vector<std::string> requested;
  for (const auto& id: ids) {
    auto value= id.toStdString ();
    if (std::find (requested.begin (), requested.end (), value) == requested.end ())
      requested.push_back (std::move (value));
  }
  if (!context || ids.isEmpty ()) {
    QObject::connect (timer, &QTimer::timeout, timer,
      [timer, completed= std::move (completed)] {
        timer->stop ();
        timer->deleteLater ();
        completed ({}, "No active vault or no source identities selected.");
      });
    timer->start (0);
    return timer;
  }
  // Resolve through the same watched content cache as document presentation.
  // Closing a chooser cancels its waiter, not a query shared with other views.
  athena::node_reference::get (requested, {}, true);
  QObject::connect (timer, &QTimer::timeout, timer,
    [timer, requested, context, completed= std::move (completed)] {
      athena::node_location::snapshot result;
      QString error;
      if (!vault_context_is_current (context)) error= "The originating vault has closed.";
      else {
        result= athena::node_reference::get (requested, {}, true).snapshot;
        if (!result || result->state == athena::node_location::status::pending) {
          timer->setInterval (40);
          return;
        }
      }
      if (result && (result->state != athena::node_location::status::resolved || result->items.empty ())) {
        error= "Could not resolve the selected source identities.";
        for (const auto& item: result->items)
          if (!item.diagnostic.empty ()) error += "\n" + QString::fromStdString (item.diagnostic);
      }
      timer->stop ();
      timer->deleteLater ();
      completed (result, error);
    });
  timer->start (0);
  return timer;
}

tree vault_source_preview (const athena::node_location::result& result) {
  tree body (DOCUMENT);
  for (const auto& item: result.items) {
    if (item.state != athena::node_location::status::resolved)
      throw std::runtime_error ("Source preview requires resolved identities");
    tree fragment= athena::document::read_xml_v2 (item.fragment_xml,
      athena::document::xml_kind::fragment);
    body << rebase_preview_images (fragment,
      url_system (string (item.source_directory.data (), item.source_directory.size ())));
  }
  return body;
}

tree
vault_link_source_body (url file) {
  const auto buffers= get_all_buffers ();
  for (int i=0; i<N(buffers); ++i)
    if (concretize (buffers[i]) == concretize (file))
      return get_buffer_body (buffers[i]);
  return import_body (file);
}

// Display-only leaves for the standard DRD-aware verbatim converter. Never
// expose symbol identities or image resource parameters as visible text.
static tree source_excerpt_tree (const tree& value) {
  if (is_atomic (value)) return value;
  if (is_func (value, NAMED_SYMBOL, 1)) {
    if (is_atomic (value[0])) {
      const string& identity= value[0]->label;
      const auto* symbol= athena::text::standard_named_symbols ().lookup (
        std::string_view (identity.data (), N(identity)));
      if (symbol && !symbol->glyph_utf8.empty ())
        return tree (string (symbol->glyph_utf8.c_str ()));
    }
    return tree ("[symbol]");
  }
  if (is_func (value, IMAGE)) return tree ("[image]");
  tree result (L(value), N(value));
  for (int i=0; i<N(value); ++i) result[i]= source_excerpt_tree (value[i]);
  return result;
}

static QString source_excerpt (const tree& value) {
  struct access_scope {
    int previous= set_access_mode (DRD_ACCESS_NORMAL);
    ~access_scope () { set_access_mode (previous); }
  } scope;
  QString text= to_qstring (tree_to_verbatim (source_excerpt_tree (value), false,
                                            "UTF-8")).simplified ();
  const auto characters= utf8_graphemes (from_qstring (text));
  if (N(characters) <= 120) return text;
  string prefix;
  for (int i=0; i<117; ++i) prefix << characters[i];
  return to_qstring (prefix) + "...";
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
          if (const auto* rich= std::get_if<athena::node::rich_text> (&name->second.data)) {
            const QString text= source_excerpt (rich->content);
            if (!text.isEmpty ()) title += ": " + text;
          }
      }
      else if (athena_heading_level (value) > 0) {
        kind= "heading";
        title= source_excerpt (value);
      }
      else if (is_nil (where)) title= "Whole document";
      if (title.isEmpty ()) title= is_atomic (value) ? "Paragraph" :
        to_qstring (as_string (L(value)));
      QString excerpt= source_excerpt (value);
      if (!excerpt.isEmpty () && excerpt != title) title += ": " + excerpt;
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
