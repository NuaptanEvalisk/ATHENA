// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#pragma once
#include "global_transformation.hpp"
#include "vault.hpp"
#include "../Interop/resolution.hpp"
void run_global_transformation ();
namespace athena::interop {
operation_result propose_global_transformation (
  const vault_context_handle&, const value& parameters);
operation_result global_transformation_status (
  const vault_context_handle&, const std::string& id);
}
