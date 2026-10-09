/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#include "peer_network.hpp"
#include "control_http.hpp"
#include "relay_credentials.hpp"
#include <QCborArray>
#include <QCborValue>
#include <QCryptographicHash>
#include <QNetworkInterface>
#include <QtEndian>
#include <sodium.h>
#include <algorithm>
#include <QDateTime>

namespace athena::hodarium {
namespace {
constexpr int frame_limit= 512*1024;
constexpr std::size_t link_limit= 64;
std::string slot_name (const std::string& member, const std::string& route) {
  return member + "\n" + route;
}
}
relay_configuration::relay_configuration (QUrl endpoint, std::string token):
  origin (std::move (endpoint)), access_token (std::move (token)) {}
relay_configuration::relay_configuration (relay_configuration&& other) noexcept:
  origin (std::move (other.origin)), access_token (std::move (other.access_token)) {}
relay_configuration& relay_configuration::operator= (relay_configuration other) {
  origin.swap (other.origin); access_token.swap (other.access_token); return *this;
}
relay_configuration::~relay_configuration () {
  if (!access_token.empty ()) sodium_memzero (access_token.data (), access_token.size ());
}
peer_network::peer_network (device_identity device, authorization authorized, consumer received):
  device_ (std::move (device)), authorized_ (std::move (authorized)), received_ (std::move (received)) {
  if (!authorized_) throw std::invalid_argument ("Peer network requires membership authorization");
  listener_= std::make_unique<direct_peer_listener> ([this] (const peer_context& c) -> std::optional<std::string> {
    auto key= keys_.find (c.remote_member);
    if (!active_ || key == keys_.end () || !authorized_ (c, key->second)) return std::nullopt;
    auto existing= links_.find (slot_name (c.remote_member, "p2p"));
    if (existing != links_.end ()) {
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
  active_= false; timer_.stop (); listener_->close (); joining_.clear ();
  auto links= std::move (links_); links_.clear ();
  for (auto& [slot, peer]: links) {
    peer.connection->close (); peer.connection->deleteLater ();
  }
  selected_.clear (); selecting_.clear (); selected_at_.clear (); improving_.clear ();
  retry_.clear (); address_index_.clear (); keys_.clear (); candidates_= {};
  next_probe_= {};
}
void peer_network::set_relays (std::vector<relay_configuration> values) {
  if (values.size () > 8) throw std::invalid_argument ("At most eight Hodarium Relays may be configured");
  std::map<std::string, relay_configuration> next;
  for (auto& value: values) {
    value.origin= QUrl (QString::fromStdString (canonical_relay_origin (value.origin)));
    validate_relay_access_token (value.access_token);
    auto route= value.origin.toString (QUrl::FullyEncoded).toStdString ();
    if (!next.emplace (route, std::move (value)).second)
      throw std::invalid_argument ("Duplicate Hodarium Relay origin");
  }
  bool changed= next.size () != relays_.size ();
  for (const auto& [route, value]: next) {
    auto old= relays_.find (route);
    changed= changed || old == relays_.end () || old->second.access_token != value.access_token;
  }
  if (!changed) return;
  auto state= state_; auto member= member_; bool running= active_;
  stop (); relays_= std::move (next);
  if (running) start (state, member);
}
std::vector<std::string> peer_network::relay_origins () const {
  std::vector<std::string> result;
  for (const auto& [route, value]: relays_) result.push_back (route);
  return result;
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
  for (const auto& [member, slot]: selected_) {
    auto found= links_.find (slot);
    if (found == links_.end ()) continue;
    const auto& peer= found->second;
    result.push_back ({member, peer.connection->established (), peer.rtt, peer.session, peer.route, peer.transfer_rate});
  }
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
  auto selected= selected_.find (member);
  if (selected == selected_.end () || bytes.size () > frame_limit - 32) return false;
  auto found= links_.find (selected->second);
  if (found == links_.end () || !found->second.connection->enqueue (frame (2, bytes))) return false;
  found->second.last_data= clock::now (); return true;
}
void peer_network::disconnect_peer (const std::string& member) {
  std::vector<QPointer<peer_connection>> closing;
  for (const auto& [slot, peer]: links_) if (peer.member == member) closing.push_back (peer.connection);
  for (auto connection: closing) if (connection) connection->close ();
}
void peer_network::attach (std::unique_ptr<peer_transport> transport, peer_context c,
  std::string key, bool outgoing, std::string route) {
  auto member= c.remote_member; auto name= slot_name (member, route);
  auto old= links_.find (name);
  if (old != links_.end ()) {
    auto connection= old->second.connection; links_.erase (old);
    connection->close (); connection->deleteLater ();
  }
  auto& slot= links_[name]; slot.outgoing= outgoing;
  slot.member= member; slot.route= std::move (route); slot.since= clock::now ();
  auto identity= std::make_shared<QPointer<peer_connection>> ();
  peer_events events;
  events.closed= [this, name, member, identity] (std::string) {
    auto found= links_.find (name);
    if (found != links_.end () && found->second.connection == *identity) {
      retry_[name]= clock::now () + std::chrono::seconds (30); links_.erase (found);
      if (selected_.count (member) && selected_.at (member) == name) selected_.erase (member);
      if (selecting_.count (member) && selecting_.at (member) == name) selecting_.erase (member);
    }
    if (*identity) (*identity)->deleteLater ();
  };
  events.received= [this, name, identity] (QByteArray bytes) {
    receive (name, identity->data (), std::move (bytes));
  };
  try {
    *identity= new peer_connection (std::move (transport), device_, key, !outgoing, c,
      [this, c, key] { return active_ && authorized_ (c, key); }, std::move (events), this);
    slot.connection= *identity;
  }
  catch (...) { links_.erase (name); throw; }
}
void peer_network::activate (const std::string& slot) {
  auto& peer= links_.at (slot);
  selected_[peer.member]= slot; selecting_.erase (peer.member);
  improving_.erase (peer.member);
  selected_at_[peer.member]= clock::now (); peer.session= next_session_++;
}
void peer_network::receive (const std::string& name, peer_connection* connection, QByteArray bytes) {
  auto found= links_.find (name);
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
      if (!connection->enqueue (frame (1, payload))) { connection->close (); return; }
    }
    else if (kind == 1 && payload.size () == 32 && payload == peer.ping) {
      auto sample= std::chrono::duration<double, std::milli> (clock::now () - peer.sent).count ();
      peer.rtt= peer.samples ? peer.rtt * 0.75 + sample * 0.25 : sample;
      ++peer.samples; peer.ping.clear ();
    }
    else if (kind == 6 && payload.size () == 65536) {
      if (!connection->enqueue (frame (7, payload))) { connection->close (); return; }
    }
    else if (kind == 7 && payload.size () == 65536 && payload == peer.probe) {
      auto elapsed= std::chrono::duration<double> (clock::now () - peer.probe_sent).count ();
      auto rate= 131072.0 / std::max (0.001, elapsed - peer.rtt / 1000.0);
      peer.transfer_rate= peer.probe_samples ? peer.transfer_rate * 0.75 + rate * 0.25 : rate;
      ++peer.probe_samples; peer.probe.clear ();
    }
    else if (kind == 3 && payload.isEmpty () && member_ > peer.member) {
      selecting_[peer.member]= name;
      if (!connection->enqueue (frame (4, {}))) { connection->close (); return; }
    }
    else if (kind == 4 && payload.isEmpty () && member_ < peer.member &&
             selecting_.count (peer.member) && selecting_.at (peer.member) == name) {
      if (!connection->enqueue (frame (5, {}))) { connection->close (); return; }
      activate (name);
    }
    else if (kind == 5 && payload.isEmpty () && member_ > peer.member &&
             selecting_.count (peer.member) && selecting_.at (peer.member) == name) activate (name);
    else if (kind == 2) {
      // Old-route bytes can already be in flight during a negotiated switch.
      // The durable revision protocol resumes on the new session from its receipts.
      if (!received_ || !selected_.count (peer.member) || selected_.at (peer.member) != name) continue;
      peer.last_data= clock::now ();
      QPointer<peer_network> alive= this;
      received_ (peer.member, std::move (payload));
      if (!alive) return;
      auto current= links_.find (name);
      if (current == links_.end () || current->second.connection != connection) return;
    }
    else throw std::invalid_argument ("Unexpected Hodarium peer frame");
  }
}
void peer_network::select_routes () {
  auto now= clock::now ();
  // Warm authenticated connections have already paid setup cost. For the
  // bounded revision frames, estimate delivery of 256 KiB plus two RTTs.
  auto cost= [] (const link& route) {
    return 2.0 * route.rtt + (route.transfer_rate > 0 ? 262144000.0 / route.transfer_rate : 0);
  };
  for (const auto& [member, key]: keys_) {
    if (member <= member_ || selecting_.count (member)) continue;
    auto best= links_.end ();
    for (auto it= links_.begin (); it != links_.end (); ++it) {
      const auto& route= it->second;
      if (route.member != member || !route.connection->established () || !route.samples) continue;
      if (best == links_.end ()) best= it;
      else if (route.probe_samples && best->second.probe_samples ?
               cost (route) < cost (best->second) : route.rtt < best->second.rtt) best= it;
    }
    if (best == links_.end ()) continue;
    auto current= selected_.find (member);
    if (current != selected_.end ()) {
      auto active= links_.find (current->second);
      if (active != links_.end () && active->second.connection->established ()) {
        if (active == best || now - selected_at_[member] < std::chrono::seconds (60) ||
            best->second.samples < 3) { improving_.erase (member); continue; }
        const bool measured= best->second.probe_samples && active->second.probe_samples;
        if (measured ? cost (best->second) >= cost (active->second) * 0.75 :
                       best->second.rtt >= active->second.rtt * 0.75) {
          improving_.erase (member); continue;
        }
        auto& advantage= improving_[member];
        if (advantage.first != best->first) advantage= {best->first, now};
        if (now - advantage.second < std::chrono::seconds (15)) continue;
      }
    }
    if (best->second.connection->enqueue (frame (3, {}))) selecting_[member]= best->first;
  }
}
void peer_network::dial_relay (const std::string& member, const std::string& route) {
  const auto& relay= relays_.at (route); auto name= slot_name (member, route);
  auto c= context (member); auto key= keys_.at (member);
  auto low= std::min (member_, member); auto high= std::max (member_, member);
  auto room_bytes= QCborValue (QCborArray{"ATHENA-HODARIUM-RELAY-v1",
    QString::fromStdString (state_.group), QString::fromStdString (state_.generation),
    QString::fromStdString (state_.epoch), QString::fromStdString (low), QString::fromStdString (high)}).toCbor ();
  auto room= QCryptographicHash::hash (room_bytes, QCryptographicHash::Sha256)
    .toBase64 (QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals).toStdString ();
  auto& attempt= joining_[name];
  attempt.http= std::make_unique<control_http> (relay.origin, nullptr);
  attempt.deadline= clock::now () + std::chrono::seconds (15);
  auto credential= std::make_shared<relay_configuration> (relay);
  join_relay (*attempt.http, credential->access_token, room, member_ < member ? 0 : 1,
    [this, name, c, key, route, credential] (control_result result, std::string ticket) {
      auto found= joining_.find (name);
      if (found == joining_.end ()) return;
      found->second.http.release ()->deleteLater (); joining_.erase (found);
      retry_[name]= clock::now () + std::chrono::seconds (30);
      if (result.failure != control_failure::none || !active_ || !authorized_ (c, key)) return;
      try { attach (relay_peer_transport (credential->origin, credential->access_token, ticket),
                    c, key, member_ < c.remote_member, route); }
      catch (...) { retry_[name]= clock::now () + std::chrono::seconds (30); }
    });
}
void peer_network::tick () {
  if (!active_) return;
  auto now= clock::now ();
  std::vector<QPointer<peer_connection>> closing;
  std::size_t connecting= joining_.size ();
  bool probing= std::any_of (links_.begin (), links_.end (), [] (const auto& pair) {
    return !pair.second.probe.isEmpty ();
  });
  for (auto it= joining_.begin (); it != joining_.end ();) {
    if (now < it->second.deadline) { ++it; continue; }
    retry_[it->first]= now + std::chrono::seconds (30); it= joining_.erase (it);
  }
  for (auto& [name, peer]: links_) {
    if (!peer.connection->established ()) { ++connecting; continue; }
    if (!peer.probe.isEmpty () && now - peer.probe_sent > std::chrono::seconds (30)) {
      closing.push_back (peer.connection); continue;
    }
    auto active= selected_.find (peer.member);
    auto current= active == selected_.end () ? links_.end () : links_.find (active->second);
    bool traffic= current != links_.end () && now - current->second.last_data < std::chrono::seconds (60);
    if (throughput_probes_ && !probing && traffic && peer.samples && now >= next_probe_ && now >= peer.next_probe) {
      QByteArray probe (65536, Qt::Uninitialized); randombytes_buf (probe.data (), probe.size ());
      if (peer.connection->enqueue (frame (6, probe))) {
        peer.probe= std::move (probe); peer.probe_sent= now;
        peer.next_probe= now + std::chrono::seconds (120);
        next_probe_= now + std::chrono::seconds (5); probing= true;
      }
    }
    if (!peer.ping.isEmpty ()) {
      if (now - peer.sent > std::chrono::seconds (30)) closing.push_back (peer.connection);
      continue;
    }
    if (now < peer.next_ping) continue;
    QByteArray nonce (32, Qt::Uninitialized); randombytes_buf (nonce.data (), nonce.size ());
    if (peer.connection->enqueue (frame (0, nonce))) {
      peer.ping= nonce; peer.sent= now;
      peer.next_ping= now + std::chrono::seconds (peer.samples < 3 ? 3 : 30);
    }
  }
  for (auto connection: closing) if (connection) connection->close ();
  select_routes ();
  for (const auto& candidate: candidates_.peers) {
    auto key= keys_.find (candidate.member);
    if (candidate.member == member_ || key == keys_.end () ||
        candidate.expires <= QDateTime::currentSecsSinceEpoch ()) continue;
    auto c= context (candidate.member);
    if (!authorized_ (c, key->second)) continue;
    auto direct= slot_name (candidate.member, "p2p");
    if (connecting < 4 && links_.size () + joining_.size () < link_limit &&
        !candidate.direct.empty () && !links_.count (direct) && now >= retry_[direct]) {
      auto& index= address_index_[candidate.member];
      const auto& endpoint= candidate.direct[index++ % candidate.direct.size ()];
      QUrl url ("tcp://" + QString::fromStdString (endpoint));
      auto transport= connect_direct_peer (QHostAddress (url.host ()), quint16 (url.port ()), c);
      attach (std::move (transport), c, key->second, true); ++connecting;
    }
    for (const auto& route: candidate.relays) {
      auto name= slot_name (candidate.member, route);
      if (connecting >= 4 || links_.size () + joining_.size () >= link_limit) break;
      if (!relays_.count (route) || links_.count (name) || joining_.count (name) || now < retry_[name]) continue;
      dial_relay (candidate.member, route); ++connecting;
    }
  }
}
} // namespace athena::hodarium
