/******************************************************************************
* MODULE     : websites.cpp
* DESCRIPTION: Vault-scoped static website public API
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "ATHENA/Data/websites_internal.hpp"

namespace athena_websites {

void
website_log (const std::string& message) {
  std::cout << "ATHENA_WEBSITE_LOG " << message << std::endl;
}

void
website_progress (size_t current, size_t total, const std::string& phase,
                  const std::string& item) {
  std::cout << "ATHENA_WEBSITE_PROGRESS " << current << " " << total
            << " " << phase << " " << item << std::endl;
}

std::set<std::string>
selector_namespaces (const athena_website_selector& selector) {
  std::set<std::string> out;
  if (selector.op == "namespace") out.insert (selector.value);
  for (const athena_website_selector& child: selector.children) {
    std::set<std::string> more = selector_namespaces (child);
    out.insert (more.begin (), more.end ());
  }
  return out;
}

fs::path
destination_for (const fs::path& root, const athena_website_entry& website) {
  fs::path dest (website.destination.empty () ? website.name :
                                             website.destination);
  if (dest.is_absolute ()) return dest.lexically_normal ();
  return (root / dest).lexically_normal ();
}

bool
run_post_command (const athena_website_entry& website, const fs::path& root,
                  const fs::path& dest, std::string& error) {
  if (!website.post_command.enabled || website.post_command.program.empty ())
    return true;
  QString program = qs (website.post_command.program);
  QString args = qs (website.post_command.arguments);
  args.replace ("{dest}", qs (dest.string ()));
  args.replace ("{vault}", qs (root.string ()));
  args.replace ("{website}", qs (website.id));
  QStringList arguments = QProcess::splitCommand (args);
  int rc = QProcess::execute (program, arguments);
  if (rc != 0) {
    error = "Post-generation command failed with status " +
            std::to_string (rc);
    return false;
  }
  return true;
}

bool
generate_website_entry (const fs::path& root,
                        const athena_website_entry& website,
                        std::string& error) {
  std::set<std::string> universe = all_document_rels (root);
  std::set<std::string> selected = eval_selector (website.selector, root,
                                                  universe);
  if (selected.empty ()) {
    error = "Website selector did not match any documents.";
    return false;
  }

  GenerationContext cx;
  cx.root = root;
  cx.destination = destination_for (root, website);
  cx.selected_files = selected;
  for (const std::string& rel: selected)
    cx.html_paths[rel] = html_rel_for_doc (rel);

  website_log ("generating " + website.name + " into " +
               cx.destination.string ());
  size_t index = 0;
  for (const std::string& rel: selected) {
    index++;
    website_progress (index, selected.size (), "Exporting", rel);
    fs::path source = root / rel;
    fs::path target = cx.destination / html_rel_for_doc (rel);
    tree doc = import_tree (url_system (std_to_tm (source.string ())),
                            "texmacs");
    cx.titles[rel] = document_title (doc, fs::path (rel).stem ().string ());
    cx.search_texts[rel] = document_search_text (doc);
    tree rewritten = rewrite_static_links (doc, rel, rel, cx);
    if (!export_document_html (rewritten, source, target, cx.html_paths[rel],
                               cx.titles[rel], error))
      return false;
  }

  std::set<std::string> namespaces = selector_namespaces (website.selector);
  if (website.entrypoint_kind == "namespace" &&
      !website.entrypoint_value.empty ())
    namespaces.insert (website.entrypoint_value);
  fs::create_directories (cx.destination / "homepages");
  for (const std::string& ns: namespaces) {
    std::string normal = "homepages/" + safe_namespace_file (ns, false);
    std::string technical = "homepages/" + safe_namespace_file (ns, true);
    if (!export_namespace_homepage (ns, false, normal, cx.destination / normal,
                                    cx, error))
      return false;
    if (!export_namespace_homepage (ns, true, technical,
                                    cx.destination / technical, cx, error))
      return false;
    cx.namespace_homepages[ns] = normal;
  }

  if (!write_site_shell (website, cx, error)) return false;
  if (!run_post_command (website, root, cx.destination, error)) return false;
  website_log ("complete");
  return true;
}

} // namespace athena_websites

using namespace athena_websites;

bool
athena_websites_registry_path (const std::string& vault_root,
                               std::string& registry_path,
                               std::string& error) {
  fs::path root = normalize_root (fs::path (vault_root));
  VaultfileWebsiteInfo info;
  if (!read_vaultfile (root, info, error)) return false;
  registry_path = registry_path_for (root, info).string ();
  return true;
}

bool
athena_websites_load (const std::string& vault_root,
                      std::vector<athena_website_entry>& websites,
                      std::string& error) {
  websites.clear ();
  std::string registry;
  if (!athena_websites_registry_path (vault_root, registry, error))
    return false;
  QFile file (qs (registry));
  if (!file.exists ()) return true;
  if (!file.open (QIODevice::ReadOnly)) {
    error = "Could not read " + registry;
    return false;
  }
  QJsonParseError parse_error;
  QJsonDocument doc = QJsonDocument::fromJson (file.readAll (), &parse_error);
  if (parse_error.error != QJsonParseError::NoError || !doc.isObject ()) {
    error = "Malformed websites registry: " +
            ss (parse_error.errorString ());
    return false;
  }
  QJsonArray array = doc.object ().value ("websites").toArray ();
  for (const QJsonValue& value: array)
    if (value.isObject ())
      websites.push_back (website_from_json (value.toObject ()));
  return true;
}

bool
athena_websites_save (const std::string& vault_root,
                      const std::vector<athena_website_entry>& websites,
                      std::string& error) {
  std::string registry;
  if (!athena_websites_registry_path (vault_root, registry, error))
    return false;
  QJsonArray array;
  for (const athena_website_entry& website: websites)
    array.append (website_to_json (website));
  QJsonObject root;
  root["version"] = 1;
  root["websites"] = array;
  QFileInfo info (qs (registry));
  QDir ().mkpath (info.absolutePath ());
  QFile file (qs (registry));
  if (!file.open (QIODevice::WriteOnly | QIODevice::Truncate)) {
    error = "Could not write " + registry;
    return false;
  }
  file.write (QJsonDocument (root).toJson (QJsonDocument::Indented));
  return true;
}

bool
athena_website_selector_files (const std::string& vault_root,
                               const athena_website_selector& selector,
                               std::vector<std::string>& files,
                               std::string& error) {
  files.clear ();
  fs::path root = normalize_root (fs::path (vault_root));
  VaultfileWebsiteInfo info;
  if (!read_vaultfile (root, info, error)) return false;
  vault_load (url_system (std_to_tm (root.string ())),
              std_to_tm (info.name),
              std_to_tm (info.map_path),
              std_to_tm (info.namespace_db_path));
  std::set<std::string> universe = all_document_rels (root);
  std::set<std::string> selected = eval_selector (selector, root, universe);
  files.assign (selected.begin (), selected.end ());
  return true;
}

std::string
athena_website_selector_summary (const athena_website_selector& selector) {
  return selector_summary_rec (selector);
}

bool
athena_website_selector_empty (const athena_website_selector& selector) {
  if (selector.op.empty ()) return true;
  if (selector.op == "path" || selector.op == "namespace")
    return selector.value.empty ();
  if (selector.children.empty ()) return true;
  for (const athena_website_selector& child: selector.children)
    if (athena_website_selector_empty (child)) return true;
  return false;
}

bool
athena_generate_website (const std::string& vault_root,
                         const std::string& website_id,
                         std::string& error) {
  fs::path root = normalize_root (fs::path (vault_root));
  VaultfileWebsiteInfo info;
  if (!read_vaultfile (root, info, error)) return false;
  vault_load (url_system (std_to_tm (root.string ())),
              std_to_tm (info.name),
              std_to_tm (info.map_path),
              std_to_tm (info.namespace_db_path));

  std::vector<athena_website_entry> websites;
  if (!athena_websites_load (root.string (), websites, error)) return false;
  for (const athena_website_entry& website: websites) {
    if (website.id == website_id || website.name == website_id)
      return generate_website_entry (root, website, error);
  }
  error = "Unknown website: " + website_id;
  return false;
}

bool
athena_generate_maintenance_websites (const std::string& vault_root,
                                      std::string& error) {
  std::vector<athena_website_entry> websites;
  if (!athena_websites_load (vault_root, websites, error)) return false;
  bool ok = true;
  for (const athena_website_entry& website: websites) {
    if (website.regenerate != "maintenance") continue;
    std::string local_error;
    if (!athena_generate_website (vault_root, website.id, local_error)) {
      ok = false;
      if (!error.empty ()) error += "\n";
      error += website.name + ": " + local_error;
    }
  }
  return ok;
}

VaultMaintenancePassResult
vault_maintenance_pass_generate_websites (VaultMaintenanceContext& ctx) {
  std::string error;
  if (!athena_generate_maintenance_websites (ctx.root.string (), error))
    return VaultMaintenancePassResult::failure (error);
  return VaultMaintenancePassResult::success (
    "maintenance website generation completed");
}
