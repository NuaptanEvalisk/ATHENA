/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#include "membership.hpp"
#include "conflict_store.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

using namespace athena::hodarium;
using json= nlohmann::json;
void require (bool value, const char* message) {
  if (!value) throw std::runtime_error (message);
}
template<typename F> void rejects (F f) {
  bool rejected= false;
  try { f (); } catch (const std::exception&) { rejected= true; }
  require (rejected, "Invalid authority statement accepted");
}
struct temporary_store {
  std::filesystem::path directory;
  temporary_store () {
    auto name= (std::filesystem::temp_directory_path () /
      "athena-hodarium-membership-XXXXXX").string ();
    auto* created= mkdtemp (name.data ());
    require (created != nullptr, "Cannot create isolated membership store");
    directory= created;
  }
  ~temporary_store () {
    std::error_code error;
    std::filesystem::remove_all (directory, error);
  }
};
int main (int argc, char** argv) {
  try {
    require (argc == 2, "Expected Go authority fixture path");
    std::ifstream input (argv[1]); json fixture; input >> fixture;
    authority_pin pin{fixture.at ("group"), fixture.at ("authority"),
                      fixture.at ("generation")};
    auto response= fixture.at ("validation");
    const std::string nonce= fixture.at ("nonce"), member= fixture.at ("member"),
                      key= fixture.at ("public_key");
    auto result= verify_validation (pin, response.dump (), nonce, member, key);
    require (result.state.revision == 2 && result.state.members.size () == 1 &&
      result.lifetime == std::chrono::hours (24), "Wrong Go authority validation");
    const auto& decision_request= fixture.at ("decision_request");
    auto decision_response= fixture.at ("decision_receipt").dump ();
    const std::string decision_nonce= fixture.at ("decision_nonce"),
      decision_subject= fixture.at ("decision_subject"),
      vault= decision_request.at ("vault"), conflict= decision_request.at ("conflict");
    auto decision= verify_decision (pin, decision_response, result.state.epoch,
      decision_nonce, decision_subject, vault, conflict);
    require (decision && decision->version == 1 && decision->member == member &&
      decision->request == decision_request.at ("request_id") &&
      decision->branches == decision_request.at ("branches") &&
      decision->resolution == decision_request.at ("resolution"),
      "Wrong Go authority conflict decision");
    rejects ([&] { verify_decision (pin, decision_response, nonce,
      decision_nonce, decision_subject, vault, conflict); });
    rejects ([&] { verify_decision (pin, decision_response, result.state.epoch,
      nonce, decision_subject, vault, conflict); });
    rejects ([&] { verify_decision (pin, decision_response, result.state.epoch,
      decision_nonce, nonce, vault, conflict); });
    rejects ([&] { verify_decision (pin, decision_response, result.state.epoch,
      decision_nonce, decision_subject, conflict, vault); });
    rejects ([&] { verify_validation (pin, response.dump (), member, member, key); });
    const auto& registration_request= fixture.at ("vault_registration_request");
    auto registration_response= fixture.at ("vault_registration_receipt").dump ();
    const std::string registration_nonce= fixture.at ("vault_registration_nonce"),
      registration_subject= fixture.at ("vault_registration_subject"), slot= registration_request.at ("slot");
    auto registration= verify_vault_registration (pin, registration_response, result.state.epoch,
      registration_nonce, registration_subject, slot);
    require (registration && registration->member == member &&
      registration->commitment == registration_request.at ("commitment"), "Wrong Go Vault registration receipt");
    rejects ([&] { verify_vault_registration (pin, registration_response, nonce,
      registration_nonce, registration_subject, slot); });
    rejects ([&] { verify_vault_registration (pin, registration_response, result.state.epoch,
      nonce, registration_subject, slot); });
    rejects ([&] { verify_vault_registration (pin, registration_response, result.state.epoch,
      registration_nonce, nonce, slot); });
    rejects ([&] { verify_vault_registration (pin, registration_response, result.state.epoch,
      registration_nonce, registration_subject, nonce); });
    auto damaged_registration= fixture.at ("vault_registration_receipt");
    damaged_registration["signature"]= fixture.at ("decision_receipt").at ("signature");
    rejects ([&] { verify_vault_registration (pin, damaged_registration.dump (), result.state.epoch,
      registration_nonce, registration_subject, slot); });
    rejects ([&] { verify_validation (pin, response.dump (), nonce, member, pin.public_key); });
    auto wrong_pin= pin; wrong_pin.generation= nonce;
    rejects ([&] { verify_membership (wrong_pin, response.at ("state").dump ()); });
    wrong_pin= pin; wrong_pin.public_key= key;
    rejects ([&] { verify_membership (wrong_pin, response.at ("state").dump ()); });
    auto corrupt= response;
    corrupt["max_offline_seconds"]= 86401;
    rejects ([&] { verify_validation (pin, corrupt.dump (), nonce, member, key); });
    corrupt= response;
    corrupt["max_offline_seconds"]= 10;
    rejects ([&] { verify_validation (pin, corrupt.dump (), nonce, member, key); });
    auto duplicated= response.dump ();
    duplicated.insert (1, "\"member\":\"duplicate\",");
    rejects ([&] { verify_validation (pin, duplicated, nonce, member, key); });
    temporary_store temporary;
    revision_store revisions (temporary.directory / "revisions.sqlite");
    revision base;
    base.vault= "local-vault"; base.object= "document"; base.origin_member= member;
    base.format= "test"; base.relative_path= "note.ath"; base.payload= "base";
    base= seal_revision (base); revisions.receive (base);
    auto left= base; left.parents= {base.id}; left.payload= "left"; left= seal_revision (left);
    auto right= left; right.payload= "right"; right= seal_revision (right);
    revisions.receive (left); revisions.receive (right);
    auto branches= revisions.heads (base.vault, base.object);
    conflict_proposal proposal;
    proposal.request= {vault, conflict, decision_request.at ("request_id"),
      decision_request.at ("branches"), decision_request.at ("resolution"), 0};
    proposal.resolution= left; proposal.resolution.parents= branches;
    proposal.resolution.payload= "manually merged";
    proposal.resolution= seal_revision (proposal.resolution);
    auto conflict_path= temporary.directory / "conflicts.sqlite";
    {
      conflict_store drafts (conflict_path, pin);
      drafts.prepare (proposal, revisions); drafts.prepare (proposal, revisions);
      auto changed= proposal; changed.resolution.payload+= "changed";
      changed.resolution= seal_revision (changed.resolution);
      rejects ([&] { drafts.prepare (changed, revisions); });
      require (drafts.pending ().size () == 1 && !revisions.get (proposal.resolution.id) &&
        revisions.heads (base.vault, base.object) == branches,
        "Unaccepted draft changed revision heads");
    }
    {
      conflict_store drafts (conflict_path, pin);
      require (drafts.proposal (proposal.request.request_id)->resolution.payload == "manually merged" &&
        drafts.pending ().size () == 1, "Restart lost prepared resolution");
      drafts.accept (proposal.request.request_id, decision_response, result.state.epoch,
        decision_nonce, decision_subject);
      require (drafts.pending ().size () == 1 && drafts.latest (vault, conflict)->version == 1 &&
        !revisions.get (proposal.resolution.id), "Receipt published a draft or lost the decision");
    }
    {
      conflict_store drafts (conflict_path, pin);
      require (drafts.pending ().size () == 1 && drafts.latest (vault, conflict)->request == proposal.request.request_id,
        "Restart lost a committed but unpublished conflict decision");
      drafts.published (proposal.request.request_id);
      require (drafts.pending ().empty (), "Published decision remained in the outbox");
    }
    rejects ([&] { conflict_store drafts (conflict_path, wrong_pin); });
    auto database= temporary.directory / "membership.sqlite";
    {
      membership_store store (database, pin);
      require (!store.current (), "New trust store has a publication");
      store.accept (fixture.at ("initial").dump ());
      store.accept (response.at ("state").dump ());
      store.accept (response.at ("state").dump ());
    }
    {
      membership_store store (database, pin);
      require (store.current ()->revision == 2, "Restart lost trusted revision");
      rejects ([&] { store.accept (fixture.at ("initial").dump ()); });
      rejects ([&] { store.accept (fixture.at ("conflicting_state").dump ()); });
      rejects ([&] { store.accept (fixture.at ("reused_epoch").dump ()); });
      require (store.current ()->revision == 2, "Rejected state altered trust");
    }
    rejects ([&] { membership_store store (database, wrong_pin); });
    membership_lease lease;
    auto steady= membership_lease::steady::now ();
    auto wall= membership_lease::wall::now ();
    using namespace std::chrono_literals;
    require (!lease.valid (steady, wall), "Restart renewed freshness");
    lease.renew (24h, steady, wall);
    require (lease.valid (steady+23h, wall+23h), "Valid lease expired early");
    require (!lease.valid (steady+24h, wall+24h), "Expired lease accepted");
    lease.renew (24h, steady, wall);
    require (!lease.valid (steady+1h, wall+25h), "Suspension extended freshness");
    lease.renew (24h, steady, wall);
    require (!lease.valid (steady+1h, wall-1h), "Clock rollback extended freshness");
    lease.renew (24h, steady, wall); lease.revoke ();
    require (!lease.valid (steady, wall), "Suspension revocation lost");
    std::cout << "Go/C++ membership signatures and freshness checks passed\n";
  }
  catch (const std::exception& e) { std::cerr << e.what () << '\n'; return 1; }
}
