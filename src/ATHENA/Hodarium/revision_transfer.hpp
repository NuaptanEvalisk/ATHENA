/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#pragma once
#include "revisions.hpp"
#include <QByteArray>
#include <functional>
#include <map>
#include <memory>
#include <deque>

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
// One direction of a Vault exchange. Probe heads, then send only missing causal
// ancestry, parent first. Both peers run this independently to preserve branches.
// The cursor is valid only while the same peer retains its store on this session.
class vault_revision_sender {
public:
  vault_revision_sender (revision_store& store, std::string vault,
    revision_receiver::authorization authorized, std::int64_t after= 0);
  QByteArray begin ();
  QByteArray acknowledge (const QByteArray& message);
  bool complete () const;
  std::int64_t completed_through () const;
private:
  revision_store& store_;
  std::string vault_;
  revision_receiver::authorization authorized_;
  std::int64_t cursor_, through_= 0;
  std::vector<revision_head> heads_;
  struct pending { std::string id; bool expanded= false; };
  std::vector<pending> stack_;
  std::unique_ptr<revision_sender> transfer_;
  enum class phase { initial, probe, transfer, complete } phase_= phase::initial;
  QByteArray advance ();
  void authorize () const;
};
// One authenticated connection, two independent request directions. The owner
// calls outgoing(), tries its bounded transport, then calls sent() only on
// acceptance. A reconnect must construct a new exchange (including cursors).
class revision_exchange {
public:
  revision_exchange (revision_store& store, revision_receiver::authorization authorized);
  revision_exchange (const revision_exchange&)= delete;
  revision_exchange& operator= (const revision_exchange&)= delete;
  bool start (const std::string& vault);
  bool busy () const;
  QByteArray outgoing () const;
  void sent ();
  void accept (const QByteArray& message);
private:
  revision_store& store_;
  revision_receiver::authorization authorized_;
  std::string receiving_vault_, sending_vault_;
  revision_receiver receiver_;
  std::unique_ptr<vault_revision_sender> sender_;
  struct queued { QByteArray bytes; std::string vault; bool reply; };
  std::deque<queued> output_;
  std::map<std::string, std::int64_t> cursors_;
  std::int64_t next_= 1, incoming_= 1, waiting_= 0;
  bool reply_pending_= false;
  void request (QByteArray message);
};
} // namespace athena::hodarium
