// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#pragma once

#include "scheme.hpp"

using native_dispatch_entry= object (*) (const array<object>&);

// Populated by generated XML interfaces during single-threaded startup.
void native_dispatch_register (const char* name, native_dispatch_entry entry);
bool native_dispatch_call (string name, const array<object>& arguments, object& result);
