// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#ifndef ATHENA_VAULT_NAVIGATION_HPP
#define ATHENA_VAULT_NAVIGATION_HPP
#include "scheme.hpp"

void native_vault_jump_to_source (url source, string anchor);
void native_node_reference_jump_to_source (url source, string identity);
void native_artifact_jump_to_position (url source, path position, object history);
void native_artifact_navigation_failed (string source);

#endif
