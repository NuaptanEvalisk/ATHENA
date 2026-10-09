/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#include "device_identity.hpp"
#include "device_key_internal.hpp"
#include <nlohmann/json.hpp>
#include <sodium.h>
#include <algorithm>

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
  detail::store_device_seed (device.handle, seed.value);
  return device;
}
std::string device_public_key (const std::string& handle) {
  validate_handle (handle);
  secret_bytes seed (crypto_sign_SEEDBYTES);
  detail::load_device_seed (handle, seed.value);
  key_pair pair (seed.value);
  return encode (pair.public_key, sizeof pair.public_key);
}
std::string sign_device_message (const device_identity& device,
                                 const std::string& message) {
  validate_handle (device.handle);
  if (message.size () > 8*1024*1024)
    throw std::invalid_argument ("Hodarium signing message exceeds budget");
  secret_bytes seed (crypto_sign_SEEDBYTES);
  detail::load_device_seed (device.handle, seed.value);
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
      purpose != "resolve" && purpose != "rendezvous")
    throw std::invalid_argument ("Unsupported Hodarium device proof purpose");
  validate_handle (subject);
  return sign_device_message (device, nlohmann::json::array ({
    "ATHENA-HODARIUM-PROOF-v1", group, purpose, subject, nonce}).dump ());
}
} // namespace athena::hodarium
