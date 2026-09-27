/******************************************************************************
* MODULE     : document_node_model.cpp
* DESCRIPTION: Explicit detached source identities and typed property schemas
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "ATHENA/Data/document_node_model.hpp"
#include "ATHENA/Data/heading_word_count.hpp"
#include "ATHENA/Data/document_node_copy.hpp"
#include "ATHENA/Data/enunciation_model.hpp"
#include "unicode_text.hpp"

#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace athena::document_node {
namespace {

std::string bytes (const string& value) {
  return std::string (value.data (), N(value));
}

bool tagged (const tree& source, const char* tag) {
  return is_compound (source) && as_string (L(source)) == tag;
}

bool canonical_enunciation (const tree& source) {
  return tagged (source, "enunciation");
}

// These are source-language boundaries, not semantic enunciation categories.
bool excluded (const tree& source) {
  if (!is_compound (source)) return false;
  switch (L(source)) {
  case ASSIGN: case PROVIDE: case MACRO: case XMACRO: case DRD_PROPS:
  case QUOTE: case QUASI: case QUASIQUOTE: case UNQUOTE: case VAR_UNQUOTE:
  case EVAL: case EVAL_ARGS: case EXTERN: case COMPOUND: case MAP_ARGS:
  case IF: case VAR_IF: case CASE: case WHILE: case FOR_EACH:
  case USE_PACKAGE: case USE_MODULE: case INCLUDE: case VAR_INCLUDE:
  case NEW_THEME: case COPY_THEME: case APPLY_THEME: case SELECT_THEME:
  case SCRIPT: case ACTION: case WRITE: case GET_ATTACHMENT:
  case INACTIVE: case VAR_INACTIVE: case REWRITE_INACTIVE:
  case INLINE_TAG: case OPEN_TAG: case MIDDLE_TAG: case CLOSE_TAG:
  case RAW_DATA: case COLLECTION: case ASSOCIATE: case TUPLE:
    return true;
  default: break;
  }
  const auto tag= bytes (as_string (L(source)));
  static const std::set<std::string> tags {
    "style", "initial", "references", "auxiliary", "hide-preamble",
    "show-preamble", "preamble", "verbatim", "code", "code*", "code-block",
    "session", "input", "output", "script-input", "script-output"
  };
  return tags.count (tag) != 0;
}

struct failure {
  diagnostic value;
};

[[noreturn]] void fail (const source_path& where, issue code,
                       const std::string& detail, const std::string& property= "") {
  throw failure {{where, property, code, detail}};
}

struct traversal_budget {
  limits limit;
  std::size_t nodes= 0;
  void enter (std::size_t depth, const source_path& where,
              const std::string& property= "") {
    if (depth > limit.maximum_depth || ++nodes > limit.maximum_nodes)
      fail (where, issue::resource_limit, "Source traversal budget exceeded", property);
  }
};

// Audit every identity without interpreting data as document content. Shared
// source occurrences count separately, even if they share a tree_rep pointer.
class identity_audit {
  traversal_budget budget;
  std::set<const tree_rep*> active;
  std::set<std::string> identities;
  std::map<source_path, std::set<std::string>> owners;
  source_path property_owner;

  void value (const node::property& property, source_path& where,
              const std::string& key, std::size_t depth) {
    budget.enter (depth, where, key);
    if (const auto* list= std::get_if<node::property::list> (&property.data)) {
      for (std::size_t i= 0; i < list->size (); ++i)
        value ((*list)[i], where, key + "[" + std::to_string (i) + "]", depth + 1);
    }
    else if (const auto* dict= std::get_if<node::property::dictionary> (&property.data)) {
      for (const auto& entry: *dict)
        value (entry.second, where, key + "/" + entry.first, depth + 1);
    }
    else if (const auto* text= std::get_if<node::rich_text> (&property.data))
      visit (text->content, where, key + "/rich-text", depth + 1);
  }

  void visit (const tree& source, source_path& where, const std::string& key,
              std::size_t depth) {
    budget.enter (depth, where, key);
    if (is_generic (source))
      fail (where, issue::invalid_metadata, "Opaque trees are not detached source", key);
    if (!active.insert (inside (source)).second)
      fail (where, issue::invalid_metadata, "Cyclic source tree", key);
    if (const auto* metadata= node::get (source)) {
      if (!metadata->id.empty ()) {
        if (!node::valid_id (metadata->id))
          fail (where, issue::invalid_id, metadata->id, key);
        if (!identities.insert (metadata->id).second)
          fail (where, issue::duplicate_id, metadata->id, key);
        owners[key.empty () ? where : property_owner].insert (metadata->id);
      }
      const auto saved_owner= property_owner;
      if (key.empty ()) property_owner= where;
      for (const auto& entry: metadata->properties)
        value (entry.second, where, key + "/" + entry.first, depth + 1);
      property_owner= saved_owner;
    }
    if (is_compound (source)) {
      for (int i= 0; i < N(source); ++i) {
        where.push_back (i);
        visit (source[i], where, key, depth + 1);
        where.pop_back ();
      }
    }
    active.erase (inside (source));
  }

public:
  explicit identity_audit (limits limit): budget {limit} {}
  std::set<std::string> inspect (const tree& source,
      std::map<source_path, std::set<std::string>>* collected= nullptr) {
    source_path where;
    visit (source, where, "", 0);
    if (collected) *collected= std::move (owners);
    return std::move (identities);
  }
};

role_declaration roles_for (const tree& source, drd_info drd,
                            const role_resolver& resolve, const source_path& where) {
  if (excluded (source)) return {semantic_role::data, "", {}};
  if (is_func (source, WITH) || is_func (source, STYLE_WITH) ||
      is_func (source, VAR_STYLE_WITH)) {
    if (N(source) < 1 || (N(source) & 1) == 0)
      fail (where, issue::unsupported_ambiguous_role, "Malformed source bindings");
    const tree* mode= nullptr;
    for (int i= 0; i + 1 < N(source); i+= 2) {
      if (!is_atomic (source[i]))
        fail (where, issue::unsupported_ambiguous_role, "Computed binding name cannot be classified without evaluation");
      if (source[i] == "mode") mode= &source[i + 1];
    }
    if (mode) {
      if (!is_atomic (*mode))
        fail (where, issue::unsupported_ambiguous_role, "Computed mode cannot be classified without evaluation");
      if (*mode == "prog" || *mode == "src")
        return {semantic_role::data, "", {}};
      if (*mode != "text" && *mode != "math")
        fail (where, issue::unsupported_ambiguous_role, "Unknown source mode");
    }
  }
  std::optional<role_declaration> supplied;
  if (resolve) supplied= resolve (source);
  if (supplied && supplied->role != semantic_role::data &&
      supplied->children.size () != static_cast<std::size_t> (N(source)))
    fail (where, issue::invalid_role_declaration, "Expected one role per physical child");

  if (canonical_enunciation (source)) {
    if (N(source) != 1 || !is_func (source[0], DOCUMENT))
      fail (where, issue::invalid_role_declaration, "Canonical enunciation requires one DOCUMENT body");
    if (supplied && (supplied->role != semantic_role::enunciation ||
                     supplied->children != std::vector<child_role> {child_role::body}))
      fail (where, issue::invalid_role_declaration, "Conflicting canonical enunciation roles");
    return {semantic_role::enunciation, supplied ? supplied->category : "",
            {child_role::body}};
  }
  if (supplied) return *supplied;
  if (athena_heading_level (source) > 0 || athena_heading_title_tree (source)) {
    if (N(source) > 1)
      fail (where, issue::unsupported_ambiguous_role, "Heading arity needs an explicit role contract");
    return {semantic_role::heading, "heading",
            std::vector<child_role> (N(source), child_role::inline_content)};
  }

  if (!drd->contains (as_string (L(source))))
    fail (where, issue::unsupported_ambiguous_role, "No DRD or explicit body-role contract for " +
          bytes (as_string (L(source))));
  if (!drd->correct_arity (L(source), N(source)))
    fail (where, issue::unsupported_ambiguous_role, "Arity does not match DRD");

  // Read physical child descriptors directly: is_accessible_child depends on
  // live source/hidden access mode and must not control migration semantics.
  tag_info info= drd->info[L(source)];
  if (info->pi.type == TYPE_CODE || info->pi.type == TYPE_RAW)
    return {semantic_role::data, "", {}};
  role_declaration result;
  for (int i= 0; i < N(source); ++i) {
    const int index= info->get_index (i, N(source));
    if (index < 0 || index >= N(info->ci))
      fail (where, issue::unsupported_ambiguous_role, "DRD has no physical child role");
    const child_info& child= info->ci[index];
    if (child.type == TYPE_ADHOC || child.type == TYPE_UNKNOWN || child.type == TYPE_ERROR)
      fail (where, issue::unsupported_ambiguous_role, "DRD child role is ambiguous");
    if (child.type != TYPE_REGULAR || child.accessible == ACCESSIBLE_NEVER) {
      result.children.push_back (child_role::data);
      continue;
    }
    const tree mode= drd_env_read (drd->get_env (L(source), index), "mode", "");
    if (mode == "prog" || mode == "src") {
      result.children.push_back (child_role::data);
      continue;
    }
    if (child.block == BLOCK_REQUIRE_INLINE)
      result.children.push_back (child_role::inline_content);
    // Legacy child_info initializes block to zero (BLOCK_REQUIRE_BLOCK), even
    // for CONCAT. It is not evidence of a declared paragraph/body boundary.
    // Nested DOCUMENTs are recognized structurally; other body slots need a
    // named DRD contract or the caller's explicit role declaration.
    else if (drd->get_child_name (L(source), index) == "body")
      result.children.push_back (child_role::body);
    else result.children.push_back (child_role::content);
  }
  return result;
}

class identity_planner {
  drd_info drd;
  const role_resolver& resolve;
  traversal_budget budget;
  source_path where;
  source_path scope;
  std::vector<identity_request> candidates;

  void visit (const tree& source, child_role context, bool paragraph,
              std::size_t depth) {
    budget.enter (depth, where);
    if (context == child_role::data || excluded (source)) return;
    if (is_atomic (source)) {
      if (paragraph || context == child_role::body)
        candidates.push_back ({where, identity_role::paragraph, ""});
      return;
    }
    if (is_func (source, DOCUMENT)) {
      if (context == child_role::inline_content)
        fail (where, issue::unsupported_ambiguous_role, "DOCUMENT in an inline-only slot");
      candidates.push_back ({where, identity_role::body, ""});
      const int begin= depth < scope.size () ? scope[depth] : 0;
      const int end= depth < scope.size () ? begin + 1 : N(source);
      for (int i= begin; i < end; ++i) {
        where.push_back (i);
        visit (source[i], child_role::content, true, depth + 1);
        where.pop_back ();
      }
      return;
    }
    const auto contract= roles_for (source, drd, resolve, where);
    if (contract.role == semantic_role::data) return;
    if (contract.role == semantic_role::enunciation)
      candidates.push_back ({where, identity_role::enunciation, contract.category});
    else if (contract.role == semantic_role::heading)
      candidates.push_back ({where, identity_role::heading, contract.category});
    else if (paragraph || context == child_role::body)
      candidates.push_back ({where, identity_role::paragraph, contract.category});
    const int begin= depth < scope.size () ? scope[depth] : 0;
    const int end= depth < scope.size () ? begin + 1 : N(source);
    for (int i= begin; i < end; ++i) {
      where.push_back (i);
      auto child= contract.children[i];
      if (context == child_role::inline_content && child == child_role::content)
        child= child_role::inline_content;
      visit (source[i], child, false, depth + 1);
      where.pop_back ();
    }
  }

public:
  identity_planner (drd_info value, const role_resolver& resolver, limits limit):
    drd (value), resolve (resolver), budget {limit} {}
  std::vector<identity_request> inspect (const tree& body, source_path selected= {}) {
    scope= std::move (selected);
    const tree* target= &body;
    for (int index: scope) {
      if (!is_compound (*target) || index < 0 || index >= N(*target))
        fail (scope, issue::invalid_path, "Identity planning scope is not in the source");
      target= &(*target)[index];
    }
    visit (body, child_role::body, false, 0);
    return std::move (candidates);
  }
};

template<typename T> T& at (T& root, const source_path& where) {
  T* current= &root;
  for (int index: where) current= &(*current)[index];
  return *current;
}

property_type type_of (const node::property& value) {
  return std::visit ([] (const auto& item) {
    using T= std::decay_t<decltype (item)>;
    if constexpr (std::is_same_v<T, std::string>) return property_type::string;
    else if constexpr (std::is_same_v<T, bool>) return property_type::boolean;
    else if constexpr (std::is_same_v<T, std::int64_t>) return property_type::integer;
    else if constexpr (std::is_same_v<T, double>) return property_type::real;
    else if constexpr (std::is_same_v<T, node::property::list>) return property_type::list;
    else if constexpr (std::is_same_v<T, node::property::dictionary>) return property_type::dictionary;
    else if constexpr (std::is_same_v<T, node::reference>) return property_type::reference;
    else return property_type::rich_text;
  }, value.data);
}

class property_validator {
  traversal_budget budget;
  std::set<const tree_rep*> active;
  std::vector<diagnostic> errors;

  void error (issue code, const std::string& key, const std::string& detail) {
    errors.push_back ({{}, key, code, detail});
  }

  void text (const std::string& value, const std::string& key) {
    if (!athena::text::valid_utf8 (value))
      error (issue::invalid_property_value, key, "Expected UTF-8 text");
  }

  void rich_text (const tree& source, const std::string& key, std::size_t depth) {
    budget.enter (depth, {}, key);
    if (is_generic (source) || L(source) == UNINIT || L(source) == RAW_DATA) {
      error (issue::unsafe_rich_text, key, "Opaque, raw or uninitialized tree is not rich text");
      return;
    }
    if (!active.insert (inside (source)).second) {
      error (issue::unsafe_rich_text, key, "Cyclic rich text");
      return;
    }
    if (const auto* metadata= node::get (source)) {
      if (!metadata->id.empty () && !node::valid_id (metadata->id))
        error (issue::invalid_id, key, "Invalid rich-text node ID");
      for (const auto& entry: metadata->properties) {
        text (entry.first, key);
        if (entry.first.empty ())
          error (issue::invalid_property_value, key, "Empty property key");
        value (entry.second, key + "/properties/" + entry.first, depth + 1);
      }
    }
    if (is_atomic (source)) text (bytes (source->label), key);
    else {
      text (bytes (as_string (L(source))), key);
      // Stored markup is inert data, not a request to resolve or execute its tag.
      for (int i= 0; i < N(source); ++i)
        rich_text (source[i], key + "/" + std::to_string (i), depth + 1);
    }
    active.erase (inside (source));
  }

  void value (const node::property& property, const std::string& key,
              std::size_t depth) {
    budget.enter (depth, {}, key);
    if (const auto* s= std::get_if<std::string> (&property.data)) text (*s, key);
    else if (const auto* n= std::get_if<double> (&property.data)) {
      if (!std::isfinite (*n)) error (issue::invalid_property_value, key, "Expected finite number");
    }
    else if (const auto* ref= std::get_if<node::reference> (&property.data)) {
      if (!node::valid_id (ref->id)) error (issue::invalid_property_value, key, "Expected UUID reference");
    }
    else if (const auto* list= std::get_if<node::property::list> (&property.data)) {
      for (std::size_t i= 0; i < list->size (); ++i)
        value ((*list)[i], key + "[" + std::to_string (i) + "]", depth + 1);
    }
    else if (const auto* dict= std::get_if<node::property::dictionary> (&property.data)) {
      for (const auto& entry: *dict) {
        text (entry.first, key);
        if (entry.first.empty ()) error (issue::invalid_property_value, key, "Empty dictionary key");
        value (entry.second, key + "/" + entry.first, depth + 1);
      }
    }
    else if (const auto* rich= std::get_if<node::rich_text> (&property.data))
      rich_text (rich->content, key, depth + 1);
  }

public:
  explicit property_validator (limits limit): budget {limit} {}

  std::vector<diagnostic> inspect (const tree& source) {
    const bool enunciation= canonical_enunciation (source);
    if (enunciation && (N(source) != 1 || !is_func (source[0], DOCUMENT)))
      error (issue::invalid_body, "", "Canonical enunciation requires one DOCUMENT body");
    const auto* metadata= node::get (source);
    if (metadata && !metadata->id.empty () && !node::valid_id (metadata->id))
      error (issue::invalid_id, "", "Invalid node ID");
    const auto& schema= enunciation_property_schema ();
    if (enunciation) {
      for (const auto& rule: schema)
        if (rule.required && (!metadata || metadata->properties.count (rule.name) == 0))
          error (issue::missing_property, rule.name, "Required enunciation property");
    }
    if (metadata) {
      for (const auto& entry: metadata->properties) {
        const auto rule= std::find_if (schema.begin (), schema.end (), [&] (const property_rule& r) {
          return enunciation && r.name == entry.first;
        });
        if (rule != schema.end ()) {
          if (type_of (entry.second) != rule->type)
            error (issue::wrong_property_type, entry.first, "Does not match enunciation property schema");
          if (rule->element_type) {
            if (const auto* list= std::get_if<node::property::list> (&entry.second.data))
              for (std::size_t i= 0; i < list->size (); ++i)
                if (type_of ((*list)[i]) != *rule->element_type)
                  error (issue::wrong_property_type,
                         entry.first + "[" + std::to_string (i) + "]",
                         "List member does not match property element schema");
          }
          if (entry.first == "kind") {
            const auto* kind= std::get_if<std::string> (&entry.second.data);
            if (kind && (kind->empty () || kind->find ('\0') != std::string::npos))
              error (issue::invalid_property_value, entry.first, "Expected nonempty kind");
          }
        }
        else if (entry.first == artifact_bindings_property) {
          const auto* bindings= std::get_if<node::property::dictionary> (&entry.second.data);
          if (!bindings)
            error (issue::wrong_property_type, entry.first, "Artifact bindings require a role-to-UUID dictionary");
          else for (const auto& binding: *bindings) {
            const auto* id= std::get_if<std::string> (&binding.second.data);
            if (binding.first.empty () || !id || !node::valid_id (*id))
              error (issue::invalid_property_value, entry.first + "/" + binding.first,
                     "Artifact binding requires a nonempty role and canonical UUID");
          }
        }
        else if (!is_namespaced_extension (entry.first))
          error (issue::unknown_property, entry.first, "Unknown property must be namespaced");
        value (entry.second, entry.first, 0);
      }
    }
    return std::move (errors);
  }
};

} // namespace

std::optional<role_declaration> standard_source_role (const tree& source) {
  const auto& registry= enunciation::standard_registry ();
  const int body= registry.body_index (source);
  if (body < 0) return {};
  std::vector<child_role> children (N(source), child_role::inline_content);
  children[body]= child_role::body;
  return role_declaration {semantic_role::enunciation, registry.category (source), std::move (children)};
}

identity_result assign_detached_source_ids (
    const tree& body, drd_info drd, const role_resolver& roles,
    const identity_allocator& allocate, limits budget) {
  identity_result result;
  try {
    if (!is_func (body, DOCUMENT))
      fail ({}, issue::invalid_body, "Expected extracted DOCUMENT body");
    for (int i= 0; i < N(body); ++i)
      if (tagged (body[i], "body") || tagged (body[i], "TeXmacs"))
        fail ({i}, issue::invalid_body, "Extract the body from the file envelope first");
    auto reserved= identity_audit (budget).inspect (body);
    const auto candidates= identity_planner (drd, roles, budget).inspect (body);
    std::vector<assigned_identity> pending;
    for (const auto& request: candidates) {
      if (!node::id (at (body, request.where)).empty ()) continue;
      if (!allocate) fail (request.where, issue::allocation_failed, "Missing caller-provided allocator");
      std::string id;
      try { id= allocate (request); }
      catch (const std::exception& error) {
        fail (request.where, issue::allocation_failed, error.what ());
      }
      if (!node::valid_id (id)) fail (request.where, issue::invalid_id, "Allocator returned invalid UUID");
      if (!reserved.insert (id).second) fail (request.where, issue::duplicate_id, id);
      pending.push_back ({request, std::move (id)});
    }
    tree output= copy (body);
    for (const auto& assignment: pending) {
      tree& target= at (output, assignment.request.where);
      node::metadata metadata;
      if (const auto* existing= node::get (target)) metadata= *existing;
      metadata.id= assignment.id;
      node::set (target, metadata);
    }
    result.body= std::move (output);
    result.assigned= std::move (pending);
  }
  catch (const failure& problem) { result.diagnostics.push_back (problem.value); }
  catch (const std::exception& error) {
    result.diagnostics.push_back ({{}, "", issue::invalid_metadata, error.what ()});
  }
  return result;
}

namespace {
bool within (const source_path& parent, const source_path& child) {
  return parent.size () <= child.size () && std::equal (parent.begin (), parent.end (), child.begin ());
}
path native_path (const source_path& value) {
  path result;
  for (auto i= value.rbegin (); i != value.rend (); ++i) result= path (*i, result);
  return result;
}
}

std::vector<diagnostic> source_identity_state::initialize (const tree& body, limits budget) {
  ready= false;
  try {
    if (!is_func (body, DOCUMENT)) fail ({}, issue::invalid_body, "Expected extracted DOCUMENT body");
    std::map<source_path, std::set<std::string>> next;
    identity_audit (budget).inspect (body, &next);
    std::map<std::string, source_path> indexed;
    for (const auto& entry: next)
      for (const auto& id: entry.second) indexed.emplace (id, entry.first);
    owners= std::move (next); locations= std::move (indexed);
    dirty= source_path {}; ++revision; ready= true;
  }
  catch (const failure& problem) { return {problem.value}; }
  catch (const std::exception& error) { return {{{}, "", issue::invalid_metadata, error.what ()}}; }
  return {};
}

void source_identity_state::observe (modification mod) {
  if (!ready || applying || mod->k == MOD_SET_CURSOR) return;
  source_path affected;
  for (path p= root (mod); !is_nil (p); p= p->next) affected.push_back (p->item);
  if (!dirty) dirty= std::move (affected);
  else {
    std::size_t common= 0;
    while (common < dirty->size () && common < affected.size () && (*dirty)[common] == affected[common])
      ++common;
    dirty->resize (common);
  }
  ++revision;
}

identity_edit_plan source_identity_state::prepare (const tree& body, drd_info drd,
    const role_resolver& roles, const identity_allocator& allocate, limits budget) const {
  identity_edit_plan plan;
  plan.revision= revision;
  try {
    if (!ready) fail ({}, issue::invalid_metadata, "Source identity index is not initialized");
    if (!is_func (body, DOCUMENT)) fail ({}, issue::invalid_body, "Expected extracted DOCUMENT body");
    if (!dirty) return plan;
    plan.scope= *dirty;
    // Classify the ancestry first, validating the selected path before access.
    const auto candidates= identity_planner (drd, roles, budget).inspect (body, *dirty);
    std::map<source_path, std::set<std::string>> local;
    auto reserved= identity_audit (budget).inspect (at (body, *dirty), &local);
    for (auto& entry: local) {
      source_path owner= *dirty;
      owner.insert (owner.end (), entry.first.begin (), entry.first.end ());
      for (const auto& id: entry.second) {
        auto existing= locations.find (id);
        if (existing != locations.end () && !within (*dirty, existing->second))
          fail (owner, issue::duplicate_id, id);
      }
      plan.owners.emplace (std::move (owner), std::move (entry.second));
    }
    for (const auto& candidate: candidates) {
      const tree& target= at (body, candidate.where);
      if (!node::id (target).empty ()) continue;
      if (!allocate) fail (candidate.where, issue::allocation_failed, "Missing caller-provided allocator");
      std::string id;
      try { id= allocate (candidate); }
      catch (const std::exception& error) { fail (candidate.where, issue::allocation_failed, error.what ()); }
      if (!node::valid_id (id)) fail (candidate.where, issue::invalid_id, "Allocator returned invalid UUID");
      if (locations.count (id) || !reserved.insert (id).second)
        fail (candidate.where, issue::duplicate_id, id);
      tree header= node_header (target);
      node::metadata metadata;
      if (const auto* previous= node::get (header)) metadata= *previous;
      metadata.id= id;
      node::set (header, metadata);
      plan.changes.push_back (mod_set_metadata (native_path (candidate.where), header));
      if (!within (*dirty, candidate.where)) {
        auto previous= owners.find (candidate.where);
        if (previous != owners.end ()) plan.owners[candidate.where]= previous->second;
      }
      plan.owners[candidate.where].insert (id);
      plan.assigned.push_back ({candidate, std::move (id)});
    }
  }
  catch (const failure& problem) { plan.diagnostics.push_back (problem.value); }
  catch (const std::exception& error) {
    plan.diagnostics.push_back ({{}, "", issue::invalid_metadata, error.what ()});
  }
  if (!plan.ok ()) { plan.changes.clear (); plan.assigned.clear (); plan.owners.clear (); }
  return plan;
}

void source_identity_state::apply (tree& body, const identity_edit_plan& plan) {
  if (!ready || !plan.ok () || plan.revision != revision ||
      (dirty && plan.scope != *dirty))
    throw std::logic_error ("Stale or invalid source identity plan");
  if (!dirty) return;
  applying= true;
  try {
    for (const auto& change: plan.changes) ::apply (body, change);
    auto i= owners.lower_bound (*dirty);
    while (i != owners.end () && within (*dirty, i->first)) {
      for (const auto& id: i->second) locations.erase (id);
      i= owners.erase (i);
    }
    for (const auto& entry: plan.owners) {
      owners[entry.first]= entry.second;
      for (const auto& id: entry.second) locations[id]= entry.first;
    }
    dirty.reset (); ++revision; applying= false;
  }
  catch (...) { applying= false; ready= false; throw; }
}

std::vector<diagnostic> source_identity_state::cancelled (const tree& body, limits budget) {
  auto errors= initialize (body, budget);
  if (errors.empty ()) dirty.reset ();
  return errors;
}

const std::vector<property_rule>& enunciation_property_schema () {
  static const std::vector<property_rule> schema {
    {"kind", property_type::string, true},
    {"name", property_type::rich_text, true},
    {"attribution", property_type::list, false, property_type::rich_text},
    {"year", property_type::string, false},
    {"numbered", property_type::boolean, true},
    {"target", property_type::reference, false},
    {"variant", property_type::string, false},
    {"legacy-tag", property_type::string, false}
  };
  return schema;
}

bool is_namespaced_extension (const std::string& name) {
  const auto separator= name.find (':');
  if (separator == std::string::npos || separator == 0 || separator + 1 == name.size () ||
      name.find (':', separator + 1) != std::string::npos) return false;
  for (std::size_t i= 0; i < name.size (); ++i) {
    if (i == separator) continue;
    const unsigned char c= name[i];
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
          (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.')) return false;
  }
  return true;
}

std::vector<diagnostic> validate_node_properties (
    const tree& source, limits budget) {
  try { return property_validator (budget).inspect (source); }
  catch (const failure& problem) { return {problem.value}; }
  catch (const std::exception& error) {
    return {{{}, "", issue::invalid_metadata, error.what ()}};
  }
}

prepared_property_edit prepare_property_edit (
    const tree& scope, const source_path& where, const property_edit& edit,
    limits budget) {
  prepared_property_edit result;
  try {
    if (where.size () > budget.maximum_depth)
      fail (where, issue::resource_limit, "Property edit path exceeds depth budget");
    tree target= scope;
    path location;
    for (int index: where) {
      if (!is_compound (target) || index < 0 || index >= N(target))
        fail (where, issue::invalid_path, "Property edit target no longer exists");
      target= target[index];
      location= location * path (index);
    }
    if (is_generic (target) || L(target) == UNINIT)
      fail (where, issue::invalid_metadata, "Cannot edit properties of opaque or uninitialized data");
    node::metadata metadata;
    if (const auto* old= node::get (target)) metadata= *old;
    const auto writable= [&] (const std::string& key) {
      if (key == "id" || key == "uuid" || key == artifact_bindings_property)
        fail (where, issue::protected_property, "Identity is not an editable property", key);
    };
    std::set<std::string> removed;
    for (const auto& key: edit.remove) {
      writable (key);
      if (key.empty () || !removed.insert (key).second || edit.set.count (key))
        fail (where, issue::invalid_property_value, "Ambiguous property removal", key);
      metadata.properties.erase (key);
    }
    for (const auto& entry: edit.set) {
      writable (entry.first);
      metadata.properties.insert_or_assign (entry.first, entry.second);
    }
    if (edit.ensure_id && metadata.id.empty ()) metadata.id= node::new_id ();

    // Preserve physical child indices and avoid copying the body for metadata
    // validation. node::set owns copies of any incoming rich property trees.
    tree candidate= is_atomic (target) ? tree (target->label) : tree (L(target), N(target));
    if (is_compound (target))
      for (int i= 0; i < N(target); ++i) candidate[i]= target[i];
    node::set (candidate, metadata);
    result.diagnostics= validate_node_properties (candidate, budget);
    for (auto& error: result.diagnostics) error.where= where;
    if (!result.ok ()) return result;
    const auto mod= mod_set_metadata (location, candidate);
    // clean_apply copies only the ancestor spine. Audit occurrences, not
    // tree_rep addresses: sharing the same annotated child twice is a conflict.
    identity_audit (budget).inspect (clean_apply (scope, mod));
    result.id= metadata.id;
    if (!node::equal_metadata (target, candidate)) result.change= mod;
  }
  catch (const failure& problem) { result.diagnostics.push_back (problem.value); }
  catch (const std::exception& error) {
    result.diagnostics.push_back ({where, "", issue::invalid_metadata, error.what ()});
  }
  return result;
}

tree property_header (const tree& source) {
  tree header= is_atomic (source) ? tree ("") : tree (L(source), 0);
  node::copy_metadata (source, header);
  return header;
}

prepared_property_edit prepare_property_replacement (
    const tree& scope, const source_path& where, const tree& expected,
    const tree& desired, limits budget) {
  const auto reject= [&] (issue code, const char* message) {
    prepared_property_edit result;
    result.diagnostics.push_back ({where, "", code, message});
    return result;
  };
  if (where.size () > budget.maximum_depth)
    return reject (issue::resource_limit, "Property target exceeds depth budget");
  tree target= scope;
  for (int index: where) {
    if (!is_compound (target) || index < 0 || index >= N(target))
      return reject (issue::invalid_path, "Property edit target no longer exists");
    target= target[index];
  }
  if (property_header (target) != expected)
    return reject (issue::stale_properties, "Node properties changed while the editor was open");
  if (L(desired) != L(expected) || node::id (desired) != node::id (expected) ||
      (is_atomic (desired) ? desired->label != "" : N(desired) != 0))
    return reject (issue::protected_property, "A property edit cannot change the node type, UUID or body");
  const node::property::dictionary empty;
  const auto* before= node::get (expected);
  const auto* after= node::get (desired);
  const auto& old_values= before ? before->properties : empty;
  const auto& new_values= after ? after->properties : empty;
  property_edit delta;
  for (const auto& item: old_values)
    if (!new_values.count (item.first)) delta.remove.push_back (item.first);
  for (const auto& item: new_values) {
    auto old= old_values.find (item.first);
    if (old == old_values.end () || !node::equal (old->second, item.second))
      delta.set.emplace (item.first, item.second);
  }
  return prepare_property_edit (scope, where, delta, budget);
}

} // namespace athena::document_node
