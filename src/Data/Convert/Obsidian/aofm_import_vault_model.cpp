#include "aofm_import_vault_internal.hpp"

#include <filesystem>
#include <fstream>
#include <system_error>

#include "ATHENA/Data/vaultfile_json.hpp"
#include "ATHENA/Data/vault_map_sqlite.hpp"
#include "file.hpp"
#include "namespaces_private.hpp"
#include <sqlite3.h>

namespace aofm_import_vault_internal {

bool
validate_destination_dir(url destination_root, bool ignore_nonempty) {
  if (!exists(destination_root)) {
    mkdir(destination_root);
    return true;
  }
  if (!is_directory(destination_root)) {
    report_import_error("destination path is not a directory");
    return false;
  }

  bool err = false;
  array<string> entries = read_directory(destination_root, err);
  if (err) {
    report_import_error("could not inspect destination directory");
    return false;
  }
  for (int i = 0; i < N(entries); ++i) {
    if (entries[i] == "." || entries[i] == "..") continue;
    if (ignore_nonempty) {
      report_import_warning("destination directory is not empty");
      return true;
    }
    report_import_error("destination directory is not empty");
    return false;
  }
  return true;
}

std::string
join_unix_paths(const std::string& root, const std::string& rel) {
  if (root.empty()) return rel;
  if (rel.empty()) return root;
  if (root[root.size() - 1] == '/') return root + rel;
  return root + "/" + rel;
}

bool
write_vaultfile(const std::string& destination_root_path,
                const std::string& vault_name,
                const std::string& prefs_path,
                const std::string& namespace_db_path) {
  AthenaVaultfileInfo info;
  info.name = vault_name;
  info.map_path = "map.sqlite";
  info.preferences_path = prefs_path;
  info.namespace_db_path = namespace_db_path.empty () ? "ns.sqlite" :
                                                     namespace_db_path;
  std::string error;
  if (athena_vaultfile_write (std::filesystem::path (destination_root_path),
                              info, error))
    return true;
  report_import_error ("failed to write Vaultfile.json: " + error);
  return false;
}

std::vector<std::string>
scan_quoted_strings(const std::string& text) {
  std::vector<std::string> out;
  bool in = false;
  bool esc = false;
  std::string cur;
  for (char c : text) {
    if (!in) {
      if (c == '"') {
        in = true;
        cur.clear();
      }
      continue;
    }
    if (esc) {
      cur.push_back(c);
      esc = false;
      continue;
    }
    if (c == '\\') {
      esc = true;
      continue;
    }
    if (c == '"') {
      out.push_back(cur);
      in = false;
      continue;
    }
    cur.push_back(c);
  }
  return out;
}

std::string
model_join_path(const std::string& root, const std::string& rel) {
  if (rel.empty()) return "";
  std::filesystem::path p(rel);
  if (p.is_absolute()) return p.string();
  return (std::filesystem::path(root) / p).string();
}

bool
copy_model_file(const std::string& source, const std::string& destination) {
  if (source.empty() || destination.empty()) return true;
  if (!std::filesystem::exists(source)) return true;
  std::error_code ec;
  std::filesystem::path dest(destination);
  if (dest.has_parent_path()) std::filesystem::create_directories(dest.parent_path(), ec);
  if (ec) return false;
  std::filesystem::copy_file(source, destination,
                            std::filesystem::copy_options::overwrite_existing,
                            ec);
  return !ec;
}

void
copy_model_namespace_resource(const AofmModelVaultInfo& info,
                              const std::string& destination_root_path,
                              string path) {
  if (path == "") return;
  std::string rel = std::string(as_charp(path));
  std::filesystem::path p(rel);
  if (p.is_absolute()) return;
  std::string source = model_join_path(info.root, rel);
  std::string destination = join_unix_paths(destination_root_path, rel);
  if (std::filesystem::exists(source) && !copy_model_file(source, destination))
    report_import_warning("could not copy model namespace resource: " + rel);
}

string
style_name_from_namespace_path(string style_path) {
  if (style_path == "") return "";
  std::filesystem::path p(std::string(as_charp(style_path)));
  if (p.extension() != ".ts") return "";
  return string(p.stem().string().c_str());
}

static string
aofm_column_string(sqlite3_stmt* st, int col) {
  const unsigned char* text = sqlite3_column_text(st, col);
  return text == nullptr ? string("") : string((const char*) text);
}

static bool
load_model_namespaces(const std::string& db_path,
                      std::vector<athena_namespace_definition>& out,
                      std::string& error) {
  if (db_path.empty() || !std::filesystem::exists(db_path)) return true;
  sqlite3* db = nullptr;
  if (sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) !=
      SQLITE_OK) {
    error = db == nullptr ? db_path : sqlite3_errmsg(db);
    if (db != nullptr) sqlite3_close(db);
    return false;
  }

  const char* sql =
    "SELECT name, kind, template, sorter_trivial, sorter_path, style_path, "
    "initial_content_path, homepage_path FROM namespaces ORDER BY name;";
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(db, sql, -1, &st, nullptr) != SQLITE_OK) {
    error = sqlite3_errmsg(db);
    sqlite3_close(db);
    return false;
  }
  while (true) {
    int status = sqlite3_step(st);
    if (status == SQLITE_DONE) break;
    if (status != SQLITE_ROW) {
      error = sqlite3_errmsg(db);
      sqlite3_finalize(st);
      sqlite3_close(db);
      return false;
    }
    athena_namespace_definition ns;
    ns.name = aofm_column_string(st, 0);
    ns.kind = athena_namespaces::canonical_kind(aofm_column_string(st, 1));
    ns.templ = aofm_column_string(st, 2);
    ns.sorter_trivial = sqlite3_column_int(st, 3) != 0;
    ns.sorter_path = aofm_column_string(st, 4);
    ns.style_path = aofm_column_string(st, 5);
    ns.initial_content_path = aofm_column_string(st, 6);
    ns.homepage_path = aofm_column_string(st, 7);
    out.push_back(ns);
  }
  sqlite3_finalize(st);
  sqlite3_close(db);
  return true;
}

