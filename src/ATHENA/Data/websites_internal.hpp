/******************************************************************************
* MODULE     : websites_internal.hpp
* DESCRIPTION: Internal helpers for vault website generation
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef ATHENA_WEBSITES_INTERNAL_HPP
#define ATHENA_WEBSITES_INTERNAL_HPP

#include "ATHENA/Data/websites.hpp"

#include "ATHENA/Data/new_buffer.hpp"
#include "ATHENA/Data/vault_maintenance_internal.hpp"
#include "analyze.hpp"
#include "boot.hpp"
#include "file.hpp"
#include "glue.hpp"
#include "namespaces.hpp"
#include "scheme.hpp"
#include "tm_ostream.hpp"
#include "url.hpp"
#include "vault.hpp"
#include "colors.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QProcess>
#include <QUrl>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <vector>

namespace athena_websites {

namespace fs = std::filesystem;

struct VaultfileWebsiteInfo {
  std::string name = "Vault";
  std::string map_path = "map.tmdb";
  std::string preferences_path;
  std::string namespace_db_path = "ns.sqlite";
  std::string startup_page;
  std::string one_time_startup_page;
  std::string maintenance_summary_path;
  std::string rag_index_path = "rag.sqlite";
  std::string websites_path = "websites.json";
};

struct GenerationContext {
  fs::path root;
  fs::path destination;
  std::set<std::string> selected_files;
  std::map<std::string,std::string> html_paths;
  std::map<std::string,std::string> titles;
  std::map<std::string,std::string> search_texts;
  std::map<std::string,std::string> namespace_homepages;
};

QString qs (const std::string& s);
std::string ss (const QString& s);
std::string generic_path (fs::path p);
std::string clean_relative (const std::string& path);
bool is_doc_path (const fs::path& path);
std::string scheme_quote (const std::string& text);
std::string json_script_string (const std::string& text);
std::string site_theme_css ();
bool website_template_text (const std::string& name, std::string& text);

bool read_vaultfile (const fs::path& root, VaultfileWebsiteInfo& info,
                     std::string& error);
fs::path registry_path_for (const fs::path& root,
                            const VaultfileWebsiteInfo& info);
QJsonObject selector_to_json (const athena_website_selector& selector);
athena_website_selector selector_from_json (const QJsonObject& obj);
QJsonObject website_to_json (const athena_website_entry& website);
athena_website_entry website_from_json (const QJsonObject& obj);
std::string selector_summary_rec (const athena_website_selector& selector);

std::set<std::string> all_document_rels (const fs::path& root);
std::set<std::string> eval_selector (
  const athena_website_selector& selector,
  const fs::path& root,
  const std::set<std::string>& universe);
std::set<std::string> selector_namespaces (
  const athena_website_selector& selector);

std::string html_rel_for_doc (const std::string& rel);
std::string safe_namespace_file (const std::string& name,
                                 bool technical = false);
std::string relative_href (const std::string& from_html,
                           const std::string& to_html,
                           const std::string& anchor = "");

std::string document_search_text (tree doc);
std::string document_title (tree t, const std::string& fallback);
tree rewrite_static_links (tree t, const std::string& source_rel,
                           const std::string& output_rel,
                           const GenerationContext& cx);
bool export_document_html (tree doc, const fs::path& source,
                           const fs::path& target,
                           const std::string& output_rel,
                           const std::string& title,
                           std::string& error);

bool write_site_shell (const athena_website_entry& website,
                       const GenerationContext& cx,
                       std::string& error);
bool export_namespace_homepage (const std::string& name, bool technical,
                                const std::string& output_rel,
                                const fs::path& target,
                                const GenerationContext& cx,
                                std::string& error);
bool generate_website_entry (const fs::path& root,
                             const athena_website_entry& website,
                             std::string& error);

} // namespace athena_websites

#endif // ATHENA_WEBSITES_INTERNAL_HPP
