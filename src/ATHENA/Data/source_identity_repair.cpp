/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#include "source_identity_repair.hpp"
#include "document_history_store.hpp"
#include "confined_filesystem.hpp"
#include "buffer_actor.hpp"
#include "buffer_state.hpp"
#include "buffer_name_catalog.hpp"
#include "editor.hpp"
#include "node_metadata.hpp"
#include "node_location_cache.hpp"
#include "node_reference.hpp"
#include "Data/Convert/Xml/document_upgrade_file.hpp"
#include "Data/Convert/Xml/document_file_codec.hpp"
#include <mutex>

namespace athena::document_node {
void repair_source_identity (const std::filesystem::path& vault,
    const std::string& path, const std::string& expected,
    std::function<void(bool, std::string)> completed) {
  try {
    filesystem::confined_root root (vault);
    auto entry= root.open (path);
    const auto revision= entry.stat ();
    const auto bytes= entry.read (document::codec_limits ().input_bytes);
    if (!filesystem::same_revision (revision, entry.stat ()))
      throw std::runtime_error ("Document changed; retry identity repair");
    auto decoded= document::decode_native_xml_bytes (bytes);
    tree& source= decoded.document;
    int body= -1;
    for (int i=0; i<N(source); ++i)
      if (is_compound (source[i], "body", 1)) body= i;
    if (body < 0 || node::id (source[body][0]) != expected)
      throw std::runtime_error ("Document identity changed; refresh the conflict list");
    const auto id= node::new_id ();
    const auto hash= document::storage_bytes_fingerprint (bytes);
    history::document_history_store history;
    std::string error; std::int64_t version= 0;
    if (!history.open (vault, error) ||
        !history.protect (path, bytes, "source-identity-repair-" + id, version, error))
      throw std::runtime_error (error);
    const auto name= (vault / path).generic_string ();
    const auto owner= published_buffer_source (name);
    if (owner.first) {
      const auto continuation= actor_continuation_registry::instance ().store (
        [name, expected, id, hash, version= decoded.version, completed] {
          std::string error;
          try {
            const auto* execution= current_scheme_execution_context ();
            if (!execution || !execution->actor || !execution->editor)
              throw std::runtime_error ("Document view is no longer available; retry");
            auto* state= execution->actor->current_state ();
            const auto actual= as_string (execution->actor->current_buffer_url ());
            if (std::string (actual.data (), N(actual)) != name || !state->storage ||
                state->storage->source_sha256 () != hash || state->storage_version != version)
              throw std::runtime_error ("Document changed; retry identity repair");
            auto* editor= execution->editor;
            tree body= editor->the_buffer ();
            if (state->read_only || !state->node_identities ||
                state->node_identities->pending () || editor->get_input_mode () != 0)
              throw std::runtime_error ("Document is read-only or busy; retry identity repair");
            if (node::id (body) != expected)
              throw std::runtime_error ("Document identity has already changed");
            tree header ("");
            auto metadata= *node::get (body); metadata.id= id;
            node::set (header, metadata);
            editor->archive_state ();
            editor->start_editing ();
            try { apply (body, mod_set_metadata (::path (), header)); }
            catch (...) { editor->cancel_editing (); throw; }
            editor->end_editing ();
            if (node::id (editor->the_buffer ()) != id)
              throw std::runtime_error ("Editor rejected the identity change");
          }
          catch (const std::exception& e) { error= e.what (); }
          catch (const string& e) { error.assign (e.data (), N(e)); }
          catch (...) { error= "Document identity repair failed"; }
          completed (true, std::move (error));
        });
      if (!buffer_actor::try_submit_to (owner.first, actor_command_kind::run_native_continuation,
            owner.second, ATHENA_NO_BLOB, ATHENA_NO_BLOB, SCHEME_CAPABILITY_BUFFER, continuation)) {
        actor_continuation_registry::instance ().discard (continuation);
        throw std::runtime_error ("Document actor is busy; retry identity repair");
      }
      return;
    }
    auto metadata= *node::get (source[body][0]); metadata.id= id;
    node::set (source[body][0], metadata);
    const auto output= document::encode_native_xml_bytes (source, decoded.version);
    std::unique_lock<std::recursive_mutex> publication (document_publication_mutex (), std::defer_lock);
    auto saved= root.replace (path, entry, revision, output, [&] {
      if (!publication.try_lock () || published_buffer_source (name).first)
        throw std::runtime_error ("Document was opened; retry identity repair");
    });
    if (!saved.directory_synced)
      throw std::runtime_error ("Identity published but directory fsync failed");
    node_location::persistent_index_wake ();
    node_reference::source_changed ();
    completed (false, {});
  }
  catch (const std::exception& e) { completed (false, e.what ()); }
  catch (const string& e) { completed (false, std::string (e.data (), N(e))); }
}
}
