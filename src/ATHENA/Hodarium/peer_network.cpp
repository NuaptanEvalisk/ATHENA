/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#include "peer_network.hpp"
#include <QCborArray>
#include <QCborValue>
#include <QNetworkInterface>
#include <QtEndian>
#include <sodium.h>
#include <algorithm>
#include <QDateTime>

namespace athena::hodarium {
namespace {
constexpr int frame_limit= 512*1024;
constexpr std::size_t link_limit= 16;
}
peer_network::peer_network (device_identity device, authorization authorized, consumer received):
  device_ (std::move (device)), authorized_ (std::move (authorized)), received_ (std::move (received)) {
  if (!authorized_) throw std::invalid_argument ("Peer network requires membership authorization");
  listener_= std::make_unique<direct_peer_listener> ([this] (const peer_context& c) -> std::optional<std::string> {
    auto key= keys_.find (c.remote_member);
    if (!active_ || key == keys_.end () || !authorized_ (c, key->second)) return std::nullopt;
    auto existing= links_.find (c.remote_member);
    if (existing != links_.end ()) {
      // Deterministic tie-break for simultaneous dialing, not a restriction on
      // which endpoint may initiate when only one direction is reachable.
      if (existing->second.connection->established () || !existing->second.outgoing ||
          c.remote_member > member_) return std::nullopt;
    }
    else if (links_.size () >= link_limit) return std::nullopt;
    return key->second;
  }, [this] (std::unique_ptr<peer_transport> transport, peer_context c, std::string key) {
    attach (std::move (transport), std::move (c), std::move (key), false);
  });
  timer_.setInterval (1000);
  connect (&timer_, &QTimer::timeout, this, [this] { tick (); });
}
peer_network::~peer_network () { stop (); }
peer_context peer_network::context (const std::string& peer) const {
  return {state_.group, state_.generation, state_.epoch, member_, peer};
}
void peer_network::start (const membership_state& state, const std::string& member) {
  if (active_ && state_.group == state.group && state_.generation == state.generation &&
      state_.epoch == state.epoch && member_ == member) return;
  stop (); state_= state; member_= member;
  for (const auto& peer: state.members) keys_.emplace (peer.id, peer.public_key);
  if (!listener_->listen ()) throw std::runtime_error ("Hodarium direct listener: " + listener_->error ());
  active_= true; timer_.start ();
}
void peer_network::stop () {
  active_= false; timer_.stop (); listener_->close ();
  for (auto& [member, peer]: links_) {
    peer.connection->close (); peer.connection->deleteLater ();
  }
  links_.clear (); retry_.clear (); address_index_.clear (); keys_.clear (); candidates_= {};
}
std::vector<std::string> peer_network::addresses () const {
  std::vector<std::string> result;
  if (!active_) return result;
  for (const auto& interface: QNetworkInterface::allInterfaces ()) {
    auto flags= interface.flags ();
    if (!flags.testFlag (QNetworkInterface::IsUp) || !flags.testFlag (QNetworkInterface::IsRunning) ||
        flags.testFlag (QNetworkInterface::IsLoopBack)) continue;
    for (const auto& entry: interface.addressEntries ()) {
      auto ip= entry.ip ();
      if (!ip.isGlobal () || ip.isLoopback () || !ip.scopeId ().isEmpty ()) continue;
      QString host= ip.toString ();
      if (ip.protocol () == QAbstractSocket::IPv6Protocol) host= "[" + host + "]";
      result.push_back ((host + ":" + QString::number (listener_->port ())).toStdString ());
    }
  }
  std::sort (result.begin (), result.end ());
  result.erase (std::unique (result.begin (), result.end ()), result.end ());
  if (result.size () > 8) result.resize (8);
  return result;
}
std::vector<peer_route_status> peer_network::status () const {
  std::vector<peer_route_status> result;
  for (const auto& [member, peer]: links_)
    result.push_back ({member, peer.connection->established (), peer.rtt, peer.session});
  return result;
}
void peer_network::discover (const presence_snapshot& presence) {
  candidates_= presence;
  if (active_) tick ();
}
QByteArray peer_network::frame (int kind, const QByteArray& bytes) {
  auto encoded= QCborValue (QCborArray{kind, bytes}).toCbor ();
  if (encoded.size () > frame_limit) throw std::invalid_argument ("Hodarium peer frame exceeds budget");
  QByteArray result (4, Qt::Uninitialized);
  qToBigEndian<quint32> (quint32 (encoded.size ()), result.data ()); result+= encoded;
  return result;
}
bool peer_network::send (const std::string& member, QByteArray bytes) {
  auto found= links_.find (member);
  if (found == links_.end () || bytes.size () > frame_limit - 32) return false;
  return found->second.connection->enqueue (frame (2, bytes));
}
void peer_network::disconnect_peer (const std::string& member) {
  auto found= links_.find (member);
  if (found != links_.end ()) found->second.connection->close ();
}
void peer_network::attach (std::unique_ptr<peer_transport> transport, peer_context c,
  std::string key, bool outgoing) {
  auto member= c.remote_member;
  auto old= links_.find (member);
  if (old != links_.end ()) {
    old->second.connection->close (); old->second.connection->deleteLater (); links_.erase (old);
  }
  // The slot is reserved before constructing TLS, preventing authenticated
  // routing hints from opening an unbounded number of handshakes.
  auto& slot= links_[member]; slot.outgoing= outgoing;
  slot.session= next_session_++;
  auto identity= std::make_shared<QPointer<peer_connection>> ();
  peer_events events;
  events.closed= [this, member, identity] (std::string) {
    auto found= links_.find (member);
    if (found != links_.end () && found->second.connection == *identity) {
      retry_[member]= clock::now () + std::chrono::seconds (30); links_.erase (found);
    }
    if (*identity) (*identity)->deleteLater ();
  };
  events.received= [this, member, identity] (QByteArray bytes) {
    receive (member, identity->data (), std::move (bytes));
  };
  try {
    *identity= new peer_connection (std::move (transport), device_, key, !outgoing, c,
      [this, c, key] { return active_ && authorized_ (c, key); }, std::move (events), this);
    slot.connection= *identity;
  }
  catch (...) { links_.erase (member); throw; }
}
void peer_network::receive (const std::string& member, peer_connection* connection, QByteArray bytes) {
  auto found= links_.find (member);
  if (found == links_.end () || found->second.connection != connection) return;
  auto& peer= found->second;
  peer.incoming+= bytes;
  while (peer.incoming.size () >= 4) {
    auto size= qFromBigEndian<quint32> (peer.incoming.constData ());
    if (size == 0 || size > frame_limit) throw std::invalid_argument ("Invalid Hodarium peer frame length");
    if (peer.incoming.size () < qsizetype (size) + 4) return;
    QCborParserError error;
    auto value= QCborValue::fromCbor (peer.incoming.mid (4, size), &error);
    if (error.error != QCborError::NoError || error.offset != size || !value.isArray ())
      throw std::invalid_argument ("Invalid Hodarium peer frame encoding");
    auto array= value.toArray ();
    if (array.size () != 2 || !array[0].isInteger () || !array[1].isByteArray ())
      throw std::invalid_argument ("Invalid Hodarium peer frame schema");
    auto kind= array[0].toInteger (); auto payload= array[1].toByteArray ();
    peer.incoming.remove (0, size + 4);
    if (kind == 0 && payload.size () == 32) {
      if (!connection->enqueue (frame (1, payload))) connection->close ();
    }
    else if (kind == 1 && payload.size () == 32 && payload == peer.ping) {
      peer.rtt= std::chrono::duration<double, std::milli> (clock::now () - peer.sent).count ();
      peer.ping.clear ();
    }
    else if (kind == 2 && received_) {
      QPointer<peer_network> alive= this;
      received_ (member, std::move (payload));
      if (!alive) return;
      auto current= links_.find (member);
      if (current == links_.end () || current->second.connection != connection) return;
    }
    else throw std::invalid_argument ("Unexpected Hodarium peer frame");
  }
}
void peer_network::tick () {
  if (!active_) return;
  auto now= clock::now ();
  std::size_t connecting= 0;
  for (auto& [member, peer]: links_) {
    if (!peer.connection->established ()) { ++connecting; continue; }
    if (!peer.ping.isEmpty ()) {
      if (now - peer.sent > std::chrono::seconds (30)) peer.connection->close ();
      continue;
    }
    if (now < peer.next_ping) continue;
    QByteArray nonce (32, Qt::Uninitialized); randombytes_buf (nonce.data (), nonce.size ());
    if (peer.connection->enqueue (frame (0, nonce))) {
      peer.ping= nonce; peer.sent= now; peer.next_ping= now + std::chrono::seconds (30);
    }
  }
  for (const auto& candidate: candidates_.peers) {
    if (connecting >= 4 || links_.size () >= link_limit) break;
    auto key= keys_.find (candidate.member);
    if (key == keys_.end () || candidate.expires <= QDateTime::currentSecsSinceEpoch () ||
        candidate.direct.empty () || links_.count (candidate.member) ||
        now < retry_[candidate.member]) continue;
    auto c= context (candidate.member);
    if (!authorized_ (c, key->second)) continue;
    auto& index= address_index_[candidate.member];
    const auto& endpoint= candidate.direct[index++ % candidate.direct.size ()];
    QUrl url ("tcp://" + QString::fromStdString (endpoint));
    auto transport= connect_direct_peer (QHostAddress (url.host ()), quint16 (url.port ()), c);
    attach (std::move (transport), std::move (c), key->second, true); ++connecting;
  }
}
} // namespace athena::hodarium
