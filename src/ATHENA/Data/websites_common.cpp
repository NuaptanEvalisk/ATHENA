/******************************************************************************
* MODULE     : websites_common.cpp
* DESCRIPTION: Common website registry helpers
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "ATHENA/Data/websites_internal.hpp"
#include "ATHENA/Data/vaultfile_json.hpp"

namespace athena_websites {

QString
qs (const std::string& s) {
  return QString::fromUtf8 (s.c_str ());
}

std::string
ss (const QString& s) {
  QByteArray bytes = s.toUtf8 ();
  return std::string (bytes.constData (), (size_t) bytes.size ());
}

std::string
generic_path (fs::path p) {
  return p.lexically_normal ().generic_string ();
}

std::string
clean_relative (const std::string& path) {
  fs::path p (path);
  if (p.is_absolute ()) return generic_path (p);
  fs::path out;
  for (const fs::path& part: p) {
    if (part.empty () || part == ".") continue;
    if (part == "..") continue;
    out /= part;
  }
  return generic_path (out);
}

bool
is_doc_path (const fs::path& path) {
  std::string ext = lower_copy (path.extension ().string ());
  return ext == ".ath" || ext == ".tm";
}

std::string
scheme_quote (const std::string& text) {
  std::string out = "\"";
  for (char c: text) {
    if (c == '\\' || c == '"') out.push_back ('\\');
    out.push_back (c);
  }
  out.push_back ('"');
  return out;
}

std::string
json_script_string (const std::string& text) {
  QString compact = QString::fromUtf8 (
    QJsonDocument (QJsonArray { qs (text) }).toJson (QJsonDocument::Compact));
  if (compact.startsWith ('[')) compact.remove (0, 1);
  if (compact.endsWith (']')) compact.chop (1);
  return ss (compact);
}

std::string
website_css_color_preference (const char* key, const char* fallback) {
  string pref = get_preference (string (key), string (fallback));
  return tm_to_std (get_hex_color (pref));
}

std::string
site_theme_css () {
  std::string link = website_css_color_preference ("locus-color", "#404080");
  std::string visited =
    website_css_color_preference ("visited-color", "#702070");
  std::string selection =
    website_css_color_preference ("gui selection color", "red");
  return ":root{--athena-link-color:" + link +
         ";--athena-visited-color:" + visited +
         ";--athena-selection-color:" + selection + "}\n"
         "::selection{background:var(--athena-selection-color)}\n"
         "::-moz-selection{background:var(--athena-selection-color)}\n";
}

bool
read_vaultfile (const fs::path& root, VaultfileWebsiteInfo& info,
                std::string& error) {
  AthenaVaultfileInfo vault_info;
  if (!athena_vaultfile_read (root, vault_info, error))
    return false;

  info.name = vault_info.name;
  info.map_path = vault_info.map_path;
  info.preferences_path = vault_info.preferences_path;
  info.namespace_db_path = vault_info.namespace_db_path;
  info.startup_page = vault_info.startup_page;
  info.one_time_startup_page = vault_info.one_time_startup_page;
  info.maintenance_summary_path = vault_info.maintenance_summary_path;
  info.rag_index_path = vault_info.rag_index_path;
  info.websites_path = vault_info.websites_path;
  return true;
}

fs::path
registry_path_for (const fs::path& root, const VaultfileWebsiteInfo& info) {
  fs::path p (info.websites_path.empty () ? "websites.json" :
                                      info.websites_path);
  if (p.is_absolute ()) return p.lexically_normal ();
  return (root / p).lexically_normal ();
}

QJsonObject
selector_to_json (const athena_website_selector& selector) {
  QJsonObject obj;
  obj["kind"] = qs (selector.op);
  if (selector.op == "path") obj["path"] = qs (selector.value);
  else if (selector.op == "namespace") obj["name"] = qs (selector.value);
  else {
    QJsonArray children;
    for (const athena_website_selector& child: selector.children)
      children.append (selector_to_json (child));
    obj["children"] = children;
  }
  return obj;
}

athena_website_selector
selector_from_json (const QJsonObject& obj) {
  athena_website_selector selector;
  selector.op = ss (obj.value ("kind").toString ());
  if (selector.op.empty ()) selector.op = ss (obj.value ("op").toString ());
  if (selector.op == "path")
    selector.value = clean_relative (ss (obj.value ("path").toString ()));
  else if (selector.op == "namespace")
    selector.value = ss (obj.value ("name").toString ());
  else {
    QJsonArray children = obj.value ("children").toArray ();
    for (const QJsonValue& value: children)
      if (value.isObject ())
        selector.children.push_back (selector_from_json (value.toObject ()));
  }
  return selector;
}

QJsonObject
website_to_json (const athena_website_entry& website) {
  QJsonObject obj;
  obj["id"] = qs (website.id);
  obj["name"] = qs (website.name);
  obj["selector"] = selector_to_json (website.selector);
  obj["destination"] = qs (website.destination);
  obj["publicUrl"] = qs (website.public_url);
  obj["description"] = qs (website.description);
  obj["favicon"] = qs (website.favicon);
  obj["generateSitemap"] = website.generate_sitemap;
  obj["generatePdfs"] = website.generate_pdfs;
  QJsonObject redirections;
  redirections["enabled"] = website.generate_redirections;
  QJsonArray redirect_items;
  for (const athena_website_redirection& redirection:
       website.redirections) {
    QJsonObject item;
    item["shortcut"] = qs (redirection.shortcut);
    item["path"] = qs (redirection.document);
    redirect_items.append (item);
  }
  redirections["items"] = redirect_items;
  obj["redirections"] = redirections;
  obj["regenerate"] = qs (website.regenerate.empty () ? "manual" :
                                                 website.regenerate);
  QJsonObject entrypoint;
  entrypoint["kind"] = qs (website.entrypoint_kind);
  if (website.entrypoint_kind == "namespace")
    entrypoint["name"] = qs (website.entrypoint_value);
  else entrypoint["path"] = qs (website.entrypoint_value);
  obj["entrypoint"] = entrypoint;
  QJsonObject post;
  post["enabled"] = website.post_command.enabled;
  post["program"] = qs (website.post_command.program);
  post["arguments"] = qs (website.post_command.arguments);
  obj["postCommand"] = post;
  return obj;
}

athena_website_entry
website_from_json (const QJsonObject& obj) {
  athena_website_entry website;
  website.id = ss (obj.value ("id").toString ());
  website.name = ss (obj.value ("name").toString ());
  website.selector = selector_from_json (
    obj.value ("selector").toObject ());
  website.destination = ss (obj.value ("destination").toString ());
  website.public_url = ss (obj.value ("publicUrl").toString (
    obj.value ("baseUrl").toString ()));
  website.description = ss (obj.value ("description").toString ());
  website.favicon = ss (obj.value ("favicon").toString ());
  website.generate_sitemap = obj.contains ("generateSitemap") ?
    obj.value ("generateSitemap").toBool (false) :
    !website.public_url.empty ();
  website.generate_pdfs = obj.value ("generatePdfs").toBool (false);
  QJsonObject redirections = obj.value ("redirections").toObject ();
  website.generate_redirections =
    redirections.value ("enabled").toBool (false);
  for (const QJsonValue& value: redirections.value ("items").toArray ()) {
    if (!value.isObject ()) continue;
    QJsonObject item = value.toObject ();
    athena_website_redirection redirection;
    redirection.shortcut = ss (item.value ("shortcut").toString ());
    redirection.document = clean_relative (
      ss (item.value ("path").toString ()));
    website.redirections.push_back (redirection);
  }
  website.regenerate = ss (obj.value ("regenerate").toString ("manual"));
  QJsonObject entrypoint = obj.value ("entrypoint").toObject ();
  website.entrypoint_kind = ss (entrypoint.value ("kind").toString ());
  if (website.entrypoint_kind == "namespace")
    website.entrypoint_value = ss (entrypoint.value ("name").toString ());
  else {
    website.entrypoint_kind = "file";
    website.entrypoint_value = clean_relative (
      ss (entrypoint.value ("path").toString ()));
  }
  QJsonObject post = obj.value ("postCommand").toObject ();
  website.post_command.enabled = post.value ("enabled").toBool (false);
  website.post_command.program = ss (post.value ("program").toString ());
  website.post_command.arguments = ss (post.value ("arguments").toString ());
  return website;
}

std::string
selector_summary_rec (const athena_website_selector& selector) {
  if (selector.op == "path") return "path:" + selector.value;
  if (selector.op == "namespace") return "namespace:" + selector.value;
  if (selector.op == "not" && selector.children.size () == 1)
    return "NOT (" + selector_summary_rec (selector.children[0]) + ")";
  std::string op = selector.op.empty () ? "?" : selector.op;
  std::transform (op.begin (), op.end (), op.begin (),
                  [] (unsigned char c) { return (char) std::toupper (c); });
  std::vector<std::string> parts;
  for (const athena_website_selector& child: selector.children)
    parts.push_back ("(" + selector_summary_rec (child) + ")");
  std::string out;
  for (size_t i=0; i<parts.size (); i++) {
    if (i != 0) out += " " + op + " ";
    out += parts[i];
  }
  return out.empty () ? "<empty>" : out;
}

std::string
html_rel_for_doc (const std::string& rel) {
  fs::path p (rel);
  p.replace_extension (".html");
  return generic_path (p);
}

std::string
safe_namespace_file (const std::string& name, bool technical) {
  std::string out;
  for (char c: name) {
    if (std::isalnum ((unsigned char) c) || c == '-' || c == '_')
      out.push_back (c);
    else out.push_back ('_');
  }
  if (out.empty ()) out = "namespace";
  if (technical) out += "-technical";
  return out + ".html";
}

std::string
relative_href (const std::string& from_html, const std::string& to_html,
               const std::string& anchor) {
  fs::path from_dir = fs::path ("/") / fs::path (from_html).parent_path ();
  fs::path to = fs::path ("/") / fs::path (to_html);
  std::error_code ec;
  fs::path rel = fs::relative (to.lexically_normal (),
                               from_dir.lexically_normal (), ec);
  std::string out = ec ? generic_path (to_html) : generic_path (rel);
  if (out.empty ()) out = ".";
  if (!anchor.empty ()) out += "#" + anchor;
  return out;
}

bool
website_template_text (const std::string& name, std::string& text) {
  std::vector<fs::path> roots;
  std::string athena_path = tm_to_std (get_env ("ATHENA_PATH"));
  if (!athena_path.empty ())
    roots.push_back (fs::path (athena_path) / "misc" / "websites");
  roots.push_back (fs::current_path () / "ATHENA" / "misc" / "websites");

  for (const fs::path& root: roots) {
    fs::path candidate = root / name;
    if (read_file_bytes (candidate, text)) return true;
  }
  return false;
}

} // namespace athena_websites
