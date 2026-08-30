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

#include <QCryptographicHash>

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
public_site_base (const std::string& raw, QUrl& base) {
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
url_string (const QUrl& url) {
  return ss (url.toString (QUrl::FullyEncoded));
}

std::string
sitemap_loc (const QUrl& base, const std::string& page) {
  QString encoded = QString::fromLatin1 (
    QUrl::toPercentEncoding (qs (page), "/"));
  QUrl url = base.resolved (QUrl (encoded));
  return url_string (url);
}

std::string
canonical_link (const athena_website_entry& website) {
  QUrl base;
  if (!public_site_base (website.public_url, base)) return "";
  return "<link rel=\"canonical\" href=\"" + xml_escape (url_string (base)) +
         "\">\n";
}

std::string
description_meta (const athena_website_entry& website) {
  if (website.description.empty ()) return "";
  return "<meta name=\"description\" content=\"" +
         xml_escape (website.description) + "\">\n";
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
  if (!public_site_base (website.public_url, base)) {
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
  out << "  <url><loc>" << xml_escape (url_string (base))
      << "</loc></url>\n";
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

bool
valid_redirect_shortcut (const std::string& shortcut) {
  if (shortcut.empty () || shortcut[0] != '/' ||
      shortcut.rfind ("//", 0) == 0)
    return false;
  for (unsigned char c: shortcut)
    if (std::isspace (c) || c == '#' || c == '?') return false;
  return true;
}

std::string
redirect_destination (const std::string& html_path) {
  return "/" + ss (QUrl::toPercentEncoding (qs (html_path), "/"));
}

bool
write_redirections (const athena_website_entry& website,
                    const GenerationContext& cx, std::string& error) {
  fs::path target = cx.destination / "_redirects";
  if (!website.generate_redirections) {
    std::error_code ec;
    fs::remove (target, ec);
    return true;
  }

  if (website.redirections.size () > 2000) {
    error = "Cloudflare Pages supports at most 2,000 static redirects per "
            "_redirects file.";
    return false;
  }

  std::set<std::string> shortcuts;
  std::ostringstream out;
  for (const athena_website_redirection& redirection:
       website.redirections) {
    if (!valid_redirect_shortcut (redirection.shortcut)) {
      error = "Invalid website redirection shortcut: " +
              redirection.shortcut;
      return false;
    }
    if (!shortcuts.insert (redirection.shortcut).second) {
      error = "Duplicate website redirection shortcut: " +
              redirection.shortcut;
      return false;
    }
    if (cx.selected_files.count (redirection.document) == 0) {
      error = "Website redirection target is outside the exported range: " +
              redirection.document;
      return false;
    }
    auto html = cx.html_paths.find (redirection.document);
    if (html == cx.html_paths.end ()) {
      error = "Website redirection target has no generated HTML path: " +
              redirection.document;
      return false;
    }
    out << redirection.shortcut << " "
        << redirect_destination (html->second) << " 302\n";
  }
  if (!write_file_bytes (target, out.str ())) {
    error = "Could not write Cloudflare Pages _redirects.";
    return false;
  }
  return true;
}

bool
copy_site_icons (const fs::path& destination, std::string& error) {
  fs::path root = fs::path (tm_to_std (get_env ("ATHENA_PATH"))) /
                  "misc" / "icons" / "libreoffice" / "colibre" /
                  "cmd" / "32";
  const std::map<std::string,std::string> icons = {
    {"vault.svg", "navigator.svg"},
    {"namespace.svg", "viewdatasourcebrowser.svg"},
    {"outline.svg", "outlinemode.svg"},
    {"search.svg", "searchdialog.svg"},
    {"switcher.svg", "sidebar.svg"},
    {"pdf.svg", "exportdirecttopdf.svg"},
    {"close.svg", "cancel.svg"}
  };
  std::error_code ec;
  fs::create_directories (destination, ec);
  if (ec) {
    error = "Could not create website icon folder: " + ec.message ();
    return false;
  }
  for (const auto& icon: icons) {
    fs::copy_file (root / icon.second, destination / icon.first,
                   fs::copy_options::overwrite_existing, ec);
    if (ec) {
      error = "Could not copy website icon " + icon.second + ": " +
              ec.message ();
      return false;
    }
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
    auto pdf = cx.pdf_paths.find (rel);
    file["pdf"] = pdf == cx.pdf_paths.end () ? QString () :
                                                   qs (pdf->second);
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
  root["description"] = qs (website.description);
  root["generateSitemap"] = website.generate_sitemap;
  root["generatePdfs"] = website.generate_pdfs;
  root["generateRedirections"] = website.generate_redirections;

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

bool
copy_favicon (const athena_website_entry& website,
              const GenerationContext& cx, std::string& error) {
  fs::path src;
  if (website.favicon.empty ())
    src = fs::path (tm_to_std (get_env ("ATHENA_PATH"))) /
          "misc" / "images" / "ATHENA-512.png";
  else {
    src = fs::path (website.favicon);
    if (!src.is_absolute ()) src = cx.root / src;
    src = src.lexically_normal ();
  }
  if (!fs::is_regular_file (src)) {
    error = website.favicon.empty () ?
      "Could not find the default ATHENA website favicon." :
      "Could not find website favicon: " + src.string ();
    return false;
  }

  fs::path dest = cx.destination / "icons" / "favicon.png";
  std::error_code ec;
  fs::create_directories (dest.parent_path (), ec);
  if (ec) {
    error = "Could not create website icon directory: " + ec.message ();
    return false;
  }
  fs::copy_file (src, dest, fs::copy_options::overwrite_existing, ec);
  if (ec) {
    error = "Could not copy website favicon: " + ec.message ();
    return false;
  }
  return true;
}

bool
write_site_shell (const athena_website_entry& website,
                  const GenerationContext& cx, std::string& error) {
  std::error_code ec;
  fs::remove_all (cx.destination / "js", ec);
  ec.clear ();
  fs::remove_all (cx.destination / "css", ec);
  ec.clear ();
  fs::create_directories (cx.destination, ec);
  if (ec) {
    error = "Could not create website destination: " + ec.message ();
    return false;
  }

  std::string index;
  if (!website_template_text ("index.html", index)) {
    error = "Could not read website template index.html.";
    return false;
  }

  QJsonObject manifest = site_manifest (website, cx);
  replace_all (index, "{{TITLE}}", website.name);
  replace_all (index, "{{CANONICAL}}", canonical_link (website));
  replace_all (index, "{{DESCRIPTION_META}}", description_meta (website));
  replace_all (index, "{{ENTRY}}",
               json_script_string (ss (manifest["entry"].toString ())));

  if (!write_file_bytes (cx.destination / "index.html", index)) {
    error = "Could not write website entry page.";
    return false;
  }
  std::string manifest_json = ss (QJsonDocument (manifest).toJson (
    QJsonDocument::Compact));
  if (!write_file_bytes (cx.destination / "site-manifest.json",
                         manifest_json)) {
    error = "Could not write website manifest.";
    return false;
  }
  std::string site_data_version = ss (QCryptographicHash::hash (
    QByteArray::fromStdString (manifest_json), QCryptographicHash::Sha256)
    .toHex ().left (16));
  std::string site_data = "window.ATHENA_SITE_DATA=" + manifest_json + ";\n";
  if (!write_file_bytes (cx.destination / "site-data.js", site_data)) {
    error = "Could not write website data script.";
    return false;
  }
  if (!copy_site_icons (cx.destination / "icons", error)) return false;

  for (const std::string& rel: cx.selected_files) {
    auto html = cx.html_paths.find (rel);
    if (html == cx.html_paths.end ()) continue;
    auto title = cx.titles.find (rel);
    auto pdf = cx.pdf_paths.find (rel);
    std::string pdf_href =
      website.generate_pdfs && pdf != cx.pdf_paths.end () ?
      relative_href (html->second, pdf->second) : "";
    if (!decorate_website_document (
          cx.destination / html->second, html->second,
          title == cx.titles.end () ? fs::path (rel).stem ().string () :
                                     title->second,
          pdf_href, site_data_version)) {
      error = "Could not decorate generated document " + html->second + ".";
      return false;
    }
  }
  for (const auto& homepage: cx.namespace_homepages) {
    std::string title = "Namespace homepage: " + homepage.first;
    if (!decorate_website_document (
          cx.destination / homepage.second, homepage.second, title, "",
          site_data_version)) {
      error = "Could not decorate namespace homepage " + homepage.second +
              ".";
      return false;
    }
    std::string technical = "homepages/" +
      safe_namespace_file (homepage.first, true);
    if (fs::exists (cx.destination / technical) &&
        !decorate_website_document (
          cx.destination / technical, technical,
          "Namespace technical summary: " + homepage.first, "",
          site_data_version)) {
      error = "Could not decorate namespace technical summary " +
              technical + ".";
      return false;
    }
  }

  if (!write_sitemap (website, cx, error)) return false;
  if (!write_redirections (website, cx, error)) return false;
  return copy_favicon (website, cx, error);
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
  std::string title = technical ? "Namespace technical summary: " + name :
                                  "Namespace homepage: " + name;
  return export_document_html (rewritten, fs::path ("tmfs://ns/" + tmfs),
                               target, output_rel, title, "", error);
}

} // namespace athena_websites
