/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#pragma once
#include <stdexcept>
#include <string>

namespace athena::hodarium {
enum class key_store_failure { unavailable, locked, missing, corrupt };
class key_store_error: public std::runtime_error {
public:
  const key_store_failure reason;
  key_store_error (key_store_failure why, const char* message):
    std::runtime_error (message), reason (why) {}
};
struct device_identity {
  std::string handle;
  std::string public_key;
};
// Blocking platform calls: run on the identity worker, never the GUI/actor.
// Missing/locked storage never creates a replacement for an enrolled identity.
device_identity create_device_identity ();
std::string device_public_key (const std::string& handle);
std::string sign_device_message (const device_identity& device,
                                 const std::string& message);
std::string sign_device_proof (const device_identity& device,
  const std::string& group, const std::string& purpose,
  const std::string& subject, const std::string& nonce);
// May be called from the application thread before queuing worker suspension.
// Cancels active blocking Secret Service calls; does not delete stored keys.
void cancel_device_key_operations ();
} // namespace athena::hodarium
