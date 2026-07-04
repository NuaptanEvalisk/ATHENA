/******************************************************************************
* MODULE     : websites_shell.cpp
* DESCRIPTION: Static website shell writer
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "ATHENA/Data/websites_internal.hpp"

namespace athena_websites {

namespace {

void
replace_all (std::string& text, const std::string& from,
             const std::string& to) {
  if (from.empty ()) return;
  size_t pos = 0;
  while ((pos = text.find (from, pos)) != std::string::npos) {
    text.replace (pos, from.size (), to);
    pos += to.size ();
  }
}

bool
write_template_file (const fs::path& target, const std::string& name) {
  std::string text;
  if (!website_template_text (name, text)) return false;
  return write_file_bytes (target, text);
}

std::string
xml_escape (const std::string& text) {
  std::string out;
  for (char c: text) {
    switch (c) {
    case '&': out += "&amp;"; break;
    case '<': out += "&lt;"; break;
    case '>': out += "&gt;"; break;
    case '"': out += "&quot;"; break;
    case '\'': out += "&apos;"; break;
    default: out += c; break;
    }
  }
  return out;
}

bool
public_sitemap_base (const std::string& raw, QUrl& base) {
  QString text = qs (raw).trimmed ();
  if (text.isEmpty ()) return false;
  QUrl url (text);
  QString scheme = url.scheme ().toLower ();
  if (!url.isValid () || url.isRelative () ||
      (scheme != "http" && scheme != "https") || url.host ().isEmpty ())
    return false;
  url.setQuery (QString ());
  url.setFragment (QString ());
  QString path = url.path ();
  if (path.isEmpty ()) path = "/";
  if (!path.endsWith ('/')) path += "/";
  url.setPath (path);
  base = url;
  return true;
}

std::string
sitemap_loc (const QUrl& base, const std::string& page) {
  QString encoded = QString::fromLatin1 (
    QUrl::toPercentEncoding (qs (page), "/"));
  QUrl url = base.resolved (QUrl (encoded));
  return ss (url.toString (QUrl::FullyEncoded));
}

bool
write_sitemap (const athena_website_entry& website,
               const GenerationContext& cx, std::string& error) {
  fs::path target = cx.destination / "sitemap.xml";
  if (!website.generate_sitemap) {
    std::error_code ec;
    fs::remove (target, ec);
    return true;
  }

  QUrl base;
  if (!public_sitemap_base (website.public_url, base)) {
    error = "Website base URL must be an absolute http(s) URL to write "
            "sitemap.xml.";
    return false;
  }

  std::set<std::string> pages;
  pages.insert ("index.html");
  for (const auto& item: cx.html_paths)
    pages.insert (item.second);
  for (const auto& item: cx.namespace_homepages)
    pages.insert (item.second);

  std::ostringstream out;
  out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
      << "<urlset xmlns=\"http://www.sitemaps.org/schemas/sitemap/0.9\">\n";
  for (const std::string& page: pages)
    out << "  <url><loc>" << xml_escape (sitemap_loc (base, page))
        << "</loc></url>\n";
  out << "</urlset>\n";
  if (!write_file_bytes (target, out.str ())) {
    error = "Could not write sitemap.xml.";
    return false;
  }
  return true;
}

} // namespace

QJsonObject
site_manifest (const athena_website_entry& website,
               const GenerationContext& cx) {
  QJsonObject root;
  root["title"] = qs (website.name);

  QJsonArray files;
  for (const std::string& rel: cx.selected_files) {
    QJsonObject file;
    file["path"] = qs (rel);
    file["html"] = qs (cx.html_paths.at (rel));
    QString stem_title = qs (fs::path (rel).stem ().string ());
    file["stemTitle"] = stem_title;
    auto title = cx.titles.find (rel);
    file["displayTitle"] = title == cx.titles.end () ?
      stem_title : qs (title->second);
    file["title"] = file["displayTitle"];
    auto search = cx.search_texts.find (rel);
    file["searchText"] = search == cx.search_texts.end () ? QString () :
                                                    qs (search->second);
    files.append (file);
  }
  root["files"] = files;

  QJsonArray namespaces;
  for (const auto& item: cx.namespace_homepages) {
    QJsonObject ns;
    ns["name"] = qs (item.first);
    ns["homepage"] = qs (item.second);
    namespaces.append (ns);
  }
  root["namespaces"] = namespaces;
  root["storageKey"] = qs ("athena-website:" + website.id);
  root["publicUrl"] = qs (website.public_url);
  root["generateSitemap"] = website.generate_sitemap;

  std::string entry = "about:blank";
  if (website.entrypoint_kind == "namespace") {
    auto it = cx.namespace_homepages.find (website.entrypoint_value);
    if (it != cx.namespace_homepages.end ()) entry = it->second;
  }
  else if (cx.selected_files.count (website.entrypoint_value) != 0)
    entry = cx.html_paths.at (website.entrypoint_value);
  else if (!cx.selected_files.empty ())
    entry = cx.html_paths.at (*cx.selected_files.begin ());
  root["entry"] = qs (entry);
  return root;
}

std::string
site_data_js (const QJsonObject& manifest) {
  std::string data = ss (QJsonDocument (manifest).toJson (
    QJsonDocument::Compact));
  return std::string ("window.ATHENA_SITE_DATA=") + data + ";\n";
}

bool
copy_favicon (const fs::path& dest) {
  fs::path src = fs::path (tm_to_std (get_env ("ATHENA_PATH"))) /
                 "misc" / "images" / "ATHENA-512.png";
  std::error_code ec;
  if (!fs::exists (src)) return true;
  fs::create_directories (dest.parent_path (), ec);
  fs::copy_file (src, dest, fs::copy_options::overwrite_existing, ec);
  return !ec;
}

bool
write_site_shell (const athena_website_entry& website,
                  const GenerationContext& cx, std::string& error) {
  std::error_code ec;
  fs::create_directories (cx.destination / "js", ec);
  fs::create_directories (cx.destination / "css", ec);
  if (ec) {
    error = "Could not create website support folders: " + ec.message ();
    return false;
  }

  std::string index;
  if (!website_template_text ("index.html", index)) {
    error = "Could not read website template index.html.";
    return false;
  }
  replace_all (index, "{{TITLE}}", website.name);

  if (!write_template_file (cx.destination / "css" / "site.css",
                            "site.css") ||
      !write_file_bytes (cx.destination / "css" / "theme.css",
                         site_theme_css ()) ||
      !write_file_bytes (cx.destination / "js" / "site-data.js",
                         site_data_js (site_manifest (website, cx))) ||
      !write_template_file (cx.destination / "js" / "window-manager.js",
                            "window-manager.js") ||
      !write_template_file (cx.destination / "js" / "explorers.js",
                            "explorers.js") ||
      !write_template_file (cx.destination / "js" / "outline.js",
                            "outline.js") ||
      !write_template_file (cx.destination / "js" / "search.js",
                            "search.js") ||
      !write_template_file (cx.destination / "js" / "quick-switcher.js",
                            "quick-switcher.js") ||
      !write_template_file (cx.destination / "js" / "app.js", "app.js") ||
      !write_file_bytes (cx.destination / "index.html", index)) {
    error = "Could not write website shell files.";
    return false;
  }
  if (!write_sitemap (website, cx, error)) return false;
  copy_favicon (cx.destination / "css" / "favicon.png");
  return true;
}

bool
export_namespace_homepage (const std::string& name, bool technical,
                           const std::string& output_rel,
                           const fs::path& target,
                           const GenerationContext& cx,
                           std::string& error) {
  std::string tmfs = technical ? "!" + name : name;
  tree doc = athena_namespace_info_page (std_to_tm (tmfs));
  tree rewritten = rewrite_static_links (doc, "", output_rel, cx);
  return export_document_html (rewritten, fs::path ("tmfs://ns/" + tmfs),
                               target, error);
}

} // namespace athena_websites
