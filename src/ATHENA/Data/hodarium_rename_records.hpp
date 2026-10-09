/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#pragma once
#include "hodarium_application.hpp"

namespace athena::hodarium {
// Every invocation opens its own database connections. The returned callbacks
// contain only the canonical root, never thread-affine database or GUI objects.
// Local plans live under .athena and are not synchronization objects.
document_rename_records make_document_rename_records (
  const std::filesystem::path& root);
}
