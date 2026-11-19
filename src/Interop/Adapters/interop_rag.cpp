#include "interop_rag.hpp"
#include "interop_vault_resource.hpp"
#include "Services/RAG/rag_index.hpp"

namespace athena::interop {
namespace {
bool open_index (rag::RagIndex& index, const vault_context_handle& context) {
  rag::RagConfig config;
  config.vault_root= context->root;
  config.db_path= rag::rag_default_db_path (context->root);
  config.read_only_query= true;
  return index.open (config);
}

value describe (const rag::RagChunk& chunk) {
  return {{"chunk_id", chunk.chunk_id}, {"rel_path", chunk.rel_path},
          {"kind", chunk.kind}, {"tree_path", chunk.tree_path}, {"anchor", chunk.anchor},
          {"title", chunk.title}, {"heading_path", chunk.heading_path}};
}

class rag_resource final: public vault_resource {
  vault_context_handle context;
  std::string chunk_id;
public:
  rag_resource (vault_context_handle context, std::string id= {}):
    context (std::move (context)), chunk_id (std::move (id)) {}
  vault_context_handle captured_vault () const override { return context; }
  std::string type () const override { return chunk_id.empty () ? "rag" : "rag_chunk"; }
  std::string identity () const override { return context->incarnation + ":rag:" + chunk_id; }
  value properties () const override {
    return {{"resource_type", type ()}, {"name", chunk_id.empty () ? "rag" : chunk_id}};
  }
  value inspect () const override {
    value spec= {{"inspect", {{"parameters", value::object ()}}}};
    spec[chunk_id.empty () ? "status" : "get"]= {{"parameters", value::object ()}};
    if (chunk_id.empty ()) spec["search"]= {{"parameters", {{"query", "string"},
      {"limit", "optional integer, 1..50 (default 10)"}}}};
    return spec;
  }
  operation_result operate (const std::string& command, const value& args) const override {
    if (!vault_context_is_current (context)) return {"STALE", "Vault changed"};
    if (!inspect ().contains (command)) return {"UNKNOWN_COMMAND", command};
    if (!args.is_object ()) return {"INVALID_ARGUMENT", "Expected an object"};
    int limit= 10;
    if (command == "search") {
      if (!args.contains ("query") || !args["query"].is_string () ||
          args["query"] == "" || args.size () != 1 + args.count ("limit"))
        return {"INVALID_ARGUMENT", "Expected query and optional limit"};
      if (args.contains ("limit")) {
        if (!args["limit"].is_number_integer () || args["limit"] < 1 || args["limit"] > 50)
          return {"INVALID_ARGUMENT", "limit must be an integer from 1 to 50"};
        limit= args["limit"].get<int> ();
      }
    } else if (!args.empty ()) return {"INVALID_ARGUMENT", "Expected no parameters"};
    if (command == "inspect") return {"OK", inspect ()};

    rag::RagIndex index;
    const bool open= open_index (index, context);
    value result;
    if (command == "status") {
      const auto status= index.status ();
      result= {{"open", open}, {"error", status.last_error}, {"documents", status.document_count},
               {"chunks", status.chunk_count}, {"malformed", status.malformed_count},
               {"embedding_space", status.embedding_space}};
    } else if (!open) return {"ERROR", index.status ().last_error};
    else if (command == "search") {
      result= value::array ();
      for (const auto& hit: index.search (args["query"].get<std::string> (), limit)) {
        auto row= describe (hit);
        row["snippet"]= hit.source;
        row["score"]= hit.score;
        result.push_back (std::move (row));
      }
    } else {
      const auto chunk= index.read_chunk (chunk_id);
      if (!chunk) return {"NOT_FOUND", "No such indexed chunk"};
      result= describe (*chunk);
      result["text"]= chunk->text;
    }
    if (!vault_context_is_current (context)) return {"STALE", "Vault changed"};
    return {"OK", std::move (result)};
  }
};

class rag_resolver_rep final: public resolver {
  resolver_outcome resolve (const resolution_request& req, resolution_output& out) const override {
    if (!req.basepoint || req.state) return resolver_outcome::irrelevant;
    auto parent= std::dynamic_pointer_cast<const vault_resource> (req.basepoint->accessor);
    const auto& s= req.selectors.at (req.offset);
    if (!parent || s.type != selector::kind::name || !s.positions.empty ())
      return resolver_outcome::irrelevant;
    if (parent->type () == "vault" && s.name == "rag")
      out.publish (std::make_shared<rag_resource> (parent->captured_vault ()), req.offset+1);
    else if (parent->type () == "rag" && !s.name.empty ()) {
      const auto context= parent->captured_vault ();
      if (!vault_context_is_current (context)) throw std::runtime_error ("STALE: vault changed");
      rag::RagIndex index;
      if (!open_index (index, context)) throw std::runtime_error (index.status ().last_error);
      if (!index.read_chunk (s.name)) return resolver_outcome::miss;
      out.publish (std::make_shared<rag_resource> (context, s.name), req.offset+1);
    }
    else return resolver_outcome::irrelevant;
    return resolver_outcome::resolved;
  }
};
}
std::shared_ptr<const resolver> rag_resolver () { return std::make_shared<rag_resolver_rep> (); }
}
