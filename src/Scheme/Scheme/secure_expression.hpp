// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#pragma once
#include "scheme.hpp"
bool native_secure_expression (object expression);
bool native_secure_function (string name);
object native_secure_eval (object expression);
void native_secure_initialize ();
