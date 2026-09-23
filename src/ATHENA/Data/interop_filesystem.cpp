/******************************************************************************
* MODULE     : interop_filesystem.cpp
* DESCRIPTION: Confined physical filesystem resolution, metadata and native document checking
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#include "interop_filesystem.hpp"
#include "Data/Convert/Xml/document_file_codec.hpp"
#include "interop_document_source.hpp"
#include "../Interop/traversal.hpp"
#include "convert.hpp"
#include "buffer_name_catalog.hpp"
#include <cerrno>
#include <system_error>

namespace athena::interop {
namespace fs = std::filesystem;
namespace {
std::string text (const string& s) { return std::string (s.data (), N(s)); }
value time_value (const athena::filesystem::timestamp& t) {
  return {{"seconds", t.seconds}, {"nanoseconds", t.nanoseconds}};
}
std::string error_status (const std::system_error& e) {
  switch (e.code ().value ()) {
    case ESTALE: return "STALE";
    case ENOENT: return "NOT_FOUND";
    case EACCES: case EPERM: case EXDEV: case ELOOP: return "DENIED";
    case EAGAIN: return "CONFLICT";
    default: return "ERROR";
  }
}
}
filesystem_resource::filesystem_resource (vault_context_handle context,
    std::shared_ptr<const athena::filesystem::confined_root> root, fs::path relative,
    athena::filesystem::entry entry):
  context (std::move (context)), root (std::move (root)),
  relative (std::move (relative)), pinned (std::move (entry)) {}

athena::filesystem::entry filesystem_resource::current () const {
  if (!vault_context_is_current (context))
    throw std::system_error (ESTALE, std::generic_category (), "Vault has closed or been replaced");
  auto entry = root->open (relative);
  if (!entry.same_object (pinned))
    throw std::system_error (ESTALE, std::generic_category (), "Filesystem entry was replaced");
  return entry;
}
std::string filesystem_resource::type () const { return pinned.stat ().directory ? "directory" : "file"; }
std::string filesystem_resource::identity () const {
  const auto s = pinned.stat ();
  return context->incarnation + ":fs:" + std::to_string (s.device) + ":" + std::to_string (s.inode);
}
value filesystem_resource::properties () const {
  auto entry = current ();
  const auto s = entry.stat ();
  value result = {{"name", relative == "." ? root->path ().filename ().string () : relative.filename ().string ()},
    {"type", s.directory ? "directory" : "file"}, {"absolute_path", entry.path ().string ()},
    {"created_time", s.created ? time_value (*s.created) : value ()},
    {"modified_time", time_value (s.modified)}, {"accessed_time", time_value (s.accessed)}};
  if (!s.directory) result["size"] = s.size;
  return result;
}
value filesystem_resource::inspect () const {
  value commands = {{"get", {{"parameters", value::object ()}}},
                    {"inspect", {{"parameters", value::object ()}}}};
  if (type () == "file") commands["check"] = {{"parameters", value::object ()}};
  if (type () == "file" && relative.extension () == ".ath")
    commands["buffers"] = {{"parameters", value::object ()}};
  return commands;
}
operation_result filesystem_resource::operate (const std::string& command, const value& p) const {
  try {
    if (!p.is_object () || !p.empty ()) return {"INVALID_ARGUMENT", "This command takes no parameters"};
    if (command == "inspect") return {"OK", inspect ()};
    if (command == "get") return {"OK", properties ()};
    if (command == "buffers" && type () == "file" && relative.extension () == ".ath") {
      const auto path= current ().path ().string ();
      return {"OK", published_file_buffers (text (as_string (url_system (string (path.c_str ())))))};
    }
    if (command != "check" || type () != "file") return {"UNKNOWN_COMMAND", command};
    auto entry = current ();
    if (entry.path ().extension () != ".ath")
      return {"INVALID_ARGUMENT", "Document checking requires an .ath file"};
    const auto bytes = entry.read (64 * 1024 * 1024);
    tree doc;
    try {
      doc= athena::document::decode_document_bytes (
        std::string_view (bytes.data (), bytes.size ()), entry.path ()).document;
    }
    catch (const std::exception& e) {
      return {"OK", {{"valid", false}, {"reason", e.what ()}}};
    }
    const auto error= interop_document_source_error (doc);
    return {"OK", {{"valid", error.empty ()}, {"reason", error}}};
  }
  catch (const std::system_error& e) { return {error_status (e), e.what ()}; }
  catch (const std::invalid_argument& e) { return {"INVALID_ARGUMENT", e.what ()}; }
  catch (const std::length_error& e) { return {"LIMIT", e.what ()}; }
  catch (const string& e) { return {"ERROR", text (e)}; }
}

namespace {
class filesystem_resolver_rep final: public resolver {
  void children (const resolution_request& req, const filesystem_resource& base,
      const std::shared_ptr<traversal_budget>& budget, std::uint64_t depth,
      resolution_output& out) const {
    if (traversal_stopped (req, *budget, out) ||
        (budget->limits.max_depth && depth > *budget->limits.max_depth)) return;
    if (base.type () != "directory") return;
    const auto directory = base.current ();
    for (const auto& name: directory.names ()) {
      if (traversal_stopped (req, *budget, out)) break;
      const auto relative = base.relative_path () == "." ? fs::path (name) : base.relative_path () / name;
      try {
        auto entry = base.filesystem_root ()->open (relative);
        publish_candidate (out, std::make_shared<filesystem_resource> (
          base.captured_vault (), base.filesystem_root (), relative, std::move (entry)),
          req.offset, budget, depth, "filesystem");
      }
      catch (const std::system_error& e) {
        // Broken, external or special-file links are not accessible resources.
        const auto code = e.code ().value ();
        if (code != ENOENT && code != ENOTDIR && code != EACCES && code != EPERM &&
            code != ELOOP && code != EXDEV && code != ENOTSUP) throw;
      }
    }
  }
public:
  resolver_outcome resolve (const resolution_request& req, resolution_output& out) const override {
    if (!req.basepoint) return resolver_outcome::irrelevant;
    auto state = std::dynamic_pointer_cast<const traversal> (req.state);
    auto base = std::dynamic_pointer_cast<const filesystem_resource> (req.basepoint->accessor);
    if (state && state->step == traversal::phase::candidate) {
      if (!base || state->domain != "filesystem") return resolver_outcome::irrelevant;
      bool descend = true;
      for (auto ancestor = req.basepoint->parent; ancestor; ancestor = ancestor->parent)
        if (ancestor->accessor->identity () == base->identity ()) { descend = false; break; }
      try { return visit_candidate (req, *state, out, descend); }
      catch (const std::system_error& e) {
        // Enumeration is not a filesystem snapshot. A queued candidate may
        // disappear before this worker evaluates its properties.
        if (e.code ().value () != ENOENT && e.code ().value () != ENOTDIR) throw;
        return resolver_outcome::miss;
      }
    }
    if (state && !state->domain.empty () && state->domain != "filesystem")
      return resolver_outcome::irrelevant;
    auto vault = std::dynamic_pointer_cast<const vault_resource> (req.basepoint->accessor);
    const auto& s = req.selectors.at (req.offset);
    if ((base || (vault && vault->type () == "vault")) && !s.positions.empty ())
      throw std::invalid_argument ("Indices require a document or node basepoint");
    if (!base) {
      if (!vault || vault->type () != "vault") return resolver_outcome::irrelevant;
      const bool marker = !state && s.type == selector::kind::name && s.name == "filesystem";
      if (!marker && !state && (s.type == selector::kind::name || s.type == selector::kind::default_resource))
        return resolver_outcome::irrelevant;
      auto context = vault->captured_vault ();
      if (!vault_context_is_current (context)) throw std::runtime_error ("Vault has closed or been replaced");
      auto root = std::make_shared<athena::filesystem::confined_root> (context->root);
      auto entry = std::make_shared<filesystem_resource> (context, root, ".", root->open ("."));
      if (marker) out.publish (std::move (entry), req.offset + 1);
      else publish_candidate (out, std::move (entry), req.offset,
        state ? state->budget : std::make_shared<traversal_budget> (s.limits),
        state ? state->depth + 1 : 1, "filesystem");
    }
    else {
      if (base->type () != "directory") return resolver_outcome::irrelevant;
      if (!state && s.type == selector::kind::name) {
        athena::filesystem::confined_root::validate_component (s.name);
        base->current ();
        auto relative = base->relative_path () == "." ? fs::path (s.name) : base->relative_path () / s.name;
        try {
          auto entry = base->filesystem_root ()->open (relative);
          out.publish (std::make_shared<filesystem_resource> (base->captured_vault (),
            base->filesystem_root (), relative, std::move (entry)), req.offset + 1);
        }
        catch (const std::system_error& e) {
          if (e.code ().value () != ENOENT && e.code ().value () != ENOTDIR) throw;
        }
      }
      else {
        try {
          children (req, *base, state ? state->budget : std::make_shared<traversal_budget> (s.limits),
                    state ? state->depth + 1 : 1, out);
        }
        catch (const std::system_error& e) {
          if (e.code ().value () != ENOENT && e.code ().value () != ENOTDIR) throw;
        }
      }
    }
    return out.branches.empty () ? resolver_outcome::miss : resolver_outcome::resolved;
  }
};
}
std::shared_ptr<const resolver> filesystem_resolver () { return std::make_shared<filesystem_resolver_rep> (); }
} // namespace athena::interop
