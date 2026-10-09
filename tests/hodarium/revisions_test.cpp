/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#include "revisions.hpp"
#include "revision_transfer.hpp"
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

int main () {
  try {
    revision_store store (":memory:");
    revision initial;
    initial.vault= "vault"; initial.object= "object";
    initial.origin_member= "member-A"; initial.format= "ath-xml-v2";
    initial.relative_path= "Notes/example.ath";
    initial.payload= std::string ("body\0bytes", 10);
    auto base= seal_revision (initial);
    check_resumable_receipt (base);
    check_transfer_protocol (base);
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
