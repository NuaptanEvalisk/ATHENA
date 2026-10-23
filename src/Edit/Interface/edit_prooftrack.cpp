#include "edit_interface.hpp"
#include "ATHENA/Data/prooftrack.hpp"
#include "node_metadata.hpp"
#include "tree_cursor.hpp"
#include <nlohmann/json.hpp>
#include <map>
#include <set>

namespace {
using json= nlohmann::json;
struct endpoint {
  std::string marker;
  SI x, y;
};
using points= std::vector<endpoint>;
struct geometry {
  box owner;
  points endpoints;
  std::vector<std::shared_ptr<const geometry>> children;
  explicit geometry (box b): owner (b) {}
};
// Mirror persistent box branches, not the mutable source tree. Unchanged
// branches reuse both endpoint coordinates and their descendants' cache.
std::shared_ptr<const geometry> collect (box b, std::shared_ptr<const geometry> previous) {
  if (previous && previous->owner.operator-> () == b.operator-> ()) return previous;
  auto result= std::make_shared<geometry> (b);
  tree ids= b->get_info ("locus-ids");
  if (is_func (ids, TUPLE) && b->w () > 0 && b->h () > 0)
    for (int i=0; i<N(ids); ++i)
      if (is_atomic (ids[i]) && starts (ids[i]->label, "prooftrack:")) {
        const string id= ids[i]->label;
        result->endpoints.push_back ({std::string (id.data ()+11, N(id)-11),
                           b->x1, (b->y1+b->y2)/2});
        return result;
      }
  for (int i=0; i<b->subnr (); ++i) {
    auto child= collect (b->subbox (i), previous && i < int (previous->children.size ()) ?
      previous->children[i] : nullptr);
    result->children.push_back (child);
    for (auto point: child->endpoints) {
      point.x+= b->sx (i); point.y+= b->sy (i);
      result->endpoints.push_back (std::move (point));
    }
  }
  return result;
}
}

// A document's immediate children are actual paragraphs. A proof may target
// any persistent paragraph UUID without creating a new Enunciation or mutating
// the display/source tree. Look up only targets with known proof relations.
void paragraph_targets (tree t, path where, bool paragraph,
                        const std::set<std::string>& targets,
                        std::map<std::string, path>& found) {
  if (paragraph && !is_compound (t, "enunciation")) {
    const std::string id= athena::node::id (t);
    if (targets.count (id)) found.emplace (id, where);
  }
  if (is_atomic (t)) return;
  const bool children_are_paragraphs= is_func (t, DOCUMENT) ||
                                      is_func (t, PARA);
  for (int i=0; i<N(t); ++i)
    paragraph_targets (t[i], where * i, children_are_paragraphs,
                       targets, found);
}

struct edit_interface_rep::prooftrack_geometry {
  std::shared_ptr<const geometry> cached;
  std::string published;
};

void edit_interface_rep::refresh_prooftrack_geometry () {
  if (is_embedded_widget () || is_nil (eb)) return;
  if (!(env_change & (THE_TREE | THE_ENVIRONMENT | THE_EXTENTS))) return;
  if (!prooftrack_geometry_) prooftrack_geometry_= std::make_shared<prooftrack_geometry> ();
  auto& state= *prooftrack_geometry_;
  state.cached= collect (eb, state.cached);
  json payload= json::array ();
  // A multiword heading has several locus boxes. Its first visible fragment
  // anchors the track. Distinct transclusion occurrences remain distinct.
  std::set<std::string> seen;
  for (const auto& point: state.cached->endpoints) {
    if (!seen.insert (point.marker).second) continue;
    auto item= json::parse (point.marker);
    item["x"]= double (point.x)*magf/PIXEL;
    item["y"]= -double (point.y)*magf/PIXEL;
    item["linkable"]= true;
    item["writable"]= !editor_command_state_snapshot ().read_only () &&
                       !item.value ("transclusion", false);
    path source;
    for (const auto& component: item.at ("path")) source= source * component.get<int> ();
    if (rp <= source && has_subtree (et, source) &&
        athena::node::id (subtree (et, source)) == item.at ("id").get<std::string> ()) {
      selection area= eb->find_check_selection (start (et, source), end (et, source));
      if (area->valid && !is_nil (area->rs)) {
        rectangle r= least_upper_bound (area->rs);
        item["bounds"]= {double (r->x1)*magf/PIXEL, -double (r->y2)*magf/PIXEL,
                         double (r->x2-r->x1)*magf/PIXEL, double (r->y2-r->y1)*magf/PIXEL};
      }
    }
    item.erase ("path");
    payload.push_back (std::move (item));
  }
  // Existing native Enunciation markers provide proof.target immediately,
  // even before that edit has been saved to the background index. The index
  // also covers proofs located in other documents of this Vault.
  std::set<std::string> targets;
  const auto snapshot= athena::prooftrack::read ();
  for (const auto& [id, proofs]: snapshot->by_target)
    if (!proofs.empty ()) targets.insert (id);
  for (const auto& point: payload) {
    if (point.value ("proof", false)) {
      const auto id= point.value ("target", std::string ());
      if (!id.empty ()) targets.insert (id);
    }
  }
  if (!targets.empty () && has_subtree (et, rp)) {
    std::map<std::string, path> matched;
    paragraph_targets (subtree (et, rp), rp, false, targets, matched);
    for (const auto& [id, where]: matched) {
      const cursor at= eb->find_check_cursor (start (et, where));
      if (!at->valid) continue; // Folded/offscreen-progressive nodes are absent.
      const double x= double (at->ox)*magf/PIXEL;
      const double y= -double (at->oy+(at->y1+at->y2)/2)*magf/PIXEL;
      payload.push_back ({
        {"id", id}, {"target", ""}, {"title", "Paragraph"},
        {"occurrence", id}, {"proof", false}, {"transclusion", false},
        {"x", x}, {"y", y}
      });
    }
  }
  auto bytes= payload.dump ();
  if (bytes == state.published) return;
  state.published= bytes;
  publish_ui_text (actor_command_kind::ui_prooftrack, string (bytes.data (), bytes.size ()));
}
