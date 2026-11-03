/******************************************************************************
* MODULE     : rag_delegation_crypto.hpp
* DESCRIPTION: Key and envelope helpers for ATHENA RAG delegation
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef RAG_DELEGATION_CRYPTO_HPP
#define RAG_DELEGATION_CRYPTO_HPP

#include <filesystem>
#include <string>

namespace athena::rag::delegation {

struct KeyPair {
  std::string public_key;
  std::string private_key;
};

bool initialize_crypto (std::string& error);

std::string base64_encode (const std::string& bytes);
bool base64_decode (const std::string& text, std::string& bytes,
                    std::string& error);

std::string hex_encode (const std::string& bytes);
std::string fingerprint_for_public_key (const std::string& public_key);

bool generate_keypair (KeyPair& keypair, std::string& error);
bool load_keypair (const std::filesystem::path& dir,
                   const std::string& prefix, KeyPair& keypair,
                   std::string& error);
bool save_keypair (const std::filesystem::path& dir,
                   const std::string& prefix, const KeyPair& keypair,
                   std::string& error);
bool ensure_keypair (const std::filesystem::path& dir,
                     const std::string& prefix, KeyPair& keypair,
                     bool* generated, std::string& error);

bool encrypt_payload (const KeyPair& sender,
                      const std::string& recipient_public_key,
                      const std::string& plaintext,
                      std::string& nonce_b64,
                      std::string& ciphertext_b64,
                      std::string& error);

bool decrypt_payload (const KeyPair& recipient,
                      const std::string& sender_public_key,
                      const std::string& nonce_b64,
                      const std::string& ciphertext_b64,
                      std::string& plaintext,
                      std::string& error);

std::string random_hex_id (int bytes);

} // namespace athena::rag::delegation

#endif // RAG_DELEGATION_CRYPTO_HPP
