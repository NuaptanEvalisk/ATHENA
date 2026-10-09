/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#pragma once
#include <string>

namespace athena::hodarium::detail {
// Private interface: exactly 32 seed bytes, never a string or persistent file.
enum class secret_kind { device, vault, relay };
void store_protected_seed (const std::string& handle, const unsigned char* seed, secret_kind kind);
void load_protected_seed (const std::string& handle, unsigned char* seed, secret_kind kind);
void delete_protected_seed (const std::string& handle, secret_kind kind);
} // namespace athena::hodarium::detail
