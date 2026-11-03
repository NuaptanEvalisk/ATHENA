// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#pragma once

#include "command_value.hpp"

using native_dispatch_entry= command_value (*) (const command_arguments&);

// Populated by generated XML interfaces during single-threaded startup.
void native_dispatch_register (const char* name, native_dispatch_entry entry);
bool native_dispatch_call (string name, const command_arguments& arguments, command_value& result);

void initialize_native_commands ();
