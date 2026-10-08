/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#include "device_identity.hpp"
#include <libsecret/secret.h>
#include <sodium.h>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

using namespace athena::hodarium;
void require (bool value, const char* message) {
  if (!value) throw std::runtime_error (message);
}
int main () {
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
    std::cout << "Protected identity roundtrip, signature, mismatch and lock checks passed\n";
  }
  catch (const std::exception& e) { std::cerr << e.what () << '\n'; return 1; }
}
