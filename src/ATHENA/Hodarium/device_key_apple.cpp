/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#include "device_identity.hpp"
#include "device_key_internal.hpp"
#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>
#include <cstring>

namespace athena::hodarium::detail {
namespace {
template<typename T> struct cf_owned {
  T value;
  explicit cf_owned (T p): value (p) {
    if (!value) throw std::bad_alloc ();
  }
  ~cf_owned () { CFRelease (value); }
  cf_owned (const cf_owned&)= delete;
  cf_owned& operator= (const cf_owned&)= delete;
};
void check (OSStatus status) {
  if (status == errSecSuccess) return;
  if (status == errSecItemNotFound) throw key_store_error (key_store_failure::missing,
    "Protected Hodarium secret is missing; explicit recovery is required");
  if (status == errSecInteractionNotAllowed || status == errSecAuthFailed)
    throw key_store_error (key_store_failure::locked,
      "Unlock this device to use the Hodarium Keychain identity");
  throw key_store_error (key_store_failure::unavailable,
    "Hodarium cannot access its Keychain identity");
}
struct query {
  cf_owned<CFMutableDictionaryRef> values {CFDictionaryCreateMutable (
    nullptr, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks)};
  cf_owned<CFStringRef> account;
  query (const std::string& handle, secret_kind kind): account (CFStringCreateWithBytes (
    nullptr, reinterpret_cast<const UInt8*> (handle.data ()), handle.size (),
    kCFStringEncodingUTF8, false)) {
    set (kSecClass, kSecClassGenericPassword);
    set (kSecAttrService, kind == secret_kind::device ? CFSTR ("org.athena.Hodarium.Device") : CFSTR ("org.athena.Hodarium.Vault"));
    set (kSecAttrAccount, account.value);
    set (kSecAttrSynchronizable, kCFBooleanFalse);
  }
  void set (const void* key, const void* value) {
    CFDictionarySetValue (values.value, key, value);
  }
};
}
void store_protected_seed (const std::string& handle, const unsigned char* seed, secret_kind kind) {
  query q (handle, kind);
  cf_owned<CFDataRef> data (CFDataCreate (nullptr, seed, 32));
  q.set (kSecValueData, data.value);
  q.set (kSecAttrAccessible, kSecAttrAccessibleWhenUnlockedThisDeviceOnly);
  q.set (kSecAttrLabel, kind == secret_kind::device ? CFSTR ("ATHENA Hodarium device identity") : CFSTR ("ATHENA Hodarium Vault secret"));
  check (SecItemAdd (q.values.value, nullptr));
}
void load_protected_seed (const std::string& handle, unsigned char* seed, secret_kind kind) {
  query q (handle, kind);
  q.set (kSecReturnData, kCFBooleanTrue);
  q.set (kSecMatchLimit, kSecMatchLimitOne);
  q.set (kSecUseAuthenticationUI, kSecUseAuthenticationUIFail);
  CFTypeRef result= nullptr;
  check (SecItemCopyMatching (q.values.value, &result));
  cf_owned<CFTypeRef> data (result);
  if (CFGetTypeID (result) != CFDataGetTypeID () ||
      CFDataGetLength (static_cast<CFDataRef> (result)) != 32)
    throw key_store_error (key_store_failure::corrupt, "Invalid protected Hodarium identity");
  std::memcpy (seed, CFDataGetBytePtr (static_cast<CFDataRef> (result)), 32);
}
} // namespace athena::hodarium::detail

namespace athena::hodarium {
void cancel_device_key_operations () {
  // SecItemCopyMatching with authentication UI disabled has no cancellable
  // prompt. The owner discards completion when its lifecycle generation changes.
}
} // namespace athena::hodarium
