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

bool       is_vault_active = false;
vault_info current_vault;

bool
vault_active () {
  return is_vault_active;
}

url
vault_get_root () {
  return current_vault.root;
}

void
vault_load (url root_dir, string name, string db_rel_path) {
  current_vault.root   = root_dir;
  current_vault.name   = name;
  current_vault.db_url = root_dir * url (db_rel_path);
  
  // Trigger TMDB loading
  (void) get_database (current_vault.db_url);
  
  is_vault_active = true;
}

void
vault_close () {
  if (is_vault_active) {
    sync_databases ();
  }
  is_vault_active = false;
  current_vault.root = url_none ();
  current_vault.name = "";
  current_vault.db_url = url_none ();
}

void
vault_set_node (string uuid, string path, string anchor_begin, string anchor_end) {
  if (!is_vault_active) return;
  url u = current_vault.db_url;
  
  strings s_path; s_path << path;
  strings s_begin; s_begin << anchor_begin;
  strings s_end; s_end << anchor_end;

  set_field (u, uuid, "v-path", s_path, 0);
  set_field (u, uuid, "v-anchor-begin", s_begin, 0);
  set_field (u, uuid, "v-anchor-end", s_end, 0);
  
  sync_databases ();
}

tree
vault_get_node (string uuid) {
  if (!is_vault_active) return UNINIT;
  url u = current_vault.db_url;
  
  strings p = get_field (u, uuid, "v-path", 0);
  strings b = get_field (u, uuid, "v-anchor-begin", 0);
  strings e = get_field (u, uuid, "v-anchor-end", 0);
  
  if (N(p) == 0) return UNINIT;
  
  tree res (TUPLE);
  res << tree (p[0]);
  res << tree (N(b) > 0 ? b[0] : "");
  res << tree (N(e) > 0 ? e[0] : "");
  return res;
}

void
vault_remove_node (string uuid) {
  if (!is_vault_active) return;
  url u = current_vault.db_url;
  remove_entry (u, uuid, 0);
  sync_databases ();
}

bool
vault_has_node (string uuid) {
  if (!is_vault_active) return false;
  strings p = get_field (current_vault.db_url, uuid, "v-path", 0);
  return N(p) > 0;
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
  if (!is_vault_active) return res;
  scan_recursive (current_vault.root, res);
  return res;
}

static void
find_labels (tree t, strings& res) {
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

strings
vault_get_anchors (url u) {
  strings res;
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
  if (!is_vault_active) return "";
  url u = current_vault.db_url;
  
  tree ql (TUPLE);
  ql << tuple ("v-path", path);
  ql << tuple ("v-anchor-begin", anchor_begin);
  ql << tuple ("v-anchor-end", anchor_end);
  
  strings ids = query (u, ql, 0, 1, 0);
  if (N(ids) > 0) return ids[0];
  return "";
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
