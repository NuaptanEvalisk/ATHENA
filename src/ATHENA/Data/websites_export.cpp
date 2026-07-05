/******************************************************************************
* MODULE     : websites_export.cpp
* DESCRIPTION: Static website document export helpers
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "ATHENA/Data/websites_internal.hpp"

#include <QTemporaryDir>

namespace athena_websites {

namespace {

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

} // namespace

bool
path_begins_with_parent (const fs::path& path) {
  for (const fs::path& part: path) {
    if (part == "..") return true;
    if (part != ".") return false;
  }
  return false;
}

std::string
without_url_suffix (const std::string& path) {
  size_t pos = path.find_first_of ("?#");
  return pos == std::string::npos ? path : path.substr (0, pos);
}

std::string
url_suffix_part (const std::string& path) {
  size_t pos = path.find_first_of ("?#");
  return pos == std::string::npos ? std::string () : path.substr (pos);
}

bool
is_web_image_path (const std::string& path) {
  std::string clean = without_url_suffix (path);
  std::string ext = lower_copy (fs::path (clean).extension ().string ());
  return ext == ".gif" || ext == ".jpg" || ext == ".jpeg" ||
         ext == ".png" || ext == ".bmp" || ext == ".svg";
}

bool
looks_like_static_asset_path (const std::string& path) {
  std::string clean = without_url_suffix (path);
  std::string ext = lower_copy (fs::path (clean).extension ().string ());
  return !ext.empty () && ext != ".ath" && ext != ".tm" &&
         ext != ".texmacs" && ext != ".html" && ext != ".xhtml";
}

bool
is_remote_or_special_path (const std::string& path) {
  std::string lower = lower_copy (path);
  return path.empty () || starts_with (path, "$") || starts_with (path, "~") ||
         starts_with (lower, "http:") || starts_with (lower, "https:") ||
         starts_with (lower, "ftp:") || starts_with (lower, "data:") ||
         starts_with (lower, "tmfs:") ||
         path.find ("://") != std::string::npos;
}

std::string
html_escape (const std::string& text) {
  std::string out;
  for (char c: text) {
    switch (c) {
    case '&': out += "&amp;"; break;
    case '<': out += "&lt;"; break;
    case '>': out += "&gt;"; break;
    case '"': out += "&quot;"; break;
    case '\'': out += "&#39;"; break;
    default: out.push_back (c); break;
    }
  }
  return out;
}

std::string
decode_file_path_text (const std::string& path) {
  QString decoded = QUrl::fromPercentEncoding (qs (path).toUtf8 ());
  return ss (decoded);
}

std::string
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

bool
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

bool
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

bool
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

bool
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

bool
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

bool
copy_static_image (const std::string& image_path,
                   const std::string& source_rel,
                   const std::string& output_rel,
                   const GenerationContext& cx,
                   std::string& href) {
  std::string clean = without_url_suffix (image_path);
  if (!is_web_image_path (clean)) return false;
  return copy_static_asset (image_path, source_rel, output_rel, cx, href);
}

std::string
shell_call_href (const std::string& function_name, const std::string& arg) {
  return "javascript:" + function_name + "(" +
         json_script_string (arg) + ")";
}

std::string
document_shell_href (const std::string& html_rel,
                     const std::string& anchor = "") {
  std::string target = html_rel;
  if (!anchor.empty ()) target += "#" + anchor;
  return shell_call_href ("athenaOpenDoc", target);
}

void
inject_or_replace_document_title (std::string& html, const std::string& title) {
  std::string escaped = html_escape (title.empty () ? "Document" : title);
  std::string next = "<title>" + escaped + "</title>";
  size_t begin = html.find ("<title>");
  size_t end = begin == std::string::npos ? std::string::npos :
                                            html.find ("</title>", begin);
  if (begin != std::string::npos && end != std::string::npos) {
    end += 8;
    html.replace (begin, end - begin, next);
    return;
  }
  size_t head = html.find ("</head>");
  if (head != std::string::npos) html.insert (head, next + "\n");
  else html.insert (0, next + "\n");
}

void
inject_document_favicon (std::string& html, const std::string& output_rel) {
  if (html.find ("rel=\"icon\"") != std::string::npos ||
      html.find ("rel='icon'") != std::string::npos)
    return;
  std::string href = relative_href (output_rel, "css/favicon.png");
  std::string link = "<link rel=\"icon\" href=\"" + html_escape (href) +
                     "\"></link>\n";
  size_t head = html.find ("</head>");
  if (head != std::string::npos) html.insert (head, link);
  else html.insert (0, link);
}

std::string
tree_string (tree t) {
  if (is_atomic (t)) return tm_to_std (t->label);
  return tm_to_std (tree_as_string (t));
}

bool
append_document_title_text (tree t, std::string& out) {
  if (is_atomic (t)) {
    out += tm_to_std (t->label);
    return true;
  }

  struct MacroName {
    const char* name;
    const char* text;
  };
  static const MacroName macros[] = {
    {"ATHENA", "ATHENA"},
    {"athena", "ATHENA"},
    {"TeXmacs", "TeXmacs"},
    {"LaTeX", "LaTeX"},
    {"TeX", "TeX"}
  };
  for (const MacroName& macro: macros) {
    if (is_compound (t, macro.name, 0)) {
      out += macro.text;
      return true;
    }
  }

  bool appended = false;
  for (int i=0; i<N(t); i++)
    appended = append_document_title_text (t[i], out) || appended;
  return appended;
}

std::string
document_title_text (tree t) {
  std::string out;
  append_document_title_text (t, out);
  return out;
}

void
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

std::string
document_search_text (tree doc) {
  std::string out;
  append_plain_text (doc, out, 20000);
  return out;
}

std::string
document_title (tree t, const std::string& fallback) {
  if (is_atomic (t)) return fallback;
  if ((is_compound (t, "doc-title", 1) ||
       is_compound (t, "title", 1) ||
       is_compound (t, "tmdoc-title", 1) ||
       is_compound (t, "tmweb-title", 1)) && N(t) >= 1) {
    std::string title = document_title_text (t[0]);
    if (!title.empty ()) return title;
  }
  for (int i=0; i<N(t); i++) {
    std::string title = document_title (t[i], "");
    if (!title.empty ()) return title;
  }
  return fallback;
}

bool
is_absolute_image_path (const std::string& path) {
  return path.empty () || starts_with (path, "/") || starts_with (path, "~") ||
         starts_with (path, "$") || path.find ("://") != std::string::npos;
}

std::string
rebase_image_path (const std::string& path, const fs::path& source_dir) {
  if (is_absolute_image_path (path)) return path;
  return (source_dir / path).lexically_normal ().string ();
}

tree
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

tree
strip_labels (tree t) {
  if (is_atomic (t)) return copy (t);
  if (is_func (t, LABEL, 1)) return tree (CONCAT);
  tree r (L(t));
  for (int i=0; i<N(t); i++) r << strip_labels (t[i]);
  return r;
}

bool
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

bool
find_label_path (tree t, const std::string& label, std::vector<int>& found) {
  if (label.empty ()) return false;
  std::vector<int> current;
  return find_label_path_rec (t, label, current, found);
}

tree
subtree_at (tree t, const std::vector<int>& path) {
  tree out = t;
  for (int index: path) out = out[index];
  return out;
}

std::vector<int>
common_prefix (const std::vector<int>& a, const std::vector<int>& b) {
  std::vector<int> out;
  size_t n = std::min (a.size (), b.size ());
  for (size_t i=0; i<n; i++) {
    if (a[i] != b[i]) break;
    out.push_back (a[i]);
  }
  return out;
}

tree
document_children_or_self (tree t) {
  if (is_func (t, DOCUMENT)) return copy (t);
  tree doc (DOCUMENT);
  doc << copy (t);
  return doc;
}

tree
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

bool
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

bool
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

std::string
modal_href (const std::string& label) {
  return shell_call_href ("athenaMissingTarget", label);
}

bool
local_document_target (const std::string& destination,
                       const std::string& current_rel,
                       const GenerationContext& cx,
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

  if (starts_with (lower_copy (target), "file:")) {
    QUrl url (qs (target));
    if (url.isLocalFile ()) target = ss (url.toLocalFile ());
  }

  fs::path target_path (target);
  if (target_path.is_absolute ()) {
    fs::path root = cx.root.lexically_normal ();
    fs::path absolute = target_path.lexically_normal ();
    std::error_code ec;
    fs::path rel = fs::relative (absolute, root, ec);
    if (!ec && !rel.empty () && !rel.is_absolute () &&
        !path_begins_with_parent (rel))
      rel_path = clean_relative (generic_path (rel));
    else
      rel_path = generic_path (absolute);
  }
  else {
    fs::path current_dir = fs::path (current_rel).parent_path ();
    rel_path = clean_relative (generic_path (current_dir / target_path));
  }
  return true;
}

tree
rewrite_static_links (tree t, const std::string& source_rel,
                      const std::string& output_rel,
                      const GenerationContext& cx);

tree
rewrite_link_like (tree t, const std::string& source_rel,
                   const std::string& output_rel,
                   const GenerationContext& cx, bool force_plain_hlink) {
  tree body = rewrite_static_links (t[0], source_rel, output_rel, cx);
  std::string destination = tree_string (t[1]);
  std::string rel_path;
  std::string anchor;
  bool handled = decode_wikilink_target (destination, rel_path, anchor);
  if (!handled)
    handled = local_document_target (destination, source_rel, cx, rel_path,
                                     anchor);

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

std::string
transclusion_source_rel (tree t) {
  if (N(t) < 1) return "";
  tree node = vault_get_node (std_to_tm (tree_string (t[0])));
  if (!is_func (node, TUPLE) || N(node) < 1) return "";
  return clean_relative (tree_string (node[0]));
}

tree
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

tree
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


void
set_current_save_urls (const fs::path& source, const fs::path& target) {
  eval ("(set! current-save-source (string->url " *
        std_to_tm (scheme_quote (source.string ())) * "))");
  eval ("(set! current-save-target (string->url " *
        std_to_tm (scheme_quote (target.string ())) * "))");
}

std::string
document_bridge_script () {
  std::string js;
  if (!website_template_text ("document-bridge.js", js)) return "";
  return "<script data-athena-website-bridge=\"2\">\n"
         "/* ATHENA_WEBSITE_BRIDGE_BEGIN */\n" + js +
         "\n/* ATHENA_WEBSITE_BRIDGE_END */\n</script>\n";
}

