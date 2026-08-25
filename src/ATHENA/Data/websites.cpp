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

#include <QCryptographicHash>
#include <QSaveFile>

namespace athena_websites {

namespace {

constexpr int website_source_manifest_version= 1;
constexpr int website_document_export_revision= 3;
constexpr const char* website_source_manifest_name=
  ".athena-source-hashes.json";

struct WebsiteSourceEntry {
  std::string source_hash;
  std::string html;
  std::string pdf;
  std::string title;
  std::string search_text;
  bool has_transclusion= false;
  std::string vault_hash;
};

struct WebsiteSourceManifest {
  bool valid= false;
  bool generate_pdfs= false;
  std::set<std::string> selected_files;
  std::map<std::string,WebsiteSourceEntry> sources;
};

std::string
pdf_rel_for_doc (const std::string& rel) {
  fs::path path= fs::path ("pdf") / fs::path (rel);
  path.replace_extension (".pdf");
  return generic_path (path);
}

bool
file_sha256 (const fs::path& path, std::string& hash, std::string& error) {
  QFile file (qs (path.string ()));
  if (!file.open (QIODevice::ReadOnly)) {
    error= "Could not hash website source " + path.string ();
    return false;
  }
  QCryptographicHash digest (QCryptographicHash::Sha256);
  while (!file.atEnd ()) digest.addData (file.read (1024 * 1024));
  hash= digest.result ().toHex ().toStdString ();
  return true;
}

bool
valid_pdf_file (const fs::path& path) {
  QFile file (qs (path.string ()));
  return file.open (QIODevice::ReadOnly) && file.size () >= 5 &&
         file.read (5) == "%PDF-";
}

std::string
source_collection_hash (const std::map<std::string,std::string>& hashes) {
  QCryptographicHash digest (QCryptographicHash::Sha256);
  for (const auto& item: hashes) {
    digest.addData (QByteArray::fromStdString (item.first));
    digest.addData (QByteArray (1, '\0'));
    digest.addData (QByteArray::fromStdString (item.second));
    digest.addData (QByteArray (1, '\0'));
  }
  return digest.result ().toHex ().toStdString ();
}

bool
contains_transclusion (tree t) {
  if (is_atomic (t)) return false;
  if (is_compound (t, "transclude") && N(t) >= 4) return true;
  for (int i=0; i<N(t); i++)
    if (contains_transclusion (t[i])) return true;
  return false;
}

WebsiteSourceManifest
load_source_manifest (const fs::path& destination) {
  WebsiteSourceManifest out;
  QFile file (qs ((destination / website_source_manifest_name).string ()));
  if (!file.open (QIODevice::ReadOnly)) return out;
  QJsonParseError parse_error;
  QJsonDocument document= QJsonDocument::fromJson (file.readAll (),
                                                    &parse_error);
  if (parse_error.error != QJsonParseError::NoError || !document.isObject ())
    return out;
  QJsonObject root= document.object ();
  if (root.value ("version").toInt () != website_source_manifest_version ||
      root.value ("documentExportRevision").toInt () !=
        website_document_export_revision)
    return out;
  out.generate_pdfs= root.value ("generatePdfs").toBool (false);

  QJsonArray selected= root.value ("selectedFiles").toArray ();
  for (const QJsonValue& value: selected)
    if (value.isString ()) out.selected_files.insert (ss (value.toString ()));

  QJsonObject sources= root.value ("sources").toObject ();
  for (auto it= sources.begin (); it != sources.end (); ++it) {
    if (!it.value ().isObject ()) continue;
    QJsonObject value= it.value ().toObject ();
    WebsiteSourceEntry entry;
    entry.source_hash= ss (value.value ("sha256").toString ());
    entry.html= ss (value.value ("html").toString ());
    entry.pdf= ss (value.value ("pdf").toString ());
    entry.title= ss (value.value ("title").toString ());
    entry.search_text= ss (value.value ("searchText").toString ());
    entry.has_transclusion= value.value ("hasTransclusion").toBool (false);
    entry.vault_hash= ss (value.value ("vaultSourceHash").toString ());
    if (!entry.source_hash.empty () && !entry.html.empty ())
      out.sources[ss (it.key ())]= entry;
  }
  out.valid= true;
  return out;
}

bool
write_source_manifest (const fs::path& destination,
                       const std::set<std::string>& selected_files,
                       const std::map<std::string,WebsiteSourceEntry>& entries,
                       bool generate_pdfs,
                       std::string& error) {
  QJsonObject root;
  root["version"]= website_source_manifest_version;
  root["documentExportRevision"]= website_document_export_revision;
  root["generatePdfs"]= generate_pdfs;
  QJsonArray selected;
  for (const std::string& rel: selected_files) selected.append (qs (rel));
  root["selectedFiles"]= selected;

  QJsonObject sources;
  for (const auto& item: entries) {
    QJsonObject value;
    value["sha256"]= qs (item.second.source_hash);
    value["html"]= qs (item.second.html);
    if (!item.second.pdf.empty ()) value["pdf"]= qs (item.second.pdf);
    value["title"]= qs (item.second.title);
    value["searchText"]= qs (item.second.search_text);
    value["hasTransclusion"]= item.second.has_transclusion;
    if (item.second.has_transclusion)
      value["vaultSourceHash"]= qs (item.second.vault_hash);
    sources[qs (item.first)]= value;
  }
  root["sources"]= sources;

  QSaveFile file (qs ((destination / website_source_manifest_name).string ()));
  if (!file.open (QIODevice::WriteOnly)) {
    error= "Could not write website source hash manifest.";
    return false;
  }
  if (file.write (QJsonDocument (root).toJson (QJsonDocument::Indented)) < 0 ||
      !file.commit ()) {
    error= "Could not commit website source hash manifest.";
    return false;
  }
  return true;
}

bool
safe_generated_html (const std::string& path) {
  fs::path p (path);
  if (path.empty () || p.extension () != ".html" || p.is_absolute ())
    return false;
  for (const fs::path& component: p.lexically_normal ())
    if (component == "..") return false;
  return true;
}

void
remove_stale_document_pages (const WebsiteSourceManifest& old_manifest,
                             const GenerationContext& cx,
                             bool generate_pdfs) {
  if (!old_manifest.valid) return;
  std::set<std::string> current;
  for (const auto& item: cx.html_paths) current.insert (item.second);
  for (const auto& item: old_manifest.sources) {
    const std::string& html= item.second.html;
    if (current.count (html) != 0 || !safe_generated_html (html)) continue;
    std::error_code ec;
    fs::remove (cx.destination / fs::path (html), ec);
  }
  for (const auto& item: old_manifest.sources) {
    const std::string& pdf= item.second.pdf;
    bool retained= generate_pdfs &&
      cx.pdf_paths.count (item.first) != 0 &&
      cx.pdf_paths.at (item.first) == pdf;
    if (retained || pdf.empty ()) continue;
    fs::path path (pdf);
    bool safe= path.extension () == ".pdf" && !path.is_absolute ();
    for (const fs::path& component: path.lexically_normal ())
      if (component == "..") safe= false;
    if (safe) {
      std::error_code ec;
      fs::remove (cx.destination / path, ec);
    }
  }
}

} // namespace

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
  website_log ("running post-generation script");
  int rc = QProcess::execute (program, arguments);
  if (rc != 0) {
    std::cout << std::endl << "ATHENA_WEBSITE_POST_STATUS failed " << rc
              << std::endl;
    error = "Post-generation command failed with status " +
            std::to_string (rc);
    return false;
  }
  std::cout << std::endl << "ATHENA_WEBSITE_POST_STATUS complete" << std::endl;
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
  for (const std::string& rel: selected) {
    cx.html_paths[rel] = html_rel_for_doc (rel);
    if (website.generate_pdfs) cx.pdf_paths[rel]= pdf_rel_for_doc (rel);
  }

