// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#include "cursor_history.hpp"
#include "boot.hpp"
#include "vault_navigation.hpp"
#include "ATHENA/Data/application_lifecycle.hpp"
#include "ATHENA/Data/compound_edit_batch.hpp"
#include "buffer_name_catalog.hpp"
#include "editor.hpp"
#include "native_interfaces.hpp"
#include "execution_context.hpp"
#include "server.hpp"
#include "ATHENA/Data/artifact_document.hpp"
#include "QTMArtifactsPane.hpp"
#include "QTMVaultChooser.hpp"
#include "tree_traverse.hpp"
#include "qt_utilities.hpp"
#include "ATHENA/Data/vault.hpp"
#include "file.hpp"
#include "gui.hpp"
#include <QUrl>
#include <functional>
#include <string>
#include <vector>
#include <memory>

namespace {
void navigation_message (string text, string title) {
  const auto* owner= current_execution_context ();
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

struct link_repair: std::enable_shared_from_this<link_repair> {
  bool transclusion= false;
  std::string old_id, new_id, source, begin, end;
  std::vector<std::pair<std::uint64_t, std::uint64_t>> owners;
  size_t next= 0;
  bool changed= false;

  bool replace (tree node) const {
    if (is_atomic (node)) return false;
    if (transclusion && is_compound (node, "transclude") && N(node) >= 1 &&
        is_atomic (node[0]) && node[0]->label == string (old_id.c_str ())) {
      tree updated (as_tree_label ("transclude"));
      updated << string (old_id.c_str ()) << "" << string (begin.c_str ()) << string (end.c_str ());
      tree_assign (node, updated);
      return true;
    }
    bool found= false;
    if (!transclusion && is_func (node, HLINK) && N(node) >= 2 && is_atomic (node[1])) {
      const string text= node[1]->label;
      const string lower= "tmfs://wikilink/" * string (old_id.c_str ());
      const string upper= "tmfs://Wikilink/" * string (old_id.c_str ());
      if (starts (text, lower) || starts (text, upper)) {
        tree_assign (node[1], "tmfs://wikilink/" * string (new_id.c_str ()) *
                             text (N(lower), N(text)));
        found= true;
      }
    }
    for (int i= 0; i < N(node); ++i) found= replace (node[i]) || found;
    return found;
  }

  // Only immutable strings and stable owner ids cross threads. The next task
  // starts on the GUI after the preceding owner has finished its mutations.
  void advance () {
    if (next == owners.size ()) {
      navigation_message (transclusion ? (changed ? "Transclusion repaired" :
        "Transclusion target repaired, but no open source node matched") : "Link repaired", "Vault");
      if (!transclusion)
        native_vault_jump_to_source (url_system (string (source.c_str ())), string (end.c_str ()));
      return;
    }
    const auto owner= owners[next++];
    auto self= shared_from_this ();
    athena::avd::submit_source_task (owner.first, owner.second,
      [self] (editor_rep& ed) { self->changed= self->replace (ed.the_buffer ()) || self->changed; },
      [self] (std::string error) {
        qt_post_to_main_thread ([self, error] {
          if (!error.empty ()) {
            navigation_message ("Link repair stopped: " * string (error.c_str ()), "Vault");
            return;
          }
          self->advance ();
        });
      });
  }

  void start (const std::string& relative) {
    if (!vault_active ()) {
      navigation_message ("No active vault. Please load a vault first.", "Error");
      return;
    }
    vault_set_node (string (new_id.c_str ()), string (relative.c_str ()),
                    string (begin.c_str ()), string (end.c_str ()));
    const string file= as_string (vault_get_root () * url_unix (string (relative.c_str ())));
    source.assign (file.data (), N(file));
    for (const auto& entry: published_buffer_metadata ())
      owners.emplace_back (entry.second.actor_id, entry.second.source_view);
    advance ();
  }
};
}

void native_wikilink_repair_apply (string bad_uuid, string new_uuid,
                                  string source, string begin, string end) {
  auto repair= std::make_shared<link_repair> ();
  repair->old_id.assign (bad_uuid.data (), N(bad_uuid));
  repair->new_id.assign (new_uuid.data (), N(new_uuid));
  repair->begin.assign (begin.data (), N(begin));
  repair->end.assign (end.data (), N(end));
  const std::string relative (source.data (), N(source));
  qt_post_to_main_thread ([repair, relative] { repair->start (relative); });
}

void native_vault_transclude_repair (string uuid, string, string, string) {
  if (!vault_active ()) {
    navigation_message ("No active vault. Please load a vault first.", "Error");
    return;
  }
  const std::string id (uuid.data (), N(uuid));
  vault_choose_link_native (true, [id] (tree result) {
    if (!is_func (result, TUPLE, 4) || vault_get_node_model_version () >= 1) return;
    auto repair= std::make_shared<link_repair> ();
    repair->transclusion= true;
    repair->old_id= repair->new_id= id;
    repair->begin.assign (result[1]->label.data (), N(result[1]->label));
    repair->end.assign (result[2]->label.data (), N(result[2]->label));
    const std::string relative (result[0]->label.data (), N(result[0]->label));
    qt_post_to_main_thread ([repair, relative] { repair->start (relative); });
  });
}

tree native_wikilink_repair_page (string name) {
  const QString encoded= QString::fromUtf8 (name.data (), N(name));
  auto decode= [&] (QString component) {
    const QByteArray bytes= QUrl::fromPercentEncoding (component.toUtf8 ()).toUtf8 ();
    return string (bytes.constData (), bytes.size ());
  };
  const string uuid= decode (encoded.section ('/', 0, 0));
  const string file_hint= decode (encoded.section ('/', 1, 1));
  const string anchor_hint= decode (encoded.section ('/', 2));
  auto document= [] (tree body) {
    return tree (DOCUMENT, compound ("style", tree (TUPLE, "generic")), compound ("body", body));
  };
  if (vault_get_node_model_version () >= 1)
    return document (tree (DOCUMENT, compound ("strong", "Broken source UUID: "), uuid,
      " -- file and anchor hints are not identity in a migrated Vault."));
  if (file_hint == "")
    return document (tree (DOCUMENT, compound ("strong", "Error: "),
      "Broken Wikilink and no file hint provided."));

  bool valid_limit= false;
  const string preference= get_preference ("vault fuzzy search limit", "3");
  const int configured= QString::fromUtf8 (preference.data (), N(preference)).toInt (&valid_limit);
  const int limit= valid_limit ? configured : 3;
  auto matches= [] (string value, string hint) {
    return QString::fromUtf8 (value.data (), N(value)).contains (
      QString::fromUtf8 (hint.data (), N(hint)), Qt::CaseInsensitive);
  };
  auto encode= [] (string value) {
    const QByteArray bytes= QUrl::toPercentEncoding (QString::fromUtf8 (value.data (), N(value)));
    return string (bytes.constData (), bytes.size ());
  };
  tree candidates (DOCUMENT);
  const array<url> files= vault_get_all_files ();
  for (int i= 0; i < N(files) && N(candidates) < limit && !gui_interrupted (); ++i) {
    if (!matches (as_string (tail (files[i])), file_hint)) continue;
    string anchor;
    if (anchor_hint != "") {
      const array<string> labels= vault_get_anchors (files[i]);
      for (int j= 0; j < N(labels); ++j)
        if (matches (labels[j], anchor_hint)) { anchor= labels[j]; break; }
    }
    const string relative= as_string (delta (vault_get_root () * url (""), files[i]), URL_UNIX);
    string target_id= vault_find_uuid (relative, "", anchor);
    if (target_id == "") target_id= vault_generate_uuid ();
    const string target= "tmfs://wikilink-repair-apply/" * encode (uuid) * "/" *
      encode (target_id) * "/" * encode (relative) * "/" * encode (anchor);
    candidates << tree (CONCAT, compound ("item"), tree (HLINK, relative * " #" * anchor, target),
                        " (UUID: " * target_id * ")");
  }
  if (N(candidates) == 0)
    return document (tree (DOCUMENT, compound ("strong", "Error: "),
      "Could not find any matches for: ", file_hint));
  return document (tree (DOCUMENT, compound ("section", "Repair Wikilink"),
    "The link target could not be resolved. Select a candidate to repair:",
    compound ("enumerate", candidates)));
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

void native_artifact_jump_to_position (url source, path position, bool record_history) {
  std::vector<int> indices;
  for (; !is_nil (position); position= position->next) indices.push_back (position->item);
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
    if (record_history) cursor_history_add (ed.the_path ());
  });
}

