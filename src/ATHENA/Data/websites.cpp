/******************************************************************************
* MODULE     : websites.cpp
* DESCRIPTION: Vault-scoped static website registry and generator
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

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

#include <QDir>
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

namespace fs = std::filesystem;

namespace {

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

struct HtmlExportPreferenceScope {
  std::map<string,string> saved;

  HtmlExportPreferenceScope () {
    set ("texmacs->html:css", "on");
    set ("texmacs->html:mathjax", "on");
    set ("texmacs->html:mathml", "off");
    set ("texmacs->html:images", "on");
    set ("texmacs->html:css-stylesheet", "---");
  }

  ~HtmlExportPreferenceScope () {
    for (const auto& item: saved)
      set_user_preference (item.first, item.second);
  }

  void set (string key, string value) {
    saved[key]= get_user_preference (key, "");
    set_user_preference (key, value);
  }
};

static QString
qs (const std::string& s) {
  return QString::fromUtf8 (s.c_str ());
}

static std::string
ss (const QString& s) {
  QByteArray bytes = s.toUtf8 ();
  return std::string (bytes.constData (), (size_t) bytes.size ());
}

static std::string
generic_path (fs::path p) {
  return p.lexically_normal ().generic_string ();
}

static std::string
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

static bool
is_doc_path (const fs::path& path) {
  std::string ext = lower_copy (path.extension ().string ());
  return ext == ".ath" || ext == ".tm";
}

static std::string
scheme_quote (const std::string& text) {
  std::string out = "\"";
  for (char c: text) {
    if (c == '\\' || c == '"') out.push_back ('\\');
    out.push_back (c);
  }
  out.push_back ('"');
  return out;
}

static std::string
json_script_string (const std::string& text) {
  QString compact = QString::fromUtf8 (
    QJsonDocument (QJsonArray { qs (text) }).toJson (QJsonDocument::Compact));
  if (compact.startsWith ('[')) compact.remove (0, 1);
  if (compact.endsWith (']')) compact.chop (1);
  return ss (compact);
}

static std::vector<std::string>
parse_vaultfile_strings (const std::string& text) {
  std::vector<std::string> values;
  for (size_t i=0; i<text.size (); i++) {
    if (text[i] != '"') continue;
    i++;
    std::string value;
    while (i<text.size ()) {
      char c = text[i++];
      if (c == '\\' && i<text.size ()) {
        value.push_back (text[i++]);
        continue;
      }
      if (c == '"') break;
      value.push_back (c);
    }
    values.push_back (value);
  }
  return values;
}

static bool
read_vaultfile (const fs::path& root, VaultfileWebsiteInfo& info,
                std::string& error) {
  fs::path vault_file = root / "Vaultfile";
  std::string text;
  if (!read_file_bytes (vault_file, text)) {
    error = "Could not read Vaultfile in " + root.string ();
    return false;
  }

  std::vector<std::string> fields = parse_vaultfile_strings (text);
  if (fields.size () < 2) {
    error = "Invalid Vaultfile in " + root.string ();
    return false;
  }

  info.name = fields[0].empty () ? "Vault" : fields[0];
  info.map_path = fields[1].empty () ? "map.tmdb" : fields[1];
  if (fields.size () >= 3) info.preferences_path = fields[2];
  if (fields.size () >= 4 && !fields[3].empty ())
    info.namespace_db_path = fields[3];
  if (fields.size () >= 5) info.startup_page = fields[4];
  if (fields.size () >= 6) info.one_time_startup_page = fields[5];
  if (fields.size () >= 7) info.maintenance_summary_path = fields[6];
  if (fields.size () >= 8 && !fields[7].empty ())
    info.rag_index_path = fields[7];
  if (fields.size () >= 9 && !fields[8].empty ())
    info.websites_path = fields[8];
  return true;
}

static fs::path
registry_path_for (const fs::path& root, const VaultfileWebsiteInfo& info) {
  fs::path p (info.websites_path.empty () ? "websites.json" :
                                      info.websites_path);
  if (p.is_absolute ()) return p.lexically_normal ();
  return (root / p).lexically_normal ();
}

static QJsonObject
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

static athena_website_selector
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

static QJsonObject
website_to_json (const athena_website_entry& website) {
  QJsonObject obj;
  obj["id"] = qs (website.id);
  obj["name"] = qs (website.name);
  obj["selector"] = selector_to_json (website.selector);
  obj["destination"] = qs (website.destination);
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

static athena_website_entry
website_from_json (const QJsonObject& obj) {
  athena_website_entry website;
  website.id = ss (obj.value ("id").toString ());
  website.name = ss (obj.value ("name").toString ());
  website.selector = selector_from_json (
    obj.value ("selector").toObject ());
  website.destination = ss (obj.value ("destination").toString ());
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

static std::string
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

static std::string
file_rel_from_url (url file) {
  url rel = delta (vault_get_root () * url (""), file);
  return clean_relative (tm_to_std (as_unix_string (rel)));
}

static std::set<std::string>
all_document_rels (const fs::path& root) {
  std::set<std::string> out;
  for (const fs::path& doc: scan_documents (root)) {
    fs::path rel = doc.lexically_relative (root);
    if (!rel.empty () && is_doc_path (rel))
      out.insert (generic_path (rel));
  }
  return out;
}

static void
set_union_into (std::set<std::string>& a, const std::set<std::string>& b) {
  a.insert (b.begin (), b.end ());
}

static std::set<std::string>
set_intersection_of (const std::set<std::string>& a,
                     const std::set<std::string>& b) {
  std::set<std::string> out;
  std::set_intersection (a.begin (), a.end (), b.begin (), b.end (),
                         std::inserter (out, out.begin ()));
  return out;
}

static std::set<std::string>
set_difference_of (const std::set<std::string>& a,
                   const std::set<std::string>& b) {
  std::set<std::string> out;
  std::set_difference (a.begin (), a.end (), b.begin (), b.end (),
                       std::inserter (out, out.begin ()));
  return out;
}

static std::set<std::string>
path_selector_files (const fs::path& root, const std::string& path) {
  std::set<std::string> out;
  std::string rel = clean_relative (path);
  fs::path abs = (root / rel).lexically_normal ();
  if (fs::is_regular_file (abs) && is_doc_path (abs)) {
    out.insert (generic_path (fs::path (rel)));
    return out;
  }
  for (const std::string& doc: all_document_rels (root)) {
    if (rel.empty () || doc == rel || starts_with (doc, rel + "/"))
      out.insert (doc);
  }
  return out;
}

static std::map<std::string,std::vector<std::string> >
namespace_children () {
  std::map<std::string,athena_namespace_definition> all;
  for (const athena_namespace_definition& ns: athena_namespaces_list ())
    all[tm_to_std (ns.name)] = ns;

  std::set<std::string> denied;
  for (const athena_namespace_relation& r: athena_namespace_relations_list ()) {
    if (r.decision == "deny")
      denied.insert (tm_to_std (r.parent) + "\n" + tm_to_std (r.child));
  }

  std::map<std::string,std::vector<std::string> > children;
  for (const auto& item: all) {
    const std::string& child_name = item.first;
    const athena_namespace_definition& child = item.second;
    for (int i=0; i<N(child.parents); i++) {
      std::string parent = tm_to_std (child.parents[i]);
      if (all.count (parent) != 0 &&
          denied.count (parent + "\n" + child_name) == 0)
        children[parent].push_back (child_name);
    }
    for (int i=0; i<N(child.derived_parents); i++) {
      std::string parent = tm_to_std (child.derived_parents[i]);
      if (all.count (parent) != 0 &&
          denied.count (parent + "\n" + child_name) == 0)
        children[parent].push_back (child_name);
    }
  }

  for (const athena_namespace_relation& r: athena_namespace_relations_list ()) {
    std::string parent = tm_to_std (r.parent);
    std::string child = tm_to_std (r.child);
    if (all.count (parent) == 0 || all.count (child) == 0) continue;
    std::vector<std::string>& kids = children[parent];
    kids.erase (std::remove (kids.begin (), kids.end (), child), kids.end ());
    if (r.decision == "allow") kids.push_back (child);
  }

  for (auto& item: children) {
    std::sort (item.second.begin (), item.second.end ());
    item.second.erase (std::unique (item.second.begin (), item.second.end ()),
                       item.second.end ());
  }
  return children;
}

static std::set<std::string>
namespace_descendants_inclusive (const std::string& root) {
  std::map<std::string,std::vector<std::string> > children =
    namespace_children ();
  std::set<std::string> seen;
  std::vector<std::string> pending;
  pending.push_back (root);
  while (!pending.empty ()) {
    std::string current = pending.back ();
    pending.pop_back ();
    if (seen.count (current) != 0) continue;
    seen.insert (current);
    for (const std::string& child: children[current])
      pending.push_back (child);
  }
  return seen;
}

static std::set<std::string>
namespace_selector_files (const std::string& name) {
  std::set<std::string> out;
  for (const std::string& ns_name: namespace_descendants_inclusive (name)) {
    athena_namespace_definition ns;
    if (!athena_namespace_get (std_to_tm (ns_name), ns)) continue;
    if (ns.kind == "abstract") continue;
    string error;
    std::vector<athena_namespace_match> members =
      athena_namespace_members (std_to_tm (ns_name), error);
    for (const athena_namespace_match& match: members)
      out.insert (file_rel_from_url (match.file));
  }
  return out;
}

static std::set<std::string>
eval_selector (const athena_website_selector& selector, const fs::path& root,
               const std::set<std::string>& universe) {
  if (selector.op == "path") return path_selector_files (root, selector.value);
  if (selector.op == "namespace") return namespace_selector_files (selector.value);
  if (selector.op == "not") {
    if (selector.children.empty ()) return universe;
    return set_difference_of (universe,
                              eval_selector (selector.children[0], root,
                                             universe));
  }
  if (selector.children.empty ()) return std::set<std::string> ();

  std::string combine_op = selector.op;
  if (combine_op == "nand") combine_op = "and";
  if (combine_op == "nor") combine_op = "or";

  std::set<std::string> out =
    eval_selector (selector.children[0], root, universe);
  for (size_t i=1; i<selector.children.size (); i++) {
    std::set<std::string> rhs =
      eval_selector (selector.children[i], root, universe);
    if (combine_op == "and") out = set_intersection_of (out, rhs);
    else if (combine_op == "xor") {
      std::set<std::string> both = set_intersection_of (out, rhs);
      set_union_into (out, rhs);
      out = set_difference_of (out, both);
    }
    else {
      set_union_into (out, rhs);
    }
  }
  if (selector.op == "nand")
    out = set_difference_of (universe, out);
  else if (selector.op == "nor")
    out = set_difference_of (universe, out);
  return out;
}

static std::string
html_rel_for_doc (const std::string& rel) {
  fs::path p (rel);
  p.replace_extension (".html");
  return generic_path (p);
}

static std::string
safe_namespace_file (const std::string& name, bool technical = false) {
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

static std::string
relative_href (const std::string& from_html, const std::string& to_html,
               const std::string& anchor = "") {
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

static bool
path_begins_with_parent (const fs::path& path) {
  for (const fs::path& part: path) {
    if (part == "..") return true;
    if (part != ".") return false;
  }
  return false;
}

static std::string
without_url_suffix (const std::string& path) {
  size_t pos = path.find_first_of ("?#");
  return pos == std::string::npos ? path : path.substr (0, pos);
}

static std::string
url_suffix_part (const std::string& path) {
  size_t pos = path.find_first_of ("?#");
  return pos == std::string::npos ? std::string () : path.substr (pos);
}

static bool
is_web_image_path (const std::string& path) {
  std::string clean = without_url_suffix (path);
  std::string ext = lower_copy (fs::path (clean).extension ().string ());
  return ext == ".gif" || ext == ".jpg" || ext == ".jpeg" ||
         ext == ".png" || ext == ".bmp" || ext == ".svg";
}

static bool
looks_like_static_asset_path (const std::string& path) {
  std::string clean = without_url_suffix (path);
  std::string ext = lower_copy (fs::path (clean).extension ().string ());
  return !ext.empty () && ext != ".ath" && ext != ".tm" &&
         ext != ".texmacs" && ext != ".html" && ext != ".xhtml";
}

static bool
is_remote_or_special_path (const std::string& path) {
  std::string lower = lower_copy (path);
  return path.empty () || starts_with (path, "$") || starts_with (path, "~") ||
         starts_with (lower, "http:") || starts_with (lower, "https:") ||
         starts_with (lower, "ftp:") || starts_with (lower, "data:") ||
         starts_with (lower, "tmfs:") ||
         path.find ("://") != std::string::npos;
}

static std::string
decode_file_path_text (const std::string& path) {
  QString decoded = QUrl::fromPercentEncoding (qs (path).toUtf8 ());
  return ss (decoded);
}

static std::string
safe_external_asset_rel (const fs::path& path) {
  std::string raw = generic_path (path);
  std::string out = "assets/external/";
  for (char c: raw) {
    if (std::isalnum ((unsigned char) c) || c == '.' || c == '-' ||
        c == '_' || c == '/')
      out.push_back (c == '/' ? '_' : c);
    else out.push_back ('_');
  }
  return out;
}

static bool
vault_relative_asset_path (const fs::path& source, const GenerationContext& cx,
                           std::string& asset_rel) {
  std::error_code ec1, ec2, ec3;
  fs::path source_abs = fs::weakly_canonical (source, ec1);
  fs::path root_abs = fs::weakly_canonical (cx.root, ec2);
  if (!ec1 && !ec2) {
    fs::path rel = fs::relative (source_abs, root_abs, ec3);
    if (!ec3 && !rel.empty () && !path_begins_with_parent (rel)) {
      asset_rel = generic_path (rel);
      return true;
    }
  }
  return false;
}

static bool
copy_website_asset (const fs::path& source, const std::string& asset_rel,
                    const GenerationContext& cx) {
  std::error_code ec;
  fs::path target = (cx.destination / asset_rel).lexically_normal ();
  fs::create_directories (target.parent_path (), ec);
  if (ec) return false;
  std::error_code same_ec;
  if (fs::exists (target) && fs::equivalent (source, target, same_ec))
    return true;
  fs::copy_file (source, target, fs::copy_options::overwrite_existing, ec);
  return !ec;
}

static bool
find_unique_asset_by_filename (const std::string& asset_path,
                               const GenerationContext& cx,
                               fs::path& source) {
  if (!looks_like_static_asset_path (asset_path)) return false;
  fs::path wanted = fs::path (without_url_suffix (asset_path)).filename ();
  if (wanted.empty ()) return false;

  std::error_code ec;
  fs::path found;
  size_t count = 0;
  for (fs::recursive_directory_iterator it (cx.root, ec), end;
       !ec && it != end; it.increment (ec)) {
    if (ec) break;
    if (!it->is_regular_file (ec)) continue;
    if (it->path ().filename () != wanted) continue;
    found = it->path ();
    count++;
    if (count > 1) return false;
  }
  if (count != 1) return false;
  source = found.lexically_normal ();
  return true;
}

static bool
resolve_local_asset_source (const std::string& asset_path,
                            const std::string& source_rel,
                            const GenerationContext& cx,
                            fs::path& source) {
  std::string clean = without_url_suffix (asset_path);
  if (is_remote_or_special_path (clean))
    return false;

  fs::path p (decode_file_path_text (clean));
  source = p.is_absolute () ?
    p.lexically_normal () :
    (cx.root / fs::path (source_rel).parent_path () / p).lexically_normal ();
  std::error_code ec;
  if (fs::is_regular_file (source, ec)) return true;
  return find_unique_asset_by_filename (clean, cx, source);
}

static bool
copy_static_asset (const std::string& asset_path,
                   const std::string& source_rel,
                   const std::string& output_rel,
                   const GenerationContext& cx,
                   std::string& href) {
  fs::path source;
  if (!resolve_local_asset_source (asset_path, source_rel, cx, source))
    return false;

  std::string asset_rel;
  if (!vault_relative_asset_path (source, cx, asset_rel))
    asset_rel = safe_external_asset_rel (source);
  asset_rel = clean_relative (asset_rel);
  if (asset_rel.empty ()) return false;

  if (!copy_website_asset (source, asset_rel, cx)) return false;
  href = relative_href (html_rel_for_doc (output_rel), asset_rel) +
         url_suffix_part (asset_path);
  return true;
}

static bool
copy_static_image (const std::string& image_path,
                   const std::string& source_rel,
                   const std::string& output_rel,
                   const GenerationContext& cx,
                   std::string& href) {
  std::string clean = without_url_suffix (image_path);
  if (!is_web_image_path (clean)) return false;
  return copy_static_asset (image_path, source_rel, output_rel, cx, href);
}

static std::string
shell_call_href (const std::string& function_name, const std::string& arg) {
  return "javascript:" + function_name + "(" +
         json_script_string (arg) + ")";
}

static std::string
document_shell_href (const std::string& html_rel,
                     const std::string& anchor = "") {
  std::string target = html_rel;
  if (!anchor.empty ()) target += "#" + anchor;
  return shell_call_href ("athenaOpenDoc", target);
}

static std::string
tree_string (tree t) {
  if (is_atomic (t)) return tm_to_std (t->label);
  return tm_to_std (tree_as_string (t));
}

static void
append_plain_text (tree t, std::string& out, size_t limit) {
  if (out.size () >= limit) return;
  if (is_atomic (t)) {
    std::string s = tm_to_std (t->label);
    if (!s.empty ()) {
      if (!out.empty ()) out.push_back (' ');
      out += s;
      if (out.size () > limit) out.resize (limit);
    }
    return;
  }
  for (int i=0; i<N(t); i++) append_plain_text (t[i], out, limit);
}

static std::string
document_search_text (tree doc) {
  std::string out;
  append_plain_text (doc, out, 20000);
  return out;
}

static std::string
document_title (tree t, const std::string& fallback) {
  if (is_atomic (t)) return fallback;
  if ((is_compound (t, "doc-title", 1) ||
       is_compound (t, "title", 1) ||
       is_compound (t, "tmdoc-title", 1) ||
       is_compound (t, "tmweb-title", 1)) && N(t) >= 1) {
    std::string title = tree_string (t[0]);
    if (!title.empty ()) return title;
  }
  for (int i=0; i<N(t); i++) {
    std::string title = document_title (t[i], "");
    if (!title.empty ()) return title;
  }
  return fallback;
}

static bool
is_absolute_image_path (const std::string& path) {
  return path.empty () || starts_with (path, "/") || starts_with (path, "~") ||
         starts_with (path, "$") || path.find ("://") != std::string::npos;
}

static std::string
rebase_image_path (const std::string& path, const fs::path& source_dir) {
  if (is_absolute_image_path (path)) return path;
  return (source_dir / path).lexically_normal ().string ();
}

static tree
rebase_images (tree t, const fs::path& source_dir) {
  if (is_atomic (t)) return copy (t);
  tree r (L(t));
  for (int i=0; i<N(t); i++) {
    if (i == 0 && is_func (t, IMAGE) && is_atomic (t[i]))
      r << tree (std_to_tm (rebase_image_path (tm_to_std (t[i]->label),
                                               source_dir)));
    else r << rebase_images (t[i], source_dir);
  }
  return r;
}

static tree
strip_labels (tree t) {
  if (is_atomic (t)) return copy (t);
  if (is_func (t, LABEL, 1)) return tree (CONCAT);
  tree r (L(t));
  for (int i=0; i<N(t); i++) r << strip_labels (t[i]);
  return r;
}

static bool
find_label_path_rec (tree t, const std::string& label,
                     std::vector<int>& current, std::vector<int>& found) {
  if (!is_atomic (t) && is_func (t, LABEL, 1) && tree_string (t[0]) == label) {
    found = current;
    return true;
  }
  if (is_atomic (t)) return false;
  for (int i=0; i<N(t); i++) {
    current.push_back (i);
    if (find_label_path_rec (t[i], label, current, found)) return true;
    current.pop_back ();
  }
  return false;
}

static bool
find_label_path (tree t, const std::string& label, std::vector<int>& found) {
  if (label.empty ()) return false;
  std::vector<int> current;
  return find_label_path_rec (t, label, current, found);
}

static tree
subtree_at (tree t, const std::vector<int>& path) {
  tree out = t;
  for (int index: path) out = out[index];
  return out;
}

static std::vector<int>
common_prefix (const std::vector<int>& a, const std::vector<int>& b) {
  std::vector<int> out;
  size_t n = std::min (a.size (), b.size ());
  for (size_t i=0; i<n; i++) {
    if (a[i] != b[i]) break;
    out.push_back (a[i]);
  }
  return out;
}

static tree
document_children_or_self (tree t) {
  if (is_func (t, DOCUMENT)) return copy (t);
  tree doc (DOCUMENT);
  doc << copy (t);
  return doc;
}

static tree
extract_transclusion_range (tree doc, const std::string& begin,
                            const std::string& end,
                            const fs::path& source_dir) {
  if (begin.empty () && end.empty ())
    return rebase_images (strip_labels (document_children_or_self (doc)),
                          source_dir);

  std::vector<int> p_begin;
  std::vector<int> p_end;
  if (!find_label_path (doc, begin, p_begin)) return tree (DOCUMENT);
  bool has_end = !end.empty () && find_label_path (doc, end, p_end);
  if (!end.empty () && !has_end) return tree (DOCUMENT);

  std::vector<int> prefix = has_end ? common_prefix (p_begin, p_end) :
                                      p_begin;
  if (!has_end && !prefix.empty ()) prefix.pop_back ();

  tree parent = subtree_at (doc, prefix);
  std::vector<int> rem_begin (p_begin.begin () + (long) prefix.size (),
                              p_begin.end ());
  std::vector<int> rem_end;
  if (has_end)
    rem_end.assign (p_end.begin () + (long) prefix.size (), p_end.end ());

  int i_begin = rem_begin.empty () ? 0 : rem_begin[0];
  int i_end = (!has_end || rem_end.empty ()) ? N(parent) - 1 : rem_end[0];
  tree out (DOCUMENT);
  if (i_begin <= i_end) {
    for (int i=i_begin; i<=i_end && i<N(parent); i++)
      out << rebase_images (strip_labels (parent[i]), source_dir);
  }
  return out;
}

static bool
decode_wikilink_target (const std::string& destination,
                        std::string& rel_path, std::string& anchor) {
  const std::string prefix = "tmfs://wikilink/";
  if (!starts_with (lower_copy (destination), prefix)) return false;
  std::string rest = destination.substr (prefix.size ());
  size_t slash = rest.find ('/');
  std::string uuid = slash == std::string::npos ? rest : rest.substr (0, slash);
  tree node = vault_get_node (std_to_tm (ss (QUrl::fromPercentEncoding (
    qs (uuid).toUtf8 ()))));
  if (!is_func (node, TUPLE) || N(node) < 3) return true;
  rel_path = clean_relative (tree_string (node[0]));
  std::string anchor_begin = tree_string (node[1]);
  std::string anchor_end = tree_string (node[2]);
  anchor = anchor_end.empty () ? anchor_begin : anchor_end;
  return true;
}

static bool
decode_wikilink_file_hint (const std::string& destination,
                           std::string& hint) {
  const std::string prefix = "tmfs://wikilink/";
  if (!starts_with (lower_copy (destination), prefix)) return false;
  std::string rest = destination.substr (prefix.size ());
  size_t slash = rest.find ('/');
  if (slash == std::string::npos) return false;
  std::string encoded = rest.substr (slash + 1);
  size_t next = encoded.find ('/');
  if (next != std::string::npos) encoded = encoded.substr (0, next);
  if (encoded.empty ()) return false;
  hint = ss (QUrl::fromPercentEncoding (qs (encoded).toUtf8 ()));
  return !hint.empty ();
}

static std::string
modal_href (const std::string& label) {
  return shell_call_href ("athenaMissingTarget", label);
}

static bool
local_document_target (const std::string& destination,
                       const std::string& current_rel,
                       std::string& rel_path,
                       std::string& anchor) {
  if (starts_with (destination, "http:") ||
      starts_with (destination, "https:") ||
      starts_with (destination, "ftp:") ||
      starts_with (destination, "mailto:") ||
      starts_with (destination, "#") ||
      starts_with (destination, "javascript:"))
    return false;

  std::string target = destination;
  size_t hash = target.find ('#');
  if (hash != std::string::npos) {
    anchor = target.substr (hash + 1);
    target = target.substr (0, hash);
  }
  if (!(ends_with (target, ".ath") || ends_with (target, ".tm"))) return false;

  fs::path current_dir = fs::path (current_rel).parent_path ();
  rel_path = clean_relative (generic_path (current_dir / target));
  return true;
}

static tree
rewrite_static_links (tree t, const std::string& source_rel,
                      const std::string& output_rel,
                      const GenerationContext& cx);

static tree
rewrite_link_like (tree t, const std::string& source_rel,
                   const std::string& output_rel,
                   const GenerationContext& cx, bool force_plain_hlink) {
  tree body = rewrite_static_links (t[0], source_rel, output_rel, cx);
  std::string destination = tree_string (t[1]);
  std::string rel_path;
  std::string anchor;
  bool handled = decode_wikilink_target (destination, rel_path, anchor);
  if (!handled)
    handled = local_document_target (destination, source_rel, rel_path, anchor);

  std::string next = destination;
  if (handled) {
    if (!rel_path.empty () && cx.selected_files.count (rel_path) != 0) {
      next = document_shell_href (html_rel_for_doc (rel_path), anchor);
    }
    else {
      std::string hint;
      std::string href;
      if (decode_wikilink_file_hint (destination, hint) &&
          copy_static_asset (hint, source_rel, output_rel, cx, href))
        next = href;
      else next = modal_href (rel_path.empty () ? destination : rel_path);
    }
  }
  else {
    std::string href;
    if (copy_static_asset (destination, source_rel, output_rel, cx, href))
      next = href;
  }

  (void) force_plain_hlink;
  return compound ("hlink", body, tree (std_to_tm (next)));
}

static std::string
transclusion_source_rel (tree t) {
  if (N(t) < 1) return "";
  tree node = vault_get_node (std_to_tm (tree_string (t[0])));
  if (!is_func (node, TUPLE) || N(node) < 1) return "";
  return clean_relative (tree_string (node[0]));
}

static tree
resolve_transclusion (tree t, const std::string& source_rel,
                      const std::string& output_rel,
                      const GenerationContext& cx) {
  if (N(t) != 4) return copy (t);

  static tmscm fun= scm_lookup_string ("vault-resolve-transclude");
  tmscm res_scm= call_scheme (fun,
                              tree_to_tmscm (t[0]),
                              tree_to_tmscm (t[1]),
                              tree_to_tmscm (t[2]),
                              tree_to_tmscm (t[3]));
  tree content= tmscm_to_content (res_scm);
  std::string transcluded_rel = transclusion_source_rel (t);
  if (transcluded_rel.empty ()) transcluded_rel = source_rel;
  return rewrite_static_links (content, transcluded_rel, output_rel, cx);
}

static tree
rewrite_static_links (tree t, const std::string& source_rel,
                      const std::string& output_rel,
                      const GenerationContext& cx) {
  if (is_atomic (t)) return copy (t);
  if (is_func (t, HLINK, 2))
    return rewrite_link_like (t, source_rel, output_rel, cx, false);
  if (is_compound (t, "cardlink", 2))
    return rewrite_link_like (t, source_rel, output_rel, cx, true);
  if (is_compound (t, "transclude") && N(t) >= 4)
    return resolve_transclusion (t, source_rel, output_rel, cx);
  if (is_func (t, IMAGE) && N(t) > 0 && is_atomic (t[0])) {
    std::string href;
    if (copy_static_image (tree_string (t[0]), source_rel, output_rel, cx,
                           href)) {
      tree r (L(t));
      r << tree (std_to_tm (href));
      for (int i=1; i<N(t); i++)
        r << rewrite_static_links (t[i], source_rel, output_rel, cx);
      return r;
    }
  }

  tree r (L(t));
  for (int i=0; i<N(t); i++)
    r << rewrite_static_links (t[i], source_rel, output_rel, cx);
  return r;
}

static void
set_current_save_urls (const fs::path& source, const fs::path& target) {
  eval ("(set! current-save-source (string->url " *
        std_to_tm (scheme_quote (source.string ())) * "))");
  eval ("(set! current-save-target (string->url " *
        std_to_tm (scheme_quote (target.string ())) * "))");
}

static std::string
document_bridge_script () {
  return
    "<script>\n"
    "(function(){\n"
    "  function send(type,payload){\n"
    "    if(window.parent&&window.parent!==window)\n"
    "      window.parent.postMessage(Object.assign({type:type},payload),'*');\n"
    "  }\n"
    "  window.athenaMissingTarget=function(target){\n"
    "    send('athena-missing-target',{target:String(target)});\n"
    "  };\n"
    "  window.athenaOpenDoc=function(path){\n"
    "    send('athena-open-doc',{path:String(path)});\n"
    "  };\n"
    "})();\n"
    "</script>\n";
}

static bool
inject_document_bridge (const fs::path& target) {
  std::string html;
  if (!read_file_bytes (target, html)) return false;
  std::string script = document_bridge_script ();
  if (html.find ("athena-missing-target") != std::string::npos)
    return true;
  size_t head = html.find ("</head>");
  if (head != std::string::npos)
    html.insert (head, script);
  else {
    size_t body = html.find ("<body");
    if (body != std::string::npos) {
      size_t end = html.find ('>', body);
      if (end != std::string::npos) html.insert (end + 1, script);
      else html.insert (0, script);
    }
    else html.insert (0, script);
  }
  return write_file_bytes (target, html);
}

static bool
export_document_html (tree doc, const fs::path& source,
                      const fs::path& target, std::string& error) {
  std::error_code ec;
  fs::create_directories (target.parent_path (), ec);
  if (ec) {
    error = "Could not create " + target.parent_path ().string () + ": " +
            ec.message ();
    return false;
  }
  set_current_save_urls (source, target);
  HtmlExportPreferenceScope html_scope;
  if (export_tree (doc, url_system (std_to_tm (target.string ())), "html")) {
    error = "HTML export failed for " + source.string ();
    return false;
  }
  if (!inject_document_bridge (target)) {
    error = "Could not inject website bridge into " + target.string ();
    return false;
  }
  return true;
}

static void
website_log (const std::string& message) {
  std::cout << "ATHENA_WEBSITE_LOG " << message << std::endl;
}

static void
website_progress (size_t current, size_t total, const std::string& phase,
                  const std::string& item) {
  std::cout << "ATHENA_WEBSITE_PROGRESS " << current << " " << total
            << " " << phase << " " << item << std::endl;
}

static QJsonObject
site_manifest (const athena_website_entry& website,
               const GenerationContext& cx) {
  QJsonObject root;
  root["title"] = qs (website.name);

  QJsonArray files;
  for (const std::string& rel: cx.selected_files) {
    QJsonObject file;
    file["path"] = qs (rel);
    file["html"] = qs (cx.html_paths.at (rel));
    auto title = cx.titles.find (rel);
    file["title"] = title == cx.titles.end () ? qs (rel) : qs (title->second);
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

static std::string
site_css () {
  return
    "html,body{height:100%;margin:0;background:#b7b7b7;color:#111;"
    "font:14px 'Courier New',monospace}button,input,select{font:inherit}"
    ".desktop{position:relative;height:calc(100% - 28px);overflow:hidden;"
    "background:#9aa0a6}"
    ".taskbar{height:27px;display:flex;gap:4px;align-items:center;"
    "background:#c8c8c8;border-top:1px solid #777;padding:0 6px;overflow:hidden}"
    ".task{height:21px;max-width:220px;overflow:hidden;text-overflow:ellipsis;"
    "white-space:nowrap;border:2px outset #eee;background:#ddd;color:#111;"
    "padding:0 8px;cursor:pointer}.task.active{border-style:inset;background:#f2f2f2}"
    ".win{position:absolute;background:#eee;border:2px solid #333;"
    "box-shadow:4px 4px 0 #666;min-width:220px;min-height:120px}"
    ".win.minimized,.win.closed{display:none}.win.maximized{box-shadow:none}"
    ".title{height:24px;background:#0a246a;color:white;display:flex;"
    "align-items:center;justify-content:space-between;padding:0 6px;"
    "cursor:move;font-weight:bold}.title-left{display:flex;align-items:center;"
    "gap:6px;min-width:0}.caption{overflow:hidden;text-overflow:ellipsis;"
    "white-space:nowrap}.buttons,.doc-nav{display:flex;gap:3px}"
    ".ctrl{position:relative;height:18px;width:22px;border:2px outset #eee;"
    "background:#d8d8d8;padding:0;margin:0;cursor:pointer;color:#111}"
    ".ctrl:disabled{cursor:default;opacity:.45}"
    ".ctrl:active{border-style:inset}.ctrl:before,.ctrl:after{content:'';"
    "position:absolute;left:50%;top:50%;transform:translate(-50%,-50%);"
    "box-sizing:border-box}.ctrl.min:before{width:11px;height:2px;background:#111;"
    "top:65%}.ctrl.max:before{width:10px;height:9px;border:2px solid #111;"
    "background:transparent}.ctrl.restore{display:none}.win.maximized "
    ".ctrl.restore{display:inline-block}.win.maximized .ctrl.max{display:none}"
    ".ctrl.restore:before{width:10px;height:8px;border:2px solid #111;"
    "background:#d8d8d8;left:55%;top:45%}.ctrl.restore:after{width:10px;"
    "height:8px;border:2px solid #111;background:#d8d8d8;left:45%;top:58%}"
    ".ctrl.close:before{width:13px;height:2px;background:#111;transform:"
    "translate(-50%,-50%) rotate(45deg)}.ctrl.close:after{width:13px;height:2px;"
    "background:#111;transform:translate(-50%,-50%) rotate(-45deg)}"
    ".ctrl.back:before{width:0;height:0;border-top:5px solid transparent;"
    "border-bottom:5px solid transparent;border-right:8px solid #111;left:45%}"
    ".ctrl.forward:before{width:0;height:0;border-top:5px solid transparent;"
    "border-bottom:5px solid transparent;border-left:8px solid #111;left:55%}"
    ".content{height:calc(100% - 24px);"
    "overflow:auto;background:#f7f7f7}.tree{list-style:none;margin:0;"
    "padding:8px}.tree li{margin:2px 0}.tree a{color:#004c99;"
    "text-decoration:none}.tree a:hover{text-decoration:underline}"
    ".pane-tools{display:flex;gap:6px;padding:6px;background:#ddd;"
    "border-bottom:1px solid #888}.pane-tools input{flex:1;background:white;"
    "border:1px solid #555;padding:2px 4px}.results{list-style:none;margin:0;"
    "padding:4px}.results li{padding:4px;border-bottom:1px solid #ccc;"
    "cursor:pointer}.results li.active,.results li:hover{background:#c7dfd5}"
    ".result-title{font-weight:bold}.result-path{color:#555;font-size:12px}"
    ".result-snippet{margin-top:2px;color:#222;white-space:normal}"
    "#viewer iframe{width:100%;height:100%;border:0;background:white}"
    "#viewer .content{overflow:hidden}.resize{position:absolute;right:0;"
    "bottom:0;width:12px;height:12px;background:#555;cursor:nwse-resize}"
    ".modal{position:absolute;left:50%;top:50%;transform:translate(-50%,-50%);"
    "background:#f0f0f0;border:2px solid #222;box-shadow:4px 4px 0 #555;"
    "padding:14px;z-index:9999;display:none;max-width:420px}"
    ".modal button{float:right;margin-top:10px}";
}

static std::string
site_data_js (const QJsonObject& manifest) {
  std::string data = ss (QJsonDocument (manifest).toJson (
    QJsonDocument::Compact));
  return std::string ("window.ATHENA_SITE_DATA=") + data + ";\n";
}

static std::string
window_manager_js () {
  return
    R"JS(
var athenaTopZ=20;
var athenaDocHistory=[];
var athenaDocIndex=-1;
var athenaBooting=true;
var athenaRestoringState=false;
function byId(id){return document.getElementById(id);}
function athenaTaskId(id){return 'task-'+id;}
function athenaTaskFor(id){
  return byId(athenaTaskId(id));
}
function athenaStorageKey(){
  var data=window.ATHENA_SITE_DATA || {};
  return data.storageKey || ('athena-website:'+location.pathname);
}
function athenaLoadState(){
  try{
    var raw=localStorage.getItem(athenaStorageKey());
    return raw ? JSON.parse(raw) : null;
  }
  catch(e){return null;}
}
function athenaWindowState(win){
  return {
    left:win.style.left,
    top:win.style.top,
    width:win.style.width,
    height:win.style.height,
    zIndex:win.style.zIndex || '',
    closed:win.classList.contains('closed'),
    minimized:win.classList.contains('minimized'),
    maximized:win.classList.contains('maximized')
  };
}
function athenaSaveState(){
  if(athenaBooting || athenaRestoringState) return;
  var windows={};
  ['vault','namespaces','global-search','quick-switcher','viewer'].forEach(function(id){
    var win=byId(id);
    if(win) windows[id]=athenaWindowState(win);
  });
  var current=athenaDocIndex>=0 ? athenaDocHistory[athenaDocIndex] :
    (byId('docframe') ? byId('docframe').getAttribute('src') : '');
  try{
    localStorage.setItem(athenaStorageKey(),JSON.stringify({
      version:1,
      currentDoc:current || '',
      docHistory:athenaDocHistory,
      docIndex:athenaDocIndex,
      topZ:athenaTopZ,
      windows:windows
    }));
  }
  catch(e){}
}
function athenaApplyWindowState(id,state){
  var win=byId(id);
  if(!win || !state) return;
  ['left','top','width','height','zIndex'].forEach(function(name){
    if(state[name]) win.style[name]=state[name];
  });
  win.classList.toggle('closed',!!state.closed);
  win.classList.toggle('minimized',!!state.minimized);
  win.classList.toggle('maximized',!!state.maximized);
  var task=athenaTaskFor(id);
  if(task) task.classList.toggle('active',!state.closed && !state.minimized);
}
function athenaApplySavedState(state){
  if(!state || !state.windows) return false;
  athenaRestoringState=true;
  athenaTopZ=state.topZ || athenaTopZ;
  Object.keys(state.windows).forEach(function(id){
    athenaApplyWindowState(id,state.windows[id]);
  });
  athenaDocHistory=Array.isArray(state.docHistory) ? state.docHistory.slice() : [];
  athenaDocIndex=typeof state.docIndex==='number' ? state.docIndex : -1;
  athenaRestoringState=false;
  return true;
}
function athenaDefaultLayout(){
  ['vault','namespaces','global-search','quick-switcher'].forEach(function(id){
    var win=byId(id);
    if(win && win.athenaClose) win.athenaClose({silent:true});
  });
  var viewer=byId('viewer');
  if(viewer && viewer.athenaMaximize) viewer.athenaMaximize({silent:true});
  athenaFocusWindow(viewer);
}
function athenaOpenInitialDoc(path,state){
  if(state && Array.isArray(state.docHistory) && state.docHistory.length){
    athenaDocHistory=state.docHistory.slice();
    athenaDocIndex=typeof state.docIndex==='number' ? state.docIndex :
      athenaDocHistory.indexOf(path);
    if(athenaDocIndex<0 || athenaDocIndex>=athenaDocHistory.length)
      athenaDocIndex=athenaDocHistory.length-1;
    openDoc(athenaDocHistory[athenaDocIndex],{
      noHistory:true,
      preserveViewerState:true
    });
  }
  else openDoc(path || 'about:blank');
}
function athenaFocusWindow(win){
  if(!win || win.classList.contains('closed')) return;
  athenaTopZ+=1;
  win.style.zIndex=athenaTopZ;
  document.querySelectorAll('.task').forEach(function(t){t.classList.remove('active');});
  var task=athenaTaskFor(win.id);
  if(task) task.classList.add('active');
  athenaSaveState();
}
function athenaDocStem(path){
  if(!path || path==='about:blank') return 'Document';
  var clean=String(path).split('#')[0].split('?')[0];
  var data=window.ATHENA_SITE_DATA || {};
  var files=data.files || [];
  for(var i=0;i<files.length;i++){
    if(files[i].html===clean) return files[i].title || athenaDocStem(files[i].path);
  }
  var name=clean.substring(clean.lastIndexOf('/')+1);
  var dot=name.lastIndexOf('.');
  if(dot>0) name=name.substring(0,dot);
  return name || 'Document';
}
function athenaSetDocTitle(path){
  var title=athenaDocStem(path);
  var cap=byId('viewer-title');
  if(cap) cap.textContent=title;
  var task=athenaTaskFor('viewer');
  if(task) task.textContent=title;
}
function athenaUpdateDocNav(){
  var back=byId('doc-back'), forward=byId('doc-forward');
  if(back) back.disabled=athenaDocIndex<=0;
  if(forward) forward.disabled=athenaDocIndex<0 ||
    athenaDocIndex>=athenaDocHistory.length-1;
}
function openDoc(path, options){
  options=options||{};
  byId('docframe').src=path;
  if(!options.noHistory){
    if(athenaDocIndex<athenaDocHistory.length-1)
      athenaDocHistory=athenaDocHistory.slice(0,athenaDocIndex+1);
    if(athenaDocHistory[athenaDocHistory.length-1]!==path){
      athenaDocHistory.push(path);
      athenaDocIndex=athenaDocHistory.length-1;
    }
  }
  athenaSetDocTitle(path);
  athenaUpdateDocNav();
  var viewer=byId('viewer');
  if(!options.preserveViewerState){
    if(viewer){
      viewer.classList.remove('closed');
      viewer.classList.remove('minimized');
      var task=athenaTaskFor('viewer');
      if(task) task.classList.add('active');
    }
    athenaFocusWindow(viewer);
  }
  athenaSaveState();
}
function athenaOpenDoc(path){openDoc(path);}
function athenaDocBack(){
  if(athenaDocIndex<=0) return;
  athenaDocIndex-=1;
  openDoc(athenaDocHistory[athenaDocIndex],{noHistory:true});
}
function athenaDocForward(){
  if(athenaDocIndex>=athenaDocHistory.length-1) return;
  athenaDocIndex+=1;
  openDoc(athenaDocHistory[athenaDocIndex],{noHistory:true});
}
function athenaMissingTarget(target){
  var m=byId('missing-modal');
  byId('missing-text').textContent='Destination is not in the exported site: '+target;
  m.style.display='block';
  m.style.zIndex=++athenaTopZ;
}
function closeMissing(){byId('missing-modal').style.display='none';}
window.addEventListener('message',function(ev){
  var data=ev.data || {};
  if(data.type==='athena-missing-target') athenaMissingTarget(data.target || '');
  else if(data.type==='athena-open-doc') athenaOpenDoc(data.path || 'about:blank');
});
function installWindow(id){
  var win=byId(id), title=win.querySelector('.title'), resizing=false, moving=false;
  var sx=0, sy=0, ox=0, oy=0, ow=0, oh=0, restore=null;
  var defaultRect={
    left:win.style.left,
    top:win.style.top,
    width:win.style.width,
    height:win.style.height
  };
  function taskButton(){
    var bar=byId('taskbar'), btn=byId('task-'+id);
    if(btn) return btn;
    btn=document.createElement('button');
    btn.id='task-'+id; btn.className='task active';
    btn.textContent=title.querySelector('.caption').textContent;
    btn.onclick=function(){restoreWindow();};
    bar.appendChild(btn);
    return btn;
  }
  function activateTask(active){taskButton().classList.toggle('active',active);}
  function restoreWindow(){
    win.classList.remove('closed');
    win.classList.remove('minimized');
    activateTask(true);
    if(win.classList.contains('maximized')){
      var rect=restore || defaultRect;
      win.classList.remove('maximized');
      win.style.left=rect.left; win.style.top=rect.top;
      win.style.width=rect.width; win.style.height=rect.height;
      restore=null;
    }
    athenaFocusWindow(win);
    athenaSaveState();
  }
  function maximizeWindow(options){
    options=options||{};
    if(win.classList.contains('maximized')) return;
    restore={left:win.style.left,top:win.style.top,width:win.style.width,height:win.style.height};
    win.classList.add('maximized'); win.classList.remove('minimized');
    win.classList.remove('closed');
    win.style.left='8px'; win.style.top='8px';
    win.style.width='calc(100% - 24px)'; win.style.height='calc(100% - 24px)';
    activateTask(true);
    athenaFocusWindow(win);
    if(!options.silent) athenaSaveState();
  }
  function minimizeWindow(){
    win.classList.add('minimized');
    activateTask(false);
    athenaSaveState();
  }
  function closeWindow(options){
    options=options||{};
    win.classList.add('closed');
    win.classList.remove('minimized');
    activateTask(false);
    if(!options.silent) athenaSaveState();
  }
  win.athenaRestore=restoreWindow;
  win.athenaMaximize=maximizeWindow;
  win.athenaMinimize=minimizeWindow;
  win.athenaClose=closeWindow;
  title.onmousedown=function(e){
    if(e.target.closest('button')) return;
    if(win.classList.contains('maximized')) return;
    athenaFocusWindow(win);
    moving=true;sx=e.clientX;sy=e.clientY;ox=win.offsetLeft;oy=win.offsetTop;e.preventDefault();
  };
  win.onmousedown=function(){athenaFocusWindow(win);};
  win.querySelector('.min').onclick=minimizeWindow;
  win.querySelector('.max').onclick=maximizeWindow;
  win.querySelector('.restore').onclick=restoreWindow;
  win.querySelector('.close').onclick=closeWindow;
  win.querySelector('.resize').onmousedown=function(e){
    if(win.classList.contains('maximized')) return;
    athenaFocusWindow(win);
    resizing=true;sx=e.clientX;sy=e.clientY;ow=win.offsetWidth;oh=win.offsetHeight;e.preventDefault();
  };
  taskButton();
  athenaFocusWindow(win);
  document.addEventListener('mousemove',function(e){
    if(moving){win.style.left=(ox+e.clientX-sx)+'px';win.style.top=(oy+e.clientY-sy)+'px';}
    if(resizing){win.style.width=Math.max(220,ow+e.clientX-sx)+'px';win.style.height=Math.max(120,oh+e.clientY-sy)+'px';}
  });
  document.addEventListener('mouseup',function(){
    if(moving || resizing) athenaSaveState();
    moving=false;resizing=false;
  });
}
)JS";
}

static std::string
explorers_js () {
  return
    R"JS(
function treeList(items, makeHref){
  var ul=document.createElement('ul'); ul.className='tree';
  items.forEach(function(item){
    var li=document.createElement('li'); var a=document.createElement('a');
    a.href='#'; a.textContent=item.title||item.name||item.path;
    a.onclick=function(ev){ev.preventDefault(); athenaOpenDoc(makeHref(item));};
    li.appendChild(a); ul.appendChild(li);
  });
  return ul;
}
function initExplorers(){
  var d=window.ATHENA_SITE_DATA;
  byId('vault-content').appendChild(treeList(d.files,function(x){return x.html;}));
  byId('namespace-content').appendChild(treeList(d.namespaces,function(x){return x.homepage;}));
}
)JS";
}

static std::string
search_js () {
  return
    R"JS(
function renderResults(container, items){
  container.innerHTML='';
  var ul=document.createElement('ul'); ul.className='results';
  items.forEach(function(item,index){
    var li=document.createElement('li');
    if(index===0) li.className='active';
    li.tabIndex=0;
    li.onclick=function(){athenaOpenDoc(item.html||item.homepage);};
    var title=document.createElement('div'); title.className='result-title';
    title.textContent=item.title||item.name||item.path;
    var path=document.createElement('div'); path.className='result-path';
    path.textContent=item.path||item.name;
    li.appendChild(title); li.appendChild(path);
    if(item.snippet){
      var snip=document.createElement('div'); snip.className='result-snippet';
      snip.textContent=item.snippet; li.appendChild(snip);
    }
    ul.appendChild(li);
  });
  container.appendChild(ul);
}
function searchFiles(query){
  query=query.toLowerCase().trim();
  var files=window.ATHENA_SITE_DATA.files || [];
  if(!query) return files.slice(0,25).map(function(f){return Object.assign({},f,{snippet:''});});
  return files.map(function(f){
    var hay=((f.title||'')+' '+(f.path||'')+' '+(f.searchText||'')).toLowerCase();
    var hit=hay.indexOf(query);
    if(hit<0) return null;
    var text=f.searchText||'';
    var p=text.toLowerCase().indexOf(query);
    var snippet=p<0?'':text.substring(Math.max(0,p-60),Math.min(text.length,p+160));
    return Object.assign({},f,{snippet:snippet});
  }).filter(Boolean).slice(0,50);
}
function initGlobalSearch(){
  var input=byId('global-search-input');
  var results=byId('global-search-results');
  function update(){renderResults(results,searchFiles(input.value));}
  input.oninput=update;
  input.onkeydown=function(ev){
    if(ev.key==='Enter'){
      var first=results.querySelector('li');
      if(first){first.click(); ev.preventDefault();}
    }
  };
  update();
}
)JS";
}

static std::string
quick_switcher_js () {
  return
    R"JS(
function quickItems(query){
  query=query.toLowerCase().trim();
  var files=(window.ATHENA_SITE_DATA.files||[]).map(function(f){
    return {title:f.title||f.path,path:f.path,html:f.html,kind:'file'};
  });
  var namespaces=(window.ATHENA_SITE_DATA.namespaces||[]).map(function(n){
    return {title:n.name,path:n.name,html:n.homepage,kind:'namespace'};
  });
  var all=files.concat(namespaces);
  if(!query) return all.slice(0,40);
  return all.filter(function(x){
    return (x.title+' '+x.path+' '+x.kind).toLowerCase().indexOf(query)>=0;
  }).slice(0,40);
}
function initQuickSwitcher(){
  var input=byId('quick-switcher-input');
  var results=byId('quick-switcher-results');
  function update(){renderResults(results,quickItems(input.value));}
  input.oninput=update;
  input.onkeydown=function(ev){
    var items=Array.prototype.slice.call(results.querySelectorAll('li'));
    var active=results.querySelector('li.active');
    var index=items.indexOf(active);
    if(ev.key==='ArrowDown' && items.length){
      if(active) active.classList.remove('active');
      items[Math.min(items.length-1,index+1)].classList.add('active');
      ev.preventDefault();
    }
    else if(ev.key==='ArrowUp' && items.length){
      if(active) active.classList.remove('active');
      items[Math.max(0,index-1)].classList.add('active');
      ev.preventDefault();
    }
    else if(ev.key==='Enter' && active){
      active.click(); ev.preventDefault();
    }
  };
  update();
}
)JS";
}

static std::string
app_js () {
  return
    R"JS(
window.onload=function(){
  initExplorers();
  initGlobalSearch();
  initQuickSwitcher();
  installWindow('vault');
  installWindow('namespaces');
  installWindow('global-search');
  installWindow('quick-switcher');
  installWindow('viewer');
  byId('doc-back').onclick=athenaDocBack;
  byId('doc-forward').onclick=athenaDocForward;
  var state=athenaLoadState();
  if(!athenaApplySavedState(state)) athenaDefaultLayout();
  athenaOpenInitialDoc((state && state.currentDoc) ||
    ((window.ATHENA_SITE_DATA && window.ATHENA_SITE_DATA.entry) || 'about:blank'),
    state);
  athenaBooting=false;
  athenaSaveState();
};
)JS";
}

static std::string
index_html (const std::string& title) {
  return "<!doctype html>\n<html><head><meta charset=\"utf-8\">\n"
         "<title>" + title + "</title>\n"
         "<link rel=\"icon\" href=\"css/favicon.png\">\n"
         "<link rel=\"stylesheet\" href=\"css/site.css\">\n"
         "<script src=\"js/site-data.js\"></script>\n"
         "<script src=\"js/window-manager.js\"></script>\n"
         "<script src=\"js/explorers.js\"></script>\n"
         "<script src=\"js/search.js\"></script>\n"
         "<script src=\"js/quick-switcher.js\"></script>\n"
         "<script src=\"js/app.js\"></script>\n"
         "</head><body>\n"
         "<div class=\"desktop\">\n"
         "<div id=\"vault\" class=\"win\" style=\"left:16px;top:16px;width:320px;height:380px\">"
         "<div class=\"title\"><span class=\"caption\">Vault Explorer</span><span class=\"buttons\"><button class=\"ctrl min\" aria-label=\"Minimize\"></button><button class=\"ctrl max\" aria-label=\"Maximize\"></button><button class=\"ctrl restore\" aria-label=\"Restore\"></button><button class=\"ctrl close\" aria-label=\"Close\"></button></span></div>"
         "<div id=\"vault-content\" class=\"content\"></div><div class=\"resize\"></div></div>\n"
         "<div id=\"namespaces\" class=\"win\" style=\"left:360px;top:16px;width:320px;height:300px\">"
         "<div class=\"title\"><span class=\"caption\">Namespace Explorer</span><span class=\"buttons\"><button class=\"ctrl min\" aria-label=\"Minimize\"></button><button class=\"ctrl max\" aria-label=\"Maximize\"></button><button class=\"ctrl restore\" aria-label=\"Restore\"></button><button class=\"ctrl close\" aria-label=\"Close\"></button></span></div>"
         "<div id=\"namespace-content\" class=\"content\"></div><div class=\"resize\"></div></div>\n"
         "<div id=\"global-search\" class=\"win\" style=\"left:704px;top:16px;width:420px;height:300px\">"
         "<div class=\"title\"><span class=\"caption\">Global Search</span><span class=\"buttons\"><button class=\"ctrl min\" aria-label=\"Minimize\"></button><button class=\"ctrl max\" aria-label=\"Maximize\"></button><button class=\"ctrl restore\" aria-label=\"Restore\"></button><button class=\"ctrl close\" aria-label=\"Close\"></button></span></div>"
         "<div class=\"content\"><div class=\"pane-tools\"><input id=\"global-search-input\" placeholder=\"Search exported documents\"></div>"
         "<div id=\"global-search-results\"></div></div><div class=\"resize\"></div></div>\n"
         "<div id=\"quick-switcher\" class=\"win\" style=\"left:1148px;top:16px;width:360px;height:300px\">"
         "<div class=\"title\"><span class=\"caption\">Quick Switcher</span><span class=\"buttons\"><button class=\"ctrl min\" aria-label=\"Minimize\"></button><button class=\"ctrl max\" aria-label=\"Maximize\"></button><button class=\"ctrl restore\" aria-label=\"Restore\"></button><button class=\"ctrl close\" aria-label=\"Close\"></button></span></div>"
         "<div class=\"content\"><div class=\"pane-tools\"><input id=\"quick-switcher-input\" placeholder=\"Jump to file or namespace\"></div>"
         "<div id=\"quick-switcher-results\"></div></div><div class=\"resize\"></div></div>\n"
         "<div id=\"viewer\" class=\"win\" style=\"left:40px;top:340px;width:calc(100% - 80px);height:calc(100% - 370px)\">"
         "<div class=\"title\"><span class=\"title-left\"><span id=\"viewer-title\" class=\"caption\">Document</span><span class=\"doc-nav\"><button id=\"doc-back\" class=\"ctrl back\" aria-label=\"Back\"></button><button id=\"doc-forward\" class=\"ctrl forward\" aria-label=\"Forward\"></button></span></span><span class=\"buttons\"><button class=\"ctrl min\" aria-label=\"Minimize\"></button><button class=\"ctrl max\" aria-label=\"Maximize\"></button><button class=\"ctrl restore\" aria-label=\"Restore\"></button><button class=\"ctrl close\" aria-label=\"Close\"></button></span></div>"
         "<div class=\"content\"><iframe id=\"docframe\"></iframe></div><div class=\"resize\"></div></div>\n"
         "<div id=\"missing-modal\" class=\"modal\"><strong>ATHENA</strong><p id=\"missing-text\"></p>"
         "<button onclick=\"closeMissing()\">OK</button></div>\n"
         "</div><div id=\"taskbar\" class=\"taskbar\"></div></body></html>\n";
}

static bool
copy_favicon (const fs::path& dest) {
  fs::path src = fs::path (tm_to_std (get_env ("ATHENA_PATH"))) /
                 "misc" / "images" / "ATHENA-512.png";
  std::error_code ec;
  if (!fs::exists (src)) return true;
  fs::create_directories (dest.parent_path (), ec);
  fs::copy_file (src, dest, fs::copy_options::overwrite_existing, ec);
  return !ec;
}

static bool
write_site_shell (const athena_website_entry& website,
                  const GenerationContext& cx, std::string& error) {
  std::error_code ec;
  fs::create_directories (cx.destination / "js", ec);
  fs::create_directories (cx.destination / "css", ec);
  if (ec) {
    error = "Could not create website support folders: " + ec.message ();
    return false;
  }
  if (!write_file_bytes (cx.destination / "css" / "site.css", site_css ()) ||
      !write_file_bytes (cx.destination / "js" / "site-data.js",
                         site_data_js (site_manifest (website, cx))) ||
      !write_file_bytes (cx.destination / "js" / "window-manager.js",
                         window_manager_js ()) ||
      !write_file_bytes (cx.destination / "js" / "explorers.js",
                         explorers_js ()) ||
      !write_file_bytes (cx.destination / "js" / "search.js",
                         search_js ()) ||
      !write_file_bytes (cx.destination / "js" / "quick-switcher.js",
                         quick_switcher_js ()) ||
      !write_file_bytes (cx.destination / "js" / "app.js", app_js ()) ||
      !write_file_bytes (cx.destination / "index.html",
                         index_html (website.name))) {
    error = "Could not write website shell files.";
    return false;
  }
  copy_favicon (cx.destination / "css" / "favicon.png");
  return true;
}

static bool
export_namespace_homepage (const std::string& name, bool technical,
                           const fs::path& target, std::string& error) {
  std::string tmfs = technical ? "!" + name : name;
  tree doc = athena_namespace_info_page (std_to_tm (tmfs));
  return export_document_html (doc, fs::path ("tmfs://ns/" + tmfs), target,
                               error);
}

static std::set<std::string>
selector_namespaces (const athena_website_selector& selector) {
  std::set<std::string> out;
  if (selector.op == "namespace") out.insert (selector.value);
  for (const athena_website_selector& child: selector.children) {
    std::set<std::string> more = selector_namespaces (child);
    out.insert (more.begin (), more.end ());
  }
  return out;
}

static fs::path
destination_for (const fs::path& root, const athena_website_entry& website) {
  fs::path dest (website.destination.empty () ? website.name :
                                             website.destination);
  if (dest.is_absolute ()) return dest.lexically_normal ();
  return (root / dest).lexically_normal ();
}

static bool
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

static bool
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
    if (!export_document_html (rewritten, source, target, error))
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
    if (!export_namespace_homepage (ns, false, cx.destination / normal, error))
      return false;
    if (!export_namespace_homepage (ns, true, cx.destination / technical,
                                    error))
      return false;
    cx.namespace_homepages[ns] = normal;
  }

  if (!write_site_shell (website, cx, error)) return false;
  if (!run_post_command (website, root, cx.destination, error)) return false;
  website_log ("complete");
  return true;
}

} // namespace

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
