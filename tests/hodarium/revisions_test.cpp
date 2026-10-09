/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#include "revisions.hpp"
#include "revision_transfer.hpp"
#include "client_settings.hpp"
#include <iostream>
#include <stdexcept>
#include <QTemporaryDir>

using namespace athena::hodarium;

void require (bool value, const char* message) {
  if (!value) throw std::runtime_error (message);
}
template<typename F> void rejects (F f) {
  bool rejected= false;
  try { f (); } catch (const std::invalid_argument&) { rejected= true; }
  require (rejected, "Invalid revision accepted");
}
void check_resumable_receipt (const revision& base) {
  QTemporaryDir directory;
  require (directory.isValid (), "Cannot create isolated revision directory");
  auto path= std::filesystem::path (directory.path ().toStdString ()) / "revisions.sqlite";
  auto next= base; next.parents= {base.id}; next.payload= std::string (700*1024, 'r');
  next.payload[17]= '\0'; next.payload[600000]= char (255); next= seal_revision (next);
  auto metadata= next; metadata.payload.clear ();
  {
    revision_store target (path);
    target.receive (base); target.record_applied (base.id, std::nullopt);
    require (target.begin_receive (metadata, next.payload.size ()) == 0, "New transfer has bytes");
    require (target.receive_chunk (next.id, 0, next.payload.substr (0, 128*1024)) == 128*1024,
             "First chunk was not acknowledged");
    rejects ([&] { target.receive_chunk (next.id, 0, "replay"); });
    rejects ([&] { target.finish_receive (next.id); });
    require (!target.get (next.id), "Partial transfer entered immutable revisions");
  }
  {
    revision_store target (path);
    auto offset= target.begin_receive (metadata, next.payload.size ());
    require (offset == 128*1024, "Restart lost durable transfer offset");
    auto conflicting= metadata; conflicting.relative_path+= "wrong";
    rejects ([&] { target.begin_receive (conflicting, next.payload.size ()); });
    while (offset < next.payload.size ())
      offset= target.receive_chunk (next.id, offset, next.payload.substr (offset, 128*1024));
    require (target.finish_receive (next.id), "Complete transfer was not committed");
    require (target.get (next.id)->payload == next.payload, "Transferred binary payload changed");
    require (target.applied (base.vault, base.object) == base.id, "Receipt changed the applied revision");
    require (target.begin_receive (metadata, next.payload.size ()) == next.payload.size (), "Known transfer not recognized");
    require (!target.finish_receive (next.id), "Known revision was published twice");
    auto offered= target.offer (next.id);
    require (offered && offered->metadata.payload.empty () && offered->size == next.payload.size (),
             "Offer loaded or changed payload");
    std::string streamed;
    while (streamed.size () < offered->size) streamed+= target.payload_chunk (next.id, streamed.size ());
    require (streamed == next.payload, "Incremental outgoing payload changed");
    auto corrupt= next; corrupt.payload= "expected"; corrupt= seal_revision (corrupt);
    auto header= corrupt; header.payload.clear ();
    target.begin_receive (header, corrupt.payload.size ());
    target.receive_chunk (corrupt.id, 0, "tampered");
    rejects ([&] { target.finish_receive (corrupt.id); });
    require (!target.get (corrupt.id), "Hash failure published a revision");
    target.discard_receive (corrupt.id);
    require (target.begin_receive (header, corrupt.payload.size ()) == 0, "Discard retained corrupt bytes");
    target.discard_receive (corrupt.id);
    auto tombstone= next; tombstone.parents= {next.id}; tombstone.deleted= true;
    tombstone.payload.clear (); tombstone= seal_revision (tombstone);
    target.begin_receive (tombstone, 0);
    require (target.finish_receive (tombstone.id), "Empty tombstone transfer failed");
  }
}
void check_transfer_protocol (revision value) {
  value.payload= std::string (800*1024, 'p'); value= seal_revision (value);
  revision_store source (":memory:"), target (":memory:"); source.receive (value);
  bool permitted= true;
  auto allowed= [&] (const std::string& vault) { return permitted && vault == value.vault; };
  {
    revision_sender sender (source, value.vault, value.id, allowed);
    revision_receiver receiver (target, allowed);
    auto chunk= sender.acknowledge (receiver.accept (sender.begin ()));
    auto acknowledged= receiver.accept (chunk);
    // Simulate losing the acknowledgement and both per-connection controllers.
    require (!acknowledged.isEmpty () && !target.get (value.id), "Partial wire transfer was published");
  }
  {
    revision_sender sender (source, value.vault, value.id, allowed);
    revision_receiver receiver (target, allowed);
    auto message= sender.begin ();
    int messages= 0;
    while (!sender.complete ()) {
      require (++messages <= 8, "Revision resume did not converge");
      message= sender.acknowledge (receiver.accept (message));
    }
    require (messages == 5, "Wire transfer did not resume after its committed first chunk");
    require (target.get (value.id)->payload == value.payload, "Wire transfer changed content");
    require (!target.applied (value.vault, value.object), "Wire transfer applied content");
  }
  {
    revision_sender sender (source, value.vault, value.id, allowed);
    revision_receiver receiver (target, allowed);
    auto message= sender.begin ();
    permitted= false;
    rejects ([&] { receiver.accept (message); });
    permitted= true;
    auto acknowledgement= receiver.accept (message);
    permitted= false;
    rejects ([&] { sender.acknowledge (acknowledgement); });
    permitted= true;
    auto finish= sender.acknowledge (acknowledgement);
    permitted= false;
    rejects ([&] { receiver.accept (finish); });
  }
}

