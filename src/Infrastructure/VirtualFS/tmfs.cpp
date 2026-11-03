// Copyright (C) 2006 Joris van der Hoeven, 2026 ATHENA contributors.
// GPL-3.0-or-later.
#include "Infrastructure/VirtualFS/tmfs.hpp"
#include "analyze.hpp"
#include "App/Frontend/new_buffer.hpp"
#include "Domains/Artifacts/artifact_document.hpp"
#include "Materials/Model/materials_document.hpp"
#include "Domains/Namespaces/namespaces.hpp"
#include "Domains/Vault/vault_frontend.hpp"
#include "Editor/Commands/vault_navigation.hpp"
#include "file.hpp"
#include "sys_utils.hpp"

namespace {
std::pair<string, string> split (string name) {
  int i= search_forwards ("/", 0, name);
  if (i < 0) return {"file", name};
  return {name (0, i), name (i+1, N(name))};
}
std::pair<string, string> route (url source) {
  string name= as_string (source, URL_UNIX);
  return split (starts (name, "tmfs://") ? name (7, N(name)) : name);
}
tree empty_document (string text) {
  return tree (DOCUMENT, compound ("style", tree (TUPLE, "generic")),
               compound ("body", tree (DOCUMENT, text)));
}
}

tree tmfs_load (url source) {
  auto [kind, name]= route (source);
  if (kind == "wikilink-repair") return native_wikilink_repair_page (name);
  if (kind == "ns") return athena_namespace_info_page (name);
  if (kind == "welcome") return vault_welcome_page_native ();
  if (kind == "artifact") return native_artifact_open_page (name);
  if (kind == "artifact-disambiguation") return athena_artifact_disambiguation_page (name);
  if (kind == "artifact-name-query") return athena_artifact_name_query_page (name);
  if (kind == "material")
    return athena_material_info_page (std::string (name.data (), N(name)));
  if (kind == "aux") {
    tree document= get_buffer_tree (source);
    return document == tree ("") ? empty_document ("") : document;
  }
  if (kind == "import") {
    auto [format, encoded]= split (name);
    if (search_forwards ("/", 0, name) < 0 || search_forwards ("/", 0, encoded) < 0)
      return empty_document ("");
    return import_tree (tmfs_decode_url (encoded), format);
  }
  return empty_document ("Invalid tmfs document.");
}

string tmfs_title (url source) {
  auto [kind, name]= route (source);
  if (kind == "aux") return name;
  if (kind == "artifact") return "Artifact";
  if (kind == "material") return "Material";
  if (kind == "artifact-disambiguation" || kind == "artifact-name-query")
    return "Artifact disambiguation";
  if (kind == "import") {
    auto [format, encoded]= split (name);
    if (search_forwards ("/", 0, name) >= 0 && search_forwards ("/", 0, encoded) >= 0)
      return as_string (tail (tmfs_decode_url (encoded)), URL_SYSTEM) *
             " - " * upcase_first (format);
    return as_string (tail (source), URL_SYSTEM);
  }
  return as_string (source, URL_SYSTEM);
}

bool tmfs_permission (url source, string permission) {
  string name= as_string (source);
  if (ends (name, "~") || ends (name, "#")) return false;
  return permission == "read" || (permission == "write" && route (source).first == "aux");
}

url tmfs_master (url source) {
  if (route (source).first != "aux") return source;
  url master= get_master_buffer (source);
  return is_none (master) ? source : master;
}

string tmfs_encode_url (url source) {
  url resources= get_texmacs_path ();
  if (descends (source, resources) && source != resources)
    return "tm/" * as_string (delta (resources * "x", source), URL_UNIX);
  string protocol= get_root (source), file= as_string (unroot (source), URL_UNIX);
  if (protocol == "") return "here/" * file;
  if (protocol == "default") {
#ifdef OS_MINGW
    if (N(file) >= 3 && file[1] == ':' && file[2] == '/')
      file= file (0, 1) * file (2, N(file));
#endif
    return "file/" * file;
  }
  return protocol * "/" * file;
}

url tmfs_decode_url (string source) {
  if (search_forwards ("/", 0, source) < 0) return url_unix (source);
  auto [protocol, file]= split (source);
  if (protocol == "tm") return get_texmacs_path () * url_unix (file);
  if (protocol == "here") return url_unix (file);
  if (protocol == "file") {
#ifdef OS_MINGW
    return url ("/" * file);
#else
    return url_root ("default") * url_unix (file);
#endif
  }
  if (protocol == "http" || protocol == "https" || protocol == "ftp" || protocol == "tmfs")
    return url_root (protocol) * url_unix (file);
  return url_root ("default") * url_unix (source);
}
