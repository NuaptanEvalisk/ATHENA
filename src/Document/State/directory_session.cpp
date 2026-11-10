// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#include "Document/State/directory_session.hpp"
#include "Document/State/auxiliary_cache.hpp"
#include "Document/State/derived_directory.hpp"
#include "Interop/Adapters/interop_document_codec.hpp"
#include "iterator.hpp"
#include "merge_sort.hpp"
#include "tm_configure.hpp"
#include <immer/map.hpp>
#include <sodium.h>
#include <exception>
#include <mutex>

namespace athena::derived {
namespace {
using athena::interop::value;
using references= immer::map<std::string,node::frozen_tree>;
string native (const std::string& value) { return string (value.data (), value.size ()); }
std::string digest (const cache_bytes& bytes) {
  unsigned char hash[crypto_generichash_BYTES];
  crypto_generichash (hash, sizeof hash, bytes.data (), bytes.size (), nullptr, 0);
  char hex[2 * sizeof hash + 1];
  sodium_bin2hex (hex, sizeof hex, hash, sizeof hash);
  return hex;
}

hashmap<string,tree> used_references (const tree& source, const references& bindings) {
  hashmap<string,tree> result (UNINIT);
  std::vector<tree> pending {source};
  while (!pending.empty ()) {
    tree entry= pending.back (); pending.pop_back ();
    if (is_func (entry, PAGEREF, 1) && is_atomic (entry[0])) {
      const string key= entry[0]->label;
      if (const auto* value= bindings.find (std::string (key.data (), N(key))))
        result (key)= tree (*value);
    }
    else if (is_compound (entry))
      for (int i= 0; i < N(entry); ++i) pending.push_back (entry[i]);
  }
  return result;
}

struct publication {
  std::uint64_t generation;
  std::exception_ptr error;
  std::string signature;
  std::map<std::string,tree> views;
};
struct mailbox {
  std::mutex mutex;
  std::unique_ptr<publication> pending;
  std::string applied_signature;
};
struct directory_input {
  directory_request request;
  tree entries;
  hashmap<string,tree> refs;
  std::string receipt;
};

bool compute (auxiliary_cache& cache, const contribution_log& log,
              const references& bindings, std::uint64_t generation,
              const std::shared_ptr<mailbox>& delivery) {
  auto channels= contribution_channels (log);
  std::map<std::string,directory_input> inputs;
  value signature= value::array ();
  for (auto& request: contribution_directories (log)) {
    tree entries= channels->contains (native (request.channel)) ?
      channels[native (request.channel)] : tree (DOCUMENT);
    auto refs= request.kind == "the-index" ? used_references (entries, bindings) :
      hashmap<string,tree> (UNINIT);
    value receipt= {request.kind, request.channel, request.language, request.break_style,
      athena::interop::document_node_to_value_v3 (entries),
      athena::interop::document_node_to_value_v3 (make_collection (refs))};
    const auto key= request.key ();
    inputs.emplace (key, directory_input {
      std::move (request), entries, refs, digest (value::to_msgpack (receipt))});
  }
  for (const auto& [key, input]: inputs) signature.push_back ({key, input.receipt});
  auto output= std::make_unique<publication> ();
  output->generation= generation;
  output->signature= digest (value::to_msgpack (signature));
  {
    std::lock_guard<std::mutex> guard (delivery->mutex);
    if (output->signature == delivery->applied_signature) return false;
  }
  for (const auto& [key, input]: inputs) {
    tree result;
    bool restored= false;
    if (const auto record= cache.lookup (key, input.receipt)) {
      try {
        result= athena::interop::document_node_from_value_v3 (value::from_msgpack (record->bytes));
        restored= true;
      }
      catch (const std::exception&) { /* A damaged record is recomputed below. */ }
    }
    if (!restored) {
      tree declaration= compound (native (input.request.kind), native (input.request.channel), "");
      result= directory (declaration, input.entries, input.refs,
                        native (input.request.language), native (input.request.break_style));
      if (is_func (result, DOCUMENT, 0)) result << tree ("");
      auto bytes= value::to_msgpack (athena::interop::document_node_to_value_v3 (result));
      cache.request ({key, input.receipt, [bytes= std::move (bytes)] { return bytes; }, true, false});
    }
    output->views.emplace (key, std::move (result));
  }
  std::unique_ptr<publication> retired;
  {
    std::lock_guard<std::mutex> guard (delivery->mutex);
    retired.swap (delivery->pending);
    delivery->pending= std::move (output);
  }
  return true;
}
}

struct directory_session::impl {
  std::shared_ptr<auxiliary_cache> cache;
  std::shared_ptr<mailbox> delivery= std::make_shared<mailbox> ();
  contribution_log previous;
  references refs;
  std::uint64_t generation= 0, ticket= 0;
  std::exception_ptr error;
  std::map<std::string,tree> views;
  std::function<void()> ready;
};

directory_session::directory_session (std::filesystem::path source, std::function<void()> ready):
  data (std::make_unique<impl> ()) {
  data->cache= std::make_shared<auxiliary_cache> (
    std::move (source), "directories-v1:" ATHENA_VERSION ":" BUILD_DATE);
  data->ready= std::move (ready);
}
directory_session::~directory_session () { data->cache->close (); }

void directory_session::submit (contribution_log log, hashmap<string,tree> refs,
                               bool pages_changed) {
  if (data->generation && log == data->previous &&
      !(log && log->needs_page_references && pages_changed)) return;
  data->previous= log;
  data->error= nullptr;
  const auto generation= ++data->generation;
  if (log && log->needs_page_references) {
    auto retained= data->refs;
    for (const auto& entry: data->refs)
      if (!refs->contains (native (entry.first))) retained= retained.erase (entry.first);
    for (auto it= iterate (refs); it->busy (); ) {
      const string key= it->next ();
      const std::string name (key.data (), N(key));
      const auto* old= retained.find (name);
      if (!old || !old->matches (refs[key])) retained= retained.set (name, node::frozen_tree (refs[key]));
    }
    data->refs= std::move (retained);
  }
  const std::weak_ptr<auxiliary_cache> cache= data->cache;
  data->ticket= data->cache->post ("$directory-refresh",
    [cache, log= std::move (log), refs= data->refs, generation,
     delivery= data->delivery, ready= data->ready] {
      if (auto owner= cache.lock ()) {
        bool changed= true;
        try { changed= compute (*owner, log, refs, generation, delivery); }
        catch (...) {
          auto failed= std::make_unique<publication> ();
          failed->generation= generation;
          failed->error= std::current_exception ();
          std::lock_guard<std::mutex> guard (delivery->mutex);
          delivery->pending.swap (failed);
        }
        if (changed && ready) ready ();
      }
    });
}

bool directory_session::receive () {
  std::unique_ptr<publication> output;
  {
    std::lock_guard<std::mutex> guard (data->delivery->mutex);
    output= std::move (data->delivery->pending);
    if (output && output->generation == data->generation) {
      data->error= output->error;
      if (!output->error) data->delivery->applied_signature= output->signature;
    }
  }
  if (data->error) std::rethrow_exception (data->error);
  if (!output || output->generation != data->generation) return false;
  data->views= std::move (output->views);
  return true;
}
const std::map<std::string,tree>& directory_session::views () const { return data->views; }
tree directory_session::glossary_view (const std::string& channel) const {
  for (const auto& request: contribution_directories (data->previous))
    if (request.channel == channel &&
        (request.kind == "the-glossary" || request.kind == "list-of-figures" ||
         request.kind == "list-of-tables")) {
      const auto found= data->views.find (request.key ());
      if (found != data->views.end ()) return copy (found->second);
    }
  throw std::runtime_error ("No prepared glossary view for channel " + channel);
}
bool directory_session::wait () {
  data->cache->wait (data->ticket);
  return receive ();
}
}
