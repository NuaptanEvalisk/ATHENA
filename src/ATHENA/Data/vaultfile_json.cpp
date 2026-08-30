/******************************************************************************
* MODULE     : vaultfile_json.cpp
* DESCRIPTION: ATHENA vault metadata JSON reader/writer and legacy migration
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "ATHENA/Data/vaultfile_json.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QString>

#include <fstream>
#include <sstream>
#include <system_error>

namespace {

std::string
read_file (const std::filesystem::path& path, bool& ok) {
  std::ifstream in (path, std::ios::binary);
  if (!in) {
    ok= false;
    return {};
  }
  std::ostringstream buffer;
  buffer << in.rdbuf ();
  ok= true;
  return buffer.str ();
}

bool
write_file_atomic (const std::filesystem::path& path, const std::string& text,
                   std::string& error) {
  std::filesystem::path tmp= path;
  tmp += ".tmp";
  {
    std::ofstream out (tmp, std::ios::binary | std::ios::trunc);
    if (!out) {
      error= "Could not write " + tmp.string ();
      return false;
    }
    out << text;
    if (!out) {
      error= "Could not write " + tmp.string ();
      return false;
    }
  }
  std::error_code ec;
  std::filesystem::rename (tmp, path, ec);
  if (ec) {
    std::filesystem::remove (tmp);
    error= "Could not replace " + path.string () + ": " + ec.message ();
    return false;
  }
  return true;
}

QString
qs (const std::string& s) {
  return QString::fromUtf8 (s.c_str (), (qsizetype) s.size ());
}

std::string
ss (const QString& s) {
  QByteArray bytes= s.toUtf8 ();
  return std::string (bytes.constData (), (size_t) bytes.size ());
}

std::string
json_string (const QJsonObject& obj, const char* key,
             const std::string& fallback= "") {
  QJsonValue v= obj.value (QString::fromLatin1 (key));
  if (!v.isString ()) return fallback;
  return ss (v.toString ());
}

std::filesystem::path
legacy_backup_path (const std::filesystem::path& root) {
  std::filesystem::path base= root / "Vaultfile.old.bak";
  if (!std::filesystem::exists (base)) return base;
  for (int i=1; i<10000; i++) {
    std::filesystem::path candidate= root /
      ("Vaultfile.old.bak." + std::to_string (i));
    if (!std::filesystem::exists (candidate)) return candidate;
  }
  return root / "Vaultfile.old.bak.overflow";
}

bool
read_json_file (const std::filesystem::path& path, AthenaVaultfileInfo& info,
                std::string& error) {
  bool ok= false;
  std::string text= read_file (path, ok);
  if (!ok) {
    error= "Could not read " + path.string ();
    return false;
  }
  QJsonParseError parse_error;
  QJsonDocument doc= QJsonDocument::fromJson (
    QByteArray (text.data (), (qsizetype) text.size ()), &parse_error);
  if (parse_error.error != QJsonParseError::NoError || !doc.isObject ()) {
    error= "Invalid Vaultfile.json in " + path.parent_path ().string () +
           ": " + ss (parse_error.errorString ());
    return false;
  }
  QJsonObject obj= doc.object ();
  info.name= json_string (obj, "name", "Vault");
  info.map_path= json_string (obj, "map_path", "map.sqlite");
  info.preferences_path= json_string (obj, "preferences_path");
  info.namespace_db_path= json_string (obj, "namespace_db_path",
                                      "ns.sqlite");
  info.startup_page= json_string (obj, "startup_page");
  info.one_time_startup_page= json_string (obj, "one_time_startup_page");
  info.maintenance_summary_path= json_string (obj,
                                             "maintenance_summary_path");
  info.rag_index_path= json_string (obj, "rag_index_path", "rag.sqlite");
  info.websites_path= json_string (obj, "websites_path", "websites.json");
  info.root_namespace= json_string (obj, "root_namespace");
  info.artifacts_path= json_string (obj, "artifacts_path", "artifacts.db");
  info.enunciations_path= json_string (obj, "enunciations_path",
                                      "enunciations.db");
  info.bold_text_path= json_string (obj, "bold_text_path", "bold-text.db");
  info.materials_db_path= json_string (obj, "materials_db_path",
                                      "materials.sqlite");
  info.materials_directory= json_string (obj, "materials_directory",
                                         "materials");
  info.artifact_title_filter_path= json_string (
    obj, "artifact_title_filter_path", "artifact-title-filter.lst");
  info.backup_dispatchers.clear ();
  QJsonValue dispatchers_value= obj.value ("backup_dispatchers");
  if (dispatchers_value.isArray ()) {
    for (const QJsonValue& value: dispatchers_value.toArray ()) {
      if (!value.isObject ()) {
        error= "Invalid backup dispatcher in " + path.string ();
        return false;
      }
      QJsonObject dispatcher= value.toObject ();
      AthenaBackupDispatcher entry;
      entry.destination= json_string (dispatcher, "destination");
      entry.trigger= json_string (dispatcher, "trigger");
      if (entry.destination.empty () ||
          (entry.trigger != "realtime" && entry.trigger != "maintenance" &&
           entry.trigger != "idle")) {
        error= "Invalid backup dispatcher destination or trigger in " +
               path.string ();
        return false;
      }
      info.backup_dispatchers.push_back (entry);
    }
  }
  info= athena_vaultfile_normalize (info);
  return true;
}

} // namespace

std::filesystem::path
athena_vaultfile_json_path (const std::filesystem::path& root) {
  return root / "Vaultfile.json";
}

std::filesystem::path
athena_vaultfile_legacy_path (const std::filesystem::path& root) {
  return root / "Vaultfile";
}

bool
athena_vaultfile_present (const std::filesystem::path& root) {
  return std::filesystem::exists (athena_vaultfile_json_path (root)) ||
         std::filesystem::exists (athena_vaultfile_legacy_path (root));
}

std::vector<std::string>
athena_vaultfile_legacy_strings (const std::string& text) {
  std::vector<std::string> values;
  bool in= false;
  bool esc= false;
  std::string cur;
  for (char c: text) {
    if (!in) {
      if (c == '"') {
        in= true;
        cur.clear ();
      }
      continue;
    }
    if (esc) {
      cur.push_back (c);
      esc= false;
      continue;
    }
    if (c == '\\') {
      esc= true;
      continue;
    }
    if (c == '"') {
      values.push_back (cur);
      in= false;
      continue;
    }
    cur.push_back (c);
  }
  return values;
}

AthenaVaultfileInfo
athena_vaultfile_normalize (const AthenaVaultfileInfo& info) {
  AthenaVaultfileInfo out= info;
  if (out.name.empty ()) out.name= "Vault";
  if (out.map_path.empty ()) out.map_path= "map.sqlite";
  if (out.namespace_db_path.empty ()) out.namespace_db_path= "ns.sqlite";
  if (out.rag_index_path.empty ()) out.rag_index_path= "rag.sqlite";
  if (out.websites_path.empty ()) out.websites_path= "websites.json";
  if (out.artifacts_path.empty ()) out.artifacts_path= "artifacts.db";
  if (out.enunciations_path.empty ()) out.enunciations_path= "enunciations.db";
  if (out.bold_text_path.empty ()) out.bold_text_path= "bold-text.db";
  if (out.materials_db_path.empty ())
    out.materials_db_path= "materials.sqlite";
  if (out.materials_directory.empty ()) out.materials_directory= "materials";
  if (out.artifact_title_filter_path.empty ())
    out.artifact_title_filter_path= "artifact-title-filter.lst";
  return out;
}

AthenaVaultfileInfo
athena_vaultfile_from_fields (const std::vector<std::string>& fields) {
  AthenaVaultfileInfo info;
  if (fields.size () >= 1) info.name= fields[0];
  if (fields.size () >= 2) info.map_path= fields[1];
  if (fields.size () >= 3) info.preferences_path= fields[2];
  if (fields.size () >= 4) info.namespace_db_path= fields[3];
  if (fields.size () >= 5) info.startup_page= fields[4];
  if (fields.size () >= 6) info.one_time_startup_page= fields[5];
  if (fields.size () >= 7) info.maintenance_summary_path= fields[6];
  if (fields.size () >= 8) info.rag_index_path= fields[7];
  if (fields.size () >= 9) info.websites_path= fields[8];
  if (fields.size () >= 10) info.root_namespace= fields[9];
  if (fields.size () >= 11) info.artifacts_path= fields[10];
  if (fields.size () >= 12) info.enunciations_path= fields[11];
  if (fields.size () >= 13) info.bold_text_path= fields[12];
  if (fields.size () >= 14) info.materials_db_path= fields[13];
  if (fields.size () >= 15) info.materials_directory= fields[14];
  if (fields.size () >= 16) info.artifact_title_filter_path= fields[15];
  return athena_vaultfile_normalize (info);
}

std::vector<std::string>
athena_vaultfile_to_fields (const AthenaVaultfileInfo& info) {
  AthenaVaultfileInfo out= athena_vaultfile_normalize (info);
  return { out.name,
           out.map_path,
           out.preferences_path,
           out.namespace_db_path,
           out.startup_page,
           out.one_time_startup_page,
           out.maintenance_summary_path,
           out.rag_index_path,
           out.websites_path,
           out.root_namespace,
           out.artifacts_path,
           out.enunciations_path,
           out.bold_text_path,
           out.materials_db_path,
           out.materials_directory,
           out.artifact_title_filter_path };
}

bool
athena_vaultfile_write (const std::filesystem::path& root,
                        const AthenaVaultfileInfo& info,
                        std::string& error) {
  std::error_code ec;
  if (!std::filesystem::exists (root, ec) ||
      !std::filesystem::is_directory (root, ec)) {
    error= "Vault root is not a directory: " + root.string ();
    return false;
  }

  AthenaVaultfileInfo out= athena_vaultfile_normalize (info);
  for (const AthenaBackupDispatcher& dispatcher: out.backup_dispatchers) {
    if (dispatcher.destination.empty () ||
        (dispatcher.trigger != "realtime" &&
         dispatcher.trigger != "maintenance" &&
         dispatcher.trigger != "idle")) {
      error= "Invalid backup dispatcher destination or trigger";
      return false;
    }
  }
  QJsonObject obj;
  obj["version"]= 1;
  obj["name"]= qs (out.name);
  obj["map_path"]= qs (out.map_path);
  obj["preferences_path"]= qs (out.preferences_path);
  obj["namespace_db_path"]= qs (out.namespace_db_path);
  obj["startup_page"]= qs (out.startup_page);
  obj["one_time_startup_page"]= qs (out.one_time_startup_page);
  obj["maintenance_summary_path"]= qs (out.maintenance_summary_path);
  obj["rag_index_path"]= qs (out.rag_index_path);
  obj["websites_path"]= qs (out.websites_path);
  obj["root_namespace"]= qs (out.root_namespace);
  obj["artifacts_path"]= qs (out.artifacts_path);
  obj["enunciations_path"]= qs (out.enunciations_path);
  obj["bold_text_path"]= qs (out.bold_text_path);
  obj["materials_db_path"]= qs (out.materials_db_path);
  obj["materials_directory"]= qs (out.materials_directory);
  obj["artifact_title_filter_path"]= qs (out.artifact_title_filter_path);
  QJsonArray dispatchers;
  for (const AthenaBackupDispatcher& entry: out.backup_dispatchers) {
    QJsonObject dispatcher;
    dispatcher["destination"]= qs (entry.destination);
    dispatcher["trigger"]= qs (entry.trigger);
    dispatchers.append (dispatcher);
  }
  obj["backup_dispatchers"]= dispatchers;
  QJsonDocument doc (obj);
  std::string text= ss (QString::fromUtf8 (
    doc.toJson (QJsonDocument::Indented)));
  return write_file_atomic (athena_vaultfile_json_path (root), text, error);
}

bool
athena_vaultfile_ensure_json (const std::filesystem::path& root,
                              std::string& error) {
  std::filesystem::path json_path= athena_vaultfile_json_path (root);
  if (std::filesystem::exists (json_path)) return true;

  std::filesystem::path legacy_path= athena_vaultfile_legacy_path (root);
  if (!std::filesystem::exists (legacy_path)) {
    error= "Missing Vaultfile.json in " + root.string ();
    return false;
  }

  bool ok= false;
  std::string text= read_file (legacy_path, ok);
  if (!ok) {
    error= "Could not read legacy Vaultfile in " + root.string ();
    return false;
  }
  std::vector<std::string> fields= athena_vaultfile_legacy_strings (text);
  if (fields.size () < 2) {
    error= "Invalid legacy Vaultfile in " + root.string ();
    return false;
  }
  AthenaVaultfileInfo info= athena_vaultfile_from_fields (fields);
  if (!athena_vaultfile_write (root, info, error)) return false;

  std::filesystem::path backup= legacy_backup_path (root);
  std::error_code ec;
  std::filesystem::rename (legacy_path, backup, ec);
  if (ec) {
    error= "Could not move legacy Vaultfile to " + backup.string () +
           ": " + ec.message ();
    return false;
  }
  return true;
}

bool
athena_vaultfile_read (const std::filesystem::path& root,
                       AthenaVaultfileInfo& info,
                       std::string& error) {
  if (!athena_vaultfile_ensure_json (root, error)) return false;
  return read_json_file (athena_vaultfile_json_path (root), info, error);
}
