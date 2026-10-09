/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#pragma once
#include "revisions.hpp"
#include <QByteArray>
#include <functional>
#include <map>

namespace athena::hodarium {
// Messages must travel inside an authenticated peer channel. The owner binds
// Vault grants to that peer and rechecks them for each call, including retries.
class revision_receiver {
public:
  using authorization= std::function<bool (const std::string& vault)>;
  revision_receiver (revision_store& store, authorization authorized);
  QByteArray accept (const QByteArray& message);
private:
  revision_store& store_;
  authorization authorized_;
  std::map<std::string, revision_offer> active_;
};
// One outstanding message: only advance after its durable acknowledgement.
// After disconnect, create a new sender; the receiver supplies the resume offset.
class revision_sender {
public:
  revision_sender (revision_store& store, std::string vault, std::string id,
                   revision_receiver::authorization authorized);
  QByteArray begin ();
  QByteArray acknowledge (const QByteArray& message);
  bool complete () const;
private:
  revision_store& store_;
  revision_offer offer_;
  revision_receiver::authorization authorized_;
  enum class phase { initial, offer, chunk, finish, complete } phase_= phase::initial;
  std::uint64_t expected_= 0;
  QByteArray next (std::uint64_t offset);
};
} // namespace athena::hodarium