void check_vault_exchange (revision base) {
  revision_store source (":memory:"), target (":memory:");
  source.receive (base);
  auto a= base; a.parents= {base.id}; a.payload= "left"; a= seal_revision (a);
  auto b= a; b.payload= "right"; b= seal_revision (b);
  source.receive (a); source.receive (b);
  bool allowed= true;
  auto grant= [&] (const std::string& vault) { return allowed && vault == base.vault; };
  auto run= [&] (vault_revision_sender& sender, QByteArray message) {
    revision_receiver receiver (target, grant);
    int requests= 0;
    while (!sender.complete ()) {
      require (++requests < 1000, "Vault exchange did not converge");
      message= sender.acknowledge (receiver.accept (message));
    }
    require (message.isEmpty (), "Completed exchange emitted a request");
    return requests;
  };
  vault_revision_sender first (source, base.vault, grant);
  auto message= first.begin ();
  // A merge arriving after begin must not hide either snapshot head.
  auto merged= a; merged.parents= {a.id,b.id}; merged.payload= "merged"; merged= seal_revision (merged);
  source.receive (merged);
  run (first, message);
  require (target.heads (base.vault, base.object).size () == 2, "Snapshot heads were lost");
  require (target.contains (base.vault, a.id) && target.contains (base.vault, b.id) &&
           !target.contains (base.vault, merged.id), "Exchange changed its discovery snapshot");
  require (!target.applied (base.vault, base.object), "Discovery applied document content");
  vault_revision_sender next (source, base.vault, grant, first.completed_through ());
  run (next, next.begin ());
  require (target.heads (base.vault, base.object) == std::vector<std::string>{merged.id}, "Merge failed to converge");
  vault_revision_sender unchanged (source, base.vault, grant, next.completed_through ());
  require (run (unchanged, unchanged.begin ()) == 0, "Unchanged session sent inventory again");
  vault_revision_sender reconnect (source, base.vault, grant);
  require (run (reconnect, reconnect.begin ()) == 1, "Known head did not prune its ancestry");
  // More than one inventory page, including an object from another Vault.
  for (int i= 0; i < 70; ++i) {
    auto object= base; object.object= "object-" + std::to_string (i);
    source.receive (seal_revision (object));
  }
  auto foreign= base; foreign.vault= "other-vault"; foreign= seal_revision (foreign); source.receive (foreign);
  vault_revision_sender pages (source, base.vault, grant, next.completed_through ());
  run (pages, pages.begin ());
  require (target.inventory_heads (base.vault, 0, target.inventory_tip (base.vault), 256).size () == 71,
           "Paged inventory omitted objects");
  require (!target.contains (foreign.vault, foreign.id), "Inventory leaked another Vault");
  vault_revision_sender revoked (source, base.vault, grant);
  message= revoked.begin ();
  revision_receiver receiver (target, grant);
  auto ack= receiver.accept (message); allowed= false;
  rejects ([&] { revoked.acknowledge (ack); });
  rejects ([&] { receiver.accept (message); });
}

