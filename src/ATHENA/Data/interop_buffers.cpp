/******************************************************************************
* MODULE     : interop_buffers.cpp
* DESCRIPTION: Resolve stable buffer IDs and the active buffer from GUI publications
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#include "interop_buffers.hpp"
#include "interop_document.hpp"
#include "../Interop/traversal.hpp"
#include "buffer_name_catalog.hpp"
#include <algorithm>
#include <cerrno>
#include <system_error>

namespace athena::interop {
namespace {
class buffer_resource final: public resource {
public:
  const std::uint64_t id;
  explicit buffer_resource (std::uint64_t id): id (id) {}
  std::string type () const override { return "buffer"; }
  std::string identity () const override { return "buffer:" + std::to_string (id); }
  value properties () const override {
    for (const auto& entry: published_buffer_metadata ()) {
      if (entry.second.actor_id != id) continue;
      return {{"type", type ()}, {"id", id}, {"name", entry.second.title},
              {"url", entry.first}, {"modified", entry.second.modified},
              {"active", published_active_buffer () == id}};
    }
    throw std::system_error (ESTALE, std::generic_category (), "Buffer has closed");
  }
  value inspect () const override {
    return {{"get", {{"parameters", value::object ()}}},
            {"inspect", {{"parameters", value::object ()}}}};
  }
  operation_result operate (const std::string& command, const value& p) const override {
    if (!p.is_object () || !p.empty ()) return {"INVALID_ARGUMENT", "This command takes no parameters"};
    try {
      auto props= properties ();
      if (command == "get") return {"OK", std::move (props)};
      if (command == "inspect") return {"OK", inspect ()};
      return {"UNKNOWN_COMMAND", command};
    }
    catch (const std::system_error& e) { return {"STALE", e.what ()}; }
  }
};

class buffers_resolver_rep final: public resolver {
public:
  resolver_outcome resolve (const resolution_request& req, resolution_output& out) const override {
    if (!req.basepoint) return resolver_outcome::irrelevant;
    auto state= std::dynamic_pointer_cast<const traversal> (req.state);
    auto base= std::dynamic_pointer_cast<const buffer_resource> (req.basepoint->accessor);
    if (state && state->step == traversal::phase::candidate) {
      if (!base || state->domain != "buffer") return resolver_outcome::irrelevant;
      return visit_candidate (req, *state, out);
    }
    if (state && !state->domain.empty () && state->domain != "buffer")
      return resolver_outcome::irrelevant;
    const auto& s= req.selectors.at (req.offset);
    if (base) {
      // Scoped buffer traversal does not enter the document domain.
      if (state && !state->domain.empty ()) return resolver_outcome::miss;
      const bool direct= !state && s.type == selector::kind::name && s.name == "document";
      if (!direct && !state && s.type != selector::kind::local &&
          s.type != selector::kind::scoped && s.type != selector::kind::recursive)
        return resolver_outcome::irrelevant;
      if (!s.positions.empty ()) throw std::invalid_argument ("Select document children after /document");
      auto budget= state ? state->budget : std::make_shared<traversal_budget> (s.limits);
      if (traversal_stopped (req, *budget, out)) return resolver_outcome::miss;
      auto document= buffer_document (base->id);
      if (direct) out.publish (std::move (document), req.offset + 1);
      else publish_candidate (out, std::move (document), req.offset, budget,
                              state ? state->depth + 1 : 1, "document");
    }
    else {
      if (req.basepoint->accessor->type () != "root") return resolver_outcome::irrelevant;
      std::size_t offset= req.offset;
      const bool marker= !state && s.type == selector::kind::name && s.name == "buffers";
      if (marker) {
        if (!s.positions.empty ()) throw std::invalid_argument ("Use /buffers/[ID] to select a buffer");
        if (++offset == req.selectors.size ()) return resolver_outcome::miss;
      }
      else if (!state && s.type != selector::kind::local &&
               s.type != selector::kind::scoped && s.type != selector::kind::recursive)
        return resolver_outcome::irrelevant;
      const auto& target= req.selectors.at (offset);
      if (!target.positions.empty () && target.type != selector::kind::index)
        throw std::invalid_argument ("Buffer indices are stable IDs; use /buffers/[ID]");
      auto budget= state ? state->budget : std::make_shared<traversal_budget> (target.limits);
      const auto active= published_active_buffer ();
      std::vector<std::uint64_t> ids;
      for (const auto& entry: published_buffer_metadata ())
        if (entry.second.actor_id) ids.push_back (entry.second.actor_id);
      std::sort (ids.begin (), ids.end ());
      ids.erase (std::unique (ids.begin (), ids.end ()), ids.end ());
      for (auto id: ids) {
        if (traversal_stopped (req, *budget, out)) break;
        auto resource= std::make_shared<buffer_resource> (id);
        if (target.type == selector::kind::index) {
          if (std::find (target.positions.begin (), target.positions.end (), id) == target.positions.end ()) continue;
          budget->matches.fetch_add (1);
          out.publish (std::move (resource), offset + 1);
        }
        else publish_candidate (out, std::move (resource), offset, budget,
                                state ? state->depth + 1 : 1, "buffer", id == active);
      }
    }
    return out.branches.empty () ? resolver_outcome::miss : resolver_outcome::resolved;
  }
};
}
std::shared_ptr<const resolver> buffers_resolver () { return std::make_shared<buffers_resolver_rep> (); }
}
