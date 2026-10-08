/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#include "membership.hpp"
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
    rejects ([&] { verify_validation (pin, response.dump (), member, member, key); });
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