bool
load_model_vault_info(const std::string& model_vault,
                      const std::string& destination_root_path,
                      AofmModelVaultInfo& info) {
  if (model_vault.empty()) return true;
  std::filesystem::path root(model_vault);
  if (!root.is_absolute()) root = std::filesystem::absolute(root);
  if (!std::filesystem::is_directory(root)) {
    report_import_error("model vault path is not a directory");
    return false;
  }

  info.active = true;
  info.root = root.string();
  if (athena_vaultfile_present (root)) {
    AthenaVaultfileInfo vault_info;
    std::string error;
    if (!athena_vaultfile_read (root, vault_info, error)) {
      report_import_error ("failed to read model Vaultfile.json: " + error);
      return false;
    }
    info.prefs_rel = vault_info.preferences_path;
    info.namespace_db_rel = vault_info.namespace_db_path;
  }

  if (!info.prefs_rel.empty()) {
    if (!copy_model_file(model_join_path(info.root, info.prefs_rel),
                         join_unix_paths(destination_root_path, info.prefs_rel))) {
      report_import_error("failed to copy model vault preferences");
      return false;
    }
  }
  if (!info.namespace_db_rel.empty()) {
    if (!copy_model_file(model_join_path(info.root, info.namespace_db_rel),
                         join_unix_paths(destination_root_path,
                                         info.namespace_db_rel))) {
      report_import_error("failed to copy model vault namespace database");
      return false;
    }
  }

  std::string error;
  if (!load_model_namespaces(model_join_path(info.root, info.namespace_db_rel),
                             info.namespaces, error)) {
    report_import_error("failed to read model namespace database: " + error);
    return false;
  }
  for (const athena_namespace_definition& ns : info.namespaces) {
    copy_model_namespace_resource(info, destination_root_path, ns.sorter_path);
    copy_model_namespace_resource(info, destination_root_path, ns.style_path);
    copy_model_namespace_resource(info, destination_root_path,
                                  ns.initial_content_path);
    copy_model_namespace_resource(info, destination_root_path,
                                  ns.homepage_path);
    if (ns.style_path != "") {
      string install_error;
      tree dummy(DOCUMENT);
      dummy = athena_namespace_apply_style_to_tree(
        dummy, ns, string(info.root.c_str()), install_error);
      if (install_error != "")
        report_import_warning("could not install model namespace style " +
                              std::string(as_charp(ns.style_path)) + ": " +
                              std::string(as_charp(install_error)));
    }
  }
  return true;
}

tree
apply_model_namespace_style(tree doc, const AofmModelVaultInfo& model,
                            const ImportFileInfo& file_info) {
  if (!model.active || model.namespaces.empty()) return doc;
  std::string stem = path_stem(file_info.relative_ath_path);
  std::vector<athena_namespace_definition> matches;
  for (const athena_namespace_definition& ns : model.namespaces) {
    if (ns.kind != "concrete" || ns.templ == "") continue;
    athena_namespace_match match;
    string error;
    if (athena_namespaces::match_stem(ns, stem, match, error))
      matches.push_back(ns);
  }
  if (matches.empty()) return doc;
  if (matches.size() > 1) {
    report_import_warning("file " + file_info.relative_ath_path +
                          " matches multiple concrete namespaces; using " +
                          std::string(as_charp(matches[0].name)));
  }
  string style_name = style_name_from_namespace_path(matches[0].style_path);
  if (style_name == "") return doc;
  return change_doc_attr(doc, "style", tuple(style_name));
}

bool
write_vault_database(url destination_root, const FileIndexMap& file_map,
                     const AnchorMap& anchor_map,
                     const HeadingMap& heading_map) {
  std::vector<AthenaVaultMapNode> nodes;

  for (const auto& entry : file_map) {
    const AofmVaultFileInfo& info = entry.second;
    nodes.push_back ({info.uuid, info.relative_ath_path, "", ""});
  }

  for (const auto& entry : anchor_map) {
    const AofmVaultAnchorInfo& info = entry.second;
    nodes.push_back ({info.uuid, info.path, "", info.anchor_1});
    if (!info.transclusion_uuid.empty() && !info.anchor_2.empty()) {
      nodes.push_back ({info.transclusion_uuid, info.path,
                        info.anchor_1, info.anchor_2});
    }
  }

  for (const auto& entry : heading_map) {
    const AofmVaultHeadingInfo& info = entry.second;
    nodes.push_back ({info.uuid, info.path, "", info.label});
    if (!info.transclusion_uuid.empty()) {
      nodes.push_back ({info.transclusion_uuid, info.path,
                        info.label, info.end_label});
    }
  }

  std::filesystem::path root (std::string (as_charp (concretize (
    destination_root))));
  AthenaVaultMapSqlite map;
  std::string error;
  if (!map.open (root / "map.sqlite", true, error) ||
      !map.replace_all (nodes, error)) {
    report_import_error ("failed to write vault database map.sqlite: " +
                         error);
    return false;
  }
  return true;
}


} // namespace aofm_import_vault_internal
