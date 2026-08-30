/******************************************************************************
* MODULE     : vault_image_insertion.cpp
* DESCRIPTION: Vault-aware image insertion policy
* COPYRIGHT  : (C) 2026  Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "ATHENA/Data/vault_image_insertion.hpp"

#include "ATHENA/Data/image_background.hpp"
#include "ATHENA/Data/vault.hpp"
#include "scheme.hpp"
#include "web_files.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <regex>
#include <string>

namespace fs = std::filesystem;

namespace {

static bool
preference_on (const string& name) {
  return get_preference (name, "off") == "on";
}

static bool
auto_copy_images () {
  return preference_on ("vault auto copy images to vault");
}

static bool
normalize_image_names () {
  return preference_on ("vault normalize image filename when inserting");
}

static bool
auto_remove_background () {
  return image_auto_remove_background_enabled ();
}

static bool
download_pasted_internet_images () {
  return get_preference ("pasted internet image handling", "link") ==
         "download";
}

static std::string
to_std (string s) {
  return std::string (as_charp (s), N(s));
}

static string
to_tm (const std::string& s) {
  return string (s.c_str ());
}

static fs::path
canonical_path (const fs::path& p) {
  std::error_code ec;
  fs::path r= fs::weakly_canonical (p, ec);
  if (!ec) return r;
  r= fs::absolute (p, ec);
  return ec ? p : r.lexically_normal ();
}

static fs::path
url_path (url u) {
  return canonical_path (fs::path (to_std (concretize (u))));
}

static bool
path_descends (const fs::path& child, const fs::path& parent) {
  fs::path rel= child.lexically_relative (parent);
  if (rel.empty ()) return child == parent;
  for (const fs::path& part : rel)
    if (part == "..") return false;
  return true;
}

static bool
document_in_vault (url document, fs::path& doc_path, fs::path& root_path) {
  if (!vault_active () || is_none (document)) return false;
  if (suffix (document) != "ath") return false;
  doc_path= url_path (document);
  root_path= url_path (vault_get_root ());
  return path_descends (doc_path, root_path);
}

static std::string
lower_extension (std::string ext) {
  if (!ext.empty () && ext[0] == '.') ext= ext.substr (1);
  std::transform (ext.begin (), ext.end (), ext.begin (),
                  [] (unsigned char c) { return (char) std::tolower (c); });
  return ext;
}

static bool
canonical_image_name (const fs::path& path) {
  static const std::regex rx (
    "^figure-[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}\\.[A-Za-z0-9]+$");
  return std::regex_match (path.filename ().string (), rx);
}

static fs::path
unique_path (const fs::path& dir, const std::string& base,
             const std::string& ext) {
  fs::path target= dir / (base + (ext.empty () ? "" : "." + ext));
  if (!fs::exists (target)) return target;
  for (int i=2; ; i++) {
    target= dir / (base + "-" + std::to_string (i) +
                   (ext.empty () ? "" : "." + ext));
    if (!fs::exists (target)) return target;
  }
}

static fs::path
target_path (const fs::path& assets_dir, const fs::path& source,
             const std::string& fallback_ext) {
  std::string ext= lower_extension (
    source.empty () ? fallback_ext : source.extension ().string ());
  if (normalize_image_names ())
    do {
      fs::path target= assets_dir / ("figure-" + to_std (vault_generate_uuid ()) +
                                     (ext.empty () ? "" : "." + ext));
      if (!fs::exists (target)) return target;
    } while (true);

  std::string stem= source.empty () ? "pasted-image" : source.stem ().string ();
  if (stem.empty ()) stem= "pasted-image";
  return unique_path (assets_dir, stem, ext);
}

static bool
write_bytes (const fs::path& target, const string& data, string& error) {
  std::ofstream out (target, std::ios::binary);
  if (!out) {
    error= "could not create image file " * to_tm (target.string ());
    return false;
  }
  out.write (as_charp (data), N(data));
  if (!out) {
    error= "could not write image file " * to_tm (target.string ());
    return false;
  }
  return true;
}

static bool
copy_file (const fs::path& source, const fs::path& target, string& error) {
  std::error_code ec;
  fs::copy_file (source, target, fs::copy_options::overwrite_existing, ec);
  if (ec) {
    error= "could not copy image to vault assets: " * to_tm (ec.message ());
    return false;
  }
  return true;
}

static string
relative_ref (const fs::path& doc_path, const fs::path& target) {
  std::error_code ec;
  fs::path rel= fs::relative (target, doc_path.parent_path (), ec);
  if (ec) rel= target;
  return to_tm (rel.generic_string ());
}

static bool
policy_enabled_for_document (url document, fs::path& doc_path,
                             fs::path& root_path) {
  return (auto_copy_images () || normalize_image_names () ||
          auto_remove_background ()) &&
         document_in_vault (document, doc_path, root_path);
}

static bool
ensure_assets_dir (const fs::path& doc_path, fs::path& assets_dir,
                   string& error) {
  assets_dir= doc_path.parent_path () / "assets";
  std::error_code ec;
  fs::create_directories (assets_dir, ec);
  if (ec) {
    error= "could not create vault assets directory: " * to_tm (ec.message ());
    return false;
  }
  return true;
}

} // namespace

bool
vault_image_insertion_prepare_file (url document, url source,
                                    string& document_ref, string& error) {
  fs::path doc_path, root_path;
  if (!policy_enabled_for_document (document, doc_path, root_path)) return false;

  fs::path source_path= url_path (source);
  if (!fs::is_regular_file (source_path)) return false;

  bool in_vault= path_descends (source_path, root_path);
  if (in_vault && (!normalize_image_names () || canonical_image_name (source_path))) {
    if (auto_remove_background () &&
        lower_extension (source_path.extension ().string ()) == "png" &&
        !image_remove_white_background_png (url_system (to_tm (source_path.string ())),
                                            error)) {
      return true;
    }
    document_ref= relative_ref (doc_path, source_path);
    return true;
  }

  fs::path assets_dir;
  if (!ensure_assets_dir (doc_path, assets_dir, error)) return true;
  fs::path target= target_path (assets_dir, source_path, "");
  if (!copy_file (source_path, target, error)) return true;
  if (auto_remove_background () &&
      lower_extension (target.extension ().string ()) == "png" &&
      !image_remove_white_background_png (url_system (to_tm (target.string ())),
                                          error)) {
    return true;
  }
  document_ref= relative_ref (doc_path, target);
  return true;
}

bool
vault_image_insertion_prepare_data (url document, string data, string extension,
                                    string& document_ref, string& error) {
  fs::path doc_path, root_path;
  if (!policy_enabled_for_document (document, doc_path, root_path)) return false;

  fs::path assets_dir;
  if (!ensure_assets_dir (doc_path, assets_dir, error)) return true;
  fs::path target= target_path (assets_dir, fs::path (), to_std (extension));
  if (!write_bytes (target, data, error)) return true;
  if (auto_remove_background () &&
      lower_extension (target.extension ().string ()) == "png" &&
      !image_remove_white_background_png (url_system (to_tm (target.string ())),
                                          error)) {
    return true;
  }
  document_ref= relative_ref (doc_path, target);
  return true;
}

bool
vault_image_insertion_prepare_remote (url document, url source,
                                      string& document_ref, string& error) {
  if (!download_pasted_internet_images ()) return false;

  fs::path doc_path, root_path;
  if (!document_in_vault (document, doc_path, root_path)) return false;

  url downloaded= get_from_web (source);
  if (is_none (downloaded)) {
    error= "could not download pasted image " * as_string (source);
    return true;
  }

  fs::path downloaded_path= url_path (downloaded);
  if (!fs::is_regular_file (downloaded_path)) {
    error= "downloaded image is not a regular file";
    return true;
  }

  fs::path assets_dir;
  if (!ensure_assets_dir (doc_path, assets_dir, error)) return true;
  fs::path target= target_path (
    assets_dir, fs::path (), to_std (suffix (source)));
  if (!copy_file (downloaded_path, target, error)) return true;
  if (auto_remove_background () &&
      lower_extension (target.extension ().string ()) == "png" &&
      !image_remove_white_background_png (
        url_system (to_tm (target.string ())), error))
    return true;

  document_ref= relative_ref (doc_path, target);
  return true;
}
