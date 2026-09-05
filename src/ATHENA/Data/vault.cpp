/******************************************************************************
* MODULE     : vault.cpp
* DESCRIPTION: Vault management for Math Knowledge Workbench
* COPYRIGHT  : (C) 2026  Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "vault.hpp"
#include "file.hpp"
#include "web_files.hpp"
#include "tm_timer.hpp"
#include "analyze.hpp"
#include "convert.hpp"
#include "sys_utils.hpp"
#include "ATHENA/Data/new_buffer.hpp"
#include "ATHENA/Data/new_window.hpp"
#include "ATHENA/Data/materials.hpp"
#include "ATHENA/Data/namespace_ontology.hpp"
#include "ATHENA/Data/artifact_radioactive_links.hpp"
#include "ATHENA/Data/vault_map_sqlite.hpp"
#include "ATHENA/Data/vaultfile_json.hpp"
#include "ATHENA/Data/vault_safe_rename.hpp"
#include "ATHENA/Data/transclusion_cache.hpp"
#include "ATHENA/tm_window.hpp"

#include <filesystem>
#include <atomic>
#include <memory>
#include <string>
#include <utility>
#include <vector>

bool       is_vault_active = false;
vault_info current_vault;
static std::unique_ptr<AthenaVaultMapSqlite> current_vault_map;
static std::unique_ptr<MaterialsStore> current_materials_store;

namespace {

struct vault_public_snapshot {
  bool        active;
  std::string root;
  std::string name;
  std::string map_db;
  std::string namespace_db;
};

std::shared_ptr<const vault_public_snapshot> published_vault_snapshot=
  std::make_shared<const vault_public_snapshot> (
    vault_public_snapshot {false, "", "", "", ""});
std::atomic<const vault_public_snapshot*> published_vault_identity {
  published_vault_snapshot.get ()};

static string
fresh_tm_string (const std::string& value) {
  return string (value.data (), (int) value.size ());
}

struct vault_thread_snapshot {
  std::shared_ptr<const vault_public_snapshot> source;
  vault_info info;
};

static const vault_thread_snapshot&
vault_snapshot () {
  static thread_local vault_thread_snapshot local;
  // The identity is only a change hint, never dereferenced. Retaining source
  // prevents address reuse; shared ownership is acquired only on a change.
  if (local.source.get () !=
      published_vault_identity.load (std::memory_order_acquire)) {
    auto source= std::atomic_load_explicit (
      &published_vault_snapshot, std::memory_order_acquire);
    local.info.name= fresh_tm_string (source->name);
    local.info.root= source->active ?
      url_system (fresh_tm_string (source->root)) : url_none ();
    local.info.db_url= source->active ?
      url_system (fresh_tm_string (source->map_db)) : url_none ();
    local.info.ns_db_url= source->active ?
      url_system (fresh_tm_string (source->namespace_db)) : url_none ();
    local.source= std::move (source);
  }
  return local;
}

static void
publish_vault_snapshot (bool active, const std::filesystem::path& root,
                        const std::string& name,
                        const std::filesystem::path& map_db,
                        const std::filesystem::path& namespace_db) {
  auto next= std::make_shared<const vault_public_snapshot> (
    vault_public_snapshot {
      active, root.string (), name, map_db.string (), namespace_db.string ()});
  std::atomic_store_explicit (
    &published_vault_snapshot, next, std::memory_order_release);
  published_vault_identity.store (next.get (), std::memory_order_release);
}

} // namespace

bool
vault_active () {
  return vault_snapshot ().source->active;
}

string
vault_get_name () {
  return vault_snapshot ().info.name;
}

url
vault_get_root () {
  return vault_snapshot ().info.root;
}

url
vault_get_map_db () {
  return vault_snapshot ().info.db_url;
}

url
vault_get_namespace_db () {
  return vault_snapshot ().info.ns_db_url;
}

MaterialsStore*
vault_get_materials_store () {
  return is_vault_active ? current_materials_store.get () : nullptr;
}

static void
vault_refresh_window_titles () {
  array<url> bs= get_all_buffers ();
  for (int i=0; i<N(bs); i++) {
    tm_buffer buf= concrete_buffer (bs[i]);
    if (buf == NULL) continue;
    array<url> ws= buffer_to_windows (bs[i]);
    for (int j=0; j<N(ws); j++) {
      tm_window win= concrete_window (ws[j]);
      if (win != NULL) {
        win->set_window_name (buf->buf->title);
        win->set_window_url (buf->buf->name);
      }
    }
  }
}

static std::string
vault_std_string (string value) {
  return std::string (as_charp (value), (size_t) N(value));
}

static string
vault_tm_string (const std::string& value) {
  return string (value.data (), (int) value.size ());
}

string
vault_load (url root_dir, string name, string db_rel_path) {
  return vault_load (root_dir, name, db_rel_path, "ns.sqlite");
}

string
vault_load (url root_dir, string name, string db_rel_path,
            string ns_db_rel_path) {
  std::filesystem::path root (vault_std_string (concretize (root_dir)));
  std::string resolved;
  std::string error;
  if (!athena_vault_map_prepare (vault_std_string (db_rel_path),
                                 resolved, error))
    return vault_tm_string (error);

  std::unique_ptr<AthenaVaultMapSqlite> map (new AthenaVaultMapSqlite);
  if (!map->open (root / resolved, true, error))
    return vault_tm_string (error);
  if (!vault_safe_rename_recover (root, *map, error))
    return vault_tm_string (error);

  AthenaVaultfileInfo vaultfile;
  if (!athena_vaultfile_read (root, vaultfile, error))
    return vault_tm_string (error);
  std::unique_ptr<MaterialsStore> materials (new MaterialsStore);
  if (!materials->open (root, vaultfile, error))
    return vault_tm_string (error);

  if (is_vault_active) vault_close ();
  current_vault.root   = root_dir;
  current_vault.name   = name;
  current_vault.db_url = root_dir * url (vault_tm_string (resolved));
  current_vault.ns_db_url = root_dir * url (ns_db_rel_path);
  current_vault_map = std::move (map);
  current_materials_store = std::move (materials);
  is_vault_active = true;
  publish_vault_snapshot (
    true, root, vault_std_string (name), root / resolved,
    root / vault_std_string (ns_db_rel_path));
  athena_namespace_ontology_start (vault_get_root (),
                                   vault_get_namespace_db ());
  athena_artifact_radioactive_invalidate ();
  athena_clear_transclusion_caches ();
  vault_refresh_window_titles ();
  return "";
}

void
vault_close () {
  if (is_vault_active) {
    athena_namespace_ontology_stop ();
  }
  current_vault_map.reset ();
  current_materials_store.reset ();
  is_vault_active = false;
  athena_artifact_radioactive_invalidate ();
  current_vault.root = url_none ();
  current_vault.name = "";
  current_vault.db_url = url_none ();
  current_vault.ns_db_url = url_none ();
  publish_vault_snapshot (false, {}, "", {}, {});
  athena_clear_transclusion_caches ();
  vault_refresh_window_titles ();
}

void
vault_set_node (string uuid, string path, string anchor_begin, string anchor_end) {
  if (!is_vault_active || current_vault_map == nullptr) return;
  AthenaVaultMapNode node {vault_std_string (uuid), vault_std_string (path),
                           vault_std_string (anchor_begin),
                           vault_std_string (anchor_end)};
  std::string error;
  if (!current_vault_map->set_node (node, error))
    std_warning << "Could not update Vault map: " << error.c_str () << LF;
}

tree
vault_get_node (string uuid) {
  if (!is_vault_active || current_vault_map == nullptr) return UNINIT;
  AthenaVaultMapNode node;
  bool found = false;
  std::string error;
  if (!current_vault_map->get_node (vault_std_string (uuid), node, found,
                                    error) || !found)
    return UNINIT;
  tree res (TUPLE);
  res << tree (vault_tm_string (node.path));
  res << tree (vault_tm_string (node.anchor_begin));
  res << tree (vault_tm_string (node.anchor_end));
  return res;
}

void
vault_remove_node (string uuid) {
  if (!is_vault_active || current_vault_map == nullptr) return;
  std::string error;
  if (!current_vault_map->remove_node (vault_std_string (uuid), error))
    std_warning << "Could not remove Vault map node: " << error.c_str () << LF;
}

bool
vault_has_node (string uuid) {
  if (!is_vault_active || current_vault_map == nullptr) return false;
  bool found = false;
  std::string error;
  return current_vault_map->has_node (vault_std_string (uuid), found, error) &&
         found;
}

static void
scan_recursive (url dir, array<url>& res) {
  bool err;
  array<string> all = read_directory (dir, err);
  if (err) return;
  for (int i=0; i<N(all); i++) {
    url u = dir * url (all[i]);
    string name = all[i];
    if (name != "" && name[0] == '.') continue;
    if (is_directory (u)) {
      scan_recursive (u, res);
    } else {
      string suf = suffix (u);
      if (suf == "ath" || suf == "tm") {
        res << u;
      }
    }
  }
}

array<url>
vault_get_all_files () {
  array<url> res;
  if (!vault_active ()) return res;
  scan_recursive (vault_get_root (), res);
  return res;
}

static void
find_labels (tree t, array<string>& res) {
  if (is_atomic (t)) return;
  // Standard label tag
  if (is_func (t, LABEL, 1)) {
    res << tree_as_string (t[0]);
  }
  // Labels can also be inside assign or other tags, but find_labels is recursive.
  for (int i=0; i<N(t); i++) {
    find_labels (t[i], res);
  }
}

array<string>
vault_get_anchors (url u) {
  array<string> res;
  if (!exists (u)) return res;
  tree t = import_tree (u, "texmacs");
  find_labels (t, res);
  return res;
}

int
vault_get_mtime (url u) {
  // TeXmacs doesn't have a direct cross-platform mtime in its basic file API
  // but we can use sys_utils if needed. For now return 0 if not available.
  // Actually, let's just return 0 for now to keep it simple, or use stat if on linux.
#ifdef OS_WIN32
  return 0;
#else
  struct stat st;
  if (stat (as_charp (concretize (u)), &st) == 0) return (int) st.st_mtime;
  return 0;
#endif
}

string
vault_find_uuid (string path, string anchor_begin, string anchor_end) {
  if (!is_vault_active || current_vault_map == nullptr) return "";
  std::string uuid;
  std::string error;
  if (!current_vault_map->find_uuid (
        vault_std_string (path), vault_std_string (anchor_begin),
        vault_std_string (anchor_end), uuid, error))
    return "";
  return vault_tm_string (uuid);
}

size_t
vault_rewrite_anchor_references (string path, string encoded_renames) {
  if (!is_vault_active || current_vault_map == nullptr) return 0;
  std::string encoded = vault_std_string (encoded_renames);
  std::vector<std::pair<std::string, std::string>> renames;
  size_t begin = 0;
  while (begin <= encoded.size ()) {
    size_t end = encoded.find ((char) 30, begin);
    std::string entry = encoded.substr (
      begin, end == std::string::npos ? std::string::npos : end - begin);
    size_t separator = entry.find ((char) 31);
    if (separator != std::string::npos)
      renames.emplace_back (entry.substr (0, separator),
                            entry.substr (separator + 1));
    if (end == std::string::npos) break;
    begin = end + 1;
  }
  size_t changed = 0;
  std::string error;
  if (!current_vault_map->rewrite_anchors (vault_std_string (path), renames,
                                            changed, error)) {
    std_warning << "Could not rewrite Vault map anchors: " << error.c_str ()
                << LF;
    return 0;
  }
  return changed;
}

string
vault_generate_uuid () {
  static bool seeded = false;
  if (!seeded) {
    srandom ((int) raw_time ());
    seeded = true;
  }
  
  string res = "";
  const char* hex = "0123456789abcdef";
  for (int i=0; i<32; i++) {
    if (i == 8 || i == 12 || i == 16 || i == 20) res << "-";
    res << hex[random() % 16];
  }
  return res;
}