std::string
document_theme_style () {
  std::string background_css;
  if (get_preference ("override white document background", "off") == "on") {
    string pref = get_preference ("white document background override color",
                                  "#f7f3e8");
    std::string color = tm_to_std (get_hex_color (pref));
    background_css = "html,body{background:" + color + "}\n";
  }
  return "<style data-athena-website-theme=\"1\">\n" +
         site_theme_css () +
         background_css +
         "a:link{color:var(--athena-link-color)}\n"
         "a:visited{color:var(--athena-visited-color)}\n"
         "</style>\n";
}

void
inject_or_replace_document_theme (std::string& html) {
  std::string marker = "data-athena-website-theme=\"1\"";
  size_t old_marker = html.find (marker);
  std::string style = document_theme_style ();
  if (old_marker != std::string::npos) {
    size_t style_begin = html.rfind ("<style", old_marker);
    size_t style_end = html.find ("</style>", old_marker);
    if (style_begin != std::string::npos && style_end != std::string::npos) {
      style_end += 8;
      html.replace (style_begin, style_end - style_begin, style);
      return;
    }
  }
  size_t head = html.find ("</head>");
  if (head != std::string::npos) html.insert (head, style);
  else html.insert (0, style);
}

bool
inject_document_bridge (const fs::path& target, const std::string& output_rel,
                        const std::string& title) {
  std::string html;
  if (!read_file_bytes (target, html)) return false;
  std::string script = document_bridge_script ();
  if (script.empty ()) return false;
  inject_or_replace_document_title (html, title);
  inject_document_favicon (html, output_rel);
  inject_or_replace_document_theme (html);
  std::string begin = "/* ATHENA_WEBSITE_BRIDGE_BEGIN */";
  std::string end = "/* ATHENA_WEBSITE_BRIDGE_END */";
  size_t old_begin = html.find (begin);
  if (old_begin != std::string::npos) {
    size_t script_begin = html.rfind ("<script", old_begin);
    size_t old_end = html.find (end, old_begin);
    if (script_begin != std::string::npos && old_end != std::string::npos) {
      size_t script_end = html.find ("</script>", old_end);
      if (script_end != std::string::npos) {
        script_end += 9;
        html.replace (script_begin, script_end - script_begin, script);
        return write_file_bytes (target, html);
      }
    }
  }
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
bool
export_document_html (tree doc, const fs::path& source,
                      const fs::path& target, const std::string& output_rel,
                      const std::string& title, std::string& error) {
  std::error_code ec;
  fs::create_directories (target.parent_path (), ec);
  if (ec) {
    error = "Could not create " + target.parent_path ().string () + ": " +
            ec.message ();
    return false;
  }
  set_current_save_urls (source, target);
  HtmlExportPreferenceScope html_scope;

  QTemporaryDir temp_dir;
  if (!temp_dir.isValid ()) {
    error = "Could not create temporary HTML export directory.";
    return false;
  }

  fs::path temp_source = fs::path (ss (temp_dir.path ())) / "document.ath";
  if (export_tree (doc, url_system (std_to_tm (temp_source.string ())),
                   "texmacs")) {
    error = "Could not prepare temporary TeXmacs source for " +
            source.string ();
    return false;
  }

  fs::remove (target, ec);
  std::string command =
    "(begin "
    "(load-buffer (string->url " + scheme_quote (temp_source.string ()) +
    ") :strict) "
    "(export-buffer (string->url " + scheme_quote (target.string ()) + ")) "
    "(buffer-close (current-buffer)))";
  eval (std_to_tm (command));

  if (!fs::is_regular_file (target, ec)) {
    error = "HTML export failed for " + source.string ();
    return false;
  }
  std::string exported_html;
  if (!read_file_bytes (target, exported_html) ||
      exported_html.find_first_not_of (" \t\r\n") == std::string::npos) {
    error = "HTML export produced an empty page for " + source.string ();
    return false;
  }
  if (!inject_document_bridge (target, output_rel, title)) {
    error = "Could not inject website bridge into " + target.string ();
    return false;
  }
  return true;
}

} // namespace athena_websites
