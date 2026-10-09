/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#include "device_identity.hpp"
#include "device_key_internal.hpp"
#include "vault_secret.hpp"
#include "relay_credentials.hpp"
#include "control_http.hpp"
#include <nlohmann/json.hpp>
#include <sodium.h>
#include <algorithm>
#include <chrono>

namespace athena::hodarium {
namespace {
void initialize () {
  static const int result= sodium_init ();
  if (result < 0) throw key_store_error (key_store_failure::unavailable,
    "Cannot initialize Hodarium identity cryptography");
}
struct secret_bytes {
  unsigned char* value;
  explicit secret_bytes (std::size_t size) {
    initialize ();
    value= static_cast<unsigned char*> (sodium_malloc (size));
    if (value == nullptr) throw std::bad_alloc ();
  }
  ~secret_bytes () { sodium_free (value); }
  secret_bytes (const secret_bytes&)= delete;
  secret_bytes& operator= (const secret_bytes&)= delete;
};
std::string encode (const unsigned char* bytes, std::size_t size) {
  std::string result (sodium_base64_ENCODED_LEN (size,
    sodium_base64_VARIANT_URLSAFE_NO_PADDING), '\0');
  sodium_bin2base64 (result.data (), result.size (), bytes, size,
    sodium_base64_VARIANT_URLSAFE_NO_PADDING);
  result.resize (result.size () - 1);
  return result;
}
void validate_handle (const std::string& handle) {
  if (handle.size () != 43 || !std::all_of (handle.begin (), handle.end (),
      [] (unsigned char c) { return (c >= 'A' && c <= 'Z') ||
        (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_'; }))
    throw std::invalid_argument ("Invalid Hodarium key handle");
}
struct key_pair {
  secret_bytes secret{crypto_sign_SECRETKEYBYTES};
  unsigned char public_key[crypto_sign_PUBLICKEYBYTES];
  explicit key_pair (const unsigned char* seed) {
    crypto_sign_seed_keypair (public_key, secret.value, seed);
  }
};
}

device_identity create_device_identity () {
  secret_bytes seed (crypto_sign_SEEDBYTES);
  randombytes_buf (seed.value, crypto_sign_SEEDBYTES);
  key_pair pair (seed.value);
  unsigned char id[32]; randombytes_buf (id, sizeof id);
  device_identity device{encode (id, sizeof id),
    encode (pair.public_key, sizeof pair.public_key)};
  detail::store_protected_seed (device.handle, seed.value, detail::secret_kind::device);
  return device;
}
std::string device_public_key (const std::string& handle) {
  validate_handle (handle);
  secret_bytes seed (crypto_sign_SEEDBYTES);
  detail::load_protected_seed (handle, seed.value, detail::secret_kind::device);
  key_pair pair (seed.value);
  return encode (pair.public_key, sizeof pair.public_key);
}
std::string sign_device_message (const device_identity& device,
                                 const std::string& message) {
  validate_handle (device.handle);
  if (message.size () > 8*1024*1024)
    throw std::invalid_argument ("Hodarium signing message exceeds budget");
  secret_bytes seed (crypto_sign_SEEDBYTES);
  detail::load_protected_seed (device.handle, seed.value, detail::secret_kind::device);
  key_pair pair (seed.value);
  if (encode (pair.public_key, sizeof pair.public_key) != device.public_key)
    throw key_store_error (key_store_failure::corrupt,
      "Hodarium protected key does not match the enrolled public identity");
  unsigned char signature[crypto_sign_BYTES];
  crypto_sign_detached (signature, nullptr,
    reinterpret_cast<const unsigned char*> (message.data ()), message.size (),
    pair.secret.value);
  return encode (signature, sizeof signature);
}
std::string sign_device_proof (const device_identity& device,
  const std::string& group, const std::string& purpose,
  const std::string& subject, const std::string& nonce) {
  validate_handle (group); validate_handle (nonce);
  if (purpose != "join" && purpose != "poll" && purpose != "control" &&
      purpose != "resolve" && purpose != "rendezvous" && purpose != "decision" &&
      purpose != "decision-feed" && purpose != "vault-secret")
    throw std::invalid_argument ("Unsupported Hodarium device proof purpose");
  validate_handle (subject);
  return sign_device_message (device, nlohmann::json::array ({
    "ATHENA-HODARIUM-PROOF-v1", group, purpose, subject, nonce}).dump ());
}
namespace {
std::string vault_token (const unsigned char* key, const std::string& group,
  const std::string& vault, const char* role, const nlohmann::json& value) {
  auto bytes= nlohmann::json::to_cbor (nlohmann::json::array ({
    "ATHENA-HODARIUM-OPAQUE-v1", group, vault, role, value}));
  unsigned char digest[32];
  if (crypto_generichash (digest, sizeof digest, bytes.data (), bytes.size (), key, 32) != 0)
    throw std::runtime_error ("Cannot derive Hodarium opaque identity");
  return encode (digest, sizeof digest);
}
void validate_scope (const std::string& group, const std::string& vault) {
  validate_handle (group);
  if (vault.empty () || vault.size () > 1024 || vault.find ('\0') != std::string::npos)
    throw std::invalid_argument ("Invalid Hodarium Vault scope");
}
void validate_revision_id (const std::string& value) {
  if (value.size () != 64 || !std::all_of (value.begin (), value.end (), [] (char c) {
      return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    })) throw std::invalid_argument ("Invalid Hodarium opaque revision input");
}
}
vault_secret create_vault_secret (const std::string& group, const std::string& vault) {
  validate_scope (group, vault);
  secret_bytes key (32); randombytes_buf (key.value, 32);
  unsigned char id[32]; randombytes_buf (id, sizeof id);
  vault_secret result{encode (id, sizeof id), group, vault,
    vault_token (key.value, group, vault, "commitment", nullptr)};
  detail::store_protected_seed (result.handle, key.value, detail::secret_kind::vault);
  return result;
}
void verify_vault_secret (const vault_secret& secret) {
  validate_scope (secret.group, secret.vault); validate_handle (secret.handle);
  secret_bytes key (32);
  detail::load_protected_seed (secret.handle, key.value, detail::secret_kind::vault);
  if (secret.commitment != vault_token (key.value, secret.group, secret.vault, "commitment", nullptr))
    throw key_store_error (key_store_failure::corrupt, "Protected Hodarium secret does not match its Vault scope");
}
conflict_tokens derive_conflict_tokens (const vault_secret& secret,
  const std::string& object, std::vector<std::string> parents, const std::string& resolution_id) {
  validate_scope (secret.group, secret.vault); validate_handle (secret.handle);
  if (object.empty () || object.size () > 1024 || object.find ('\0') != std::string::npos ||
      parents.size () < 2 || parents.size () > 256)
    throw std::invalid_argument ("Invalid Hodarium conflict identity input");
  validate_revision_id (resolution_id);
  for (const auto& id: parents) validate_revision_id (id);
  std::sort (parents.begin (), parents.end ());
  parents.erase (std::unique (parents.begin (), parents.end ()), parents.end ());
  if (parents.size () < 2) throw std::invalid_argument ("Conflict needs distinct branches");
  secret_bytes key (32);
  detail::load_protected_seed (secret.handle, key.value, detail::secret_kind::vault);
  if (secret.commitment != vault_token (key.value, secret.group, secret.vault, "commitment", nullptr))
    throw key_store_error (key_store_failure::corrupt, "Protected Hodarium secret does not match its Vault scope");
  auto branch_set= nlohmann::json::array ({object, parents});
  return {vault_token (key.value, secret.group, secret.vault, "vault", nullptr),
    vault_token (key.value, secret.group, secret.vault, "conflict", branch_set),
    vault_token (key.value, secret.group, secret.vault, "branches", branch_set),
    vault_token (key.value, secret.group, secret.vault, "resolution", nlohmann::json::array ({object, resolution_id}))};
}

namespace {
using json= nlohmann::json;
json transfer_context (const vault_secret_context& c) {
  validate_scope (c.group, c.vault);
  for (const auto* token: {&c.generation, &c.epoch, &c.commitment, &c.sender_public_key, &c.recipient_public_key})
    validate_handle (*token);
  return json::array ({1, c.group, c.generation, c.epoch, c.vault, c.commitment,
                       c.sender_public_key, c.recipient_public_key});
}
void authorize_secret (const vault_secret_context& context, const vault_secret_authorization& authorized) {
  if (!authorized || !authorized (context))
    throw std::invalid_argument ("Hodarium secret exchange is not currently authorized");
}
std::string unbase64 (const std::string& value) {
  if (value.size () > 16384) throw std::invalid_argument ("Hodarium secret envelope exceeds budget");
  std::string bytes (value.size (), '\0'); std::size_t size= 0;
  if (sodium_base642bin (reinterpret_cast<unsigned char*> (bytes.data ()), bytes.size (),
      value.data (), value.size (), nullptr, &size, nullptr, sodium_base64_VARIANT_URLSAFE_NO_PADDING) != 0)
    throw std::invalid_argument ("Invalid Hodarium secret envelope encoding");
  bytes.resize (size);
  if (encode (reinterpret_cast<const unsigned char*> (bytes.data ()), bytes.size ()) != value)
    throw std::invalid_argument ("Noncanonical Hodarium secret envelope encoding");
  return bytes;
}
json transfer_json (const std::string& value) {
  if (value.size () > 16384) throw std::invalid_argument ("Hodarium secret envelope exceeds budget");
  return json::parse (value, [] (int depth, json::parse_event_t event, json&) {
    if (depth > 6 || event == json::parse_event_t::object_start)
      throw std::invalid_argument ("Invalid Hodarium secret envelope structure");
    return true;
  });
}
std::string secret_envelope (const device_identity& signer, const char* domain, const json& content) {
  auto payload= content.dump ();
  return json::array ({encode (reinterpret_cast<const unsigned char*> (payload.data ()), payload.size ()),
    sign_device_message (signer, std::string (domain) + '\0' + payload)}).dump ();
}
json verify_secret_envelope (const std::string& bytes, const std::string& public_key, const char* domain) {
  auto envelope= transfer_json (bytes);
  if (!envelope.is_array () || envelope.size () != 2)
    throw std::invalid_argument ("Invalid Hodarium secret envelope");
  auto payload= unbase64 (envelope[0].get<std::string> ());
  auto signature= unbase64 (envelope[1].get<std::string> ());
  auto key= unbase64 (public_key);
  auto message= std::string (domain) + '\0' + payload;
  if (key.size () != crypto_sign_PUBLICKEYBYTES || signature.size () != crypto_sign_BYTES ||
      crypto_sign_verify_detached (reinterpret_cast<const unsigned char*> (signature.data ()),
        reinterpret_cast<const unsigned char*> (message.data ()), message.size (),
        reinterpret_cast<const unsigned char*> (key.data ())) != 0)
    throw std::invalid_argument ("Invalid Hodarium secret exchange signature");
  return transfer_json (payload);
}
std::string transfer_subject (const std::string& request) {
  unsigned char hash[crypto_hash_sha256_BYTES];
  crypto_hash_sha256 (hash, reinterpret_cast<const unsigned char*> (request.data ()), request.size ());
  return encode (hash, sizeof hash);
}
}
struct vault_secret_receiver::implementation {
  vault_secret_context context;
  vault_secret_authorization authorized;
  secret_bytes key{crypto_box_SECRETKEYBYTES};
  unsigned char public_key[crypto_box_PUBLICKEYBYTES];
  std::string request;
  bool consumed= false;
  std::chrono::steady_clock::time_point deadline= std::chrono::steady_clock::now () + std::chrono::minutes (2);
  implementation (vault_secret_context c, vault_secret_authorization a): context (std::move (c)), authorized (std::move (a)) {}
};
vault_secret_receiver::vault_secret_receiver (const device_identity& recipient, vault_secret_context context,
  vault_secret_authorization authorized): state_ (std::make_unique<implementation> (std::move (context), std::move (authorized))) {
  auto& s= *state_;
  auto binding= transfer_context (s.context); authorize_secret (s.context, s.authorized);
  if (recipient.public_key != s.context.recipient_public_key)
    throw std::invalid_argument ("Hodarium secret recipient identity mismatch");
  crypto_box_keypair (s.public_key, s.key.value);
  unsigned char nonce[32]; randombytes_buf (nonce, sizeof nonce);
  s.request= secret_envelope (recipient, "ATHENA-HODARIUM-SECRET-REQUEST-v1",
    json::array ({binding, encode (nonce, sizeof nonce), encode (s.public_key, sizeof s.public_key)}));
  authorize_secret (s.context, s.authorized);
}
vault_secret_receiver::~vault_secret_receiver ()= default;
const std::string& vault_secret_receiver::request () const { return state_->request; }
vault_secret vault_secret_receiver::receive (const std::string& response) {
  auto& s= *state_;
  if (s.consumed || std::chrono::steady_clock::now () >= s.deadline)
    throw std::invalid_argument ("Hodarium secret request consumed or expired");
  authorize_secret (s.context, s.authorized);
  auto value= verify_secret_envelope (response, s.context.sender_public_key, "ATHENA-HODARIUM-SECRET-GRANT-v1");
  if (!value.is_array () || value.size () != 3 || value[0] != transfer_context (s.context) ||
      value[1] != transfer_subject (s.request))
    throw std::invalid_argument ("Hodarium secret grant does not match its request");
  auto cipher= unbase64 (value[2].get<std::string> ());
  secret_bytes secret (32);
  if (cipher.size () != 32 + crypto_box_SEALBYTES || crypto_box_seal_open (secret.value,
      reinterpret_cast<const unsigned char*> (cipher.data ()), cipher.size (), s.public_key, s.key.value) != 0)
    throw std::invalid_argument ("Cannot decrypt Hodarium secret grant");
  if (vault_token (secret.value, s.context.group, s.context.vault, "commitment", nullptr) != s.context.commitment)
    throw std::invalid_argument ("Hodarium secret grant has the wrong commitment");
  authorize_secret (s.context, s.authorized);
  unsigned char id[32]; randombytes_buf (id, sizeof id);
  vault_secret result{encode (id, sizeof id), s.context.group, s.context.vault, s.context.commitment};
  detail::store_protected_seed (result.handle, secret.value, detail::secret_kind::vault);
  s.consumed= true; sodium_memzero (s.key.value, crypto_box_SECRETKEYBYTES);
  return result;
}
std::string seal_vault_secret (const vault_secret& secret, const device_identity& sender,
  const vault_secret_context& context, const std::string& request,
  const vault_secret_authorization& authorized) {
  initialize (); authorize_secret (context, authorized);
  auto binding= transfer_context (context);
  if (secret.group != context.group || secret.vault != context.vault || secret.commitment != context.commitment ||
      sender.public_key != context.sender_public_key)
    throw std::invalid_argument ("Hodarium secret grant scope mismatch");
  validate_handle (secret.handle);
  auto value= verify_secret_envelope (request, context.recipient_public_key, "ATHENA-HODARIUM-SECRET-REQUEST-v1");
  if (!value.is_array () || value.size () != 3 || value[0] != binding ||
      unbase64 (value[1].get<std::string> ()).size () != 32)
    throw std::invalid_argument ("Hodarium secret request scope mismatch");
  auto recipient= unbase64 (value[2].get<std::string> ());
  if (recipient.size () != crypto_box_PUBLICKEYBYTES)
    throw std::invalid_argument ("Invalid ephemeral Hodarium recipient key");
  secret_bytes key (32);
  detail::load_protected_seed (secret.handle, key.value, detail::secret_kind::vault);
  if (vault_token (key.value, secret.group, secret.vault, "commitment", nullptr) != context.commitment)
    throw key_store_error (key_store_failure::corrupt, "Protected Hodarium Vault secret has changed");
  unsigned char cipher[32 + crypto_box_SEALBYTES];
  if (crypto_box_seal (cipher, key.value, 32, reinterpret_cast<const unsigned char*> (recipient.data ())) != 0)
    throw std::invalid_argument ("Cannot seal Hodarium Vault secret");
  auto result= secret_envelope (sender, "ATHENA-HODARIUM-SECRET-GRANT-v1",
    json::array ({binding, transfer_subject (request), encode (cipher, sizeof cipher)}));
  authorize_secret (context, authorized);
  return result;
}
std::string canonical_relay_origin (QUrl origin) {
  if (origin.path () == "/") origin.setPath ({});
  if (origin.port () == 443) origin.setPort (-1);
  validate_authority_origin (origin);
  auto result= origin.toString (QUrl::FullyEncoded).toStdString ();
  if (result.size () > 2048) throw std::invalid_argument ("Relay origin exceeds budget");
  return result;
}
namespace {
std::string relay_account (const QUrl& origin, const std::string& handle) {
  validate_handle (handle);
  auto scoped= nlohmann::json::array ({"ATHENA-HODARIUM-RELAY-CREDENTIAL-v1",
    canonical_relay_origin (origin), handle}).dump ();
  unsigned char hash[crypto_hash_sha256_BYTES];
  crypto_hash_sha256 (hash, reinterpret_cast<const unsigned char*> (scoped.data ()), scoped.size ());
  return encode (hash, sizeof hash);
}
}
std::string store_relay_token (const QUrl& origin, const std::string& token) {
  secret_bytes bytes (32); std::size_t size= 0;
  if (token.size () != 43 || sodium_base642bin (bytes.value, 32, token.data (), token.size (),
      nullptr, &size, nullptr, sodium_base64_VARIANT_URLSAFE_NO_PADDING) != 0 || size != 32 ||
      encode (bytes.value, 32) != token)
    throw std::invalid_argument ("Invalid Relay access token");
  unsigned char id[32]; randombytes_buf (id, sizeof id);
  auto handle= encode (id, sizeof id);
  detail::store_protected_seed (relay_account (origin, handle), bytes.value, detail::secret_kind::relay);
  return handle;
}
std::string load_relay_token (const QUrl& origin, const std::string& handle) {
  secret_bytes bytes (32);
  detail::load_protected_seed (relay_account (origin, handle), bytes.value, detail::secret_kind::relay);
  return encode (bytes.value, 32);
}
void delete_relay_token (const QUrl& origin, const std::string& handle) {
  initialize ();
  detail::delete_protected_seed (relay_account (origin, handle), detail::secret_kind::relay);
}
} // namespace athena::hodarium
