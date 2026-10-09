/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#include "peer_replication.hpp"
#include <QCborArray>
#include <QCborValue>
#include <algorithm>
#include <stdexcept>

namespace athena::hodarium {
namespace { const QString domain= QStringLiteral ("ATHENA-HODARIUM-VAULTS-v1"); }
peer_replication::peer_replication (peer_network& network, revision_store& store,
  std::vector<std::string> vaults, std::function<void(std::string)> error):
  network_ (network), store_ (store), vaults_ (std::move (vaults)), error_ (std::move (error)) {
  std::sort (vaults_.begin (), vaults_.end ());
  vaults_.erase (std::unique (vaults_.begin (), vaults_.end ()), vaults_.end ());
  if (vaults_.size () > 256) throw std::invalid_argument ("Too many replication Vaults");
  QCborArray ids;
  for (const auto& vault: vaults_) {
    if (vault.empty () || vault.size () > 1024) throw std::invalid_argument ("Invalid replication Vault");
    ids.append (QString::fromStdString (vault));
  }
  announcement_= QCborValue (QCborArray{domain, ids}).toCbor ();
  connect (&timer_, &QTimer::timeout, this, [this] { tick (); });
  timer_.start (50);
}
peer_replication::peer& peer_replication::current (const peer_route_status& route) {
  auto& state= peers_[route.member];
  if (state.session != route.session) { state= peer{}; state.session= route.session; }
  return state;
}
void peer_replication::receive (const std::string& member, const QByteArray& message) {
  auto routes= network_.status ();
  auto route= std::find_if (routes.begin (), routes.end (), [&] (const auto& r) { return r.member == member && r.established; });
  if (route == routes.end ()) throw std::invalid_argument ("Replication has no authenticated connection");
  auto& state= current (*route);
  if (!state.received) {
    if (message.size () > 300*1024) throw std::invalid_argument ("Vault announcement exceeds budget");
    QCborParserError error;
    auto value= QCborValue::fromCbor (message, &error);
    auto fields= value.toArray ();
    if (error.error != QCborError::NoError || error.offset != message.size () || !value.isArray () ||
        fields.size () != 2 || fields[0] != domain || !fields[1].isArray () || fields[1].toArray ().size () > 256)
      throw std::invalid_argument ("Invalid Vault announcement");
    std::vector<std::string> remote;
    for (const auto& entry: fields[1].toArray ()) {
      auto id= entry.toString ().toUtf8 ().toStdString ();
      if (!entry.isString () || id.empty () || id.size () > 1024 || (!remote.empty () && id <= remote.back ()))
        throw std::invalid_argument ("Invalid announced Vault ID");
      remote.push_back (std::move (id));
    }
    std::set_intersection (vaults_.begin (), vaults_.end (), remote.begin (), remote.end (), std::back_inserter (state.common));
    auto common= state.common;
    state.exchange= std::make_unique<revision_exchange> (store_, [common] (const std::string& vault) {
      return std::binary_search (common.begin (), common.end (), vault);
    });
    state.received= true;
  }
  else state.exchange->accept (message);
  state.deadline= clock::now () + std::chrono::seconds (60);
  pump (member, state);
}
void peer_replication::pump (const std::string& member, peer& state) {
  if (!state.announced) {
    if (!network_.send (member, announcement_)) return;
    state.announced= true;
  }
  if (!state.received) return;
  auto& exchange= *state.exchange;
  // Bound synchronous work even when a series of empty Vaults needs no I/O.
  if (!exchange.busy () && exchange.outgoing ().isEmpty () && !state.common.empty () && clock::now () >= state.cycle) {
    exchange.start (state.common[state.next++]);
    if (exchange.busy ()) state.deadline= clock::now () + std::chrono::seconds (60);
    if (state.next == state.common.size ()) { state.next= 0; state.cycle= clock::now () + std::chrono::seconds (2); }
  }
  for (int i= 0; i < 2; ++i) {
    auto message= exchange.outgoing ();
    if (message.isEmpty () || !network_.send (member, message)) break;
    exchange.sent ();
  }
}
void peer_replication::tick () {
  auto routes= network_.status ();
  std::set<std::string> live;
  for (const auto& route: routes) if (route.established) {
    live.insert (route.member);
    try {
      auto& state= current (route);
      if ((!state.received || (state.exchange && state.exchange->busy ())) && clock::now () > state.deadline)
        throw std::runtime_error ("Revision exchange timed out");
      pump (route.member, state);
    }
    catch (const std::exception& e) {
      network_.disconnect_peer (route.member); peers_.erase (route.member);
      if (error_) error_ (e.what ());
    }
  }
  for (auto it= peers_.begin (); it != peers_.end ();)
    if (!live.count (it->first)) it= peers_.erase (it); else ++it;
}
}