void check_vault_bindings () {
  QTemporaryDir directory;
  require (directory.isValid (), "Cannot create isolated settings directory");
  auto root= std::filesystem::path (directory.path ().toStdString ());
  std::filesystem::create_directories (root / "first" / "nested");
  std::filesystem::create_directory (root / "second");
  client_profile profile;
  profile.origin= QUrl ("https://authority.invalid");
  const std::string key (43, 'A');
  profile.pin= {key, key, key}; profile.recovery_public_key= key;
  profile.device= {key, key}; profile.name= "isolated";
  vault_binding binding{key, "10000000-0000-4000-8000-000000000001", root / "first", true};
  {
    client_settings settings (root / "settings.sqlite");
    settings.add_pending (profile);
    rejects ([&] { settings.bind_vault (binding); });
    settings.complete_admission (key, key, key, key);
    settings.bind_vault (binding);
    auto overlap= binding; overlap.vault= "10000000-0000-4000-8000-000000000002";
    overlap.root/= "nested";
    rejects ([&] { settings.bind_vault (overlap); });
    overlap.root= root;
    rejects ([&] { settings.bind_vault (overlap); });
    auto invalid= binding; invalid.vault= "../remote-path";
    rejects ([&] { settings.bind_vault (invalid); });
    settings.set_vault_enabled (key, binding.vault, false);
  }
  {
    client_settings settings (root / "settings.sqlite");
    auto bindings= settings.vaults (key);
    require (bindings.size () == 1 && !bindings[0].enabled && bindings[0].root == binding.root,
             "Vault selection did not survive settings reopen");
    settings.unbind_vault (key, binding.vault);
    require (settings.vaults (key).empty () && std::filesystem::exists (binding.root / "nested"),
             "Unbinding modified local Vault contents");
  }
}

void check_saved_capture (revision snapshot) {
  revision_store store (":memory:");
  snapshot.id.clear (); snapshot.parents.clear ();
  auto first= store.capture_saved (snapshot, std::nullopt);
  require (first && store.applied (snapshot.vault, snapshot.object) == first,
           "Save did not atomically establish applied revision");
  auto tip= store.inventory_tip (snapshot.vault);
  snapshot.origin_member= "another-saving-device";
  require (store.capture_saved (snapshot, first) == first && store.inventory_tip (snapshot.vault) == tip,
           "Unchanged save created a new revision");
  auto remote= snapshot; remote.parents= {*first}; remote.payload= "remote concurrent content";
  remote= seal_revision (remote); store.receive (remote);
  snapshot.payload= "local edit";
  tip= store.inventory_tip (snapshot.vault);
  require (!store.capture_saved (snapshot, first, std::string (64, '0')) &&
           store.inventory_tip (snapshot.vault) == tip, "Mismatched saved predecessor was accepted");
  auto local= store.capture_saved (snapshot, first, store.payload_fingerprint (*first));
  require (local && store.get (*local)->parents == std::vector<std::string>{*first} &&
           store.compare (*local, remote.id) == ancestry::concurrent,
           "Local save silently merged a received remote head");
  tip= store.inventory_tip (snapshot.vault);
  snapshot.payload= "stale save notification";
  require (!store.capture_saved (snapshot, first) && store.inventory_tip (snapshot.vault) == tip,
           "Stale save published a revision");
  snapshot.payload= "local edit";
  snapshot.relative_path= "Notes/renamed.ath";
  auto renamed= store.capture_saved (snapshot, local);
  require (renamed && renamed != local && store.get (*renamed)->relative_path == snapshot.relative_path,
           "Rename was mistaken for unchanged content");
  snapshot.parents= {remote.id};
  rejects ([&] { store.capture_saved (snapshot, renamed); });
  snapshot.parents.clear (); snapshot.deleted= true; snapshot.payload.clear ();
  auto deleted= store.capture_saved (snapshot, renamed);
  require (deleted && store.get (*deleted)->deleted, "Durable deletion did not create a tombstone");
}

