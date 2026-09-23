/******************************************************************************
* MODULE     : interop_document.cpp
* DESCRIPTION: Resolve saved and actor-owned document sources without GUI borrowing
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#include "interop_document.hpp"
#include "interop_document_codec.hpp"
#include "interop_document_nodes.hpp"
#include "interop_document_source.hpp"
#include "interop_filesystem.hpp"
#include "Data/Convert/Xml/document_file_codec.hpp"
#include "../Interop/traversal.hpp"
#include "buffer_actor.hpp"
#include "buffer_name_catalog.hpp"
#include "buffer_state.hpp"
#include "convert.hpp"
#include <cerrno>
#include <limits>
#include <mutex>
#include <system_error>
#include <unordered_map>

namespace athena::interop {
namespace {
enum class query_kind { root, properties, read, children, edit };
struct invalid_document: std::invalid_argument {
  using std::invalid_argument::invalid_argument;
};
struct node_result {
  value data;
  document_node node;
  std::vector<document_node> children;
};

std::string native_text (const string& text) { return {text.data (), std::size_t (N (text))}; }

void validate_document (const tree& source) {
  const auto error= interop_document_source_error (source);
  if (!error.empty ()) throw invalid_document (error);
}

void require_parameters (const value& parameters, std::initializer_list<const char*> keys) {
  if (!parameters.is_object () || parameters.size () != keys.size ())
    throw std::invalid_argument ("Unexpected or missing command parameters");
  for (const auto* key: keys)
    if (!parameters.contains (key)) throw std::invalid_argument (std::string ("Missing parameter: ") + key);
}

void edit_node (tree& source, document_nodes& nodes, const document_node& node,
                const std::string& command, const value& parameters) {
  if (command == "set") {
    require_parameters (parameters, {"tree"});
    nodes.replace (source, node, parameters.at ("tree"));
  }
  else if (command == "insert") {
    require_parameters (parameters, {"index", "children"});
    const auto& index= parameters.at ("index");
    if (!index.is_number_integer () || (!index.is_number_unsigned () && index.get<std::int64_t> () < 0) ||
        index.get<std::uint64_t> () > std::numeric_limits<std::size_t>::max ())
      throw std::invalid_argument ("index must be a nonnegative child offset");
    nodes.insert_children (source, node, index.get<std::size_t> (), parameters.at ("children"));
  }
  else if (command == "erase") {
    require_parameters (parameters, {});
    nodes.erase (source, node);
  }
  else if (command == "insert_before" || command == "insert_after") {
    require_parameters (parameters, {"siblings"});
    nodes.insert_siblings (source, node, command == "insert_after", parameters.at ("siblings"));
  }
  else if (command == "set_tag") {
    require_parameters (parameters, {"tag"});
    nodes.set_tag (source, node, parameters.at ("tag"));
  }
  else throw std::invalid_argument ("Unknown document edit command");
  validate_document (source);
}

struct document_source {
  const vault_context_handle vault;
  const std::string mode, filename;
  document_source (vault_context_handle vault, std::string mode, std::string filename):
    vault (std::move (vault)), mode (std::move (mode)), filename (std::move (filename)) {}
  virtual ~document_source () = default;
  virtual std::string identity () const { return vault->incarnation + ":document:" + mode + ":" + filename; }
  virtual value metadata () const { return {{"source", mode}, {"absolute_path", filename}}; }
  void check_vault () const {
    if (!vault_context_is_current (vault))
      throw std::system_error (ESTALE, std::generic_category (), "Vault has closed or changed");
  }
  virtual node_result query (query_kind, const document_node& = {}) const = 0;
  virtual bool matches (const athena::filesystem::entry&) const { return false; }
  virtual bool writable () const { return false; }
  virtual bool source_relocation_available () const { return false; }
  virtual value relocate_source_position (const value&) const {
    throw std::logic_error ("Legacy source relocation is not available for this source");
  }
  virtual value edit (const document_node&, const std::string&, const value&) const {
    throw std::logic_error ("Document editing is not available for this source");
  }
};

class actor_document final: public document_source {
  const athena_actor_id actor;
  const athena_view_id view;
  const bool buffer_bound;
  node_result invoke (query_kind kind, const document_node& node,
                      const std::string& command= {}, const value& parameters= {}) const {
    if (!buffer_bound) check_vault ();
    auto source_view= view;
    if (buffer_bound) source_view= endpoint ().second.source_view;
    struct response {
      node_result result;
      std::string error;
      std::error_code system_error;
      bool invalid_argument= false;
    };
    auto answer= std::make_shared<response> ();
    const auto expected= filename;
    const auto captured_vault= vault;
    const bool require_file= !buffer_bound;
    auto continuation= actor_continuation_registry::instance ().store (
      [answer, captured_vault, expected, source_view, require_file, kind, node, command, parameters] {
        try {
          if (require_file && !vault_context_is_current (captured_vault)) throw std::runtime_error ("STALE: vault changed");
          auto* owner= current_scheme_execution_context ()->actor;
          if (require_file) {
            auto actual= std::filesystem::weakly_canonical (
              std::filesystem::path (native_text (as_system_string (owner->current_buffer_url ()))));
            if (actual != std::filesystem::path (expected)) throw std::runtime_error ("STALE: buffer was renamed");
          }
          tree& source= owner->current_source (source_view);
          auto& nodes= owner->current_state ()->interop_nodes ();
          const auto target_node= node ? node : nodes.track (source, {});
          switch (kind) {
            case query_kind::root: answer->result.node= nodes.track (source, {}); break;
            case query_kind::properties: answer->result.data= nodes.properties (source, target_node); break;
            case query_kind::read: answer->result.data= nodes.read (source, target_node); break;
            case query_kind::children: answer->result.children= nodes.children (source, target_node); break;
            case query_kind::edit: {
              if (owner->current_state ()->read_only)
                throw std::system_error (EROFS, std::generic_category (), "Document buffer is read-only");
              // Preflight on an owner-local copy before sending any mutation
              // notifications to the live editor. This runs only for explicit
              // interop writes, never on the typing/rendering path.
              tree staged= copy (source);
              document_nodes staging;
              auto target= staging.track (staged, nodes.locate (source, target_node));
              edit_node (staged, staging, target, command, parameters);
              new_data projected_data;
              tree projected_body= detach_data (staged, projected_data);
              tree projected= copy (staged);
              refresh_interop_document_source (projected, projected_body, projected_data);
              if (projected != staged)
                throw std::invalid_argument ("The editor cannot preserve this source structure exactly");
              edit_node (source, nodes, target_node, command, parameters);
              owner->commit_current_source ();
              answer->result.data= {{"committed", true}, {"saved", false}};
              break;
            }
          }
        }
        catch (const std::system_error& e) { answer->error= e.what (); answer->system_error= e.code (); }
        catch (const std::invalid_argument& e) { answer->error= e.what (); answer->invalid_argument= true; }
        catch (const value::exception& e) { answer->error= e.what (); answer->invalid_argument= true; }
        catch (const std::exception& e) { answer->error= e.what (); }
        catch (const string& e) { answer->error= native_text (e); }
        catch (...) { answer->error= "Native document access failed"; }
      });
    if (!buffer_actor::invoke_on (actor, actor_command_kind::run_native_continuation,
          source_view, ATHENA_NO_BLOB, ATHENA_NO_BLOB, nullptr, SCHEME_CAPABILITY_BUFFER, continuation)) {
      actor_continuation_registry::instance ().discard (continuation);
      throw std::runtime_error ("STALE: document actor no longer accepts requests");
    }
    if (answer->system_error) throw std::system_error (answer->system_error, answer->error);
    if (answer->invalid_argument) throw std::invalid_argument (answer->error);
    if (!answer->error.empty ()) throw std::runtime_error (answer->error);
    return std::move (answer->result);
  }
public:
  actor_document (vault_context_handle vault, std::string filename,
                  athena_actor_id actor, athena_view_id view):
    document_source (std::move (vault), "online", std::move (filename)), actor (actor), view (view), buffer_bound (false) {}
  explicit actor_document (athena_actor_id actor):
    document_source ({}, "document", {}), actor (actor), view (ATHENA_NO_VIEW), buffer_bound (true) {}
  std::pair<std::string, buffer_name_catalog::metadata> endpoint () const {
    for (const auto& entry: published_buffer_metadata ())
      if (entry.second.actor_id == actor) return entry;
    throw std::system_error (ESTALE, std::generic_category (), "Buffer has closed");
  }
  std::string identity () const override {
    return buffer_bound ? "buffer:" + std::to_string (actor) + ":document" : document_source::identity ();
  }
  value metadata () const override {
    if (!buffer_bound) return document_source::metadata ();
    const auto entry= endpoint ();
    return {{"source", "buffer"}, {"buffer_id", actor},
            {"url", entry.first}};
  }
  node_result query (query_kind kind, const document_node& node) const override { return invoke (kind, node); }
  bool writable () const override { return true; }
  value edit (const document_node& node, const std::string& command, const value& parameters) const override {
    return invoke (query_kind::edit, node, command, parameters).data;
  }
};

class disk_document final: public document_source {
  const std::shared_ptr<const athena::filesystem::confined_root> root;
  const std::filesystem::path relative;
  mutable athena::filesystem::entry pinned;
  mutable athena::filesystem::metadata revision;
  mutable bool revision_available= true;
  mutable std::mutex mutex;
  mutable document_node_transfer snapshot;
  mutable athena::document::document_source_format source_format;
  mutable std::vector<athena::document::legacy_node_mapping> relocation;

  void check_revision () const {
    check_vault ();
    if (mode == "online") {
      const auto key= native_text (as_string (url_system (string (filename.c_str ()))));
      if (published_buffer_source (key).first != ATHENA_NO_ACTOR)
        throw std::runtime_error ("STALE: online document is now owned by a buffer actor");
    }
    const auto current= root->open (relative);
    if (!revision_available || !current.same_object (pinned) ||
        !athena::filesystem::same_revision (revision, current.stat ()))
      throw std::system_error (ESTALE, std::generic_category (), "Saved document changed");
  }
public:
  disk_document (vault_context_handle vault,
                 std::shared_ptr<const athena::filesystem::confined_root> filesystem,
                 std::filesystem::path relative, std::string mode,
                 const athena::filesystem::entry& entry):
    document_source (std::move (vault), std::move (mode), entry.path ().string ()),
    root (std::move (filesystem)), relative (std::move (relative)), pinned (entry), revision (entry.stat ()) {
    const auto bytes= entry.read (64 * 1024 * 1024);
    athena::document::document_read_result decoded;
    try {
      decoded= athena::document::decode_document_bytes (
        std::string_view (bytes.data (), bytes.size ()), entry.path ());
    }
    catch (const athena::document::codec_exception& e) {
      throw invalid_document (e.what ());
    }
    source_format= decoded.format;
    relocation= std::move (decoded.mappings);
    tree source= std::move (decoded.document);
    validate_document (source);
    document_nodes nodes;
    snapshot= nodes.export_nodes (source);
    if (!athena::filesystem::same_revision (revision, entry.stat ()))
      throw std::system_error (ESTALE, std::generic_category (), "Document changed while loading");
  }
  value metadata () const override {
    auto result= document_source::metadata ();
    switch (source_format) {
      case athena::document::document_source_format::xml_v1:
        result["document_format"]= "xml-v1";
        break;
      case athena::document::document_source_format::legacy_markup:
        result["document_format"]= "legacy-markup";
        break;
      case athena::document::document_source_format::legacy_scheme:
        result["document_format"]= "legacy-scheme";
        break;
    }
    result["legacy_relocation"]= !relocation.empty ();
    return result;
  }
  bool matches (const athena::filesystem::entry& entry) const override {
    std::lock_guard<std::mutex> lock (mutex);
    return revision_available && entry.same_object (pinned) &&
      athena::filesystem::same_revision (revision, entry.stat ());
  }
  bool source_relocation_available () const override {
    std::lock_guard<std::mutex> lock (mutex);
    return !relocation.empty ();
  }
  value relocate_source_position (const value& parameters) const override {
    std::lock_guard<std::mutex> lock (mutex);
    check_revision ();
    if (relocation.empty ())
      throw std::logic_error ("Legacy source relocation is no longer available");
    require_parameters (parameters, {"path", "byte", "affinity"});
    const auto& encoded_path= parameters.at ("path");
    if (!encoded_path.is_array ())
      throw std::invalid_argument ("path must be an array of nonnegative child indexes");
    athena::document::document_path path;
    path.reserve (encoded_path.size ());
    for (const auto& item: encoded_path) {
      if (!item.is_number_integer () ||
          (!item.is_number_unsigned () && item.get<std::int64_t> () < 0) ||
          item.get<std::uint64_t> () > std::uint64_t (std::numeric_limits<int>::max ()))
        throw std::invalid_argument ("path must contain nonnegative child indexes");
      path.push_back (int (item.get<std::uint64_t> ()));
    }
    const auto& encoded_byte= parameters.at ("byte");
    if (!encoded_byte.is_number_integer () ||
        (!encoded_byte.is_number_unsigned () && encoded_byte.get<std::int64_t> () < 0) ||
        encoded_byte.get<std::uint64_t> () > std::uint64_t (std::numeric_limits<std::size_t>::max ()))
      throw std::invalid_argument ("byte must be a nonnegative source byte offset");
    if (!parameters.at ("affinity").is_string ())
      throw std::invalid_argument ("affinity must be preceding or following");
    const auto affinity_text= parameters.at ("affinity").get<std::string> ();
    athena::document::boundary_affinity affinity;
    if (affinity_text == "preceding") affinity= athena::document::boundary_affinity::preceding;
    else if (affinity_text == "following") affinity= athena::document::boundary_affinity::following;
    else throw std::invalid_argument ("affinity must be preceding or following");
    athena::document::legacy_document_result mapping {tree (), relocation};
    auto relocated= mapping.relocate (
      path, std::size_t (encoded_byte.get<std::uint64_t> ()), affinity);
    if (!relocated)
      throw std::invalid_argument ("Legacy source position has no exact migrated position");
    return {{"path", relocated->node}, {"byte", relocated->offset}};
  }
  node_result query (query_kind kind, const document_node& node) const override {
    check_vault ();
    std::lock_guard<std::mutex> lock (mutex);
    check_revision ();
    node_result result;
    if (kind == query_kind::root) { result.node= snapshot.track ({}); return result; }
    auto path= node ? snapshot.locate (node) : document_node_path {};
    const value* source= &snapshot.source_value ();
    for (int index: path) source= &source->at ("children")[index];
    if (kind == query_kind::read) result.data= *source;
    else if (kind == query_kind::properties) {
      result.data= value::object ();
      for (const auto* key: {"text", "tag", "cork", "tag_cork"})
        if (source->contains (key)) result.data[key]= source->at (key);
      bool compound= source->contains ("children");
      result.data["type"]= compound ? "compound" : "text";
      result.data["arity"]= compound ? source->at ("children").size () : 0;
      result.data["path"]= path;
      if (source->contains ("tag")) result.data["name"]= source->at ("tag");
      else if (source->contains ("text")) result.data["name"]= source->at ("text");
    }
    else if (source->contains ("children")) {
      const auto count= source->at ("children").size ();
      path.push_back (0);
      for (std::size_t i= 0; i < count; ++i) {
        path.back ()= int (i);
        result.children.push_back (snapshot.track (path));
      }
    }
    return result;
  }
  bool writable () const override { return true; }
  value edit (const document_node& node, const std::string& command, const value& parameters) const override {
    std::lock_guard<std::mutex> lock (mutex);
    check_revision ();
    // Native trees/observers exist only on this operation worker. Stage the edit
    // before touching either the published snapshot or the filesystem.
    tree source= document_node_from_value (snapshot.source_value ());
    document_nodes nodes;
    nodes.import_nodes (source, snapshot);
    edit_node (source, nodes, node ? node : nodes.track (source, {}), command, parameters);
    auto next= nodes.export_nodes (source);
    std::string serialized;
    if (source_format == athena::document::document_source_format::xml_v1)
      serialized= athena::document::write_xml (source);
    else {
      string bytes= tree_to_texmacs (source);
      serialized.assign (bytes.data (), std::size_t (N(bytes)));
    }
    if (serialized.size () > 64 * 1024 * 1024)
      throw std::length_error ("Document exceeds the file size limit");
    tree validation;
    try {
      validation= athena::document::decode_document_bytes (
        std::string_view (serialized.data (), serialized.size ())).document;
    }
    catch (...) {
      throw std::invalid_argument ("The legacy file format cannot preserve this source tree exactly");
    }
    if (validation != source)
      throw std::invalid_argument ("The native file format cannot preserve this source tree exactly");
    check_revision ();
    auto replaced= root->replace (relative, pinned, revision,
      std::string_view (serialized.data (), serialized.size ()));
    pinned= std::move (replaced.file);
    snapshot= std::move (next);
    relocation.clear ();
    if (source_format != athena::document::document_source_format::xml_v1)
      source_format= athena::document::document_source_format::legacy_markup;
    // The rename already committed. A subsequent metadata error must not be
    // reported as an aborted write or leave the old snapshot usable.
    try { revision= pinned.stat (); }
    catch (const std::system_error&) { revision_available= false; }
    return {{"committed", true}, {"directory_synced", replaced.directory_synced},
            {"revision_available", revision_available}};
  }
};

// One live source per vault/path/mode, shared by all ticket-local accessors.
// A source transition creates a new native tree generation; node identities are
// never recovered by matching old offsets into a different tree.
class document_session final: public document_source {
  const std::shared_ptr<const athena::filesystem::confined_root> filesystem;
  const std::filesystem::path relative;
  mutable std::mutex mutex;
  mutable std::shared_ptr<const document_source> current;
  mutable std::pair<std::uint64_t, std::uint64_t> endpoint;

  void refresh () const {
    check_vault ();
    std::pair<std::uint64_t, std::uint64_t> active;
    if (mode == "online") {
      const auto key= native_text (as_string (url_system (string (filename.c_str ()))));
      active= published_buffer_source (key);
    }
    if (active.first != ATHENA_NO_ACTOR) {
      if (!current || active != endpoint)
        current= std::make_shared<actor_document> (vault, filename, active.first, active.second);
    }
    else {
      const auto entry= filesystem->open (relative);
      if (!current || endpoint.first != ATHENA_NO_ACTOR || !current->matches (entry))
        current= std::make_shared<disk_document> (vault, filesystem, relative, mode, entry);
    }
    endpoint= active;
  }
public:
  document_session (std::shared_ptr<const filesystem_resource> file,
                    const athena::filesystem::entry& entry, std::string mode):
    document_source (file->captured_vault (), std::move (mode), entry.path ().string ()),
    filesystem (file->filesystem_root ()), relative (entry.path ().lexically_relative (filesystem->path ())) {}
  node_result query (query_kind kind, const document_node& node) const override {
    std::lock_guard<std::mutex> lock (mutex);
    refresh ();
    return current->query (kind, node);
  }
  value metadata () const override {
    std::lock_guard<std::mutex> lock (mutex);
    refresh ();
    return current->metadata ();
  }
  bool writable () const override { return true; }
  bool source_relocation_available () const override {
    std::lock_guard<std::mutex> lock (mutex);
    refresh ();
    return current->source_relocation_available ();
  }
  value relocate_source_position (const value& parameters) const override {
    std::lock_guard<std::mutex> lock (mutex);
    refresh ();
    return current->relocate_source_position (parameters);
  }
  value edit (const document_node& node, const std::string& command, const value& parameters) const override {
    std::lock_guard<std::mutex> lock (mutex);
    refresh ();
    return current->edit (node, command, parameters);
  }
};

std::shared_ptr<const document_source> shared_document (
    const std::shared_ptr<const filesystem_resource>& file,
    const athena::filesystem::entry& entry, const std::string& mode) {
  static std::mutex mutex;
  static std::unordered_map<std::string, std::weak_ptr<const document_source>> sources;
  const auto key= file->captured_vault ()->incarnation + ":" + mode + ":" + entry.path ().string ();
  std::lock_guard<std::mutex> lock (mutex);
  auto found= sources.find (key);
  if (found != sources.end ()) if (auto live= found->second.lock ()) return live;
  for (auto it= sources.begin (); it != sources.end ();)
    if (it->second.expired ()) it= sources.erase (it); else ++it;
  auto source= std::make_shared<document_session> (file, entry, mode);
  sources[key]= source;
  return source;
}

class document_resource final: public resource {
public:
  const std::shared_ptr<const document_source> source;
  const document_node node;
  const bool document;
  document_resource (std::shared_ptr<const document_source> source, document_node node, bool document):
    source (std::move (source)), node (std::move (node)), document (document) {}
  std::string type () const override { return document ? "document" : "node"; }
  std::string identity () const override {
    if (document) return source->identity ();
    return (source->vault ? source->vault->incarnation : "buffer") +
      ":document-node:" + std::to_string (node->id);
  }
  value properties () const override {
    auto result= source->query (query_kind::properties, node).data;
    result["node_kind"]= result.at ("type");
    result["type"]= type ();
    if (document) result["name"]= source->mode;
    result.update (source->metadata ());
    return result;
  }
  value inspect () const override {
    value commands {{"get", {{"parameters", value::object ()}}},
                    {"inspect", {{"parameters", value::object ()}}}};
    if (source->writable ()) {
      commands["set"]= {{"parameters", {{"tree", "encoded node"}}}};
      commands["insert"]= {{"parameters", {{"index", "nonnegative child offset"}, {"children", "encoded node array"}}}};
      commands["insert_before"]= {{"parameters", {{"siblings", "encoded node array"}}}};
      commands["insert_after"]= {{"parameters", {{"siblings", "encoded node array"}}}};
      commands["erase"]= {{"parameters", value::object ()}};
      commands["set_tag"]= {{"parameters", {{"tag", "UTF-8 node tag"}}}};
    }
    if (document && source->source_relocation_available ())
      commands["relocate_source_position"]= {{"parameters",
        {{"path", "legacy source child-index array"},
         {"byte", "legacy source byte offset"},
         {"affinity", "preceding|following"}}}};
    return commands;
  }
  operation_result operate (const std::string& command, const value& parameters) const override {
    try {
      if (command == "inspect") {
        require_parameters (parameters, {});
        return {"OK", inspect ()};
      }
      if (command == "set" || command == "insert" || command == "erase" || command == "set_tag" ||
          command == "insert_before" || command == "insert_after") {
        if (!source->writable ()) return {"UNSUPPORTED", "Editing this source is not available"};
        return {"OK", source->edit (node, command, parameters)};
      }
      if (command == "relocate_source_position") {
        if (!document || !source->source_relocation_available ())
          return {"UNSUPPORTED", "Legacy source relocation is not available"};
        return {"OK", source->relocate_source_position (parameters)};
      }
      if (command != "get") return {"UNKNOWN_COMMAND", command};
      require_parameters (parameters, {});
      auto result= source->metadata ();
      result["tree"]= source->query (query_kind::read, node).data;
      return {"OK", std::move (result)};
    }
    catch (const std::system_error& e) {
      const auto code= e.code ().value ();
      return {code == ESTALE ? "STALE" : code == EAGAIN ? "CONFLICT" : code == EROFS ? "READ_ONLY" : "ERROR", e.what ()};
    }
    catch (const std::invalid_argument& e) { return {"INVALID_ARGUMENT", e.what ()}; }
    catch (const value::exception& e) { return {"INVALID_ARGUMENT", e.what ()}; }
    catch (const string& e) { return {"ERROR", native_text (e)}; }
    catch (const std::exception& e) {
      const std::string error= e.what ();
      return {error.rfind ("STALE:", 0) == 0 ? "STALE" : "ERROR", error};
    }
  }
  std::vector<std::shared_ptr<const document_resource>> children () const {
    std::vector<std::shared_ptr<const document_resource>> result;
    for (auto& child: source->query (query_kind::children, node).children)
      result.push_back (std::make_shared<document_resource> (source, std::move (child), false));
    return result;
  }
};

std::shared_ptr<const document_resource> open_document (
    std::shared_ptr<const filesystem_resource> file, const std::string& mode) {
  const auto entry= file->current ();
  auto source= shared_document (file, entry, mode);
  source->query (query_kind::properties);
  return std::make_shared<document_resource> (std::move (source), document_node {}, true);
}

class document_resolver_rep final: public resolver {
public:
  resolver_outcome resolve (const resolution_request& req, resolution_output& out) const override {
    if (!req.basepoint) return resolver_outcome::irrelevant;
    const auto state= std::dynamic_pointer_cast<const traversal> (req.state);
    const auto base= std::dynamic_pointer_cast<const document_resource> (req.basepoint->accessor);
    if (state && state->step == traversal::phase::candidate) {
      if (!base || state->domain != "document") return resolver_outcome::irrelevant;
      return visit_candidate (req, *state, out);
    }
    if (state && !state->domain.empty () && state->domain != "document") return resolver_outcome::irrelevant;
    const auto& s= req.selectors.at (req.offset);
    auto budget= state ? state->budget : std::make_shared<traversal_budget> (s.limits);
    if (traversal_stopped (req, *budget, out)) return resolver_outcome::miss;
    if (!base) {
      auto file= std::dynamic_pointer_cast<const filesystem_resource> (req.basepoint->accessor);
      if (!file || file->type () != "file" || file->current ().path ().extension () != ".ath")
        return resolver_outcome::irrelevant;
      if (s.type == selector::kind::name && (s.name == "online" || s.name == "saved")) {
        if (!s.positions.empty ()) throw std::invalid_argument ("Select document children with a following [index]");
        out.publish (open_document (file, s.name), req.offset + 1);
      }
      else if (s.type == selector::kind::local || s.type == selector::kind::scoped || s.type == selector::kind::recursive) {
        if (!s.positions.empty ()) throw std::invalid_argument ("Source selection indices are not supported");
        for (const auto* mode: {"online", "saved"}) {
          if (traversal_stopped (req, *budget, out)) break;
          try {
            publish_candidate (out, open_document (file, mode), req.offset, budget,
                               state ? state->depth + 1 : 1, "document");
          }
          catch (const invalid_document&) {}
        }
      }
      else return resolver_outcome::irrelevant;
    }
    else if (s.type == selector::kind::default_resource && !state) {
      if (!s.positions.empty ()) throw std::invalid_argument ("Select document children with a following [index]");
      out.publish (base, req.offset + 1);
    }
    else if (!state && (s.type == selector::kind::index || !s.positions.empty ())) {
      if (s.type == selector::kind::scoped || s.type == selector::kind::recursive)
        throw std::invalid_argument ("Indices select immediate children, not recursive result sets");
      if (budget->limits.max_depth && *budget->limits.max_depth < 1)
        return resolver_outcome::miss;
      std::vector<std::shared_ptr<const document_resource>> matches;
      for (const auto& child: base->children ()) {
        if (traversal_stopped (req, *budget, out)) break;
        const auto props= child->properties ();
        if (s.type == selector::kind::index ||
            (s.type == selector::kind::name ? props.value ("name", "") == s.name : s.filter.matches (props))) {
          budget->matches.fetch_add (1);
          matches.push_back (child);
        }
      }
      for (auto index: s.positions) {
        if (req.stopped.load ()) break;
        if (index < matches.size ()) out.publish (matches[index], req.offset + 1);
      }
    }
    else {
      for (const auto& child: base->children ()) {
        if (traversal_stopped (req, *budget, out)) break;
        publish_candidate (out, child, req.offset, budget, state ? state->depth + 1 : 1, "document");
      }
    }
    return out.branches.empty () ? resolver_outcome::miss : resolver_outcome::resolved;
  }
};
}
std::shared_ptr<const resolver> document_resolver () { return std::make_shared<document_resolver_rep> (); }
binding buffer_document (std::uint64_t buffer_id) {
  auto source= std::make_shared<actor_document> (buffer_id);
  source->query (query_kind::properties, {});
  return std::make_shared<document_resource> (std::move (source), document_node {}, true);
}
}
