/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#pragma once
#include <string>

namespace athena::hodarium {
// Linux headless backend only: link device_key_file.cpp instead of a platform
// keychain backend, never as a fallback. Call before using any protected seed.
// The directory must already exist, be owned by the effective uid, and have
// mode 0700. Supply an absolute path with no symlinks or dot components.
// Provision it outside notes/synced vaults, on a local filesystem, for a
// dedicated service uid. Files contain plaintext 32-byte secrets (mode 0600).
// No environment lookup, automatic directory creation, or permission repair.
// Configuration pins the directory inode for the process lifetime. Repeating
// the same inode is allowed; switching directories is rejected.
void configure_private_key_directory (const std::string& path);
} // namespace athena::hodarium
