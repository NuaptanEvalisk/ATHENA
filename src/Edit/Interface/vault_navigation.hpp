// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#ifndef ATHENA_VAULT_NAVIGATION_HPP
#define ATHENA_VAULT_NAVIGATION_HPP
#include "tree.hpp"
#include "path.hpp"
#include "url.hpp"

void native_vault_jump_to_source (url source, string anchor);
void native_node_reference_jump_to_source (url source, string identity);
void native_artifact_jump_to_position (url source, path position, bool record_history= false);
void native_artifact_navigation_failed (string source);
bool native_vault_navigate_url (url target, bool history);
void native_resolve_selection_as_artifact_name ();
void native_wikilink_repair_apply (string bad_uuid, string new_uuid,
                                  string source, string begin, string end);
void native_vault_transclude_repair (string uuid, string file_hint,
                                    string begin_hint, string end_hint);
tree native_wikilink_repair_page (string name);
tree native_artifact_open_page (string uuid);

#endif
