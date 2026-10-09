/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#include "peer_secrets.hpp"
#include <QCryptographicHash>
#include <nlohmann/json.hpp>
#include <algorithm>

namespace athena::hodarium {
namespace {
using json= nlohmann::json;
const QByteArray prefix= QByteArrayLiteral ("ATHENA-HODARIUM-KEY-v1\0");
std::string digest (const std::string& request) {
  return QCryptographicHash::hash (QByteArray::fromStdString (request), QCryptographicHash::Sha256)
    .toBase64 (QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals).toStdString ();
}
QByteArray packet (const char* operation, const std::string& vault, const std::string& commitment,
  const std::string& request, const std::string& capsule) {
  return prefix + QByteArray::fromStdString (json::array ({operation, vault, commitment, request, capsule}).dump ());
}
}
peer_secret_exchange::peer_secret_exchange (peer_network& network, client_settings& settings,
  client_profile profile, membership members, authorization allowed): network_ (network), settings_ (settings),
  profile_ (std::move (profile)), members_ (std::move (members)), allowed_ (std::move (allowed)) {}
peer_secret_exchange::pending& peer_secret_exchange::current (const std::string& peer, std::uint64_t session) {
  auto& value= peers_[peer];
  if (value.session != session) { value= pending{}; value.session= session; }
  return value;
}
void peer_secret_exchange::forget (const std::string& peer) { peers_.erase (peer); }
vault_secret_context peer_secret_exchange::context (const std::string& peer, const std::string& vault, bool sending) {
  auto members= members_ ();
  if (!members || members->group != profile_.pin.group || members->generation != profile_.pin.generation || !allowed_ (members->epoch))
    throw std::invalid_argument ("Vault key transfer membership unavailable");
  auto member= std::find_if (members->members.begin (), members->members.end (), [&] (const auto& m) { return m.id == peer; });
  if (member == members->members.end ()) throw std::invalid_argument ("Vault key peer is not a member");
  auto commitment= settings_.vault_secret_commitment (profile_.pin.group, profile_.pin.generation, vault);
  if (!commitment) throw std::invalid_argument ("Vault key has no authority registration");
  return {profile_.pin.group, profile_.pin.generation, members->epoch, vault, *commitment,
    sending ? profile_.device.public_key : member->public_key,
    sending ? member->public_key : profile_.device.public_key};
}
bool peer_secret_exchange::authorized (const vault_secret_context& c) {
  if (c.group != profile_.pin.group || c.generation != profile_.pin.generation || !allowed_ (c.epoch)) return false;
  auto members= members_ ();
  if (!members || members->epoch != c.epoch) return false;
  bool sender= false, recipient= false;
  for (const auto& member: members->members) {
    sender |= member.public_key == c.sender_public_key;
    recipient |= member.public_key == c.recipient_public_key;
  }
  if (!sender || !recipient) return false;
  bool selected= false;
  for (const auto& binding: settings_.vaults (c.group))
    if (binding.vault == c.vault && binding.enabled) selected= true;
  auto commitment= settings_.vault_secret_commitment (c.group, c.generation, c.vault);
  return selected && commitment && *commitment == c.commitment;
}
bool peer_secret_exchange::receive (const std::string& peer, std::uint64_t session,
  const std::vector<std::string>& common, const QByteArray& message) {
  if (!message.startsWith (prefix)) return false;
  if (message.size () > 24576) throw std::invalid_argument ("Vault key message exceeds budget");
  auto bytes= message.mid (prefix.size ());
  auto fields= json::parse (bytes.constData (), bytes.constData () + bytes.size (),
    [] (int depth, json::parse_event_t event, json&) {
      if (depth > 2 || event == json::parse_event_t::object_start)
        throw std::invalid_argument ("Invalid Vault key message structure");
      return true;
    });
  if (!fields.is_array () || fields.size () != 5 ||
      !std::all_of (fields.begin (), fields.end (), [] (const json& value) { return value.is_string (); }))
    throw std::invalid_argument ("Invalid Vault key message");
  const std::string operation= fields[0], vault= fields[1], commitment= fields[2], request= fields[3], capsule= fields[4];
  if (!std::binary_search (common.begin (), common.end (), vault))
    throw std::invalid_argument ("Vault key transfer is outside common selection");
  auto& pending= current (peer, session);
  if (operation == "request") {
    if (digest (capsule) != request || !pending.reply.isEmpty ())
      throw std::invalid_argument ("Invalid or overlapping Vault key request");
    auto local_commitment= settings_.vault_secret_commitment (profile_.pin.group, profile_.pin.generation, vault);
    if (!local_commitment) {
      pending.reply= packet ("wait", vault, commitment, request, {});
      return true;
    }
    auto binding= context (peer, vault, true);
    if (binding.commitment != commitment) throw std::invalid_argument ("Peers disagree on canonical Vault secret");
    auto secret= settings_.find_vault_secret (binding.group, binding.generation, vault, commitment);
    if (!secret) pending.reply= packet ("wait", vault, commitment, request, {});
    else pending.reply= packet ("grant", vault, commitment, request,
      seal_vault_secret (*secret, profile_.device, binding, capsule,
        [this] (const vault_secret_context& c) { return authorized (c); }));
  }
  else if (operation == "grant" || operation == "wait") {
    if (!pending.receiver || pending.vault != vault || pending.commitment != commitment || pending.request != request)
      throw std::invalid_argument ("Unsolicited Vault key response");
    if (operation == "grant") {
      // Another authenticated peer may already have delivered this commitment.
      if (!settings_.find_vault_secret (profile_.pin.group, profile_.pin.generation, vault, commitment)) {
        auto secret= pending.receiver->receive (capsule);
        settings_.remember_vault_secret (profile_.pin.generation, secret);
      }
    }
    else if (!capsule.empty ()) throw std::invalid_argument ("Invalid Vault key wait response");
    pending.receiver.reset (); pending.outgoing.clear ();
    pending.next= clock::now () + std::chrono::seconds (5);
  }
  else throw std::invalid_argument ("Unknown Vault key operation");
  return true;
}
void peer_secret_exchange::pump (const std::string& peer, std::uint64_t session,
  const std::vector<std::string>& common) {
  auto& pending= current (peer, session);
  if (!pending.reply.isEmpty ()) {
    if (!network_.send (peer, pending.reply)) return;
    pending.reply.clear ();
  }
  if (!pending.outgoing.isEmpty ()) {
    if (!network_.send (peer, pending.outgoing)) return;
    pending.outgoing.clear ();
  }
  if (pending.receiver) {
    if (clock::now () < pending.deadline) return;
    throw std::runtime_error ("Vault key transfer timed out");
  }
  if (clock::now () < pending.next || common.empty ()) return;
  pending.next= clock::now () + std::chrono::seconds (5);
  // At most one outstanding request per peer; rotate to avoid an unavailable
  // secret starving other common Vaults. No per-frame key-store access.
  for (std::size_t i= 0; i < common.size (); ++i) {
    const auto& vault= common[pending.cursor++ % common.size ()];
    auto commitment= settings_.vault_secret_commitment (profile_.pin.group, profile_.pin.generation, vault);
    if (!commitment || settings_.find_vault_secret (profile_.pin.group, profile_.pin.generation, vault, *commitment)) continue;
    auto binding= context (peer, vault, false);
    pending.receiver= std::make_unique<vault_secret_receiver> (profile_.device, binding,
      [this] (const vault_secret_context& c) { return authorized (c); });
    pending.vault= vault; pending.commitment= *commitment;
    pending.request= digest (pending.receiver->request ());
    pending.outgoing= packet ("request", vault, *commitment, pending.request, pending.receiver->request ());
    pending.deadline= clock::now () + std::chrono::seconds (90);
    break;
  }
}
}
