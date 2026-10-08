/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#include "revisions.hpp"
#include <iostream>
#include <stdexcept>

using namespace athena::hodarium;

void require (bool value, const char* message) {
  if (!value) throw std::runtime_error (message);
}
template<typename F> void rejects (F f) {
  bool rejected= false;
  try { f (); } catch (const std::invalid_argument&) { rejected= true; }
  require (rejected, "Invalid revision accepted");
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