tree native_artifact_open_page (string uuid) {
  const tree body= artifacts_open_uuid (uuid) ?
    tree (DOCUMENT, "Opening artifact...") :
    tree (DOCUMENT, compound ("strong", "Artifact not found: "), uuid);
  return tree (DOCUMENT, compound ("style", tree (TUPLE, "generic")), compound ("body", body));
}

void native_artifact_navigation_failed (string source) {
  const string message= "Artifact definition changed in " * source *
    "; rebuild artifacts for this document";
  navigation_message (message, "Artifact");
}

bool native_vault_navigate_url (url target, bool history) {
  if (is_rooted_tmfs (target, "wikilink-repair-apply")) {
    const string address= as_string (target, URL_SYSTEM);
    const QUrl parsed (QString::fromUtf8 (address.data (), N(address)));
    const auto parts= parsed.path (QUrl::FullyEncoded).mid (1).split ('/');
    if (parts.size () != 4) {
      navigation_message ("Invalid wikilink repair target", "Vault");
      return true;
    }
    auto decode= [&] (int index) {
      const QByteArray bytes= QUrl::fromPercentEncoding (parts[index].toUtf8 ()).toUtf8 ();
      return string (bytes.constData (), bytes.size ());
    };
    native_wikilink_repair_apply (decode (0), decode (1), decode (2), "", decode (3));
    return true;
  }
  if (is_rooted_tmfs (target, "transclude")) {
    athena_node_reference_open (as_string (target, URL_SYSTEM));
    return true;
  }
  const bool wiki= is_rooted_tmfs (target, "wikilink") || is_rooted_tmfs (target, "Wikilink");
  const bool source_link= is_rooted_tmfs (target, "transclusion-source");
  if (wiki || source_link) {
    const string address= as_string (target, URL_SYSTEM);
    const QUrl parsed (QString::fromUtf8 (address.data (), N(address)));
    const QString encoded= parsed.path (QUrl::FullyEncoded);
    const QString component= source_link ? encoded.mid (1) : encoded.section ('/', 1, 1);
    const QByteArray bytes= QUrl::fromPercentEncoding (component.toUtf8 ()).toUtf8 ();
    const std::string id (bytes.constData (), bytes.size ());
    if (wiki && vault_get_node_model_version () >= 1) {
      if (id.empty ()) navigation_message ("Wikilink has no source UUID", "Wikilink");
      else athena_node_reference_open ("tmfs://wikilink/" * string (id.c_str ()));
    }
    else {
      const QByteArray encoded_name= encoded.mid (1).toUtf8 ();
      const std::string name (encoded_name.constData (), encoded_name.size ());
      qt_post_to_main_thread ([id, name, wiki] {
        tree node= vault_get_node (string (id.c_str ()));
        if (wiki) {
          if (is_func (node, TUPLE, 3)) {
            const url source= vault_get_root () * url_unix (node[0]->label);
            if (exists (source)) {
              native_vault_jump_to_source (source, node[2]->label);
              return;
            }
          }
          application_browse_document (
            url_system ("tmfs://wikilink-repair/" * string (name.c_str ())));
          return;
        }
        if (!is_func (node, TUPLE)) {
          navigation_message ("Transclusion source is not in the Vault map", "Transclusion");
          return;
        }
        const url source= vault_get_root () * url_unix (node[0]->label);
        if (exists (source)) native_vault_jump_to_source (source, node[1]->label);
        else navigation_message ("Transclusion source file is missing", "Transclusion");
      });
    }
    return true;
  }
  if (!is_rooted_tmfs (target, "artifact")) return false;
  const string uuid= as_string (tail (target), URL_SYSTEM);
  const std::string id (uuid.data (), N(uuid));
  qt_post_to_main_thread ([id, history] {
    url source;
    path position;
    if (artifacts_resolve_uuid (string (id.c_str ()), source, position))
      native_artifact_jump_to_position (source, position, history);
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
  cursor_history_add (ed->the_path ());
  application_browse_document (url_system (target));
}
