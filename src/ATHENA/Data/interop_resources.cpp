/******************************************************************************
* MODULE     : interop_resources.cpp
* DESCRIPTION: Native root, vault and namespace resolvers and resource operations
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#include "../Interop/resources.hpp"
#include "../Interop/traversal.hpp"
#include "interop_vault_resource.hpp"
#include "interop_artifacts.hpp"
#include "namespaces_private.hpp"
#include <chrono>
#include <set>

namespace athena::interop {
namespace {
using athena_namespaces::tm_to_std;
using athena_namespaces::std_to_tm;

class domain_error: public std::runtime_error {
public:
  std::string status;
  domain_error (std::string status, std::string error): std::runtime_error (std::move (error)), status (std::move (status)) {}
};

void check (namespace_query_status status, const string& error) {
  if (status == namespace_query_status::ok) return;
  throw domain_error (status == namespace_query_status::stale ? "STALE" :
    status == namespace_query_status::not_found ? "NOT_FOUND" : "ERROR", tm_to_std (error));
}
void require_current (const vault_context_handle& context) {
  if (!vault_context_is_current (context)) throw domain_error ("STALE", "Vault has closed or been replaced");
}
std::string required_string (const value& parameters, const char* field) {
  if (!parameters.contains (field) || !parameters[field].is_string ())
    throw domain_error ("INVALID_ARGUMENT", std::string ("Missing string parameter: ") + field);
  return parameters[field].get<std::string> ();
}
value strings (const std::vector<string>& strings) {
  value out = value::array ();
  for (const auto& s: strings) out.push_back (tm_to_std (s));
  return out;
}
value describe (const athena_namespace_definition& ns) {
  return {{"uuid", tm_to_std (ns.uuid)}, {"name", tm_to_std (ns.name)}, {"type", "namespace"},
    {"kind", tm_to_std (ns.kind)}, {"template", tm_to_std (ns.templ)},
    {"sorter_trivial", ns.sorter_trivial}, {"sorter_path", tm_to_std (ns.sorter_path)},
    {"style_path", tm_to_std (ns.style_path)}, {"initial_content_path", tm_to_std (ns.initial_content_path)},
    {"homepage_path", tm_to_std (ns.homepage_path)}, {"parents", strings (ns.parents)},
    {"derived_parents", strings (ns.derived_parents)}};
}
athena_namespace_definition definition (const value& p) {
  athena_namespace_definition ns;
  ns.name = std_to_tm (required_string (p, "name"));
  ns.kind = std_to_tm (required_string (p, "kind"));
  if (ns.kind != "abstract" && ns.kind != "concrete" && ns.kind != "semi-concrete")
    throw domain_error ("INVALID_ARGUMENT", "Unknown namespace kind");
  ns.templ = std_to_tm (p.value ("template", ""));
  ns.sorter_trivial = p.value ("sorter_trivial", false);
  ns.sorter_path = std_to_tm (p.value ("sorter_path", ""));
  ns.style_path = std_to_tm (p.value ("style_path", ""));
  ns.initial_content_path = std_to_tm (p.value ("initial_content_path", ""));
  ns.homepage_path = std_to_tm (p.value ("homepage_path", ""));
  if (p.contains ("parents")) {
    if (!p["parents"].is_array ()) throw domain_error ("INVALID_ARGUMENT", "parents must be an array");
    for (const auto& parent: p["parents"]) ns.parents.push_back (std_to_tm (parent.get<std::string> ()));
  }
  return ns;
}

class native_resource final: public vault_resource {
  const std::string resource_type;
  const std::string uuid;
public:
  const vault_context_handle context;
  vault_context_handle captured_vault () const override { return context; }
  native_resource (std::string type, vault_context_handle context = {}, std::string uuid = {}):
    resource_type (std::move (type)), uuid (std::move (uuid)), context (std::move (context)) {}
  std::string type () const override { return resource_type; }
  std::string identity () const override {
    return type () == "root" ? "root" : context->incarnation + ":" + uuid;
  }
  std::shared_ptr<const athena_namespace_definition> current () const {
    std::shared_ptr<const athena_namespace_definition> ns;
    string error;
    check (athena_namespace_get_by_uuid (context, std_to_tm (uuid), ns, error), error);
    return ns;
  }
  value properties () const override {
    if (type () == "root") return {{"name", "ATHENA"}, {"type", "root"}};
    require_current (context);
    if (type () == "vault") return {{"name", context->name}, {"type", "vault"}, {"root", context->root.string ()}};
    return describe (*current ());
  }
  value inspect () const override {
    value commands = {{"inspect", {{"parameters", value::object ()}}}, {"get", {{"parameters", value::object ()}}}};
    if (type () == "vault") commands["create_namespace"] = {{"parameters", {{"definition", "map"}}}};
    if (type () == "namespace") {
      commands["set"] = {{"parameters", {{"definition", "map (complete replacement)"}}}};
      commands["delete"] = {{"parameters", value::object ()}};
      commands["members"] = {{"parameters", value::object ()}};
      commands["relations"] = {{"parameters", value::object ()}};
      commands["rename"] = {{"parameters", {{"name", "string"}}}};
      commands["set_relation"] = {{"parameters", {{"parent_uuid", "string"}, {"decision", "allow | deny"}}}};
      commands["remove_relation"] = {{"parameters", {{"parent_uuid", "string"}}}};
      commands["template_fields"] = {{"parameters", value::object ()}};
      commands["create_file"] = {{"parameters", {{"directory", "vault-relative string"}, {"values", "array of strings"},
        {"use_initial_content", "boolean"}}}};
      commands["sorter_source"] = {{"parameters", value::object ()}};
      commands["subproduct"] = {{"parameters", {{"other_uuid", "string"}, {"name", "string"},
        {"template", "string"}, {"aggressive_string", "boolean"}}}};
    }
    return commands;
  }
  operation_result operate (const std::string& command, const value& p) const override {
    try {
      if (command == "get") return {"OK", properties ()};
      if (type () == "root") return {"UNKNOWN_COMMAND", command};
      require_current (context);
      string error;
      if (type () == "vault" && command == "create_namespace") {
        auto ns = definition (p.at ("definition"));
        if (!athena_namespace_create (context, ns, error)) throw domain_error ("ERROR", tm_to_std (error));
        std::shared_ptr<const athena_namespace_definition> stored;
        check (athena_namespace_get (context, ns.name, stored, error), error);
        return {"OK", describe (*stored)};
      }
      if (type () != "namespace") return {"UNKNOWN_COMMAND", command};
      if (command == "rename") {
        check (athena_namespace_rename_by_uuid (context, std_to_tm (uuid),
          std_to_tm (required_string (p, "name")), error), error);
        return {"OK", describe (*current ())};
      }
      if (command == "set_relation" || command == "remove_relation") {
        check (athena_namespace_relation_write_by_uuid (context,
          std_to_tm (required_string (p, "parent_uuid")), std_to_tm (uuid),
          command == "set_relation" ? std_to_tm (required_string (p, "decision")) : string (""),
          "interop", command == "remove_relation", error), error);
        return {"OK", nullptr};
      }
      if (command == "template_fields") {
        value fields = value::array ();
        for (const auto& field: athena_namespace_template_fields (*current (), error))
          fields.push_back ({{"placeholder", tm_to_std (field.placeholder)}, {"type", tm_to_std (field.type)}});
        if (error != "") throw domain_error ("ERROR", tm_to_std (error));
        return {"OK", fields};
      }
      if (command == "sorter_source") {
        auto ns = *current ();
        if (ns.sorter_path != "") {
          auto path = std::filesystem::path (tm_to_std (ns.sorter_path));
          if (path.is_relative ()) path = context->root / path;
          ns.sorter_path = std_to_tm (path.string ());
        }
        string source;
        if (!athena_namespace_sorter_source (ns, source, error)) throw domain_error ("ERROR", tm_to_std (error));
        return {"OK", tm_to_std (source)};
      }
      if (command == "create_file") {
        const auto ns = current ();
        if (ns->kind != "concrete") throw domain_error ("INVALID_ARGUMENT", "File creation requires a concrete namespace");
        array<string> fields;
        if (!p.at ("values").is_array () || !p.at ("use_initial_content").is_boolean ())
          throw domain_error ("INVALID_ARGUMENT", "create_file requires values and use_initial_content");
        for (const auto& field: p["values"]) fields << std_to_tm (field.get<std::string> ());
        string stem;
        if (!athena_namespace_build_stem (*ns, fields, stem, error)) throw domain_error ("INVALID_ARGUMENT", tm_to_std (error));
        const auto directory = std::filesystem::canonical (context->root / required_string (p, "directory"));
        const auto target = (directory / (tm_to_std (stem) + ".ath")).lexically_normal ();
        const auto relative = target.lexically_relative (std::filesystem::canonical (context->root));
        if (relative.empty () || *relative.begin () == ".." || target.parent_path () != directory)
          throw domain_error ("INVALID_ARGUMENT", "Document must be inside the requested vault directory");
        if (!athena_namespace_create_file (*ns, url_system (std_to_tm (target.string ())),
            std_to_tm (context->root.string ()), p["use_initial_content"].get<bool> (), error))
          throw domain_error ("ERROR", tm_to_std (error));
        return {"OK", {{"path", target.string ()}}};
      }
      if (command == "subproduct") {
        const auto first = current ();
        if (required_string (p, "other_uuid") == uuid)
          throw domain_error ("INVALID_ARGUMENT", "A sub-product requires two distinct namespaces");
        std::shared_ptr<const athena_namespace_definition> second;
        check (athena_namespace_get_by_uuid (context, std_to_tm (required_string (p, "other_uuid")), second, error), error);
        athena_namespace_definition product;
        product.name = std_to_tm (required_string (p, "name"));
        product.templ = std_to_tm (required_string (p, "template"));
        const bool first_semi = first->kind != "abstract";
        const bool second_semi = second->kind != "abstract";
        for (const auto& parent: {first, second})
          if (parent->kind != "abstract" && (parent->templ == "" || (!parent->sorter_trivial && parent->sorter_path == "")))
            throw domain_error ("INVALID_ARGUMENT", "Non-abstract product parents need a template and sorter");
        if (first_semi && second_semi && product.templ == "") {
          if (!p.contains ("aggressive_string") || !p["aggressive_string"].is_boolean ())
            throw domain_error ("INVALID_ARGUMENT", "Template inference requires aggressive_string");
          if (!athena_namespace_suggest_subproduct_template (first->templ, second->templ,
              p["aggressive_string"].get<bool> (), product.templ, error))
            throw domain_error ("ERROR", tm_to_std (error));
        }
        product.kind = first_semi || second_semi ? "semi-concrete" : "abstract";
        product.parents = {first->name, second->name};
        if (first_semi && second_semi) {
          if (!athena_namespace_generate_product_sorter (context, *first, *second,
              product.templ, product.sorter_path, error)) throw domain_error ("ERROR", tm_to_std (error));
        }
        else if (first_semi || second_semi) {
          const auto& parent = first_semi ? first : second;
          if (product.templ == "") product.templ = parent->templ;
          bool derives = false;
          if (!athena_namespace_template_derives (product.templ, parent->templ, derives, error) || !derives)
            throw domain_error ("INVALID_ARGUMENT", "Product template does not derive from parent");
          product.sorter_trivial = parent->sorter_trivial;
          if (!product.sorter_trivial && !athena_namespace_generate_restricted_sorter (context, *parent,
              product.templ, product.sorter_path, error)) throw domain_error ("ERROR", tm_to_std (error));
        }
        else product.templ = "";
        if (!athena_namespace_create (context, product, error)) {
          std::error_code ignored;
          if (product.sorter_path != "") std::filesystem::remove (context->root / tm_to_std (product.sorter_path), ignored);
          throw domain_error ("ERROR", tm_to_std (error));
        }
        std::shared_ptr<const athena_namespace_definition> stored;
        check (athena_namespace_get (context, product.name, stored, error), error);
        return {"OK", describe (*stored)};
      }
      if (command == "delete") {
        check (athena_namespace_remove_by_uuid (context, std_to_tm (uuid), error), error);
        return {"OK", nullptr};
      }
      if (command == "set") {
        auto ns = definition (p.at ("definition"));
        ns.uuid = std_to_tm (uuid);
        if (!athena_namespace_save (context, ns, error)) throw domain_error ("ERROR", tm_to_std (error));
        return {"OK", describe (*current ())};
      }
      if (command == "relations") {
        const auto name = current ()->name;
        namespace_records<athena_namespace_relation> relations;
        check (athena_namespace_relations_list (context, relations, error), error);
        value result = value::array ();
        for (const auto& r: relations) if (r.parent == name || r.child == name)
          result.push_back ({{"parent", tm_to_std (r.parent)}, {"child", tm_to_std (r.child)},
                            {"decision", tm_to_std (r.decision)}, {"source", tm_to_std (r.source)}});
        return {"OK", result};
      }
      if (command == "members") {
        const auto ns = current ();
        auto members = athena_namespace_members (context, ns->uuid, error);
        if (error != "") throw domain_error ("ERROR", tm_to_std (error));
        value result = value::array ();
        for (const auto& m: members) result.push_back ({{"path", tm_to_std (m.file_path)},
          {"stem", tm_to_std (m.stem)}, {"captures", strings (m.captures)},
          {"capture_types", strings (m.capture_types)}, {"ambiguous", m.ambiguous}});
        return {"OK", result};
      }
      return {"UNKNOWN_COMMAND", command};
    }
    catch (const domain_error& e) { return {e.status, e.what ()}; }
    catch (const value::exception& e) { return {"INVALID_ARGUMENT", e.what ()}; }
  }
};

std::string default_namespace (const vault_context_handle& context) {
  std::ifstream file (context->root / "Vaultfile.json");
  if (!file) throw std::runtime_error ("Cannot read Vaultfile.json");
  const auto doc = value::parse (file);
  if (!doc.contains ("root_namespace") || doc["root_namespace"].is_null ()) return {};
  return doc.at ("root_namespace").get<std::string> ();
}

class native_resolver final: public resolver {
  const std::string managed_type;

  void children (const resolution_request& req, const native_resource& parent,
      std::size_t offset, const std::shared_ptr<traversal_budget>& budget,
      std::uint64_t depth, resolution_output& out) const {
    if (budget->limits.max_depth && depth > *budget->limits.max_depth) return;
    if (traversal_stopped (req, *budget, out)) return;
    if (parent.type () == "root") {
      auto context = vault_capture_context ();
      if (context) publish_candidate (out, std::make_shared<native_resource> ("vault", context),
        offset, budget, depth, "vault", true);
      return;
    }
    require_current (parent.context);
    namespace_records<athena_namespace_definition> definitions;
    string error;
    check (athena_namespaces_list (parent.context, definitions, error), error);
    const auto parent_name = parent.type () == "namespace" ? parent.current ()->name : string ("");
    std::string default_name;
    if (req.selectors[offset].type == selector::kind::default_resource)
      default_name = default_namespace (parent.context);
    for (const auto& ns: definitions) {
      if (req.stopped.load ()) return;
      if (parent.type () == "namespace" && !athena_namespaces::has_string (ns.parents, parent_name) &&
          !athena_namespaces::has_string (ns.derived_parents, parent_name)) continue;
      const auto id = parent.context->incarnation + ":" + tm_to_std (ns.uuid);
      for (auto ancestor = req.basepoint; ancestor; ancestor = ancestor->parent)
        if (ancestor->accessor->identity () == id) throw std::runtime_error ("Namespace graph contains a cycle");
      publish_candidate (out, std::make_shared<native_resource> ("namespace", parent.context, tm_to_std (ns.uuid)),
        offset, budget, depth, "namespace", !default_name.empty () && default_name == tm_to_std (ns.name));
    }
  }
public:
  explicit native_resolver (std::string type): managed_type (std::move (type)) {}
  resolver_outcome resolve (const resolution_request& req, resolution_output& out) const override {
    const auto& s = req.selectors.at (req.offset);
    if (!req.basepoint) {
      if (managed_type != "root") return resolver_outcome::irrelevant;
      if (s.type != selector::kind::default_resource) return resolver_outcome::miss;
      out.publish (std::make_shared<native_resource> ("root"), req.offset + 1);
      return resolver_outcome::resolved;
    }
    auto base = std::dynamic_pointer_cast<const native_resource> (req.basepoint->accessor);
    if (!base) return resolver_outcome::irrelevant;
    auto state = std::dynamic_pointer_cast<const traversal> (req.state);
    try {
      if (state && state->step == traversal::phase::candidate) {
        if (state->domain != managed_type) return resolver_outcome::irrelevant;
        return visit_candidate (req, *state, out);
      }
      const auto desired = base->type () == "root" ? "vault" : "namespace";
      if (managed_type != desired) return resolver_outcome::irrelevant;
      if (state) {
        if (!state->domain.empty () && state->domain != managed_type) return resolver_outcome::irrelevant;
        children (req, *base, req.offset, state->budget, state->depth + 1, out);
        return out.branches.empty () ? resolver_outcome::miss : resolver_outcome::resolved;
      }
      std::size_t offset = req.offset;
      if (s.type == selector::kind::name && s.name == (base->type () == "root" ? "vaults" : "namespaces")) {
        if (++offset == req.selectors.size ()) return resolver_outcome::miss;
      }
      const auto& target = req.selectors.at (offset);
      auto budget = std::make_shared<traversal_budget> (target.limits);
      children (req, *base, offset, budget, 1, out);
      return out.branches.empty () ? resolver_outcome::miss : resolver_outcome::resolved;
    }
    catch (const domain_error& e) {
      if (e.status == "NOT_FOUND") return resolver_outcome::miss;
      throw;
    }
  }
};
} // namespace

std::shared_ptr<const resolver_registry> native_resolvers () {
  static const auto registry = std::make_shared<const resolver_registry> (resolver_registry {
    std::make_shared<native_resolver> ("root"), std::make_shared<native_resolver> ("vault"),
    std::make_shared<native_resolver> ("namespace"), artifacts_resolver ()});
  return registry;
}
} // namespace athena::interop
