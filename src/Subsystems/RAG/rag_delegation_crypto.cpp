/******************************************************************************
* MODULE     : rag_delegation_crypto.cpp
* DESCRIPTION: Key and envelope helpers for ATHENA RAG delegation
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "rag_delegation_crypto.hpp"

#include <sodium.h>

#include <algorithm>
#include <array>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <system_error>
#include <vector>

#ifndef OS_MINGW
#include <sys/stat.h>
#endif

namespace fs = std::filesystem;

namespace athena::rag::delegation {
namespace {

static bool crypto_initialized= false;

bool
check_key_size (const std::string& key, size_t expected,
                const char* name, std::string& error) {
  if (key.size () == expected) return true;
  error= std::string (name) + " has invalid length";
  return false;
}

bool
read_text_file (const fs::path& path, std::string& text,
                std::string& error) {
  std::ifstream in (path, std::ios::binary);
  if (!in) {
    error= "failed to read " + path.generic_string ();
    return false;
  }
  std::ostringstream ss;
  ss << in.rdbuf ();
  text= ss.str ();
  while (!text.empty () &&
         (text.back () == '\n' || text.back () == '\r' ||
          text.back () == ' ' || text.back () == '\t'))
    text.pop_back ();
  return true;
}

bool
write_text_file (const fs::path& path, const std::string& text,
                 bool secret, std::string& error) {
  std::error_code ec;
  fs::create_directories (path.parent_path (), ec);
  if (ec) {
    error= "failed to create " + path.parent_path ().generic_string () +
           ": " + ec.message ();
    return false;
  }
  std::ofstream out (path, std::ios::binary | std::ios::trunc);
  if (!out) {
    error= "failed to write " + path.generic_string ();
    return false;
  }
  out << text << "\n";
  out.close ();
#ifndef OS_MINGW
  if (secret) chmod (path.c_str (), S_IRUSR | S_IWUSR);
#else
  (void) secret;
#endif
  return true;
}

} // namespace

bool
initialize_crypto (std::string& error) {
  if (crypto_initialized) return true;
  if (sodium_init () < 0) {
    error= "libsodium initialization failed";
    return false;
  }
  crypto_initialized= true;
  return true;
}

std::string
base64_encode (const std::string& bytes) {
  std::string error;
  if (!initialize_crypto (error)) return "";
  size_t out_len= sodium_base64_ENCODED_LEN (
    bytes.size (), sodium_base64_VARIANT_ORIGINAL);
  std::vector<char> out (out_len);
  sodium_bin2base64 (out.data (), out.size (),
                     reinterpret_cast<const unsigned char*> (bytes.data ()),
                     bytes.size (), sodium_base64_VARIANT_ORIGINAL);
  return std::string (out.data ());
}

bool
base64_decode (const std::string& text, std::string& bytes,
               std::string& error) {
  if (!initialize_crypto (error)) return false;
  std::vector<unsigned char> out (text.size ());
  size_t out_len= 0;
  if (sodium_base642bin (out.data (), out.size (), text.c_str (),
                         text.size (), nullptr, &out_len, nullptr,
                         sodium_base64_VARIANT_ORIGINAL) != 0) {
    error= "invalid base64";
    return false;
  }
  bytes.assign (reinterpret_cast<const char*> (out.data ()), out_len);
  return true;
}

std::string
hex_encode (const std::string& bytes) {
  std::ostringstream ss;
  ss << std::hex << std::setfill ('0');
  for (unsigned char c: bytes)
    ss << std::setw (2) << int (c);
  return ss.str ();
}

std::string
fingerprint_for_public_key (const std::string& public_key) {
  std::string error;
  if (!initialize_crypto (error)) return "";
  std::array<unsigned char, crypto_hash_sha256_BYTES> digest {};
  crypto_hash_sha256 (digest.data (),
                      reinterpret_cast<const unsigned char*> (
                        public_key.data ()),
                      public_key.size ());
  return hex_encode (std::string (
    reinterpret_cast<const char*> (digest.data ()), digest.size ()));
}

bool
generate_keypair (KeyPair& keypair, std::string& error) {
  if (!initialize_crypto (error)) return false;
  std::array<unsigned char, crypto_box_PUBLICKEYBYTES> pk {};
  std::array<unsigned char, crypto_box_SECRETKEYBYTES> sk {};
  crypto_box_keypair (pk.data (), sk.data ());
  keypair.public_key.assign (reinterpret_cast<const char*> (pk.data ()),
                             pk.size ());
  keypair.private_key.assign (reinterpret_cast<const char*> (sk.data ()),
                              sk.size ());
  return true;
}

bool
load_keypair (const fs::path& dir, const std::string& prefix,
              KeyPair& keypair, std::string& error) {
  std::string pub64, priv64;
  if (!read_text_file (dir / (prefix + "-public.key"), pub64, error))
    return false;
  if (!read_text_file (dir / (prefix + "-private.key"), priv64, error))
    return false;
  if (!base64_decode (pub64, keypair.public_key, error)) return false;
  if (!base64_decode (priv64, keypair.private_key, error)) return false;
  return check_key_size (keypair.public_key, crypto_box_PUBLICKEYBYTES,
                         "public key", error) &&
         check_key_size (keypair.private_key, crypto_box_SECRETKEYBYTES,
                         "private key", error);
}

bool
save_keypair (const fs::path& dir, const std::string& prefix,
              const KeyPair& keypair, std::string& error) {
  if (!check_key_size (keypair.public_key, crypto_box_PUBLICKEYBYTES,
                       "public key", error) ||
      !check_key_size (keypair.private_key, crypto_box_SECRETKEYBYTES,
                       "private key", error))
    return false;
  if (!write_text_file (dir / (prefix + "-public.key"),
                        base64_encode (keypair.public_key), false, error))
    return false;
  if (!write_text_file (dir / (prefix + "-private.key"),
                        base64_encode (keypair.private_key), true, error))
    return false;
  return true;
}

bool
ensure_keypair (const fs::path& dir, const std::string& prefix,
                KeyPair& keypair, bool* generated, std::string& error) {
  if (generated != nullptr) *generated= false;
  if (load_keypair (dir, prefix, keypair, error)) return true;
  if (fs::exists (dir / (prefix + "-public.key")) ||
      fs::exists (dir / (prefix + "-private.key")))
    return false;
  error.clear ();
  if (!generate_keypair (keypair, error)) return false;
  if (!save_keypair (dir, prefix, keypair, error)) return false;
  if (generated != nullptr) *generated= true;
  return true;
}

bool
encrypt_payload (const KeyPair& sender,
                 const std::string& recipient_public_key,
                 const std::string& plaintext,
                 std::string& nonce_b64,
                 std::string& ciphertext_b64,
                 std::string& error) {
  if (!initialize_crypto (error)) return false;
  if (!check_key_size (sender.private_key, crypto_box_SECRETKEYBYTES,
                       "sender private key", error) ||
      !check_key_size (recipient_public_key, crypto_box_PUBLICKEYBYTES,
                       "recipient public key", error))
    return false;

  std::array<unsigned char, crypto_box_NONCEBYTES> nonce {};
  randombytes_buf (nonce.data (), nonce.size ());
  std::string cipher;
  cipher.resize (plaintext.size () + crypto_box_MACBYTES);
  if (crypto_box_easy (
        reinterpret_cast<unsigned char*> (cipher.data ()),
        reinterpret_cast<const unsigned char*> (plaintext.data ()),
        plaintext.size (),
        nonce.data (),
        reinterpret_cast<const unsigned char*> (recipient_public_key.data ()),
        reinterpret_cast<const unsigned char*> (sender.private_key.data ())) !=
      0) {
    error= "encryption failed";
    return false;
  }
  nonce_b64= base64_encode (std::string (
    reinterpret_cast<const char*> (nonce.data ()), nonce.size ()));
  ciphertext_b64= base64_encode (cipher);
  return true;
}

bool
decrypt_payload (const KeyPair& recipient,
                 const std::string& sender_public_key,
                 const std::string& nonce_b64,
                 const std::string& ciphertext_b64,
                 std::string& plaintext,
                 std::string& error) {
  if (!initialize_crypto (error)) return false;
  if (!check_key_size (recipient.private_key, crypto_box_SECRETKEYBYTES,
                       "recipient private key", error) ||
      !check_key_size (sender_public_key, crypto_box_PUBLICKEYBYTES,
                       "sender public key", error))
    return false;
  std::string nonce, cipher;
  if (!base64_decode (nonce_b64, nonce, error)) return false;
  if (!base64_decode (ciphertext_b64, cipher, error)) return false;
  if (!check_key_size (nonce, crypto_box_NONCEBYTES, "nonce", error))
    return false;
  if (cipher.size () < crypto_box_MACBYTES) {
    error= "ciphertext is too short";
    return false;
  }
  plaintext.resize (cipher.size () - crypto_box_MACBYTES);
  if (crypto_box_open_easy (
        reinterpret_cast<unsigned char*> (plaintext.data ()),
        reinterpret_cast<const unsigned char*> (cipher.data ()),
        cipher.size (),
        reinterpret_cast<const unsigned char*> (nonce.data ()),
        reinterpret_cast<const unsigned char*> (sender_public_key.data ()),
        reinterpret_cast<const unsigned char*> (recipient.private_key.data ())) !=
      0) {
    error= "decryption failed";
    return false;
  }
  return true;
}

std::string
random_hex_id (int bytes) {
  std::string error;
  if (!initialize_crypto (error) || bytes <= 0) return "";
  std::string data;
  data.resize (size_t (bytes));
  randombytes_buf (data.data (), data.size ());
  return hex_encode (data);
}

} // namespace athena::rag::delegation
