/******************************************************************************
* MODULE     : document_node_model.hpp
* DESCRIPTION: Explicit detached source identities and typed property schemas
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef ATHENA_DOCUMENT_NODE_MODEL_HPP
#define ATHENA_DOCUMENT_NODE_MODEL_HPP

#include "Kernel/Types/node_metadata.hpp"
#include "drd_info.hpp"
#include "modification.hpp"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace athena::document_node {

using source_path= std::vector<int>;

enum class issue {
  invalid_body, invalid_id, duplicate_id, unsupported_ambiguous_role,
  invalid_role_declaration, allocation_failed, resource_limit,
  invalid_metadata, missing_property, unknown_property, wrong_property_type,
  invalid_property_value, unsafe_rich_text, invalid_path, protected_property,
  stale_properties
};

struct diagnostic {
  source_path where;
  // Nonempty for metadata, including nested dictionary/list/rich-text values.
  std::string property;
  issue code;
  std::string detail;
};

struct limits {
  std::size_t maximum_nodes= 1000000;
  std::size_t maximum_depth= 256;
};

enum class semantic_role { content, heading, enunciation, data };
enum class child_role { content, inline_content, body, data };

struct role_declaration {
  semantic_role role= semantic_role::content;
  std::string category;
  // Exactly one role per physical child, including non-content arguments.
  std::vector<child_role> children;
};

// The caller supplies already-known semantic contracts, not expanded/evaluated
// trees. For a registry legacy entry, return enunciation, registry.category(t),
// and [body] or [inline_content, body] according to its declared legacy layout.
// Standard headings/titles use athena_heading_level/athena_heading_title_tree;
// custom heading roles belong here. Footnotes/captions use these contracts or
// DRD child roles, not a separate exclusion list.
// Return nullopt for built-in handling or DRD; unknown/ambiguous roles are errors.
// No registry dependency or duplicated enunciation kind list lives here.
using role_resolver=
  std::function<std::optional<role_declaration> (const tree&)>;

enum class identity_role { body, paragraph, heading, enunciation };

struct identity_request {
  source_path where;
  identity_role role;
  std::string category;
};

// Called only for missing IDs, once per candidate, in source preorder. A pure
// allocator keyed by document identity + request path gives reproducible IDs.
// Return a canonical UUID. No random default, repair, collision retry or clock.
using identity_allocator= std::function<std::string (const identity_request&)>;

struct assigned_identity {
  identity_request request;
  std::string id;
};

struct identity_result {
  // Engaged only on complete success; input is never mutated or normalized.
  std::optional<tree> body;
  std::vector<assigned_identity> assigned;
  std::vector<diagnostic> diagnostics;
  bool ok () const { return body.has_value () && diagnostics.empty (); }
};

// Input is the extracted DOCUMENT body, NOT its file envelope or a BODY wrapper.
// Assign root/nested content DOCUMENTs, their paragraph children (atoms and
// CONCATs included), and semantic headings/enunciations even inside wrappers.
// Existing IDs everywhere, including excluded data and property rich text, are
// retained and reserved; duplicate source occurrences are rejected, not merged.
// DRD access modes, heuristic macro expansion, Scheme, editor state, and source
// properties do not execute. The caller supplies an owner-local DRD snapshot.
// Ambiguity/duplicates fail before allocator calls. Allocator side effects cannot
// be rolled back if a later allocation fails; prefer a pure allocator.
identity_result assign_detached_source_ids (
  const tree& body, drd_info drd, const role_resolver& roles,
  const identity_allocator& allocate, limits budget= {});

enum class property_type {
  string, boolean, integer, real, list, dictionary, reference, rich_text
};

struct property_rule {
  std::string name;
  property_type type;
  bool required;
  // When present, every direct list member must have this type; no coercion.
  std::optional<property_type> element_type= std::nullopt;
};

// kind:string, name:rich_text, numbered:bool are required. Optional attribution
// is a list of rich_text names, never a singleton rich_text. Optional year is
// text (including values such as "19XX"), never an integer or parsed date.
// target:reference, variant:string and legacy-tag:string are also optional.
// No author, attribution or year inference; stored values are not normalized.
const std::vector<property_rule>& enunciation_property_schema ();
bool is_namespaced_extension (const std::string& name);

// Read-only validation of this node's properties, not a migration. Canonical
// enunciation shape is validated, including unknown kinds. Other node types may
// carry namespaced extensions only. Unknown unnamespaced keys are diagnosed,
// never removed. All eight metadata types are allowed in extension values.
// Rich text is stored source data: unknown tags need no DRD/renderer contract.
// Validate structure, UTF-8, references and budgets, rejecting opaque runtime
// objects, raw data, uninitialized trees and cycles, not unfamiliar markup.
// No renderer, role callback, expansion or evaluation is invoked. Validation
// grants no execution capability; consumers must enforce that boundary when
// presenting stored content, including trees with executable-looking tags.
std::vector<diagnostic> validate_node_properties (
  const tree& source, limits budget= {});

struct property_edit {
  node::property::dictionary set;
  std::vector<std::string> remove;
  bool ensure_id= false;
};

struct prepared_property_edit {
  // Empty on a successful no-op. Apply only on the owner, before another edit.
  std::optional<modification> change;
  std::string id;
  std::vector<diagnostic> diagnostics;
  bool ok () const { return diagnostics.empty (); }
};

// Preflight only; neither mutates source nor deep-copies its body. The scope
// must be the owning document for live edits, so rich-text IDs cannot collide
// with body IDs. Metadata is cloned, untouched children remain owner-local.
// UUIDs can only be assigned when absent. Reserved artifact bindings cannot be
// edited here. Apply the returned modification through the observer/history
// path, never node::set on a live source. This is not a cross-actor API.
prepared_property_edit prepare_property_edit (
  const tree& scope, const source_path& where, const property_edit& edit,
  limits budget= {});

// Detached, body-free draft for a property editor. The caller must separately
// retain an owner-checked node lease; a path or this header is not an identity.
tree property_header (const tree& source);
// Compare-and-edit against the captured header. Body edits are allowed, but
// changed properties/tag or a modified UUID reject the entire draft. Unknown
// unchanged extensions and reserved bindings are preserved without rewriting.
prepared_property_edit prepare_property_replacement (
  const tree& scope, const source_path& where, const tree& expected_header,
  const tree& desired_header, limits budget= {});

} // namespace athena::document_node

#endif // ATHENA_DOCUMENT_NODE_MODEL_HPP
