/******************************************************************************
* MODULE     : websites.hpp
* DESCRIPTION: Vault-scoped static website registry and generator
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef ATHENA_WEBSITES_HPP
#define ATHENA_WEBSITES_HPP

#include <string>
#include <vector>

struct athena_website_selector {
  std::string op;
  std::string value;
  std::vector<athena_website_selector> children;
};

struct athena_website_post_command {
  bool enabled = false;
  std::string program;
  std::string arguments;
};

struct athena_website_redirection {
  std::string shortcut;
  std::string document;
};

struct athena_website_entry {
  std::string id;
  std::string name;
  athena_website_selector selector;
  std::string destination;
  std::string public_url;
  std::string description;
  std::string favicon;
  bool generate_sitemap = false;
  bool generate_pdfs = false;
  bool generate_redirections = false;
  std::vector<athena_website_redirection> redirections;
  std::string regenerate;
  std::string entrypoint_kind;
  std::string entrypoint_value;
  athena_website_post_command post_command;
};

bool athena_websites_registry_path (const std::string& vault_root,
                                    std::string& registry_path,
                                    std::string& error);
bool athena_websites_load (const std::string& vault_root,
                           std::vector<athena_website_entry>& websites,
                           std::string& error);
bool athena_websites_save (const std::string& vault_root,
                           const std::vector<athena_website_entry>& websites,
                           std::string& error);
bool athena_website_selector_files (
  const std::string& vault_root,
  const athena_website_selector& selector,
  std::vector<std::string>& files,
  std::string& error);
std::string athena_website_selector_summary (
  const athena_website_selector& selector);
bool athena_website_selector_empty (
  const athena_website_selector& selector);
bool athena_generate_website (const std::string& vault_root,
                              const std::string& website_id,
                              std::string& error);
bool athena_run_website_post_command (const std::string& vault_root,
                                      const std::string& website_id,
                                      std::string& error);
bool athena_generate_maintenance_websites (const std::string& vault_root,
                                           std::string& error);

#endif // ATHENA_WEBSITES_HPP
