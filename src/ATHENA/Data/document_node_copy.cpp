/******************************************************************************
* MODULE     : document_node_copy.cpp
* DESCRIPTION: Identity-safe source duplication for clipboard and external insertion
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#include "document_node_copy.hpp"
#include <QUrl>
#include <QStringList>

namespace athena::document_node {
namespace {
void remap_tree (tree&, const node::identity_map&);

void remap_rich_text (node::property& property, const node::identity_map& ids) {
  if (auto* rich= std::get_if<node::rich_text> (&property.data))
    remap_tree (rich->content, ids);
  else if (auto* list= std::get_if<node::property::list> (&property.data))
    for (auto& item: *list) remap_rich_text (item, ids);
  else if (auto* dict= std::get_if<node::property::dictionary> (&property.data))
    for (auto& item: *dict) remap_rich_text (item.second, ids);
}

void remap_id (tree& target, const node::identity_map& ids) {
  if (!is_atomic (target)) return;
  auto found= ids.find (std::string (as_charp (target->label), N(target->label)));
  if (found != ids.end ())
    target->label= string (found->second.data (), static_cast<int> (found->second.size ()));
}

void remap_link (tree& target, const node::identity_map& ids) {
  if (!is_atomic (target)) return;
  QUrl url= QUrl::fromEncoded (QByteArray (as_charp (target->label), N(target->label)),
                              QUrl::StrictMode);
  if (!url.isValid () || url.scheme () != "tmfs" ||
      (url.host () != "wikilink" && url.host () != "transclude") ||
      !url.userInfo ().isEmpty () || url.port () != -1) return;
  QStringList segments= url.path (QUrl::FullyEncoded).split ('/');
  if (segments.size () < 2 || !segments[0].isEmpty ()) return;
  // UUIDs in the native contract are literal canonical ASCII path components.
  auto found= ids.find (segments[1].toStdString ());
  if (found == ids.end ()) return;
  segments[1]= QString::fromStdString (found->second);
  url.setPath (segments.join ('/'), QUrl::StrictMode);
  QByteArray encoded= url.toEncoded (QUrl::FullyEncoded);
  target->label= string (encoded.constData (), encoded.size ());
}

void remap_tree (tree& value, const node::identity_map& ids) {
  if (auto* metadata= inside (value)->attributes) {
    metadata->properties.erase (artifact_bindings_property);
    for (auto& item: metadata->properties) remap_rich_text (item.second, ids);
    if (metadata->empty ()) node::clear (value);
  }
  if (is_atomic (value) || is_func (value, RAW_DATA)) return;
  if (is_func (value, HLINK, 2)) remap_link (value[1], ids);
  if (is_func (value, TRANSCLUDE, 1) && is_tuple (value[0]))
    for (int i=0; i<N(value[0]); ++i) remap_id (value[0][i], ids);
  for (int i=0; i<N(value); ++i) remap_tree (value[i], ids);
}
}

tree duplicate_source_nodes (const tree& source, node::identity_map* result) {
  node::identity_map ids;
  tree duplicated= node::duplicate (source, &ids);
  remap_tree (duplicated, ids);
  if (result) *result= std::move (ids);
  return duplicated;
}
}
