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
#include "patch.hpp"
#include <QUrl>
#include <QStringList>
#include <map>
#include <mutex>

namespace athena::document_node {
namespace {
void remap_tree (tree&, const node::identity_map&);

struct move_entry {
  tree selection;
  std::string vault;
  source_move_endpoint source;
  source_move_endpoint target;
  double marker= 0.0;
  source_move_state state= source_move_state::pending;
};

std::mutex move_mutex;
std::map<std::string,move_entry> moves;
std::map<double,std::string> moves_by_marker;

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
  if (auto* metadata= node::edit (value)) {
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

source_move_ticket issue_source_move (
    const tree& selection, std::string vault_key, source_move_endpoint source) {
  if (vault_key.empty () || source.actor == ATHENA_NO_ACTOR ||
      !node::contains_metadata (selection)) return {};
  source_move_ticket result {node::new_id (), new_marker ()};
  std::lock_guard<std::mutex> lock (move_mutex);
  moves[result.token]= {copy (selection), std::move (vault_key), source, {},
                        result.marker, source_move_state::pending};
  moves_by_marker[result.marker]= result.token;
  return result;
}

std::optional<source_move_claim> reserve_source_move (
    const std::string& token, const tree& selection, const std::string& vault_key,
    source_move_endpoint target) {
  if (token.empty () || vault_key.empty () || target.actor == ATHENA_NO_ACTOR)
    return std::nullopt;
  std::lock_guard<std::mutex> lock (move_mutex);
  auto found= moves.find (token);
  if (found == moves.end () ||
      found->second.state != source_move_state::pending ||
      found->second.vault != vault_key || found->second.selection != selection)
    return std::nullopt;
  found->second.target= target;
  found->second.state= source_move_state::reserved;
  return source_move_claim {
    token, found->second.marker, found->second.source, target};
}

bool activate_source_move (const std::string& token) {
  std::lock_guard<std::mutex> lock (move_mutex);
  auto found= moves.find (token);
  if (found == moves.end () ||
      found->second.state != source_move_state::reserved) return false;
  found->second.state= source_move_state::active;
  found->second.selection= tree ();
  return true;
}

void release_source_move (const std::string& token) {
  std::lock_guard<std::mutex> lock (move_mutex);
  auto found= moves.find (token);
  if (found == moves.end () ||
      found->second.state != source_move_state::reserved) return;
  found->second.target= {};
  found->second.state= source_move_state::pending;
}

void invalidate_source_move (const std::string& token) {
  std::lock_guard<std::mutex> lock (move_mutex);
  auto found= moves.find (token);
  if (found == moves.end ()) return;
  if (found->second.state == source_move_state::active ||
      found->second.state == source_move_state::undone)
    return;
  moves_by_marker.erase (found->second.marker);
  moves.erase (found);
}

std::optional<source_move_history> source_move_for_history (
    double marker, source_move_endpoint endpoint) {
  std::lock_guard<std::mutex> lock (move_mutex);
  auto index= moves_by_marker.find (marker);
  if (index == moves_by_marker.end ()) return std::nullopt;
  auto found= moves.find (index->second);
  if (found == moves.end () ||
      !(endpoint == found->second.source || endpoint == found->second.target) ||
      (found->second.state != source_move_state::active &&
       found->second.state != source_move_state::undone))
    return std::nullopt;
  return source_move_history {found->second.marker, found->second.source,
                              found->second.target, found->second.state};
}

bool set_source_move_history_state (
    double marker, source_move_state expected, source_move_state next) {
  std::lock_guard<std::mutex> lock (move_mutex);
  auto index= moves_by_marker.find (marker);
  if (index == moves_by_marker.end ()) return false;
  auto found= moves.find (index->second);
  if (found == moves.end () || found->second.state != expected) return false;
  found->second.state= next;
  return true;
}
}
