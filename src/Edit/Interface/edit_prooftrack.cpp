#include "edit_interface.hpp"
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
    payload.push_back (std::move (item));
  }
  auto bytes= payload.dump ();
  if (bytes == state.published) return;
  state.published= bytes;
  publish_ui_text (actor_command_kind::ui_prooftrack, string (bytes.data (), bytes.size ()));
}
