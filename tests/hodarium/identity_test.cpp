/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#include <libsecret/secret.h>
#include "device_identity.hpp"
#include "vault_secret.hpp"
#include "client_settings.hpp"
#include "peer_tls.hpp"
#include "peer_connection.hpp"
#include "peer_network.hpp"
#include "revision_transfer.hpp"
#include "peer_replication.hpp"
#include "peer_secrets.hpp"
#include "vault_registration.hpp"
#include "control_http.hpp"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QTcpServer>
#include <QDateTime>
#include <QTcpSocket>
#include <QEventLoop>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <nlohmann/json.hpp>
#include <fstream>
#include <sodium.h>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <deque>
#include <cerrno>

using namespace athena::hodarium;
void require (bool value, const char* message) {
  if (!value) throw std::runtime_error (message);
}
struct memory_transport {
  std::deque<unsigned char> incoming;
  memory_transport* other= nullptr;
  static ssize_t pull (void* context, void* output, size_t size) {
    auto& bytes= static_cast<memory_transport*> (context)->incoming;
    if (bytes.empty ()) { errno= EAGAIN; return -1; }
    auto count= std::min (size, bytes.size ());
    for (size_t i= 0; i < count; ++i) { static_cast<unsigned char*> (output)[i]= bytes.front (); bytes.pop_front (); }
    return count;
  }
  static ssize_t push (void* context, const void* input, size_t size) {
    auto& bytes= static_cast<memory_transport*> (context)->other->incoming;
    if (bytes.size () + size > 65536) { errno= EAGAIN; return -1; }
    auto* begin= static_cast<const unsigned char*> (input);
    bytes.insert (bytes.end (), begin, begin + size); return size;
  }
};
void check_peer_tls (const device_identity& a, const device_identity& b, bool wrong_peer, bool wrong_epoch= false) {
  memory_transport x, y; x.other= &y; y.other= &x;
  bool authorized= true;
  auto allowed= [&] { return authorized; };
  peer_context ac{a.public_key, b.public_key, a.handle, a.handle, b.handle};
  auto bc= ac; std::swap (bc.local_member, bc.remote_member);
  if (wrong_epoch) bc.epoch= b.handle;
  peer_tls client (a, wrong_peer ? std::string (43, 'A') : b.public_key, false, ac, allowed, &x,
    memory_transport::pull, memory_transport::push);
  peer_tls server (b, a.public_key, true, bc, allowed, &y, memory_transport::pull, memory_transport::push);
  bool client_ready= false, server_ready= false;
  for (int i= 0; i < 100 && (!client_ready || !server_ready); ++i) {
    client_ready= client.handshake (); server_ready= server.handshake ();
  }
  require (client_ready && server_ready, "Peer TLS handshake did not converge");
  require (client.channel_binding ().size () == 32 &&
    client.channel_binding () == server.channel_binding (), "Peer TLS exporter mismatch");
  const std::string message= "opaque document bytes";
  require (client.send (message.data (), message.size ()) == ssize_t (message.size ()), "Peer TLS send failed");
  char buffer[256]; auto received= server.receive (buffer, sizeof buffer);
  require (received == ssize_t (message.size ()) && std::string (buffer, received) == message,
    "Peer TLS changed content");
  authorized= false;
  bool rejected= false;
  try { client.send (message.data (), message.size ()); }
  catch (const std::runtime_error&) { rejected= true; }
  require (rejected, "Expired membership still transmitted records");
}
void check_tcp_peer (const device_identity& a, const device_identity& b) {
  QEventLoop loop;
  QTimer deadline; deadline.setSingleShot (true);
  bool authorized= true, transferred= false, revoked= false;
  std::string error;
  QByteArray payload (700*1024, 'x'), received;
  payload[100]= '\0'; payload[500000]= char (255);
  peer_context ac{a.public_key, b.public_key, a.handle, a.handle, b.handle};
  auto bc= ac; std::swap (bc.local_member, bc.remote_member);
  std::unique_ptr<peer_connection> server;
  auto ended= [&] (std::string reason) {
    if (!transferred) error= std::move (reason);
    else revoked= true;
    loop.quit ();
  };
  direct_peer_listener listener ([&] (const peer_context& context) -> std::optional<std::string> {
    if (context.group != bc.group || context.generation != bc.generation || context.epoch != bc.epoch ||
        context.local_member != bc.local_member || context.remote_member != bc.remote_member)
      return std::nullopt;
    return a.public_key;
  }, [&] (std::unique_ptr<peer_transport> transport, peer_context context, std::string key) {
    peer_events events;
    events.closed= ended;
    events.received= [&] (QByteArray bytes) {
      received+= bytes;
      if (received.size () >= payload.size ()) {
        if (received != payload) error= "TCP peer stream changed content";
        transferred= received == payload;
        authorized= false;
      }
    };
    server= std::make_unique<peer_connection> (std::move (transport), b, std::move (key),
      true, std::move (context), [&] { return authorized; }, std::move (events));
  });
  require (listener.listen (QHostAddress::LocalHost, 0), "Cannot listen for isolated peer connection");
  std::unique_ptr<peer_connection> client;
  peer_events events;
  events.closed= ended;
  events.received= [&] (QByteArray) { error= "Unexpected reverse data"; loop.quit (); };
  events.established= [&] {
    if (!client->enqueue (payload)) { error= "TCP peer refused bounded transfer"; loop.quit (); }
    if (client->enqueue (payload)) { error= "TCP peer exceeded send queue budget"; loop.quit (); }
  };
  client= std::make_unique<peer_connection> (connect_direct_peer (QHostAddress::LocalHost, listener.port (), ac),
    a, b.public_key, false, ac,
    [&] { return authorized; }, std::move (events));
  QObject::connect (&deadline, &QTimer::timeout, &loop, [&] { error= "TCP peer timed out"; loop.quit (); });
  deadline.start (20000); loop.exec ();
  require (error.empty (), error.c_str ());
  require (transferred && revoked, "TCP peer transfer or authorization shutdown failed");
}
void check_direct_rejection (const device_identity& a, const device_identity& b) {
  QEventLoop loop; QTimer deadline; deadline.setSingleShot (true);
  bool rejected= false, delivered= false;
  direct_peer_listener listener ([] (const peer_context&) -> std::optional<std::string> {
    return std::nullopt;
  }, [&] (std::unique_ptr<peer_transport>, peer_context, std::string) {
    delivered= true; loop.quit ();
  });
  require (listener.listen (QHostAddress::LocalHost, 0), "Cannot listen for routing rejection");
  peer_context context{a.public_key, b.public_key, a.handle, a.handle, b.handle};
  auto transport= connect_direct_peer (QHostAddress::LocalHost, listener.port (), context);
  transport->activity= [&] {
    if (transport->state () == transport_state::closed) { rejected= true; loop.quit (); }
  };
  QObject::connect (&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
  deadline.start (3000); loop.exec ();
  require (rejected && !delivered, "Unrecognized routing identity reached peer transport consumer");
}
void check_peer_network (const device_identity& a, const device_identity& b) {
  membership_state state{a.public_key, b.public_key, a.handle, 1,
    {{a.handle, "A", a.public_key}, {b.handle, "B", b.public_key}}};
  auto allowed= [&] (const peer_context& c, const std::string& key) {
    return c.group == state.group && c.generation == state.generation && c.epoch == state.epoch &&
      ((c.local_member == a.handle && c.remote_member == b.handle && key == b.public_key) ||
       (c.local_member == b.handle && c.remote_member == a.handle && key == a.public_key));
  };
  QEventLoop loop; QTimer poll, deadline;
  revision_store source (":memory:"), target (":memory:");
  revision value;
  value.vault= "10000000-0000-4000-8000-000000000002"; value.object= "isolated-object";
  value.origin_member= a.handle; value.format= "ath-xml-v2";
  value.relative_path= "Notes/example.ath"; value.payload= std::string (700*1024, 'n');
  value.payload[17]= '\0'; value.payload[600000]= char (255);
  auto base= value; base.payload= "common ancestor"; base= seal_revision (base);
  source.receive (base); target.receive (base);
  value.parents= {base.id}; value= seal_revision (value); source.receive (value);
  auto other= base; other.parents= {base.id}; other.origin_member= b.handle;
  other.payload= "concurrent edit"; other= seal_revision (other); target.receive (other);
  auto private_revision= base; private_revision.vault= "local-only";
  private_revision= seal_revision (private_revision); source.receive (private_revision);
  std::unique_ptr<peer_replication> outgoing, incoming;
  bool sent= false, measured= false, secret_received= false;
  peer_network first (a, allowed, [&] (const std::string& member, QByteArray bytes) {
    require (member == b.handle, "Revision acknowledgement came from wrong peer");
    outgoing->receive (member, bytes);
  });
  peer_network second (b, allowed, [&] (const std::string& member, QByteArray bytes) {
    require (member == a.handle, "Revision message came from wrong peer");
    incoming->receive (member, bytes);
  });
  auto error= [] (std::string message) { throw std::runtime_error (message); };
  QTemporaryDir temporary;
  require (temporary.isValid (), "Cannot create isolated peer settings");
  auto root= std::filesystem::path (temporary.path ().toStdString ());
  client_settings first_settings (root / "a.sqlite"), second_settings (root / "b.sqlite");
  auto profile= [&] (const device_identity& device, const char* name, client_settings& settings) {
    client_profile result{QUrl ("https://authority.invalid"), {state.group, a.public_key, state.generation},
      b.public_key, device, name, {}, false};
    settings.add_pending (result);
    settings.complete_admission (state.group, device.handle, device.public_key, device.handle);
    result.member= device.handle;
    std::filesystem::create_directory (root / name);
    settings.bind_vault ({state.group, value.vault, root / name, true});
    return result;
  };
  auto ap= profile (a, "A", first_settings), bp= profile (b, "B", second_settings);
  auto shared_secret= create_vault_secret (state.group, value.vault);
  first_settings.remember_vault_secret (state.generation, shared_secret);
  auto payload= nlohmann::json{{"protocol", 1}, {"group", state.group}, {"generation", state.generation},
    {"epoch", state.epoch}, {"challenge", a.handle}, {"subject", b.handle},
    {"slot", vault_registration_slot (state.group, value.vault)},
    {"registration", {{"commitment", shared_secret.commitment}, {"member", a.handle}, {"created", 1}}}}.dump ();
  std::string domain= "ATHENA-HODARIUM-VAULT-SECRET-v1"; domain += '\0';
  auto receipt= nlohmann::json{{"payload", QByteArray::fromStdString (payload).toBase64 (
    QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals).toStdString ()},
    {"signature", sign_device_message (a, domain + payload)}}.dump ();
  for (auto* settings: {&first_settings, &second_settings})
    settings->accept_vault_registration (state.group, value.vault, receipt, state.epoch, a.handle, b.handle);
  auto members= [&] { return std::optional<membership_state> (state); };
  auto authorized= [&] (const std::string& epoch) { return epoch == state.epoch; };
  peer_secret_exchange first_keys (first, first_settings, ap, members, authorized);
  peer_secret_exchange second_keys (second, second_settings, bp, members, authorized);
  outgoing= std::make_unique<peer_replication> (first, source, std::vector<std::string>{value.vault, "local-only"}, error, &first_keys);
  incoming= std::make_unique<peer_replication> (second, target, std::vector<std::string>{value.vault}, error, &second_keys);
  first.start (state, a.handle); second.start (state, b.handle);
  auto aa= first.addresses (), ba= second.addresses ();
  require (!aa.empty () && !ba.empty (), "Isolated network check needs a local unicast interface");
  auto expires= QDateTime::currentSecsSinceEpoch () + 90;
  first.discover ({{{b.handle, ba, {}, expires}}, {}});
  second.discover ({{{a.handle, aa, {}, expires}}, {}});
  QObject::connect (&poll, &QTimer::timeout, &loop, [&] {
    auto routes= first.status ();
    if (routes.size () != 1 || !routes[0].established) return;
    sent= source.contains (other.vault, other.id) && target.contains (value.vault, value.id);
    measured= routes[0].roundtrip_ms > 0;
    secret_received= bool (second_settings.find_vault_secret (state.group, state.generation, value.vault, shared_secret.commitment));
    if (sent && measured && secret_received) loop.quit ();
  });
  QObject::connect (&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
  poll.start (50); deadline.setSingleShot (true); deadline.start (10000); loop.exec ();
  require (sent && measured && secret_received,
           "Automatic peer dialing, secret delivery or bidirectional revision exchange failed");
  verify_vault_secret (*second_settings.find_vault_secret (state.group, state.generation, value.vault, shared_secret.commitment));
  require (target.get (value.id) && target.get (value.id)->payload == value.payload,
           "Peer revision transfer changed content");
  require (!target.applied (value.vault, value.object), "Peer transfer applied a document");
  require (!target.contains (private_revision.vault, private_revision.id), "Non-shared Vault was replicated");
  require (source.contains (other.vault, other.id) &&
           source.heads (value.vault, value.object) == target.heads (value.vault, value.object) &&
           source.heads (value.vault, value.object).size () == 2,
           "Bidirectional exchange lost a concurrent head");
  first.stop (); second.stop ();
  require (first.status ().empty () && first.addresses ().empty (), "Stopped peer network retained live routes");
  auto merge= value; merge.parents= {value.id, other.id}; merge.payload= "user merge";
  merge= seal_revision (merge); target.receive (merge);
  auto descendant= merge; descendant.parents= {merge.id}; descendant.payload= "later edit";
  descendant= seal_revision (descendant); target.receive (descendant);
  require (!target.eligible (merge.id) && !target.eligible (descendant.id) &&
           target.heads (value.vault, value.object).size () == 2, "Unauthorised merge consumed concurrent branches");
  const auto tokens= derive_conflict_tokens (shared_secret, merge.object, merge.parents, merge.id);
  auto decision_payload= nlohmann::json{{"protocol", 1}, {"group", state.group}, {"generation", state.generation},
    {"epoch", state.epoch}, {"challenge", a.handle}, {"subject", b.handle},
    {"decision", {{"vault", tokens.vault}, {"conflict", tokens.conflict}, {"branches", tokens.branches},
      {"resolution", tokens.resolution}, {"request_id", a.handle}, {"member", a.handle},
      {"generation", state.generation}, {"epoch", state.epoch}, {"version", 1}, {"created", 1}}}}.dump ();
  std::string decision_domain= "ATHENA-HODARIUM-DECISION-v1"; decision_domain+= '\0';
  decision_evidence evidence{nlohmann::json{{"payload", QByteArray::fromStdString (decision_payload).toBase64 (
    QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals).toStdString ()},
    {"signature", sign_device_message (a, decision_domain + decision_payload)}}.dump (), state.epoch, a.handle, b.handle};
  auto recipient_secret= *second_settings.find_vault_secret (state.group, state.generation, value.vault, shared_secret.commitment);
  auto wrong= evidence; wrong.subject= a.handle;
  bool rejected= false;
  try { target.accept_resolution (merge.id, ap.pin, recipient_secret, wrong); }
  catch (const std::exception&) { rejected= true; }
  require (rejected && !target.eligible (merge.id), "Unbound resolution receipt was accepted");
  require (target.accept_resolution (merge.id, ap.pin, recipient_secret, evidence) &&
           target.eligible (descendant.id) && target.heads (value.vault, value.object) == std::vector<std::string>{descendant.id},
           "Verified resolution failed to admit its waiting descendants");
}
void check_relay_peer (const device_identity& a, const device_identity& b, const char* directory) {
  const auto root= std::filesystem::path (directory);
  nlohmann::json config;
  std::ifstream (root / "relay.json") >> config;
  auto tls= QSslConfiguration::defaultConfiguration ();
  tls.addCaCertificate (QSslCertificate (QByteArray::fromStdString (config.at ("certificate"))));
  QSslConfiguration::setDefaultConfiguration (tls);
  QUrl origin (QString::fromStdString (config.at ("origin")));
  std::string token= config.at ("token");
  control_http http (origin, nullptr);
  QEventLoop loop; QTimer deadline; deadline.setSingleShot (true);
  std::string error;
  bool transferred= false;
  QByteArray payload (700*1024, 'r'), received;
  payload[10]= '\0'; payload[600000]= char (255);
  std::unique_ptr<peer_connection> client, server;
  peer_context ac{a.public_key, b.public_key, a.handle, a.handle, b.handle};
  auto bc= ac; std::swap (bc.local_member, bc.remote_member);
  auto closed= [&] (std::string reason) { error= std::move (reason); loop.quit (); };
  allocate_relay_ticket (http, token, [&] (control_result result, relay_ticket ticket) {
    if (result.failure != control_failure::none) { error= result.diagnostic; loop.quit (); return; }
    peer_events ce, se;
    ce.closed= se.closed= closed;
    ce.received= [&] (QByteArray bytes) {
      received+= bytes;
      if (received.size () >= payload.size ()) {
        transferred= received == payload;
        if (!transferred) error= "Relayed encrypted stream changed content";
        loop.quit ();
      }
    };
    se.received= [&] (QByteArray bytes) {
      if (!server->enqueue (std::move (bytes))) { error= "Relay echo exceeded queue budget"; loop.quit (); }
    };
    ce.established= [&] { if (!client->enqueue (payload)) { error= "Relay refused payload"; loop.quit (); } };
    server= std::make_unique<peer_connection> (relay_peer_transport (origin, token, ticket.endpoints[1]),
      b, a.public_key, true, bc, [] { return true; }, std::move (se));
    client= std::make_unique<peer_connection> (relay_peer_transport (origin, token, ticket.endpoints[0]),
      a, b.public_key, false, ac, [] { return true; }, std::move (ce));
  });
  QObject::connect (&deadline, &QTimer::timeout, &loop, [&] { error= "Native relay timed out"; loop.quit (); });
  deadline.start (20000); loop.exec ();
  require (error.empty (), error.c_str ()); require (transferred, "Relay transfer incomplete");
  client.reset (); server.reset ();

  // No direct hints and no manually shared tickets: both devices independently
  // rendezvous at the configured Relay, authenticate, and negotiate a data route.
  membership_state state{a.public_key, b.public_key, a.handle, 1,
    {{a.handle, "A", a.public_key}, {b.handle, "B", b.public_key}}};
  auto allowed= [&] (const peer_context& c, const std::string& key) {
    return c.group == state.group && c.generation == state.generation && c.epoch == state.epoch &&
      ((c.local_member == a.handle && c.remote_member == b.handle && key == b.public_key) ||
       (c.local_member == b.handle && c.remote_member == a.handle && key == a.public_key));
  };
  QEventLoop automatic; QTimer poll, limit;
  QByteArray forward (128*1024, 'a'), backward (96*1024, 'b'), got_forward, got_backward;
  forward[100]= '\0'; backward[1000]= char (255);
  peer_network first (a, allowed, [&] (const std::string& member, QByteArray bytes) {
    require (member == b.handle, "Automatic Relay changed peer identity"); got_backward+= bytes;
  });
  peer_network second (b, allowed, [&] (const std::string& member, QByteArray bytes) {
    require (member == a.handle, "Automatic Relay changed peer identity"); got_forward+= bytes;
  });
  first.set_relays ({{origin, token}}); second.set_relays ({{origin, token}});
  first.start (state, a.handle); second.start (state, b.handle);
  const auto routes= first.relay_origins ();
  require (routes.size () == 1 && second.relay_origins () == routes, "Relay origins changed during configuration");
  const auto expires= QDateTime::currentSecsSinceEpoch () + 60;
  first.discover ({{{b.handle, {}, routes, expires}}, {}});
  second.discover ({{{a.handle, {}, routes, expires}}, {}});
  bool forward_sent= false, backward_sent= false, routed= false;
  QObject::connect (&poll, &QTimer::timeout, &automatic, [&] {
    const auto x= first.status (), y= second.status ();
    if (x.size () != 1 || y.size () != 1 || !x[0].established || !y[0].established) return;
    require (x[0].route == routes[0] && y[0].route == routes[0], "Relay-only peers selected a different route");
    if (x[0].roundtrip_ms <= 0 || y[0].roundtrip_ms <= 0) return;
    if (!forward_sent) forward_sent= first.send (b.handle, forward);
    if (!backward_sent) backward_sent= second.send (a.handle, backward);
    if (got_forward == forward && got_backward == backward) { routed= true; automatic.quit (); }
  });
  QObject::connect (&limit, &QTimer::timeout, &automatic, &QEventLoop::quit);
  poll.start (25); limit.setSingleShot (true); limit.start (12000); automatic.exec ();
  first.stop (); second.stop ();
  require (routed, "Automatic Relay-only route failed to exchange authenticated data");
  std::ofstream (root / "done") << "passed\n";
}
int main (int argc, char** argv) {
  QCoreApplication application (argc, argv);
  try {
    require (std::getenv ("ATHENA_HODARIUM_ISOLATED_KEYRING") != nullptr,
             "Run through isolated-keyring.sh; never use the user's keyring");
    auto identity= create_device_identity ();
    require (identity.public_key == device_public_key (identity.handle),
             "Stored identity changed");
    auto signature= sign_device_message (identity, "test-message");
    unsigned char key[32], sig[64]; std::size_t length;
    require (sodium_base642bin (key, sizeof key, identity.public_key.data (),
      identity.public_key.size (), nullptr, &length, nullptr,
      sodium_base64_VARIANT_URLSAFE_NO_PADDING) == 0 && length == sizeof key,
      "Invalid public key");
    require (sodium_base642bin (sig, sizeof sig, signature.data (), signature.size (),
      nullptr, &length, nullptr, sodium_base64_VARIANT_URLSAFE_NO_PADDING) == 0 &&
      length == sizeof sig, "Invalid signature");
    require (crypto_sign_verify_detached (sig,
      reinterpret_cast<const unsigned char*> ("test-message"), 12, key) == 0,
      "Protected identity produced invalid signature");
    auto wrong= identity; wrong.public_key= std::string (43, 'A');
    bool mismatch= false;
    try { sign_device_message (wrong, "test"); }
    catch (const key_store_error& e) { mismatch= e.reason == key_store_failure::corrupt; }
    require (mismatch, "Changed public identity accepted");
    bool missing= false;
    try { device_public_key (std::string (43, 'A')); }
    catch (const key_store_error& e) { missing= e.reason == key_store_failure::missing; }
    require (missing, "Missing identity silently replaced");
    auto vault_key= create_vault_secret (identity.public_key, "10000000-0000-4000-8000-000000000001");
    auto tokens= derive_conflict_tokens (vault_key, "document", {std::string (64, 'a'), std::string (64, 'b')},
                                        std::string (64, 'c'));
    auto reordered= derive_conflict_tokens (vault_key, "document", {std::string (64, 'b'), std::string (64, 'a')},
                                           std::string (64, 'c'));
    require (tokens.vault.size () == 43 && tokens.conflict == reordered.conflict &&
      tokens.branches == reordered.branches && tokens.resolution == reordered.resolution &&
      tokens.conflict != tokens.branches && tokens.vault != vault_key.commitment,
      "Opaque conflict identities lost determinism or domain separation");
    auto other_object= derive_conflict_tokens (vault_key, "different-document",
      {std::string (64, 'a'), std::string (64, 'b')}, std::string (64, 'c'));
    require (other_object.vault == tokens.vault && other_object.conflict != tokens.conflict &&
      other_object.resolution != tokens.resolution, "Opaque identities conflate source objects");
    bool separated= false;
    try { device_public_key (vault_key.handle); }
    catch (const key_store_error& e) { separated= e.reason == key_store_failure::missing; }
    require (separated, "Vault secret can be used as a device signing seed");
    auto rebound= vault_key; rebound.vault= "different-vault";
    bool rejected_scope= false;
    try { derive_conflict_tokens (rebound, "document", {std::string (64, 'a'), std::string (64, 'b')}, std::string (64, 'c')); }
    catch (const key_store_error& e) { rejected_scope= e.reason == key_store_failure::corrupt; }
    require (rejected_scope, "Vault secret was silently rebound to another scope");
    auto peer= create_device_identity ();
    vault_secret_context exchange{vault_key.group, identity.handle, peer.handle, vault_key.vault,
      vault_key.commitment, identity.public_key, peer.public_key};
    bool exchange_allowed= true;
    auto authorize= [&] (const vault_secret_context& context) {
      return exchange_allowed && context.group == exchange.group && context.generation == exchange.generation &&
        context.epoch == exchange.epoch && context.vault == exchange.vault && context.commitment == exchange.commitment &&
        context.sender_public_key == identity.public_key && context.recipient_public_key == peer.public_key;
    };
    auto rejected= [&] (auto operation) {
      bool failed= false;
      try { operation (); } catch (const std::exception&) { failed= true; }
      require (failed, "Invalid protected Vault secret transfer accepted");
    };
    vault_secret_receiver recipient (peer, exchange, authorize);
    vault_secret_receiver other_request (peer, exchange, authorize);
    auto capsule= seal_vault_secret (vault_key, identity, exchange, recipient.request (), authorize);
    rejected ([&] { other_request.receive (capsule); });
    auto damaged= capsule; damaged[damaged.size () / 2]= '!';
    rejected ([&] { recipient.receive (damaged); });
    exchange_allowed= false;
    rejected ([&] { recipient.receive (capsule); });
    rejected ([&] { seal_vault_secret (vault_key, identity, exchange, recipient.request (), authorize); });
    exchange_allowed= true;
    auto imported= recipient.receive (capsule);
    auto shared= derive_conflict_tokens (imported, "document", {std::string (64, 'a'), std::string (64, 'b')},
                                        std::string (64, 'c'));
    require (imported.handle != vault_key.handle && imported.commitment == vault_key.commitment &&
      shared.vault == tokens.vault && shared.conflict == tokens.conflict &&
      shared.branches == tokens.branches && shared.resolution == tokens.resolution,
      "Protected transfer did not preserve shared opaque identities");
    rejected ([&] { recipient.receive (capsule); });
    QTemporaryDir settings_directory;
    require (settings_directory.isValid (), "Cannot create isolated settings directory");
    auto settings_root= std::filesystem::path (settings_directory.path ().toStdString ());
    std::filesystem::create_directory (settings_root / "vault");
    {
      client_settings settings (settings_root / "settings.sqlite");
      client_profile profile;
      profile.origin= QUrl ("https://authority.invalid");
      profile.pin= {vault_key.group, identity.public_key, exchange.generation};
      profile.recovery_public_key= peer.public_key;
      profile.device= identity; profile.name= "isolated";
      settings.add_pending (profile);
      rejected ([&] { settings.remember_vault_secret (exchange.generation, vault_key); });
      settings.complete_admission (vault_key.group, identity.handle, identity.public_key, peer.handle);
      rejected ([&] { settings.remember_vault_secret (exchange.generation, vault_key); });
      settings.bind_vault ({vault_key.group, vault_key.vault, settings_root / "vault", true});
      settings.remember_vault_secret (exchange.generation, vault_key);
      settings.remember_vault_secret (exchange.generation, vault_key);
      rejected ([&] { settings.remember_vault_secret (peer.handle, vault_key); });
      rejected ([&] { settings.remember_vault_secret (exchange.generation, imported); });
      auto corrupt= vault_key; corrupt.commitment= peer.public_key;
      rejected ([&] { settings.remember_vault_secret (exchange.generation, corrupt); });
      settings.unbind_vault (vault_key.group, vault_key.vault);
    }
    {
      client_settings reopened (settings_root / "settings.sqlite");
      auto saved= reopened.find_vault_secret (vault_key.group, exchange.generation, vault_key.vault, vault_key.commitment);
      require (saved && saved->handle == vault_key.handle, "Unbind or restart lost protected secret descriptor");
      verify_vault_secret (*saved);
      require (!reopened.find_vault_secret (vault_key.group, peer.handle, vault_key.vault, vault_key.commitment),
               "Secret lookup crossed authority generations");
    }
    check_peer_tls (identity, peer, false);
    bool peer_rejected= false;
    try { check_peer_tls (identity, peer, true); }
    catch (const std::runtime_error&) { peer_rejected= true; }
    require (peer_rejected, "Peer TLS accepted the wrong device identity");
    bool epoch_rejected= false;
    try { check_peer_tls (identity, peer, false, true); }
    catch (const std::runtime_error& e) {
      epoch_rejected= std::string (e.what ()).find ("context mismatch") != std::string::npos;
    }
    require (epoch_rejected, "Peer TLS accepted a different epoch");
    check_tcp_peer (identity, peer);
    check_direct_rejection (identity, peer);
    check_peer_network (identity, peer);
    if (const char* relay= std::getenv ("ATHENA_HODARIUM_NATIVE_RELAY")) check_relay_peer (identity, peer, relay);
    GError* error= nullptr;
    auto* service= secret_service_get_sync (SECRET_SERVICE_NONE, nullptr, &error);
    require (service != nullptr && error == nullptr, "Cannot inspect isolated service");
    auto* collection= secret_collection_for_alias_sync (service,
      SECRET_COLLECTION_DEFAULT, SECRET_COLLECTION_NONE, nullptr, &error);
    require (collection != nullptr && error == nullptr, "No isolated collection");
    GList* objects= g_list_append (nullptr, collection);
    auto count= secret_service_lock_sync (service, objects, nullptr, nullptr, &error);
    g_list_free (objects); g_object_unref (collection); g_object_unref (service);
    require (count > 0 && error == nullptr, "Could not lock isolated collection");
    bool locked= false;
    try { sign_device_message (identity, "test"); }
    catch (const key_store_error& e) { locked= e.reason == key_store_failure::locked; }
    require (locked, "Locked keyring did not suspend signing");
    std::cout << "Protected identity, peer TLS, TCP transfer/backpressure, revocation and lock checks passed\n";
  }
  catch (const std::exception& e) { std::cerr << e.what () << '\n'; return 1; }
}
