/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#pragma once
#include <string>

namespace athena::hodarium::detail {
// Private interface: exactly 32 seed bytes, never a string or persistent file.
void store_device_seed (const std::string& handle, const unsigned char* seed);
void load_device_seed (const std::string& handle, unsigned char* seed);
} // namespace athena::hodarium::detail
