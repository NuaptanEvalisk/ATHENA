/******************************************************************************
* MODULE     : buffer_actor_test.cpp
* DESCRIPTION: BufferActor ownership, ordering, and shutdown behavior
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include <QtTest/QtTest>

#include "buffer_actor.hpp"
#include "buffer_state.hpp"
#include "buffer_name_catalog.hpp"
#include "tm_buffer.hpp"
#include "ATHENA/Data/interop_document_codec.hpp"
#include "ATHENA/Data/new_buffer.hpp"
#include "ATHENA/Interop/resources.hpp"
#include "vaultfile_json.hpp"
#include "namespaces.hpp"
#include <QScopeGuard>
#include <fstream>
#include <future>
#include "drd_std.hpp"
#include "scheme.hpp"

#include <atomic>
#include <thread>
#include <vector>

bool headless_mode= true;
bool is_headless () { return true; }

class TestBufferActor: public QObject {
  Q_OBJECT

private slots:
  void startsLazily ();
  void ownsCommandsAndDocumentContext ();
  void preservesSynchronousInvocationContext ();
  void drainsInFifoOrderAndRejectsAfterShutdown ();
  void documentNodesRemainActorOwned ();
  void fullSourceRetainsUnknownAttributesAndLiveBody ();
  void onlineResolutionUsesUnsavedBody ();
  void bufferResolutionWithoutVault ();
  void sourceCommitPreservesBorrowedMetadata ();
};

void
TestBufferActor::sourceCommitPreservesBorrowedMetadata () {
  tm_buffer buffer= tm_new<tm_buffer_rep> (url ("actor-metadata-lifetime.ath"));
  auto cleanup= qScopeGuard ([&] { tm_delete (buffer); });
  auto payload= actor_tree_registry::instance ().store (
    tree (DOCUMENT, compound ("TeXmacs", TEXMACS_COMPAT_VERSION),
      compound ("style", tree (TUPLE, "generic")), compound ("body", tree (DOCUMENT, "first"))));
  QVERIFY (buffer->actor->invoke (actor_command_kind::replace_document, ATHENA_NO_VIEW, payload));
  bool body_stable= false, metadata_stable= false, references_updated= false;
  auto id= actor_continuation_registry::instance ().store ([&] {
    auto* actor= current_scheme_execution_context ()->actor;
    auto* state= actor->current_state ();
    // Retain the old owner so a regression reports an assertion, not a UAF.
    // These are the same member references borrowed by edit_env_rep.
    new_data held= state->data;
    auto& refs= held->ref;
    auto& aux= held->aux;
    auto& att= held->att;
    tree& source= actor->current_source ();
    auto& nodes= state->interop_nodes ();
    auto paragraph= nodes.track (source, {2, 0, 0});
    nodes.insert_siblings (source, paragraph, true,
      athena::interop::value::array ({athena::interop::value {{"text", "Hello, World!"}}}));
    actor->commit_current_source ();
    body_stable= &refs == &state->data->ref && &aux == &state->data->aux && &att == &state->data->att;
    tree& refreshed= actor->current_source ();
    for (const char* name: {"references", "auxiliary", "attachments"}) {
      tree field= compound (name, tree (COLLECTION, tree (ASSOCIATE, "key", "updated")));
      bool replaced= false;
      for (int i= 0; i < N (refreshed); ++i)
        if (is_compound (refreshed[i], name)) {
          assign (refreshed[i], field);
          replaced= true;
          break;
        }
      if (!replaced) insert (refreshed, N (refreshed), tree (TUPLE, field));
    }
    actor->commit_current_source ();
    metadata_stable= &refs == &state->data->ref && &aux == &state->data->aux && &att == &state->data->att;
    references_updated= refs["key"] == "updated" && aux["key"] == "updated" && att["key"] == "updated";
  });
  QVERIFY (buffer->actor->invoke (actor_command_kind::run_native_continuation,
    ATHENA_NO_VIEW, ATHENA_NO_BLOB, ATHENA_NO_BLOB, nullptr, SCHEME_CAPABILITY_BUFFER, id));
  QVERIFY (body_stable);
  QVERIFY (metadata_stable);
  QVERIFY (references_updated);
}

void
TestBufferActor::bufferResolutionWithoutVault () {
  using namespace athena::interop;
  QVERIFY (!vault_capture_context ());
  QTemporaryDir temporary;
  url name= url_system (string ((temporary.path ().toStdString () + "/unsaved.ath").c_str ()));
  const url other= url ("tmfs://interop-test/other");
  auto cleanup= qScopeGuard ([&] { publish_active_buffer (0); remove_buffer (name); remove_buffer (other); });
  auto source= [] (const char* text) {
    return tree (DOCUMENT, compound ("TeXmacs", TEXMACS_COMPAT_VERSION),
      compound ("style", tree (TUPLE, "generic")), compound ("body", tree (DOCUMENT, text)));
  };
  // The minimal Scheme test runtime does not load tmfs handlers.
  eval ("(define (tmfs-permission? name permission) #t)");
  set_buffer_tree (name, source ("first"));
  set_buffer_tree (other, source ("other"));
  auto* actor= concrete_buffer (name)->actor;
  const auto id= actor->id ();
  const auto other_id= concrete_buffer (other)->actor->id ();
  publish_active_buffer (id);
  resolution_workers workers (2);
  auto resolve= [&] (const std::string& selector) {
    std::promise<resolution_result> promise;
    auto future= promise.get_future ();
    resolution_ticket ticket (workers, native_resolvers (), parse_selection (selector),
      [&] (resolution_result result) { promise.set_value (std::move (result)); });
    if (future.wait_for (std::chrono::seconds (5)) != std::future_status::ready)
      throw std::runtime_error ("Buffer resolution timed out");
    auto result= future.get ();
    if (result.state != resolution_result::status::complete) throw std::runtime_error (result.error);
    return result;
  };
  auto selected= resolve ("@/buffers/@/document/body/[0]/[0]");
  QCOMPARE (selected.leaves.size (), std::size_t (1));
  auto target= selected.tree.back ()->accessor;
  binding buffer, document;
  for (const auto& item: selected.tree) {
    if (item->accessor->type () == "buffer") buffer= item->accessor;
    if (item->accessor->type () == "document") document= item->accessor;
  }
  QVERIFY (buffer && document);
  QCOMPARE (buffer->properties ().at ("id").get<std::uint64_t> (), id);
  const auto identity= document->identity ();
  publish_active_buffer (other_id);
  QCOMPARE (resolve ("@/buffers/@").tree.back ()->accessor->properties ().at ("id").get<std::uint64_t> (), other_id);
  QCOMPARE (resolve ("@/buffers/" + std::string ("[") + std::to_string (id) + "]").tree.back ()->accessor->identity (), buffer->identity ());
  QCOMPARE (resolve ("@/buffers/?($type = \"buffer\")").leaves.size (), std::size_t (2));
  QCOMPARE (target->operate ("insert_after", {{"siblings", value::array ({value {{"text", "hello world"}}})}}).status,
            std::string ("OK"));
  QCOMPARE (target->operate ("insert_before", {{"siblings", value::array ({value {{"text", "before"}}})}}).status,
            std::string ("OK"));
  QCOMPARE (target->properties ().at ("path").back ().get<int> (), 1);
  QVERIFY (actor->invoke (actor_command_kind::set_buffer_read_only, ATHENA_NO_VIEW,
    ATHENA_NO_BLOB, ATHENA_NO_BLOB, nullptr, SCHEME_CAPABILITY_BUFFER, 1));
  QCOMPARE (target->operate ("insert_after", {{"siblings", value::array ()}}).status, std::string ("READ_ONLY"));
  QVERIFY (actor->invoke (actor_command_kind::set_buffer_read_only, ATHENA_NO_VIEW,
    ATHENA_NO_BLOB, ATHENA_NO_BLOB, nullptr, SCHEME_CAPABILITY_BUFFER, 0));
  QCOMPARE (document->operate ("insert_before", {{"siblings", value::array ()}}).status, std::string ("INVALID_ARGUMENT"));
  auto renamed= url_system (string ((temporary.path ().toStdString () + "/renamed.ath").c_str ()));
  rename_buffer (name, renamed);
  name= renamed;
  QCOMPARE (document->identity (), identity);
  QCOMPARE (target->operate ("get", value::object ()).data.at ("tree"), value ({{"text", "first"}}));
  QCOMPARE (published_file_buffers (std::string (as_string (name).data (), N (as_string (name)))),
            std::vector<std::uint64_t> ({id}));
  remove_buffer (name);
  QCOMPARE (buffer->operate ("get", value::object ()).status, std::string ("STALE"));
  QCOMPARE (document->operate ("get", value::object ()).status, std::string ("STALE"));
  QCOMPARE (target->operate ("get", value::object ()).status, std::string ("STALE"));
  set_buffer_tree (name, source ("new lifetime"));
  QVERIFY (concrete_buffer (name)->actor->id () != id);
  QVERIFY (resolve ("@/buffers/[" + std::to_string (id) + "]").leaves.empty ());
  publish_active_buffer (0);
  QVERIFY (resolve ("@/buffers/@").leaves.empty ());
}

void
TestBufferActor::onlineResolutionUsesUnsavedBody () {
  using namespace athena::interop;
  QTemporaryDir temporary;
  const auto directory= std::filesystem::path (temporary.path ().toStdString ());
  std::string error;
  QVERIFY2 (athena_vaultfile_write (directory, AthenaVaultfileInfo {}, error), error.c_str ());
  QCOMPARE (vault_load (url_system (string (directory.c_str ())), "Interop test", "map.sqlite", "ns.sqlite"), string (""));
  auto close_vault= qScopeGuard ([] { vault_close (); });
  const auto file= directory / "live.ath";
  {
    std::ofstream disk (file);
    disk << "<TeXmacs|" << TEXMACS_COMPAT_VERSION << ">\n\n<style|generic>\n\n<\\body>\nsaved\n</body>\n";
  }
  const auto alias= directory / "alias.ath";
  std::filesystem::create_symlink (file, alias);
  const url name= url_system (string (alias.c_str ()));
  auto remove= qScopeGuard ([&] { remove_buffer (name); });
  resolution_workers workers (2);
  auto resolve= [&] (const char* mode) {
    std::promise<resolution_result> promise;
    auto result= promise.get_future ();
    resolution_ticket ticket (workers, native_resolvers (), parse_selection (
      std::string ("@/vaults/@/filesystem/live.ath/") + mode + "/body/[0]/[0]"),
      [&] (resolution_result value) { promise.set_value (std::move (value)); });
    if (result.wait_for (std::chrono::seconds (5)) != std::future_status::ready)
      throw std::runtime_error ("Online document resolution timed out");
    return result.get ();
  };
  auto closed= resolve ("online");
  QVERIFY2 (closed.state == resolution_result::status::complete, closed.error.c_str ());
  QCOMPARE (closed.leaves.size (), std::size_t (1));
  auto closed_node= closed.tree.back ()->accessor;
  binding stable_document;
  binding file_accessor;
  for (const auto& item: closed.tree)
    if (item->accessor->type () == "document") stable_document= item->accessor;
    else if (item->accessor->type () == "file") file_accessor= item->accessor;
  QVERIFY (stable_document);
  QVERIFY (file_accessor);
  QCOMPARE (file_accessor->operate ("buffers", value::object ()).data, value::array ());
  QCOMPARE (closed_node->operate ("get", value::object ()).data.at ("tree").at ("text").get<std::string> (),
            std::string ("saved"));
  set_buffer_tree (name, tree (DOCUMENT, compound ("TeXmacs", TEXMACS_COMPAT_VERSION),
    compound ("style", tree (TUPLE, "generic")), compound ("body", tree (DOCUMENT, "unsaved"))));
  tm_buffer buffer= concrete_buffer (name);
  QVERIFY (!is_nil (buffer));
  QCOMPARE (file_accessor->operate ("buffers", value::object ()).data,
            value::array ({buffer->actor->id ()}));
  auto online= resolve ("online");
  QVERIFY2 (online.state == resolution_result::status::complete, online.error.c_str ());
  QCOMPARE (online.leaves.size (), std::size_t (1));
  auto node= online.tree.back ()->accessor;
  auto text= [&] { return node->operate ("get", value::object ()).data.at ("tree").at ("text").get<std::string> (); };
  QCOMPARE (text (), std::string ("unsaved"));
  QCOMPARE (closed_node->operate ("get", value::object ()).status, std::string ("STALE"));
  auto saved= resolve ("saved");
  QVERIFY2 (saved.state == resolution_result::status::complete, saved.error.c_str ());
  QCOMPARE (saved.leaves.size (), std::size_t (1));
  QCOMPARE (saved.tree.back ()->accessor->operate ("get", value::object ()).data.at ("tree").at ("text").get<std::string> (),
            std::string ("saved"));
  auto edit= actor_continuation_registry::instance ().store ([] {
    auto* state= current_scheme_execution_context ()->actor->current_state ();
    insert (state->document[0][0], 7, "!");
  });
  QVERIFY (buffer->actor->invoke (actor_command_kind::run_native_continuation,
    ATHENA_NO_VIEW, ATHENA_NO_BLOB, ATHENA_NO_BLOB, nullptr, SCHEME_CAPABILITY_BUFFER, edit));
  QCOMPARE (text (), std::string ("unsaved!"));
  auto body= online.tree.back ()->parent->accessor;
  auto body_field= online.tree.back ()->parent->parent->accessor;
  binding envelope;
  for (const auto& item: online.tree)
    if (item->accessor->type () == "document") envelope= item->accessor;
  QVERIFY (envelope);
  QCOMPARE (envelope->identity (), stable_document->identity ());
  QCOMPARE (envelope->operate ("get", value::object ()).data,
            stable_document->operate ("get", value::object ()).data);
  auto inserted= body->operate ("insert", {{"index", 0}, {"children", value::array ({value {{"text", "prefix"}}})}});
  QVERIFY2 (inserted.status == "OK", inserted.data.dump ().c_str ());
  QVERIFY (!inserted.data.at ("saved").get<bool> ());
  QCOMPARE (text (), std::string ("unsaved!"));
  QCOMPARE (node->properties ().at ("path").back ().get<int> (), 1);
  QCOMPARE (body_field->operate ("erase", value::object ()).status, std::string ("INVALID_ARGUMENT"));
  QCOMPARE (body->operate ("set", {{"tree", {{"text", "non-document body"}}}}).status,
            std::string ("INVALID_ARGUMENT"));
  auto set= node->operate ("set", {{"tree", {{"text", "interop edit"}}}});
  QVERIFY2 (set.status == "OK", set.data.dump ().c_str ());
  QCOMPARE (node->operate ("get", value::object ()).status, std::string ("STALE"));
  QCOMPARE (body->operate ("get", value::object ()).data.at ("tree").at ("children")[1].at ("text").get<std::string> (),
            std::string ("interop edit"));
  const value custom {{"tag", "custom-field"}, {"children", value::array ({value {{"text", "retained"}}})}};
  const value association {{"tag", "associate"},
    {"children", value::array ({value {{"text", "font"}}, value {{"text", "pagella"}}})}};
  const value collection {{"tag", "collection"}, {"children", value::array ({association})}};
  const value initial {{"tag", "initial"}, {"children", value::array ({collection})}};
  const auto count= envelope->properties ().at ("arity").get<int> ();
  auto metadata= envelope->operate ("insert", {{"index", count}, {"children", value::array ({custom, initial})}});
  QVERIFY2 (metadata.status == "OK", metadata.data.dump ().c_str ());
  bool metadata_installed= false;
  auto inspect_data= actor_continuation_registry::instance ().store ([&] {
    auto* state= current_scheme_execution_context ()->actor->current_state ();
    metadata_installed= state->data->init["font"] == "pagella" && state->document[0][1] == "interop edit";
  });
  QVERIFY (buffer->actor->invoke (actor_command_kind::run_native_continuation,
    ATHENA_NO_VIEW, ATHENA_NO_BLOB, ATHENA_NO_BLOB, nullptr, SCHEME_CAPABILITY_BUFFER, inspect_data));
  QVERIFY (metadata_installed);
  actor_command_record result;
  QVERIFY (buffer->actor->invoke (actor_command_kind::query_modified,
    ATHENA_NO_VIEW, ATHENA_NO_BLOB, ATHENA_NO_BLOB, &result));
  QCOMPARE (result.argument[0], std::uint64_t (1));
  QVERIFY (buffer->actor->invoke (actor_command_kind::snapshot_document,
    ATHENA_NO_VIEW, ATHENA_NO_BLOB, ATHENA_NO_BLOB, &result));
  tree snapshot= actor_tree_registry::instance ().take (result.payload0);
  bool preserved= false;
  for (int i= 0; i < N (snapshot); ++i)
    if (is_compound (snapshot[i], "custom-field", 1) && snapshot[i][0] == "retained") preserved= true;
  QVERIFY (preserved);
  QVERIFY (buffer->actor->invoke (actor_command_kind::set_buffer_read_only,
    ATHENA_NO_VIEW, ATHENA_NO_BLOB, ATHENA_NO_BLOB, nullptr, SCHEME_CAPABILITY_BUFFER, 1));
  QCOMPARE (body->operate ("insert", {{"index", 0}, {"children", value::array ()}}).status, std::string ("READ_ONLY"));
  QVERIFY (buffer->actor->invoke (actor_command_kind::set_buffer_read_only,
    ATHENA_NO_VIEW, ATHENA_NO_BLOB, ATHENA_NO_BLOB, nullptr, SCHEME_CAPABILITY_BUFFER, 0));
  QVERIFY (buffer->actor->invoke (actor_command_kind::mark_autosaved));
  QVERIFY (buffer->actor->invoke (actor_command_kind::query_autosaved,
    ATHENA_NO_VIEW, ATHENA_NO_BLOB, ATHENA_NO_BLOB, &result));
  QCOMPARE (result.argument[0], std::uint64_t (0));
  QVERIFY (buffer->actor->invoke (actor_command_kind::query_modified,
    ATHENA_NO_VIEW, ATHENA_NO_BLOB, ATHENA_NO_BLOB, &result));
  QCOMPARE (result.argument[0], std::uint64_t (1));
  auto disk_after= resolve ("saved");
  QCOMPARE (disk_after.tree.back ()->accessor->operate ("get", value::object ()).data.at ("tree").at ("text").get<std::string> (),
            std::string ("saved"));
  QVERIFY (buffer->actor->invoke (actor_command_kind::mark_saved));
  QVERIFY (buffer->actor->invoke (actor_command_kind::query_modified,
    ATHENA_NO_VIEW, ATHENA_NO_BLOB, ATHENA_NO_BLOB, &result));
  QCOMPARE (result.argument[0], std::uint64_t (0));
  const value replacement {{"tag", "document"}, {"children", value::array ({
    value {{"tag", "TeXmacs"}, {"children", value::array ({value {{"text", TEXMACS_COMPAT_VERSION}}})}},
    value {{"tag", "style"}, {"children", value::array ({value {
      {"tag", "tuple"}, {"children", value::array ({value {{"text", "generic"}}})}}})}},
    value {{"tag", "body"}, {"children", value::array ({value {
      {"tag", "document"}, {"children", value::array ({value {{"text", "replacement"}}})}}})}}
  })}};
  auto replaced= envelope->operate ("set", {{"tree", replacement}});
  QVERIFY2 (replaced.status == "OK", replaced.data.dump ().c_str ());
  QCOMPARE (envelope->operate ("get", value::object ()).data.at ("tree"), replacement);
  QCOMPARE (stable_document->operate ("get", value::object ()).data.at ("tree"), replacement);
  auto reloaded= resolve ("online");
  QVERIFY2 (reloaded.state == resolution_result::status::complete, reloaded.error.c_str ());
  QCOMPARE (reloaded.leaves.size (), std::size_t (1));
  QCOMPARE (reloaded.tree.back ()->accessor->operate ("get", value::object ()).data.at ("tree").at ("text").get<std::string> (),
            std::string ("replacement"));
  remove_buffer (name);
  buffer= nullptr;
  QCOMPARE (body->operate ("get", value::object ()).status, std::string ("STALE"));
  QCOMPARE (reloaded.tree.back ()->accessor->operate ("get", value::object ()).status, std::string ("STALE"));
  QCOMPARE (stable_document->operate ("get", value::object ()).status, std::string ("OK"));
  auto disk_again= resolve ("online");
  QVERIFY2 (disk_again.state == resolution_result::status::complete, disk_again.error.c_str ());
  QCOMPARE (disk_again.tree.back ()->accessor->operate ("get", value::object ()).data.at ("tree").at ("text").get<std::string> (),
            std::string ("saved"));
  set_buffer_tree (name, tree (DOCUMENT, compound ("TeXmacs", TEXMACS_COMPAT_VERSION),
    compound ("style", tree (TUPLE, "generic")), compound ("body", tree (DOCUMENT, "reopened"))));
  auto reopened= resolve ("online");
  QVERIFY2 (reopened.state == resolution_result::status::complete, reopened.error.c_str ());
  QCOMPARE (reopened.tree.back ()->accessor->operate ("get", value::object ()).data.at ("tree").at ("text").get<std::string> (),
            std::string ("reopened"));
  QCOMPARE (disk_again.tree.back ()->accessor->operate ("get", value::object ()).status, std::string ("STALE"));
  binding reopened_document;
  for (const auto& item: reopened.tree)
    if (item->accessor->type () == "document") reopened_document= item->accessor;
  QVERIFY (reopened_document);
  QCOMPARE (reopened_document->identity (), stable_document->identity ());
}

void
TestBufferActor::fullSourceRetainsUnknownAttributesAndLiveBody () {
  using namespace athena::interop;
  tm_buffer buffer= tm_new<tm_buffer_rep> (url ("actor-full-source-test.ath"));
  auto command= [&] (std::function<void ()> action) {
    auto id= actor_continuation_registry::instance ().store (std::move (action));
    return buffer->actor->invoke (actor_command_kind::run_native_continuation,
      ATHENA_NO_VIEW, ATHENA_NO_BLOB, ATHENA_NO_BLOB, nullptr,
      SCHEME_CAPABILITY_BUFFER, id);
  };
  tree document (DOCUMENT, compound ("TeXmacs", "2.1.4"),
    compound ("style", tree (TUPLE, "generic")),
    compound ("custom-metadata", "preserve"),
    compound ("body", tree (DOCUMENT, compound ("transclude", "id", "source.ath", "", ""))),
    compound ("initial", tree (COLLECTION,
      tree (ASSOCIATE, "font", "pagella"), tree (ASSOCIATE, "zoom-factor", "2"))));
  auto payload= actor_tree_registry::instance ().store (std::move (document));
  QVERIFY (buffer->actor->invoke (actor_command_kind::replace_document,
    ATHENA_NO_VIEW, payload));
  document_node root, metadata, font, transclusion;
  value initial, changed;
  bool identities_preserved= false;
  QVERIFY (command ([&] {
    auto* actor= current_scheme_execution_context ()->actor;
    auto* state= actor->current_state ();
    tree& source= actor->current_source ();
    auto& nodes= state->interop_nodes ();
    root= nodes.track (source, {});
    metadata= nodes.track (source, {2});
    transclusion= nodes.track (source, {3, 0, 0});
    font= nodes.track (source, {4, 0, 0, 1});
    initial= nodes.read (source, root);
    state->data->init ("zoom-factor")= "3";
    insert (state->document[0], 0, tree (TUPLE, "live edit"));
    tree& refreshed= actor->current_source ();
    identities_preserved= nodes.track (refreshed, {}) == root &&
      nodes.track (refreshed, {2}) == metadata &&
      nodes.track (refreshed, {4, 0, 0, 1}) == font &&
      nodes.track (refreshed, {3, 0, 1}) == transclusion;
    changed= nodes.read (refreshed, root);
  }));
  QVERIFY (identities_preserved);
  QCOMPARE (initial.at ("children")[2].at ("tag").get<std::string> (), std::string ("custom-metadata"));
  QCOMPARE (changed.at ("children")[3].at ("children")[0].at ("children")[0].at ("text").get<std::string> (),
    std::string ("live edit"));
  QCOMPARE (changed.at ("children")[3].at ("children")[0].at ("children")[1].at ("tag").get<std::string> (),
    std::string ("transclude"));
  QCOMPARE (changed.at ("children")[4].at ("children")[0].at ("children")[1].at ("children")[1].at ("text").get<std::string> (),
    std::string ("3"));
  tm_delete (buffer);
}

void
TestBufferActor::documentNodesRemainActorOwned () {
  using namespace athena::interop;
  tm_buffer buffer= tm_new<tm_buffer_rep> (url ("actor-interop-test.ath"));
  document_node lease;
  value encoded;
  auto command= [&] (std::function<void ()> action) {
    auto id= actor_continuation_registry::instance ().store (std::move (action));
    return buffer->actor->invoke (actor_command_kind::run_native_continuation,
      ATHENA_NO_VIEW, ATHENA_NO_BLOB, ATHENA_NO_BLOB, nullptr,
      SCHEME_CAPABILITY_BUFFER, id);
  };
  QVERIFY (command ([&] {
    auto* state= current_scheme_execution_context ()->actor->current_state ();
    set_document (state->document, state->root_path, tree (DOCUMENT, "first", "second"));
    lease= state->interop_nodes ().track (state->document, {0, 1});
    encoded= document_node_to_value (state->document[0][1]);
  }));
  QVERIFY (encoded == value ({{"text", "second"}}));
  bool follows= false;
  QVERIFY (command ([&] {
    auto* state= current_scheme_execution_context ()->actor->current_state ();
    insert (state->document[0], 0, tree (TUPLE, "prefix"));
    follows= state->interop_nodes ().locate (state->document, lease) == document_node_path {0, 2};
  }));
  QVERIFY (follows);
  // Shutdown destroys every native observer on the actor, even with a live lease.
  tm_delete (buffer);
  std::thread releaser ([held= std::move (lease)] () mutable { held.reset (); });
  releaser.join ();
}

void
TestBufferActor::startsLazily () {
  tm_buffer buffer= tm_new<tm_buffer_rep> (url ("actor-lazy-test.ath"));
  QCOMPARE (buffer->actor->owner_thread (), std::thread::id ());
  QCOMPARE (buffer->actor->completed_commands (), std::uint64_t (0));
  tm_delete (buffer);
}

void
TestBufferActor::preservesSynchronousInvocationContext () {
  tm_buffer buffer= tm_new<tm_buffer_rep> (url ("actor-context-test.ath"));
  const athena_view_id expected_view= 7;
  bool valid_context= false;
  athena_continuation_id continuation=
    actor_continuation_registry::instance ().store ([&] {
    const SchemeExecutionContext* context=
      current_scheme_execution_context ();
    valid_context= context != nullptr && context->actor == buffer->actor &&
                   context->view_id == expected_view &&
                   context->has (SCHEME_CAPABILITY_BUFFER);
  });
  QVERIFY (buffer->actor->invoke (
    actor_command_kind::run_native_continuation, expected_view,
    ATHENA_NO_BLOB, ATHENA_NO_BLOB, nullptr, SCHEME_CAPABILITY_BUFFER,
    continuation));

  QVERIFY (valid_context);
  tm_delete (buffer);
}

void
TestBufferActor::ownsCommandsAndDocumentContext () {
  tm_buffer buffer= tm_new<tm_buffer_rep> (url ("actor-test.ath"));
  std::thread::id caller= std::this_thread::get_id ();
  std::thread::id executor;
  bool valid_context= false;

  athena_continuation_id continuation=
    actor_continuation_registry::instance ().store ([&] {
    executor= std::this_thread::get_id ();
    const SchemeExecutionContext* context=
      current_scheme_execution_context ();
    valid_context= context != nullptr && context->actor == buffer->actor &&
                   context->command_id != 0 &&
                   buffer->actor->current_state () != nullptr &&
                   &current_document_tree () ==
                     &buffer->actor->current_state ()->document;
  });
  QVERIFY (buffer->actor->invoke (
    actor_command_kind::run_native_continuation, ATHENA_NO_VIEW,
    ATHENA_NO_BLOB, ATHENA_NO_BLOB, nullptr, SCHEME_CAPABILITY_BUFFER,
    continuation));

  QVERIFY (executor != caller);
  QCOMPARE (executor, buffer->actor->owner_thread ());
  QVERIFY (valid_context);
  tm_delete (buffer);
}

void
TestBufferActor::drainsInFifoOrderAndRejectsAfterShutdown () {
  tm_buffer buffer= tm_new<tm_buffer_rep> (url ("actor-order-test.ath"));
  std::vector<int> observed;
  for (int value= 1; value <= 4; ++value) {
    athena_continuation_id continuation=
      actor_continuation_registry::instance ().store (
        [&observed, value] { observed.push_back (value); });
    QVERIFY (buffer->actor->submit (
      actor_command_kind::run_native_continuation, ATHENA_NO_VIEW,
      ATHENA_NO_BLOB, ATHENA_NO_BLOB, SCHEME_CAPABILITY_BUFFER,
      continuation));
  }

  buffer->actor->wait_until_idle ();
  QCOMPARE (observed, std::vector<int> ({ 1, 2, 3, 4 }));
  // wait_until_idle adds a barrier after the four application commands.
  QCOMPARE (buffer->actor->completed_commands (), std::uint64_t (5));

  buffer->actor->shutdown ();
  athena_continuation_id rejected=
    actor_continuation_registry::instance ().store ([] {});
  QVERIFY (!buffer->actor->try_submit (
    actor_command_kind::run_native_continuation, ATHENA_NO_VIEW,
    ATHENA_NO_BLOB, ATHENA_NO_BLOB, SCHEME_CAPABILITY_BUFFER, rejected));
  QVERIFY (actor_continuation_registry::instance ().discard (rejected));
  tm_delete (buffer);
}

static void run_tests (int argc, char** argv) {
  int result;
  {
    QTemporaryDir profile;
    if (!profile.isValid ()) std::exit (1);
    qputenv ("ATHENA_HOME_PATH", profile.path ().toUtf8 ());
    QApplication application (argc, argv);
    init_std_drd ();
    initialize_scheme ();
    TestBufferActor test;
    result= QTest::qExec (&test, argc, argv);
  }
  std::exit (result);
}

int main (int argc, char** argv) {
  qputenv ("GUILE_AUTO_COMPILE", "0");
  start_scheme (argc, argv, run_tests);
  return 1;
}
#include "buffer_actor_test.moc"
