/******************************************************************************
* MODULE     : vault.hpp
* DESCRIPTION: Vault management for Math Knowledge Workbench
* COPYRIGHT  : (C) 2026  Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef VAULT_HPP
#define VAULT_HPP

#include "tree.hpp"
#include "url.hpp"
#include "Database/database.hpp"

/* Vault metadata */
struct vault_info {
  string name;
  url    root;
  url    db_url;
};

/* Global vault state */
extern bool       is_vault_active;
extern vault_info current_vault;

/* Vault lifecycle */
bool vault_active ();
url  vault_get_root ();
void vault_load (url root_dir, string name, string db_rel_path);
void vault_close ();

/* CRUD for Wikilink/Transclusion Map */
void    vault_set_node (string uuid, string path, string anchor_begin, string anchor_end);
tree    vault_get_node (string uuid); // Returns a tuple (path, begin, end) or UNINIT
void    vault_remove_node (string uuid);
bool    vault_has_node (string uuid);
string  vault_find_uuid (string path, string anchor_begin, string anchor_end);
string  vault_generate_uuid ();

#endif // VAULT_HPP
