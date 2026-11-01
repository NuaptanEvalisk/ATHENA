// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#include "vault_navigation.hpp"
#include "ATHENA/Data/application_lifecycle.hpp"
#include "ATHENA/Data/compound_edit_batch.hpp"
#include "buffer_name_catalog.hpp"
#include "editor.hpp"
#include "native_interfaces.hpp"
#include "scheme_execution_context.hpp"
#include "server.hpp"
#include <functional>
#include <string>
#include <vector>

namespace {
void on_source (url source, std::function<void(editor_rep&)> action) {
  application_browse_document (source, [action] (url loaded) {
    const string name= as_string (loaded);
    const auto owner= published_buffer_source (std::string (name.data (), N(name)));
    athena::avd::submit_source_task (owner.first, owner.second, action,
      [] (std::string error) {
        if (!error.empty ()) std_warning << "Vault navigation: " << string (error.c_str ()) << LF;
      });
  });
}
}

void native_vault_jump_to_source (url source, string anchor) {
  const std::string text (anchor.data (), N(anchor));
  on_source (source, [text] (editor_rep& ed) {
    if (!text.empty ()) ed.go_to_label (string (text.c_str ()));
  });
}

void native_node_reference_jump_to_source (url source, string identity) {
  const std::string id (identity.data (), N(identity));
  on_source (source, [id] (editor_rep&) {
    athena_node_reference_position (string (id.c_str ()));
  });
}

void native_artifact_jump_to_position (url source, path position, object history) {
  std::vector<int> indices;
  for (; !is_nil (position); position= position->next) indices.push_back (position->item);
  const bool record_history= !is_null (history);
  on_source (source, [indices, record_history] (editor_rep& ed) {
    path relative;
    for (auto it= indices.rbegin (); it != indices.rend (); ++it) relative= path (*it, relative);
    auto locate= [&] {
      const path absolute= ed.the_buffer_path () * relative;
      if (!has_subtree (ed.the_root (), absolute)) return false;
      ed.go_to (ed.command_tree_start (ed.the_root (), absolute));
      return true;
    };
    if (locate ()) {
      ed.show_cursor_if_hidden ();
      // Expanding a folded ancestor may move the cursor to its variant entry.
      locate ();
    }
    if (record_history) call ("cursor-history-add", object (ed.the_path ()));
  });
}

void native_artifact_navigation_failed (string source) {
  const string message= "Artifact definition changed in " * source *
    "; rebuild artifacts for this document";
  const auto* owner= current_scheme_execution_context ();
  if (owner && owner->editor) owner->editor->set_message (message, "Artifact");
  else get_server ()->set_message (message, "Artifact");
}
