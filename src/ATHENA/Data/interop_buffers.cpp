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
#include "buffer_actor.hpp"
#include "buffer_state.hpp"
#include "editor.hpp"
#include "interop_document_codec.hpp"
#include "node_metadata.hpp"
#include "tree_cursor.hpp"
#include <algorithm>
#include <cerrno>
#include <system_error>

namespace athena::interop {
namespace {
value encode_path (path p) {
  value result= value::array ();
  while (!is_nil (p)) { result.push_back (p->item); p= p->next; }
  return result;
}

operation_result editor_operation (std::uint64_t id, const std::string& command, const value& p) {
  athena_view_id view= ATHENA_NO_VIEW;
  for (const auto& entry: published_buffer_metadata ())
    if (entry.second.actor_id == id) view= entry.second.source_view;
  if (view == ATHENA_NO_VIEW) return {"STALE", "Buffer has no editor view"};
  auto answer= std::make_shared<operation_result> ();
  auto continuation= actor_continuation_registry::instance ().store ([answer, view, command, p] {
    try {
      auto* actor= current_scheme_execution_context ()->actor;
      auto* editor= actor->current_editor (view);
      if (!editor) { *answer= {"STALE", "Editor view has closed"}; return; }
      const value cursor= encode_path (editor->the_path () / editor->the_buffer_path ());
      if (command == "context") {
        if (!p.is_object () || !p.empty ()) throw std::invalid_argument ("context takes no parameters");
        tree body= editor->the_buffer ();
        path paragraph= path_up (editor->the_path () / editor->the_buffer_path ());
        while (!is_nil (paragraph) && !is_func (subtree (body, path_up (paragraph)), DOCUMENT))
          paragraph= path_up (paragraph);
        *answer= {"OK", {{"body", document_node_to_value_v3 (editor->the_buffer ())},
          {"cursor", cursor}, {"epoch", actor->source_epoch ()}, {"view", view},
          {"paragraph_start", is_nil (paragraph) ? value () : encode_path (start (body, paragraph))},
          {"selection", editor->selection_active_any ()}}};
        return;
      }
      const char* payload= command == "set_cursor" ? "path" : "tree";
      if (!p.is_object () || p.size () != 4 || !p.contains (payload) || !p.contains ("epoch") ||
          !p.contains ("cursor") || !p.contains ("view"))
        throw std::invalid_argument ("Expected payload, epoch, cursor and view from context");
      if (p.at ("epoch") != actor->source_epoch () || p.at ("cursor") != cursor || p.at ("view") != view) {
        *answer= {"CONFLICT", "Document or cursor changed; invoke the command again"}; return;
      }
      if (command == "set_cursor") {
        const auto indices= p.at ("path").get<std::vector<int>> ();
        path destination;
        for (auto i= indices.rbegin (); i != indices.rend (); ++i)
          destination= path (*i, destination);
        if (is_nil (destination) || !is_inside (editor->the_buffer (), destination))
          throw std::invalid_argument ("Cursor path is outside the document body");
        editor->go_to_correct (editor->the_buffer_path () * destination);
        *answer= {"OK", {{"cursor", encode_path (editor->the_path () / editor->the_buffer_path ())}}};
        return;
      }
      if (actor->current_state ()->read_only) { *answer= {"DENIED", "Document is read-only"}; return; }
      if (editor->selection_active_any ()) { *answer= {"CONFLICT", "Clear the selection before inserting"}; return; }
      tree content= document_node_from_value_v3 (p.at ("tree"));
      if (athena::node::contains_metadata (content))
        throw std::invalid_argument ("Insertion requires new content without source metadata");
      editor->start_editing ();
      try { editor->insert_tree (content); }
      catch (...) { editor->end_editing (); throw; }
      editor->end_editing ();
      *answer= {"OK", {{"inserted", true}, {"saved", false}}};
    }
    catch (const std::exception& error) { *answer= {"ERROR", error.what ()}; }
    catch (const string& error) { *answer= {"ERROR", std::string (error.data (), N (error))}; }
  });
  if (!buffer_actor::invoke_on (id, actor_command_kind::run_native_continuation, view,
        ATHENA_NO_BLOB, ATHENA_NO_BLOB, nullptr, SCHEME_CAPABILITY_BUFFER, continuation)) {
    actor_continuation_registry::instance ().discard (continuation);
    return {"STALE", "Buffer has closed"};
  }
  return *answer;
}

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
            {"context", {{"parameters", value::object ()}}},
            {"set_cursor", {{"parameters", {{"path", "body-relative cursor path"},
              {"epoch", "context epoch"}, {"cursor", "context cursor"}, {"view", "context view"}}}}},
            {"insert_at_cursor", {{"parameters", {{"tree", "new document-model-v3 tree"},
              {"epoch", "context epoch"}, {"cursor", "context cursor"}, {"view", "context view"}}}}},
            {"inspect", {{"parameters", value::object ()}}}};
  }
  operation_result operate (const std::string& command, const value& p) const override {
    if (command == "context" || command == "insert_at_cursor" || command == "set_cursor")
      return editor_operation (id, command, p);
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