  std::map<std::string,std::string> source_hashes;
  for (const std::string& rel: universe) {
    std::string hash;
    if (!file_sha256 (root / rel, hash, error)) return false;
    source_hashes[rel]= hash;
  }
  const std::string vault_source_hash= source_collection_hash (source_hashes);
  WebsiteSourceManifest old_manifest= load_source_manifest (cx.destination);
  const bool same_export_range=
    old_manifest.valid && old_manifest.selected_files == selected;
  const bool same_pdf_configuration=
    old_manifest.valid &&
    old_manifest.generate_pdfs == website.generate_pdfs;
  std::map<std::string,WebsiteSourceEntry> next_entries;

  website_log ("generating " + website.name + " into " +
               cx.destination.string ());
  size_t html_cached_count= 0;
  size_t html_exported_count= 0;
  size_t pdf_cached_count= 0;
  size_t pdf_exported_count= 0;
  size_t index = 0;
  for (const std::string& rel: selected) {
    index++;
    fs::path source = root / rel;
    fs::path target = cx.destination / html_rel_for_doc (rel);
    auto old= old_manifest.sources.find (rel);
    bool source_cached= same_export_range &&
      old != old_manifest.sources.end () &&
      old->second.source_hash == source_hashes.at (rel) &&
      (!old->second.has_transclusion ||
       old->second.vault_hash == vault_source_hash);
    bool html_cached= source_cached && same_pdf_configuration &&
      old->second.html == cx.html_paths.at (rel) && fs::is_regular_file (target);
    bool pdf_cached= website.generate_pdfs && source_cached &&
      old->second.pdf == cx.pdf_paths.at (rel) &&
      valid_pdf_file (cx.destination / old->second.pdf);

    WebsiteSourceEntry entry;
    tree doc;
    if (html_cached) {
      cx.titles[rel]= old->second.title;
      cx.search_texts[rel]= old->second.search_text;
      entry= old->second;
      html_cached_count++;
    }
    else {
      website_progress (index, selected.size (), "Exporting HTML", rel);
      doc= import_tree (url_system (std_to_tm (source.string ())), "texmacs");
      cx.titles[rel]= document_title (doc, fs::path (rel).stem ().string ());
      cx.search_texts[rel]= document_search_text (doc);
      tree rewritten= rewrite_static_links (doc, rel, rel, cx);
      std::string pdf_href= website.generate_pdfs ? relative_href (
        cx.html_paths.at (rel), cx.pdf_paths.at (rel)) : "";
      if (!export_document_html (rewritten, source, target,
                                 cx.html_paths.at (rel), cx.titles.at (rel),
                                 pdf_href, error))
        return false;
      entry.source_hash= source_hashes.at (rel);
      entry.html= cx.html_paths.at (rel);
      entry.title= cx.titles.at (rel);
      entry.search_text= cx.search_texts.at (rel);
      entry.has_transclusion= contains_transclusion (doc);
      if (entry.has_transclusion) entry.vault_hash= vault_source_hash;
      html_exported_count++;
    }

    if (website.generate_pdfs) {
      entry.pdf= cx.pdf_paths.at (rel);
      if (pdf_cached) pdf_cached_count++;
      else {
        website_progress (index, selected.size (), "Exporting PDF", rel);
        if (!export_document_pdf (source, cx.destination / entry.pdf, error))
          return false;
        pdf_exported_count++;
      }
    }
    else entry.pdf.clear ();

    if (html_cached && (!website.generate_pdfs || pdf_cached))
      website_progress (index, selected.size (), "Cached", rel);
    next_entries[rel]= entry;
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
  remove_stale_document_pages (old_manifest, cx, website.generate_pdfs);
  if (!write_source_manifest (cx.destination, selected, next_entries,
                              website.generate_pdfs, error))
    return false;
  website_log ("HTML export summary: " +
               std::to_string (html_exported_count) + " generated, " +
               std::to_string (html_cached_count) + " cached");
  if (website.generate_pdfs)
    website_log ("PDF export summary: " +
                 std::to_string (pdf_exported_count) + " generated, " +
                 std::to_string (pdf_cached_count) + " cached");
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
  string load_error= vault_load (url_system (std_to_tm (root.string ())),
                                  std_to_tm (info.name),
                                  std_to_tm (info.map_path),
                                  std_to_tm (info.namespace_db_path));
  if (load_error != "") {
    error= tm_to_std (load_error);
    return false;
  }
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
  string load_error= vault_load (url_system (std_to_tm (root.string ())),
                                  std_to_tm (info.name),
                                  std_to_tm (info.map_path),
                                  std_to_tm (info.namespace_db_path));
  if (load_error != "") {
    error= tm_to_std (load_error);
    return false;
  }

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
athena_run_website_post_command (const std::string& vault_root,
                                 const std::string& website_id,
                                 std::string& error) {
  fs::path root = normalize_root (fs::path (vault_root));
  std::vector<athena_website_entry> websites;
  if (!athena_websites_load (root.string (), websites, error)) return false;
  for (const athena_website_entry& website: websites) {
    if (website.id != website_id && website.name != website_id) continue;
    if (!website.post_command.enabled || website.post_command.program.empty ()) {
      error = "Website has no enabled post-generation command: " + website_id;
      return false;
    }
    return run_post_command (website, root, destination_for (root, website),
                             error);
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