int main () {
  try {
    check_vault_bindings ();
    revision_store store (":memory:");
    revision initial;
    initial.vault= "vault"; initial.object= "object";
    initial.origin_member= "member-A"; initial.format= "ath-xml-v2";
    initial.relative_path= "Notes/example.ath";
    initial.payload= std::string ("body\0bytes", 10);
    auto base= seal_revision (initial);
    check_saved_capture (base);
    check_resumable_receipt (base);
    check_transfer_protocol (base);
    check_vault_exchange (base);
    require (store.receive (base), "Initial receipt failed");
    require (!store.receive (base), "Duplicate receipt not idempotent");
    require (!store.applied (base.vault, base.object), "Receipt applied content");
    require (store.get (base.id)->payload == base.payload, "Binary payload changed");
    require (store.record_applied (base.id, std::nullopt), "Initial apply failed");

    initial.parents= {base.id}; initial.payload= "edited by A";
    auto a= seal_revision (initial);
    initial.origin_member= "member-B"; initial.payload= "edited by B";
    auto b= seal_revision (initial);
    store.receive (a); store.receive (b);
    require (store.compare (a.id, b.id) == ancestry::concurrent, "Lost branches");
    require (store.compare (base.id, a.id) == ancestry::ancestor, "Wrong ancestry");
    require (store.compare (a.id, base.id) == ancestry::descendant, "Wrong descendant");
    require (store.heads (base.vault, base.object).size () == 2, "Wrong heads");
    require (store.merge_bases (a.id, b.id) == std::vector<std::string>{base.id},
             "Wrong merge base");
    require (store.record_applied (a.id, base.id), "Descendant apply failed");
    require (!store.record_applied (b.id, base.id), "Stale apply succeeded");
    rejects ([&] { store.record_applied (base.id, a.id); });

    // Adopting unchanged branch content still resolves both causal parents.
    initial.payload= a.payload; initial.parents= {b.id, a.id, b.id};
    auto merged= seal_revision (initial);
    store.receive (merged);
    require (merged.parents.size () == 2, "Parents not canonicalized");
    require (store.compare (b.id, merged.id) == ancestry::ancestor, "Merge lost parent");
    require (store.record_applied (merged.id, a.id), "Merge apply failed");
    require (store.heads (base.vault, base.object) ==
      std::vector<std::string>{merged.id}, "Merge did not converge");

    auto alternate= merged;
    alternate.origin_member= "member-C";
    alternate.payload= "alternative merge";
    alternate= seal_revision (alternate);
    store.receive (alternate);
    auto bases= store.merge_bases (merged.id, alternate.id);
    require (bases.size () == 2, "Criss-cross merge lost a base");

    auto tampered= merged; tampered.payload+= "!";
    rejects ([&] { store.receive (tampered); });
    initial.object= "other-object";
    auto unrelated= seal_revision (initial);
    rejects ([&] { store.receive (unrelated); });
    initial.object= base.object; initial.parents= {std::string (64, 'a')};
    auto missing= seal_revision (initial);
    rejects ([&] { store.receive (missing); });
    require (!store.get (missing.id), "Failed receipt was partially stored");

    initial.parents= {merged.id}; initial.deleted= true;
    rejects ([&] { seal_revision (initial); });
    initial.payload.clear ();
    auto deleted= seal_revision (initial);
    store.receive (deleted);
    require (store.record_applied (deleted.id, merged.id), "Delete apply failed");
    initial.deleted= false; initial.payload= base.payload; initial.parents= {deleted.id};
    auto restored= seal_revision (initial);
    store.receive (restored);
    require (restored.id != base.id, "Restore rolled back history");
    require (store.record_applied (restored.id, deleted.id), "Restore apply failed");
    std::cout << "Hodarium revision causality, integrity and application checks passed\n";
  }
  catch (const std::exception& e) { std::cerr << e.what () << '\n'; return 1; }
}
