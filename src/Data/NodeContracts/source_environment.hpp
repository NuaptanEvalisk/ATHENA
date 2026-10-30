// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#pragma once

#include "editing_query.hpp"

class evaluation_environment_checkpoint;

namespace athena::contracts {

// Owner-local source entries: values and contracts come from the same execution.
using source_environment_preparation= std::function<
  std::shared_ptr<const evaluation_environment_checkpoint> (path)>;
source_contract_preparation source_contracts (source_environment_preparation);

} // namespace athena::contracts
