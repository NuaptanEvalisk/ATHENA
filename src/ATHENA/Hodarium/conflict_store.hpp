/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#pragma once
#include "decisions.hpp"
#include "revisions.hpp"

namespace athena::hodarium {
struct conflict_proposal {
  decision_request request;
  revision resolution;
};

// Device-local durable outbox. Drafts are deliberately not revision DAG heads.
// One owner thread; verified decisions do not perform filesystem application.
class conflict_store {
public:
  conflict_store (const std::filesystem::path& database, authority_pin pin);
  ~conflict_store ();
  conflict_store (const conflict_store&)= delete;
  conflict_store& operator= (const conflict_store&)= delete;
  void prepare (const conflict_proposal& proposal, const revision_store& revisions);
  std::optional<conflict_proposal> proposal (const std::string& operation) const;
  std::vector<std::string> pending (const std::string& after= {}, std::uint32_t limit= 64) const;
  // Call under a current membership lease. Signature, request binding and
  // rollback/equivocation checks precede durable acceptance. The exact signed
  // envelope is retained; an accepted resolution still needs causal validation.
  std::optional<conflict_decision> accept (const std::string& operation,
    const std::string& envelope, const std::string& epoch,
    const std::string& nonce, const std::string& subject);
  std::optional<conflict_decision> latest (const std::string& vault,
                                        const std::string& conflict) const;
private:
  sqlite3* db_= nullptr;
  authority_pin pin_;
};
} // namespace athena::hodarium
