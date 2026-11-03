/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#pragma once
#include <QUrl>
#include <string>

namespace athena::hodarium {
std::string canonical_relay_origin (QUrl origin);
// Blocking protected-store operations: identity/network worker only. Persist
// only origin + returned handle in settings. Tokens are exactly 32 bytes,
// transported as canonical unpadded base64url. Missing credentials never rotate.
// cancel_device_key_operations cancels Linux calls; lifecycle owners discard
// stale completions on both platforms. No authentication prompts are permitted.
std::string store_relay_token (const QUrl& origin, const std::string& token);
std::string load_relay_token (const QUrl& origin, const std::string& handle);
void delete_relay_token (const QUrl& origin, const std::string& handle);
} // namespace athena::hodarium
