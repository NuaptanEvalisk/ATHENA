// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#pragma once

#include "descriptor_values.hpp"
#include "field_layout.hpp"
#include "property_authority.hpp"
#include "inference.hpp"
#include "evaluation_origin.hpp"
#include "evaluation_inputs.hpp"
#include "node_metadata.hpp"
#pragma push_macro("make")
#undef make
#include <immer/map.hpp>
#pragma pop_macro("make")
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace athena::contracts {

struct contract_declaration;
struct declaration_chain;
struct declaration_source_state;
using declaration_event= std::shared_ptr<const contract_declaration>;

struct field_contract {
  int type= TYPE_ADHOC, accessible= ACCESSIBLE_NEVER;
  int writability= WRITABILITY_NORMAL, block= BLOCK_REQUIRE_BLOCK;
  node::frozen_tree environment {tree (WITH)};
  property_authority type_origin= property_authority::inferred;
  property_authority accessible_origin= property_authority::inferred;
  property_authority writability_origin= property_authority::inferred;
  property_authority block_origin= property_authority::inferred;
  property_authority environment_origin= property_authority::inferred;
  field_analysis analysis;

  bool operator == (const field_contract&) const;
};

// Stored only through const entries. The positional layout is the remaining
// facade projection; payloads are immutable even when a record is copied.
struct node_contract {
  field_layout layout;
  int type= TYPE_REGULAR, border= BORDER_YES, block= BLOCK_NO;
  bool with_like= false;
  int variable_type= VAR_MACRO;
  property_authority type_origin= property_authority::inferred;
  property_authority layout_origin= property_authority::inferred;
  property_authority border_origin= property_authority::inferred;
  property_authority block_origin= property_authority::inferred;
  property_authority with_origin= property_authority::inferred;
  property_authority variable_type_origin= property_authority::inferred;
  std::vector<field_contract> fields;
  std::map<std::string, node::frozen_tree, std::less<>> attributes;

  tree attribute (std::string_view name) const;
  bool operator == (const node_contract&) const;
};

using contract_entries= immer::map<tree_label, std::shared_ptr<const node_contract>>;
// Evidence owns only detached inputs/dependencies, never its output contracts.
// Keep provenance separate so new evidence does not dirty unchanged values.
struct analysis_evidence;
using analysis_evidence_ptr= std::shared_ptr<const analysis_evidence>;
using inference_origins= immer::map<tree_label, analysis_evidence_ptr>;
using inference_receipts= immer::map<analysis_evidence_ptr, std::size_t>;
enum class definition_kind { value, macro, variadic_macro };
struct definition_signature {
  definition_kind kind= definition_kind::value;
  std::vector<std::string> parameters;
};
definition_signature signature_of (const tree& expression);

struct definition_value {
  node::frozen_tree expression;
  definition_signature signature;
};
using definition_payload= std::shared_ptr<const definition_value>;

// Each actual assignment creates a binding. Only admitted replay may reuse
// that binding; equal values may share the detached payload independently.
struct evaluated_definition {
  tree_label target;
  std::string name;
  definition_payload value;
  package_inclusion_handle package;
};
using definition_binding= std::shared_ptr<const evaluated_definition>;
using definition_entries= immer::map<tree_label, definition_binding>;
definition_binding capture_definition (const string& name, const tree& value,
                                       definition_payload previous= {},
                                       package_inclusion_handle package= {});
using group_extensions= std::vector<std::pair<std::string, std::vector<std::string>>>;
struct contract_target {
  tree_label label;
  std::string name;
};

class contract_scope;
struct contract_input_revision {};
using contract_input_identity= std::shared_ptr<const contract_input_revision>;
class contract_snapshot {
  struct layer;
  std::shared_ptr<const layer> value;
  explicit contract_snapshot (std::shared_ptr<const layer> value);
  friend class contract_scope;
public:
  contract_snapshot ()= default;
  const node_contract& resolve (tree_label label) const;
  std::shared_ptr<const node_contract> find (tree_label label) const;
  const node_contract& declared (tree_label label) const;
  std::shared_ptr<const node_contract> find_declaration (tree_label label) const;
  std::shared_ptr<const node_contract> find_inference (tree_label label) const;
  std::shared_ptr<const declaration_chain> declaration_history (tree_label label) const;
  definition_binding definition (tree_label label) const;
  std::vector<tree_label> definition_labels () const;
  bool contains (tree_label label) const;
  std::vector<tree_label> labels () const;
  const group_extensions& groups () const;
  contract_input_identity input_identity () const;
};

// Owner-local writer over persistent native maps. Snapshots share unchanged
// trie branches; a local write copies only its branch, never the whole table.
class contract_scope {
  contract_snapshot parent;
  contract_entries entries;
  // Complete persistent maps: inheriting a scope shares them in constant time.
  contract_entries projections;
  inference_origins provenance;
  inference_receipts receipts;
  std::shared_ptr<const group_extensions> extensions;
  definition_entries definitions;
  std::shared_ptr<declaration_source_state> sources;
  bool inherit_definitions= true;
  contract_input_identity input_revision= std::make_shared<const contract_input_revision> ();
  std::optional<evaluation_input_snapshot> inference_inputs;
  void touch_inputs () { input_revision= std::make_shared<const contract_input_revision> (); }
  void materialize_declaration (tree_label label, std::shared_ptr<const node_contract> value);
  void discard_declarations (tree_label label);
  bool publish_declarations (tree_label label, std::shared_ptr<const declaration_chain> chain,
                             std::shared_ptr<const node_contract> value);
  void restore_declarations (const contract_scope& before,
                             const std::vector<contract_target>& targets);
  void refresh_declarations (tree_label label, bool attach_pending);
  void invalidate_inference (tree_label label);
  void set_inference (tree_label label, std::shared_ptr<const node_contract> value,
                      analysis_evidence_ptr evidence);
public:
  contract_scope ();
  explicit contract_scope (contract_snapshot parent);
  const node_contract& resolve (tree_label label) const;
  std::shared_ptr<const node_contract> find (tree_label label) const;
  const node_contract& declared (tree_label label) const;
  std::shared_ptr<const node_contract> find_declaration (tree_label label) const;
  std::shared_ptr<const node_contract> find_inference (tree_label label) const;
  analysis_evidence_ptr inference_evidence (tree_label label) const;
  const inference_receipts& inference_sources () const { return receipts; }
  contract_input_identity input_identity () const { return input_revision; }
  void admit_inference (evaluation_input_snapshot inputs) { inference_inputs= std::move (inputs); }
  std::shared_ptr<const declaration_chain> declaration_history (tree_label label) const;
  bool apply_declaration (const declaration_event& event, bool retain_invalid= false);
  bool withdraw_declaration (const declaration_event& event);
  definition_binding definition (tree_label label) const;
  bool bind (definition_binding value);
  void forget_definition (tree_label label);
  void reset_definitions (definition_entries baseline);
  void enter_local_targets (const std::vector<contract_target>& targets);
  bool contains (tree_label label) const;
  const contract_entries& locals () const { return entries; }
  const group_extensions& groups () const { return *extensions; }
  bool same_state (const contract_scope& other) const;
  // Changed declaration identities, excluding inferred projections.
  std::vector<tree_label> changed_declarations (const contract_scope& other) const;
  bool contracts_differ (const contract_scope& other) const;
  std::vector<tree_label> changed_definitions (const contract_scope& other) const;
  contract_snapshot snapshot () const;
  void replace (tree_label label, node_contract value);
  void replace_inference (contract_entries values, inference_origins origins,
                          inference_receipts sources);
  void clear_inference ();
  void clear_inference (tree_label label) { invalidate_inference (label); }
  std::vector<tree_label> restore_targets (const contract_scope& before,
                                         const std::vector<contract_target>& targets);

  template<class Update> void edit (tree_label label, Update update) {
    node_contract next= declared (label);
    update (next);
    replace (label, std::move (next));
  }
};

} // namespace athena::contracts
