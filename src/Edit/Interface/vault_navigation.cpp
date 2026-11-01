// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#include "vault_navigation.hpp"
#include "ATHENA/Data/application_lifecycle.hpp"
#include "ATHENA/Data/compound_edit_batch.hpp"
#include "buffer_name_catalog.hpp"
#include "editor.hpp"
#include "native_interfaces.hpp"
#include "scheme_execution_context.hpp"
#include "server.hpp"
#include "ATHENA/Data/artifact_document.hpp"
#include "QTMArtifactsPane.hpp"
#include "qt_utilities.hpp"
#include "ATHENA/Data/vault.hpp"
#include "file.hpp"
#include <QUrl>
#include <functional>
#include <string>
#include <vector>

namespace {
void navigation_message (string text, string title) {
  const auto* owner= current_scheme_execution_context ();
  if (owner && owner->editor) owner->editor->set_message (text, title);
  else get_server ()->set_message (text, title);
}

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
  navigation_message (message, "Artifact");
}

bool native_vault_navigate_url (url target, bool history) {
  if (is_rooted_tmfs (target, "transclude")) {
    athena_node_reference_open (as_string (target, URL_SYSTEM));
    return true;
  }
  const bool wiki= is_rooted_tmfs (target, "wikilink") || is_rooted_tmfs (target, "Wikilink");
  const bool source_link= is_rooted_tmfs (target, "transclusion-source");
  if ((wiki && vault_get_node_model_version () >= 1) || source_link) {
    const string address= as_string (target, URL_SYSTEM);
    const QUrl parsed (QString::fromUtf8 (address.data (), N(address)));
    const QString encoded= parsed.path (QUrl::FullyEncoded);
    const QString component= source_link ? encoded.mid (1) : encoded.section ('/', 1, 1);
    const QByteArray bytes= QUrl::fromPercentEncoding (component.toUtf8 ()).toUtf8 ();
    const std::string id (bytes.constData (), bytes.size ());
    if (wiki) {
      if (id.empty ()) navigation_message ("Wikilink has no source UUID", "Wikilink");
      else athena_node_reference_open ("tmfs://wikilink/" * string (id.c_str ()));
    }
    else qt_post_to_main_thread ([id] {
      tree node= vault_get_node (string (id.c_str ()));
      if (!is_func (node, TUPLE)) {
        navigation_message ("Transclusion source is not in the Vault map", "Transclusion");
        return;
      }
      const url source= vault_get_root () * url_unix (node[0]->label);
      if (exists (source)) native_vault_jump_to_source (source, node[1]->label);
      else navigation_message ("Transclusion source file is missing", "Transclusion");
    });
    return true;
  }
  if (!is_rooted_tmfs (target, "artifact")) return false;
  const string uuid= as_string (tail (target), URL_SYSTEM);
  const std::string id (uuid.data (), N(uuid));
  qt_post_to_main_thread ([id, history] {
    url source;
    path position;
    if (artifacts_resolve_uuid (string (id.c_str ()), source, position))
      native_artifact_jump_to_position (source, position,
        history ? list_object (object (true)) : null_object ());
    else get_server ()->set_message (
      "Artifact not found or its definition has changed; rebuild artifacts", "Artifact");
  });
  return true;
}

void native_resolve_selection_as_artifact_name () {
  editor ed= get_current_editor ();
  if (!ed->selection_active_any ()) return;
  const string target= athena_artifact_name_query_url (ed->selection_get ());
  if (target == "") {
    ed->set_message ("Select an artifact name of at most 8192 bytes", "Artifact");
    return;
  }
  call ("cursor-history-add", object (ed->the_path ()));
  application_browse_document (url_system (target), null_object ());
}
