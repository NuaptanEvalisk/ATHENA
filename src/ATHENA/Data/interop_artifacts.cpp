/******************************************************************************
* MODULE     : interop_artifacts.cpp
* DESCRIPTION: Indexed mathematical artifacts in the AUDM resolver graph
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#include "interop_artifacts.hpp"
#include "interop_vault_resource.hpp"
#include "../Interop/traversal.hpp"
#include "artifacts.hpp"

namespace athena::interop {
namespace {
void require_current (const vault_context_handle& context) {
  if (!vault_context_is_current (context))
    throw std::runtime_error ("STALE: artifact vault has closed or been replaced");
}

value describe (const AthenaArtifactRecord& record) {
  return {{"uuid", record.uuid}, {"resource_type", "artifact"},
    {"type", record.type}, {"origin", record.origin},
    {"name", record.semantic_names.empty () ? record.display_text : record.semantic_names.front ()},
    {"names", record.semantic_names}, {"display_text", record.display_text},
    {"relative_path", record.relative_path}, {"document_order", record.document_order},
    {"content_uuid", record.content_uuid}, {"proof_uuid", record.proof_uuid},
    {"anchor_stem", record.anchor_stem}};
}

value binary (const std::string& bytes) {
  return value::binary (std::vector<std::uint8_t> (bytes.begin (), bytes.end ()));
}

class artifact_resource final: public vault_resource {
  const vault_context_handle context;
  const AthenaArtifactRecord selected;
public:
  artifact_resource (vault_context_handle context, AthenaArtifactRecord record):
    context (std::move (context)), selected (std::move (record)) {}
  vault_context_handle captured_vault () const override { return context; }
  std::string type () const override { return "artifact"; }
  std::string identity () const override {
    return context->incarnation + ":artifact:" + selected.uuid;
  }
  value properties () const override {
    require_current (context);
    // Predicate evaluation uses one index snapshot, not one SQL open per node.
    return describe (selected);
  }
  value inspect () const override {
    return {{"inspect", {{"parameters", value::object ()}}},
      {"get", {{"parameters", value::object ()},
               {"description", "Read the current artifact record by UUID"}}}};
  }
  operation_result operate (const std::string& command, const value& parameters) const override {
    if (!parameters.is_object () || !parameters.empty ())
      return {"INVALID_ARGUMENT", "This command takes no parameters"};
    if (!vault_context_is_current (context)) return {"STALE", "Artifact vault has closed or been replaced"};
    if (command == "inspect") return {"OK", inspect ()};
    if (command != "get") return {"UNKNOWN_COMMAND", command};
    AthenaArtifactRecord record;
    std::string error;
    bool found= false;
    if (!athena_artifact_query_uuid (context->root, selected.uuid, record, found, error, true))
      return {"ERROR", error};
    if (!vault_context_is_current (context)) return {"STALE", "Artifact vault has closed or been replaced"};
    if (!found) return {"NOT_FOUND", "Artifact no longer exists"};
    auto result= describe (record);
    result["semantic_name_trees"]= value::array ();
    for (const auto& name: record.semantic_name_trees)
      result["semantic_name_trees"].push_back (name.empty () ? value () : binary (name));
    result["keyword_tree"]= binary (record.keyword_tree);
    result["keyword_occurrence"]= record.keyword_occurrence;
    result["paragraph_offsets"]= record.paragraph_offsets;
    result["identity_decision"]= record.identity_decision;
    result["identity_evidence"]= record.identity_evidence;
    return {"OK", std::move (result)};
  }
};

class artifact_resolver_rep final: public resolver {
public:
  resolver_outcome resolve (const resolution_request& req, resolution_output& out) const override {
    if (!req.basepoint) return resolver_outcome::irrelevant;
    auto state= std::dynamic_pointer_cast<const traversal> (req.state);
    if (state && state->step == traversal::phase::candidate) {
      if (state->domain != "artifact" ||
          !std::dynamic_pointer_cast<const artifact_resource> (req.basepoint->accessor))
        return resolver_outcome::irrelevant;
      return visit_candidate (req, *state, out, false);
    }
    if (state && !state->domain.empty () && state->domain != "artifact")
      return resolver_outcome::irrelevant;
    auto vault= std::dynamic_pointer_cast<const vault_resource> (req.basepoint->accessor);
    if (!vault || vault->type () != "vault") return resolver_outcome::irrelevant;
    std::size_t offset= req.offset;
    const auto& selector= req.selectors.at (offset);
    const bool marker= !state && selector.type == selector::kind::name && selector.name == "artifacts";
    if (!state && !marker && (selector.type == selector::kind::name ||
                             selector.type == selector::kind::default_resource))
      return resolver_outcome::irrelevant;
    if (marker)
      if (++offset == req.selectors.size ()) return resolver_outcome::miss;
    const auto& target= req.selectors.at (offset);
    if (target.type == selector::kind::default_resource) return resolver_outcome::miss;
    auto budget= state ? state->budget : std::make_shared<traversal_budget> (target.limits);
    const auto depth= state ? state->depth + 1 : 1;
    if ((budget->limits.max_depth && depth > *budget->limits.max_depth) ||
        traversal_stopped (req, *budget, out)) return resolver_outcome::miss;
    const auto context= vault->captured_vault ();
    require_current (context);
    std::vector<AthenaArtifactRecord> records;
    std::string error;
    if (!athena_artifacts_query (context->root, records, error, true))
      throw std::runtime_error ("Could not read artifact index: " + error);
    require_current (context);
    for (auto& record: records) {
      if (traversal_stopped (req, *budget, out)) break;
      publish_candidate (out, std::make_shared<artifact_resource> (context, std::move (record)),
                         offset, budget, depth, "artifact");
    }
    return out.branches.empty () ? resolver_outcome::miss : resolver_outcome::resolved;
  }
};
} // namespace

std::shared_ptr<const resolver> artifacts_resolver () {
  return std::make_shared<artifact_resolver_rep> ();
}
} // namespace athena::interop
