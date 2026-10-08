/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#include "peer_tls.hpp"
#include <gnutls/abstract.h>
#include <sodium.h>
#include <cstring>
#include <stdexcept>
#include <thread>
#include <nlohmann/json.hpp>

namespace athena::hodarium {
namespace {
void check (int result) {
  if (result < 0) throw std::runtime_error (std::string ("Hodarium peer TLS: ") + gnutls_strerror (result));
}
struct datum {
  gnutls_datum_t value{};
  ~datum () { gnutls_free (value.data); }
};
struct public_key {
  gnutls_pubkey_t value= nullptr;
  explicit public_key (const std::string& encoded) {
    unsigned char bytes[32]; std::size_t count= 0;
    if (encoded.size () != 43 || sodium_base642bin (bytes, sizeof bytes, encoded.data (),
        encoded.size (), nullptr, &count, nullptr, sodium_base64_VARIANT_URLSAFE_NO_PADDING) != 0 || count != 32)
      throw std::invalid_argument ("Invalid Hodarium peer public key");
    check (gnutls_pubkey_init (&value));
    gnutls_datum_t x{bytes, sizeof bytes};
    int result= gnutls_pubkey_import_ecc_raw (value, GNUTLS_ECC_CURVE_ED25519, &x, nullptr);
    if (result < 0) { gnutls_pubkey_deinit (value); value= nullptr; check (result); }
  }
  ~public_key () { if (value) gnutls_pubkey_deinit (value); }
};
}
struct peer_tls::implementation {
  device_identity local;
  std::string expected, signing_error;
  peer_context context;
  std::function<bool ()> authorized;
  std::string outgoing, incoming, expected_context;
  std::size_t sent= 0, received= 0;
  bool server= false, tls_ready= false;
  std::thread::id owner= std::this_thread::get_id ();
  gnutls_session_t session= nullptr;
  gnutls_certificate_credentials_t credentials= nullptr;
  gnutls_privkey_t key= nullptr;
  gnutls_pcert_st certificate{};
  bool certificate_ready= false, ready= false, failed= false;
  ~implementation () {
    if (session) gnutls_deinit (session);
    if (credentials) gnutls_certificate_free_credentials (credentials);
    if (certificate_ready) gnutls_pcert_deinit (&certificate);
    if (key) gnutls_privkey_deinit (key);
  }
  void owned () {
    if (owner != std::this_thread::get_id ()) throw std::logic_error ("Peer TLS accessed outside its worker");
    if (failed) throw std::runtime_error ("Peer TLS session has failed");
    if (!authorized ()) { failed= true; throw std::runtime_error ("Hodarium peer membership is no longer authorized"); }
  }
  static int info (gnutls_privkey_t, unsigned flags, void*) {
    if (flags & GNUTLS_PRIVKEY_INFO_PK_ALGO) return GNUTLS_PK_EDDSA_ED25519;
    if (flags & GNUTLS_PRIVKEY_INFO_SIGN_ALGO) return GNUTLS_SIGN_EDDSA_ED25519;
    if (flags & GNUTLS_PRIVKEY_INFO_HAVE_SIGN_ALGO)
      return GNUTLS_FLAGS_TO_SIGN_ALGO (flags) == GNUTLS_SIGN_EDDSA_ED25519;
    if (flags & GNUTLS_PRIVKEY_INFO_PK_ALGO_BITS) return 256;
    return -1;
  }
  static int sign (gnutls_privkey_t, gnutls_sign_algorithm_t algorithm, void* data,
    unsigned, const gnutls_datum_t* message, gnutls_datum_t* output) noexcept {
    auto* self= static_cast<implementation*> (data);
    try {
      self->owned ();
      if (algorithm != GNUTLS_SIGN_EDDSA_ED25519) return GNUTLS_E_UNSUPPORTED_SIGNATURE_ALGORITHM;
      auto signature= sign_device_message (self->local,
        std::string (reinterpret_cast<char*> (message->data), message->size));
      unsigned char bytes[64]; std::size_t size= 0;
      if (sodium_base642bin (bytes, sizeof bytes, signature.data (), signature.size (), nullptr,
          &size, nullptr, sodium_base64_VARIANT_URLSAFE_NO_PADDING) != 0 || size != 64)
        return GNUTLS_E_PK_SIGN_FAILED;
      output->data= static_cast<unsigned char*> (gnutls_malloc (64));
      if (!output->data) return GNUTLS_E_MEMORY_ERROR;
      std::memcpy (output->data, bytes, 64); output->size= 64;
      return 0;
    }
    catch (const std::exception& e) {
      try { self->signing_error= e.what (); } catch (...) {}
      return GNUTLS_E_PK_SIGN_FAILED;
    }
    catch (...) { return GNUTLS_E_PK_SIGN_FAILED; }
  }
  static int retrieve (gnutls_session_t session, const gnutls_datum_t*, int,
    const gnutls_pk_algorithm_t*, int, gnutls_pcert_st** certificate,
    unsigned* count, gnutls_privkey_t* key) {
    auto* self= static_cast<implementation*> (gnutls_session_get_ptr (session));
    *certificate= &self->certificate; *count= 1; *key= self->key; return 0;
  }
  static int verify (gnutls_session_t session) {
    auto* self= static_cast<implementation*> (gnutls_session_get_ptr (session));
    unsigned count= 0;
    auto* peer= gnutls_certificate_get_peers (session, &count);
    if (gnutls_certificate_type_get2 (session, GNUTLS_CTYPE_PEERS) != GNUTLS_CRT_RAWPK ||
        count != 1 || !peer || peer[0].size != self->expected.size () ||
        sodium_memcmp (peer[0].data, self->expected.data (), self->expected.size ()) != 0)
      return GNUTLS_E_CERTIFICATE_ERROR;
    return 0;
  }
};
peer_tls::peer_tls (device_identity local, std::string peer, bool server,
  peer_context context, std::function<bool ()> authorized,
  void* transport, gnutls_pull_func pull, gnutls_push_func push):
  state_ (std::make_unique<implementation> ()) {
  static const int initialized= gnutls_global_init ();
  check (initialized);
  if (!pull || !push || !authorized) throw std::invalid_argument ("Peer TLS requires transport and membership authorization");
  for (const auto* id: {&context.group, &context.generation, &context.epoch, &context.local_member, &context.remote_member}) {
    unsigned char bytes[32]; std::size_t count= 0;
    if (id->size () != 43 || sodium_base642bin (bytes, sizeof bytes, id->data (), id->size (),
        nullptr, &count, nullptr, sodium_base64_VARIANT_URLSAFE_NO_PADDING) != 0 || count != 32)
      throw std::invalid_argument ("Invalid Hodarium peer context identity");
  }
  if (context.local_member == context.remote_member || local.public_key == peer)
    throw std::invalid_argument ("Hodarium peer connection cannot target itself");
  auto& s= *state_; s.local= std::move (local); s.context= std::move (context);
  s.authorized= std::move (authorized); s.server= server; s.owned ();
  public_key remote (peer), own (s.local.public_key);
  datum der;
  check (gnutls_pubkey_export2 (remote.value, GNUTLS_X509_FMT_DER, &der.value));
  s.expected.assign (reinterpret_cast<char*> (der.value.data), der.value.size);
  check (gnutls_pcert_import_rawpk (&s.certificate, own.value, 0));
  own.value= nullptr; s.certificate_ready= true;
  check (gnutls_privkey_init (&s.key));
  check (gnutls_privkey_import_ext4 (s.key, &s, implementation::sign, nullptr, nullptr,
    nullptr, implementation::info, 0));
  check (gnutls_certificate_allocate_credentials (&s.credentials));
  gnutls_certificate_set_retrieve_function2 (s.credentials, implementation::retrieve);
  gnutls_certificate_set_verify_function (s.credentials, implementation::verify);
  check (gnutls_init (&s.session, (server ? GNUTLS_SERVER : GNUTLS_CLIENT) |
    GNUTLS_NONBLOCK | GNUTLS_ENABLE_RAWPK | GNUTLS_NO_TICKETS));
  gnutls_session_set_ptr (s.session, &s);
  check (gnutls_priority_set_direct (s.session,
    "NORMAL:-VERS-ALL:+VERS-TLS1.3:-CTYPE-ALL:+CTYPE-RAWPK", nullptr));
  check (gnutls_credentials_set (s.session, GNUTLS_CRD_CERTIFICATE, s.credentials));
  if (server) gnutls_certificate_server_set_request (s.session, GNUTLS_CERT_REQUIRE);
  const char protocol[]= "athena-hodarium-peer-v1";
  gnutls_datum_t alpn{reinterpret_cast<unsigned char*> (const_cast<char*> (protocol)), sizeof protocol - 1};
  check (gnutls_alpn_set_protocols (s.session, &alpn, 1, GNUTLS_ALPN_MANDATORY));
  gnutls_transport_set_ptr (s.session, transport);
  gnutls_transport_set_pull_function (s.session, pull);
  gnutls_transport_set_push_function (s.session, push);
}
peer_tls::~peer_tls ()= default;
bool peer_tls::handshake () {
  auto& s= *state_; s.owned ();
  try {
  if (s.ready) return true;
  if (!s.tls_ready) {
    int result= gnutls_handshake (s.session);
    if (result == GNUTLS_E_AGAIN || result == GNUTLS_E_INTERRUPTED) return false;
    if (result < 0) {
      s.failed= true;
      if (!s.signing_error.empty ()) throw std::runtime_error (s.signing_error);
      check (result);
    }
    s.tls_ready= true;
    datum binding;
    check (gnutls_session_channel_binding (s.session, GNUTLS_CB_TLS_EXPORTER, &binding.value));
    std::string encoded (binding.value.size * 2, '\0');
    constexpr char hex[]= "0123456789abcdef";
    for (unsigned i= 0; i < binding.value.size; ++i) {
      encoded[2*i]= hex[binding.value.data[i] >> 4]; encoded[2*i+1]= hex[binding.value.data[i] & 15];
    }
    const auto& c= s.context;
    auto message= [&] (bool sender_server) {
      return nlohmann::json::array ({"ATHENA-HODARIUM-PEER-v1", c.group, c.generation, c.epoch,
        s.server ? c.remote_member : c.local_member, s.server ? c.local_member : c.remote_member,
        sender_server ? "server" : "client", encoded}).dump ();
    };
    s.outgoing= message (s.server); s.expected_context= message (!s.server);
    s.incoming.resize (s.expected_context.size ());
  }
  while (s.sent < s.outgoing.size ()) {
    auto n= gnutls_record_send (s.session, s.outgoing.data () + s.sent, s.outgoing.size () - s.sent);
    if (n == GNUTLS_E_AGAIN || n == GNUTLS_E_INTERRUPTED) return false;
    if (n <= 0) { s.failed= true; throw std::runtime_error ("Peer membership context send failed"); }
    s.sent+= n;
  }
  while (s.received < s.incoming.size ()) {
    auto n= gnutls_record_recv (s.session, s.incoming.data () + s.received, s.incoming.size () - s.received);
    if (n == GNUTLS_E_AGAIN || n == GNUTLS_E_INTERRUPTED) return false;
    if (n <= 0) { s.failed= true; throw std::runtime_error ("Peer membership context receive failed"); }
    s.received+= n;
  }
  if (s.incoming != s.expected_context) {
    s.failed= true; throw std::runtime_error ("Peer Hodarium, generation, epoch or member context mismatch");
  }
  s.owned ();
  s.ready= true; return true;
  }
  catch (...) { s.failed= true; throw; }
}
bool peer_tls::wants_write () const { state_->owned (); return gnutls_record_get_direction (state_->session) != 0; }
ssize_t peer_tls::send (const void* data, std::size_t size) {
  auto& s= *state_; s.owned ();
  if (!s.ready) throw std::logic_error ("Peer TLS handshake incomplete");
  auto result= gnutls_record_send (s.session, data, size);
  if (result < 0 && result != GNUTLS_E_AGAIN && result != GNUTLS_E_INTERRUPTED) { s.failed= true; check (int (result)); }
  return result;
}
ssize_t peer_tls::receive (void* data, std::size_t size) {
  auto& s= *state_; s.owned ();
  if (!s.ready) throw std::logic_error ("Peer TLS handshake incomplete");
  auto result= gnutls_record_recv (s.session, data, size);
  if (result < 0 && result != GNUTLS_E_AGAIN && result != GNUTLS_E_INTERRUPTED) { s.failed= true; check (int (result)); }
  return result;
}
std::string peer_tls::channel_binding () const {
  auto& s= *state_; s.owned ();
  if (!s.ready) throw std::logic_error ("Peer TLS handshake incomplete");
  datum binding;
  check (gnutls_session_channel_binding (s.session, GNUTLS_CB_TLS_EXPORTER, &binding.value));
  return std::string (reinterpret_cast<char*> (binding.value.data), binding.value.size);
}
} // namespace athena::hodarium
