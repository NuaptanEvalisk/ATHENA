// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#pragma once

#include <memory>
#include <string>

namespace athena::contracts {

// Identity of an executed boundary. Admitted layout reuse keeps this token.
struct evaluation_boundary {};
using evaluation_boundary_handle= std::shared_ptr<const evaluation_boundary>;

// One actual package inclusion. Repeated loads have distinct identities;
// cached effects share the original immutable inclusion and its ancestry.
struct package_inclusion {
  std::string source;
  std::shared_ptr<const package_inclusion> parent;
};
using package_inclusion_handle= std::shared_ptr<const package_inclusion>;

} // namespace athena::contracts
