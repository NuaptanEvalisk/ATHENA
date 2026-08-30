/******************************************************************************
* MODULE     : materials.cpp
* DESCRIPTION: Vault-native Materials database and managed attachments
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "ATHENA/Data/materials.hpp"

#include <QCryptographicHash>
#include <QFile>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QMimeDatabase>
#include <QRegularExpression>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QUuid>

#include <sqlite3.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <system_error>
#include <utility>

namespace fs= std::filesystem;

namespace {

class Statement {
public:
  Statement (sqlite3* db, const char* sql): statement (nullptr) {
    sqlite3_prepare_v2 (db, sql, -1, &statement, nullptr);
  }
  ~Statement () { if (statement != nullptr) sqlite3_finalize (statement); }
  sqlite3_stmt* get () const { return statement; }
  explicit operator bool () const { return statement != nullptr; }

private:
  sqlite3_stmt* statement;
};

std::int64_t
now_seconds () {
  return std::chrono::duration_cast<std::chrono::seconds> (
           std::chrono::system_clock::now ().time_since_epoch ()).count ();
}

std::string
uuid_v4 () {
  QByteArray bytes= QUuid::createUuid ().toString (QUuid::WithoutBraces)
                      .toUtf8 ();
  return std::string (bytes.constData (), (size_t) bytes.size ());
}

QString
qs (const std::string& text) {
  return QString::fromUtf8 (text.data (), (qsizetype) text.size ());
}

std::string
ss (const QString& text) {
  QByteArray bytes= text.toUtf8 ();
  return std::string (bytes.constData (), (size_t) bytes.size ());
}

QString
qpath (const fs::path& path) {
  std::string text= path.u8string ();
  return QString::fromUtf8 (text.data (), (qsizetype) text.size ());
}

std::string
text_column (sqlite3_stmt* statement, int column) {
  const unsigned char* value= sqlite3_column_text (statement, column);
  if (value == nullptr) return {};
  return std::string (reinterpret_cast<const char*> (value),
                      (size_t) sqlite3_column_bytes (statement, column));
}

void
bind_text (sqlite3_stmt* statement, int column, const std::string& value) {
  sqlite3_bind_text (statement, column, value.data (), (int) value.size (),
                     SQLITE_TRANSIENT);
}

bool
exec_sql (sqlite3* db, const char* sql, std::string& error) {
  char* message= nullptr;
  int status= sqlite3_exec (db, sql, nullptr, nullptr, &message);
  if (status == SQLITE_OK) return true;
  error= message == nullptr ? sqlite3_errmsg (db) : message;
  sqlite3_free (message);
  return false;
}

bool
migrate_materials_v1_to_v2 (sqlite3* db, std::string& error) {
  const char* migration=
    "PRAGMA foreign_keys=OFF;"
    "PRAGMA legacy_alter_table=ON;"
    "BEGIN IMMEDIATE;"
    "ALTER TABLE materials RENAME TO materials_v1;"
    "CREATE TABLE materials("
      "uuid TEXT PRIMARY KEY,"
      "item_type TEXT NOT NULL,"
      "review_state TEXT NOT NULL CHECK(review_state IN "
        "('ready','needs_review','unrecognized','error')),"
      "extra_json TEXT NOT NULL DEFAULT '{}',"
      "revision INTEGER NOT NULL DEFAULT 1,"
      "created_at INTEGER NOT NULL,"
      "updated_at INTEGER NOT NULL"
    ");"
    "INSERT INTO materials(uuid,item_type,review_state,extra_json,revision,"
      "created_at,updated_at) "
    "SELECT uuid,item_type,"
      "CASE WHEN review_state='needs_review' AND NOT EXISTS("
        "SELECT 1 FROM material_fields f WHERE f.material_uuid=materials_v1.uuid "
        "AND f.name='title' AND trim(f.value)<>'' "
        "AND lower(trim(f.value))<>'untitled') "
      "THEN 'unrecognized' ELSE review_state END,"
      "extra_json,revision,created_at,updated_at FROM materials_v1;"
    "DROP TABLE materials_v1;"
    "PRAGMA user_version=2;"
    "COMMIT;"
    "PRAGMA legacy_alter_table=OFF;"
    "PRAGMA foreign_keys=ON;";
  if (exec_sql (db, migration, error)) return true;
  std::string ignored;
  exec_sql (db, "ROLLBACK;PRAGMA legacy_alter_table=OFF;"
                "PRAGMA foreign_keys=ON;", ignored);
  return false;
}

bool
exec_bound (sqlite3* db, const char* sql,
            const std::vector<std::string>& parameters, std::string& error) {
  Statement statement (db, sql);
  if (!statement) {
    error= sqlite3_errmsg (db);
    return false;
  }
  for (size_t i=0; i<parameters.size (); ++i)
    bind_text (statement.get (), (int) i + 1, parameters[i]);
  if (sqlite3_step (statement.get ()) == SQLITE_DONE) return true;
  error= sqlite3_errmsg (db);
  return false;
}

bool
begin_transaction (sqlite3* db, std::string& error) {
  return exec_sql (db, "BEGIN IMMEDIATE;", error);
}

bool
commit_transaction (sqlite3* db, std::string& error) {
  return exec_sql (db, "COMMIT;", error);
}

void
rollback_transaction (sqlite3* db) {
  std::string ignored;
  exec_sql (db, "ROLLBACK;", ignored);
}

bool
json_object_valid (const std::string& json) {
  QJsonParseError parse_error;
  QJsonDocument document= QJsonDocument::fromJson (
    QByteArray (json.data (), (qsizetype) json.size ()), &parse_error);
  return parse_error.error == QJsonParseError::NoError && document.isObject ();
}

std::string
lower_ascii (std::string value) {
  std::transform (value.begin (), value.end (), value.begin (),
                  [] (unsigned char c) { return (char) std::tolower (c); });
  return value;
}

bool
valid_uuid (const std::string& value) {
  QUuid uuid (qs (value));
  if (uuid.isNull ()) return false;
  return lower_ascii (ss (uuid.toString (QUuid::WithoutBraces))) ==
         lower_ascii (value);
}

std::string
trim_ascii (std::string value) {
  auto space= [] (unsigned char c) { return std::isspace (c) != 0; };
  while (!value.empty () && space ((unsigned char) value.front ()))
    value.erase (value.begin ());
  while (!value.empty () && space ((unsigned char) value.back ()))
    value.pop_back ();
  return value;
}

bool
has_path_prefix (const fs::path& path, const fs::path& prefix) {
  auto path_it= path.begin ();
  auto prefix_it= prefix.begin ();
  for (; prefix_it != prefix.end (); ++prefix_it, ++path_it)
    if (path_it == path.end () || *path_it != *prefix_it) return false;
  return true;
}

bool
safe_vault_path (const fs::path& root, const std::string& configured,
                 fs::path& result, std::string& error) {
  fs::path relative= fs::u8path (configured);
  if (configured.empty () || relative.is_absolute ()) {
    error= "Materials paths in Vaultfile.json must be non-empty and "
           "relative to the vault";
    return false;
  }
  for (const fs::path& component: relative)
    if (component == "..") {
      error= "Materials paths may not escape the vault: " + configured;
      return false;
    }

  std::error_code ec;
  fs::path canonical_root= fs::weakly_canonical (root, ec);
  if (ec) {
    error= "Could not resolve vault root " + root.string () + ": " +
           ec.message ();
    return false;
  }
  fs::path candidate= fs::weakly_canonical (root / relative, ec);
  if (ec) {
    error= "Could not resolve Materials path " + configured + ": " +
           ec.message ();
    return false;
  }
  if (!has_path_prefix (candidate, canonical_root)) {
    error= "Materials path resolves outside the vault: " + configured;
    return false;
  }
  result= candidate;
  return true;
}

bool
file_sha256 (const fs::path& path, std::string& hash, std::string& error) {
  QFile file (qpath (path));
  if (!file.open (QIODevice::ReadOnly)) {
    error= "Could not read material file " + path.string () + ": " +
           ss (file.errorString ());
    return false;
  }
  QCryptographicHash digest (QCryptographicHash::Sha256);
  while (!file.atEnd ()) {
    QByteArray chunk= file.read (1024 * 1024);
    if (chunk.isEmpty () && file.error () != QFileDevice::NoError) {
      error= "Could not hash material file " + path.string () + ": " +
             ss (file.errorString ());
      return false;
    }
    digest.addData (chunk);
  }
  hash= digest.result ().toHex ().toStdString ();
  return true;
}

std::string
sanitize_filename_component (const std::string& input,
                             const std::string& fallback) {
  QString value= qs (input).normalized (QString::NormalizationForm_C);
  QString clean;
  clean.reserve (value.size ());
  const QString forbidden= QStringLiteral ("<>:\"/\\|?*");
  for (QChar c: value) {
    if (c.unicode () < 0x20 || forbidden.contains (c)) clean += QChar (' ');
    else clean += c;
  }
  clean.replace (QRegularExpression (QStringLiteral ("\\s+")), " ");
  clean= clean.trimmed ();
  while (clean.endsWith ('.') || clean.endsWith (' ')) clean.chop (1);
  if (clean.isEmpty ()) clean= qs (fallback);

  static const QSet<QString> reserved= {
    "CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3", "COM4",
    "COM5", "COM6", "COM7", "COM8", "COM9", "LPT1", "LPT2", "LPT3",
    "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"
  };
  if (reserved.contains (clean.toUpper ())) clean.prepend ('_');

  while (clean.toUtf8 ().size () > 180 && !clean.isEmpty ()) clean.chop (1);
  return ss (clean.trimmed ());
}

std::string
material_title (const MaterialRecord& material) {
  std::string title= material.field ("title");
  return title.empty () ? "Untitled" : title;
}

std::string
material_issued (const MaterialRecord& material) {
  std::string issued= material.field ("date");
  if (issued.empty ()) issued= material.field ("issued");
  return issued;
}

std::string
material_year (const MaterialRecord& material) {
  std::string issued= material_issued (material);
  std::smatch match;
  if (std::regex_search (issued, match, std::regex ("[0-9]{4}")))
    return match.str ();
  return "n.d.";
}

std::string
material_first_creator (const MaterialRecord& material) {
  if (material.creators.empty ()) return "Unknown";
  std::vector<MaterialCreator> creators= material.creators;
  std::stable_sort (creators.begin (), creators.end (),
                    [] (const MaterialCreator& a, const MaterialCreator& b) {
                      return a.ordinal < b.ordinal;
                    });
  for (const MaterialCreator& creator: creators) {
    if (!creator.family.empty ()) return creator.family;
    if (!creator.literal.empty ()) return creator.literal;
    if (!creator.given.empty ()) return creator.given;
  }
  return "Unknown";
}

std::string
join_creators (const MaterialRecord& material) {
  std::vector<MaterialCreator> creators= material.creators;
  std::stable_sort (creators.begin (), creators.end (),
                    [] (const MaterialCreator& a, const MaterialCreator& b) {
                      return a.ordinal < b.ordinal;
                    });
  std::string result;
  for (const MaterialCreator& creator: creators) {
    std::string name= creator.literal;
    if (name.empty ()) {
      name= creator.family;
      if (!creator.given.empty ()) {
        if (!name.empty ()) name += ", ";
        name += creator.given;
      }
    }
    if (name.empty ()) continue;
    if (!result.empty ()) result += "; ";
    result += name;
  }
  return result;
}

std::string
join_identifiers (const MaterialRecord& material) {
  std::string result;
  for (const MaterialIdentifier& identifier: material.identifiers) {
    if (!result.empty ()) result += " ";
    result += identifier.scheme + ":" + identifier.value;
  }
  return result;
}

std::string
join_tags (const MaterialRecord& material) {
  std::string result;
  for (const std::string& tag: material.tags) {
    if (!result.empty ()) result += " ";
    result += tag;
  }
  return result;
}

bool
rebuild_search_row (sqlite3* db, const MaterialRecord& material,
                    std::string& error) {
  Statement delete_cache (db, "DELETE FROM material_search WHERE uuid=?;");
  Statement delete_fts (db, "DELETE FROM materials_fts WHERE uuid=?;");
  if (!delete_cache || !delete_fts) {
    error= sqlite3_errmsg (db);
    return false;
  }
  bind_text (delete_cache.get (), 1, material.uuid);
  bind_text (delete_fts.get (), 1, material.uuid);
  if (sqlite3_step (delete_cache.get ()) != SQLITE_DONE ||
      sqlite3_step (delete_fts.get ()) != SQLITE_DONE) {
    error= sqlite3_errmsg (db);
    return false;
  }

  std::string title= material_title (material);
  std::string creators= join_creators (material);
  std::string issued= material_issued (material);
  std::string identifiers= join_identifiers (material);
  std::string abstract_text= material.field ("abstractNote");
  if (abstract_text.empty ()) abstract_text= material.field ("abstract");
  std::string tags= join_tags (material);

  Statement cache (db,
    "INSERT INTO material_search(uuid,title,creators,issued,identifiers,"
    "abstract_text,tags) VALUES(?,?,?,?,?,?,?);");
  Statement fts (db,
    "INSERT INTO materials_fts(uuid,title,creators,identifiers,abstract_text,"
    "tags) VALUES(?,?,?,?,?,?);");
  if (!cache || !fts) {
    error= sqlite3_errmsg (db);
    return false;
  }
  bind_text (cache.get (), 1, material.uuid);
  bind_text (cache.get (), 2, title);
  bind_text (cache.get (), 3, creators);
  bind_text (cache.get (), 4, issued);
  bind_text (cache.get (), 5, identifiers);
  bind_text (cache.get (), 6, abstract_text);
  bind_text (cache.get (), 7, tags);
  bind_text (fts.get (), 1, material.uuid);
  bind_text (fts.get (), 2, title);
  bind_text (fts.get (), 3, creators);
  bind_text (fts.get (), 4, identifiers);
  bind_text (fts.get (), 5, abstract_text);
  bind_text (fts.get (), 6, tags);
  if (sqlite3_step (cache.get ()) != SQLITE_DONE ||
      sqlite3_step (fts.get ()) != SQLITE_DONE) {
    error= sqlite3_errmsg (db);
    return false;
  }
  return true;
}

bool
insert_record_children (sqlite3* db, MaterialRecord& material,
                        std::string& error) {
  Statement field (db,
    "INSERT INTO material_fields(material_uuid,name,value,language,ordinal) "
    "VALUES(?,?,?,?,?);");
  Statement creator (db,
    "INSERT INTO material_creators(material_uuid,role,given_name,family_name,"
    "literal_name,suffix,ordinal) VALUES(?,?,?,?,?,?,?);");
  Statement identifier (db,
    "INSERT INTO material_identifiers(material_uuid,scheme,value,"
    "normalized_value) VALUES(?,?,?,?);");
  Statement tag (db,
    "INSERT OR IGNORE INTO material_tags(material_uuid,tag) VALUES(?,?);");
  Statement provenance (db,
    "INSERT INTO material_provenance(material_uuid,field_name,source_kind,"
    "source_reference,observed_value,confidence,observed_at) "
    "VALUES(?,?,?,?,?,?,?);");
  if (!field || !creator || !identifier || !tag || !provenance) {
    error= sqlite3_errmsg (db);
    return false;
  }

  for (MaterialField& entry: material.fields) {
    sqlite3_reset (field.get ());
    sqlite3_clear_bindings (field.get ());
    bind_text (field.get (), 1, material.uuid);
    bind_text (field.get (), 2, entry.name);
    bind_text (field.get (), 3, entry.value);
    bind_text (field.get (), 4, entry.language);
    sqlite3_bind_int (field.get (), 5, entry.ordinal);
    if (entry.name.empty () || sqlite3_step (field.get ()) != SQLITE_DONE) {
      error= entry.name.empty () ? "Material field names may not be empty"
                                 : sqlite3_errmsg (db);
      return false;
    }
  }
  for (MaterialCreator& entry: material.creators) {
    sqlite3_reset (creator.get ());
    sqlite3_clear_bindings (creator.get ());
    bind_text (creator.get (), 1, material.uuid);
    bind_text (creator.get (), 2, entry.role);
    bind_text (creator.get (), 3, entry.given);
    bind_text (creator.get (), 4, entry.family);
    bind_text (creator.get (), 5, entry.literal);
    bind_text (creator.get (), 6, entry.suffix);
    sqlite3_bind_int (creator.get (), 7, entry.ordinal);
    if (sqlite3_step (creator.get ()) != SQLITE_DONE) {
      error= sqlite3_errmsg (db);
      return false;
    }
  }
  for (MaterialIdentifier& entry: material.identifiers) {
    entry.scheme= lower_ascii (trim_ascii (entry.scheme));
    entry.value= trim_ascii (entry.value);
    entry.normalized_value= MaterialsStore::normalize_identifier (
      entry.scheme, entry.value);
    if (entry.scheme.empty () || entry.normalized_value.empty ()) continue;
    sqlite3_reset (identifier.get ());
    sqlite3_clear_bindings (identifier.get ());
    bind_text (identifier.get (), 1, material.uuid);
    bind_text (identifier.get (), 2, entry.scheme);
    bind_text (identifier.get (), 3, entry.value);
    bind_text (identifier.get (), 4, entry.normalized_value);
    if (sqlite3_step (identifier.get ()) != SQLITE_DONE) {
      error= sqlite3_errmsg (db);
      return false;
    }
  }
  for (const std::string& raw: material.tags) {
    std::string value= trim_ascii (raw);
    if (value.empty ()) continue;
    sqlite3_reset (tag.get ());
    sqlite3_clear_bindings (tag.get ());
    bind_text (tag.get (), 1, material.uuid);
    bind_text (tag.get (), 2, value);
    if (sqlite3_step (tag.get ()) != SQLITE_DONE) {
      error= sqlite3_errmsg (db);
      return false;
    }
  }
  for (const MaterialProvenance& entry: material.provenance) {
    sqlite3_reset (provenance.get ());
    sqlite3_clear_bindings (provenance.get ());
    bind_text (provenance.get (), 1, material.uuid);
    bind_text (provenance.get (), 2, entry.field_name);
    bind_text (provenance.get (), 3, entry.source_kind);
    bind_text (provenance.get (), 4, entry.source_reference);
    bind_text (provenance.get (), 5, entry.observed_value);
    sqlite3_bind_double (provenance.get (), 6,
                         std::clamp (entry.confidence, 0.0, 1.0));
    sqlite3_bind_int64 (provenance.get (), 7, now_seconds ());
    if (sqlite3_step (provenance.get ()) != SQLITE_DONE) {
      error= sqlite3_errmsg (db);
      return false;
    }
  }
  return true;
}

bool
load_record (sqlite3* db, const std::string& uuid, MaterialRecord& material,
             std::string& error) {
  Statement main (db,
    "SELECT uuid,item_type,review_state,extra_json,revision,created_at,"
    "updated_at FROM materials WHERE uuid=?;");
  if (!main) {
    error= sqlite3_errmsg (db);
    return false;
  }
  bind_text (main.get (), 1, uuid);
  int status= sqlite3_step (main.get ());
  if (status == SQLITE_DONE) return false;
  if (status != SQLITE_ROW) {
    error= sqlite3_errmsg (db);
    return false;
  }
  material= MaterialRecord {};
  material.uuid= text_column (main.get (), 0);
  material.item_type= text_column (main.get (), 1);
  material.review_state= text_column (main.get (), 2);
  material.extra_json= text_column (main.get (), 3);
  material.revision= sqlite3_column_int64 (main.get (), 4);
  material.created_at= sqlite3_column_int64 (main.get (), 5);
  material.updated_at= sqlite3_column_int64 (main.get (), 6);

  Statement fields (db,
    "SELECT name,value,language,ordinal FROM material_fields "
    "WHERE material_uuid=? ORDER BY name,ordinal;");
  Statement creators (db,
    "SELECT role,given_name,family_name,literal_name,suffix,ordinal "
    "FROM material_creators WHERE material_uuid=? ORDER BY ordinal;");
  Statement identifiers (db,
    "SELECT scheme,value,normalized_value FROM material_identifiers "
    "WHERE material_uuid=? ORDER BY scheme,value;");
  Statement tags (db,
    "SELECT tag FROM material_tags WHERE material_uuid=? ORDER BY tag;");
  Statement provenance (db,
    "SELECT field_name,source_kind,source_reference,observed_value,confidence "
    "FROM material_provenance WHERE material_uuid=? ORDER BY id;");
  if (!fields || !creators || !identifiers || !tags || !provenance) {
    error= sqlite3_errmsg (db);
    return false;
  }
  for (sqlite3_stmt* statement:
       {fields.get (), creators.get (), identifiers.get (), tags.get (),
        provenance.get ()})
    bind_text (statement, 1, uuid);

  while ((status= sqlite3_step (fields.get ())) == SQLITE_ROW)
    material.fields.push_back ({text_column (fields.get (), 0),
                                text_column (fields.get (), 1),
                                text_column (fields.get (), 2),
                                sqlite3_column_int (fields.get (), 3)});
  if (status != SQLITE_DONE) { error= sqlite3_errmsg (db); return false; }

  while ((status= sqlite3_step (creators.get ())) == SQLITE_ROW)
    material.creators.push_back ({text_column (creators.get (), 0),
                                  text_column (creators.get (), 1),
                                  text_column (creators.get (), 2),
                                  text_column (creators.get (), 3),
                                  text_column (creators.get (), 4),
                                  sqlite3_column_int (creators.get (), 5)});
  if (status != SQLITE_DONE) { error= sqlite3_errmsg (db); return false; }

  while ((status= sqlite3_step (identifiers.get ())) == SQLITE_ROW)
    material.identifiers.push_back ({text_column (identifiers.get (), 0),
                                     text_column (identifiers.get (), 1),
                                     text_column (identifiers.get (), 2)});
  if (status != SQLITE_DONE) { error= sqlite3_errmsg (db); return false; }

  while ((status= sqlite3_step (tags.get ())) == SQLITE_ROW)
    material.tags.push_back (text_column (tags.get (), 0));
  if (status != SQLITE_DONE) { error= sqlite3_errmsg (db); return false; }

  while ((status= sqlite3_step (provenance.get ())) == SQLITE_ROW)
    material.provenance.push_back ({text_column (provenance.get (), 0),
                                    text_column (provenance.get (), 1),
                                    text_column (provenance.get (), 2),
                                    text_column (provenance.get (), 3),
                                    sqlite3_column_double (
                                      provenance.get (), 4)});
  if (status != SQLITE_DONE) { error= sqlite3_errmsg (db); return false; }
  return true;
}

MaterialSearchHit
search_hit (sqlite3_stmt* statement) {
  MaterialSearchHit hit;
  hit.uuid= text_column (statement, 0);
  hit.item_type= text_column (statement, 1);
  hit.title= text_column (statement, 2);
  hit.creators= text_column (statement, 3);
  hit.issued= text_column (statement, 4);
  hit.review_state= text_column (statement, 5);
  hit.rank= sqlite3_column_double (statement, 6);
  return hit;
}

std::string
fts_query (const std::string& query) {
  QStringList terms= qs (query).split (QRegularExpression ("\\s+"),
                                       Qt::SkipEmptyParts);
  QStringList escaped;
  for (QString term: terms) {
    term.replace ('"', "\"\"");
    escaped << ("\"" + term + "\"*");
  }
  return ss (escaped.join (" AND "));
}

fs::path
unique_destination (const fs::path& directory, const std::string& filename,
                    const std::string& material_uuid) {
  fs::path requested= directory / fs::u8path (filename);
  if (!fs::exists (requested)) return requested;
  fs::path stem= requested.stem ();
  fs::path extension= requested.extension ();
  std::string suffix= material_uuid.substr (0, std::min<size_t> (8,
                                      material_uuid.size ()));
  fs::path candidate= directory /
    fs::u8path (stem.u8string () + " [" + suffix + "]" + extension.u8string ());
  if (!fs::exists (candidate)) return candidate;
  for (int index= 2; index<10000; ++index) {
    candidate= directory / fs::u8path (
      stem.u8string () + " [" + suffix + "-" + std::to_string (index) + "]" +
      extension.u8string ());
    if (!fs::exists (candidate)) return candidate;
  }
  return directory / fs::u8path (uuid_v4 () + extension.u8string ());
}

} // namespace

struct MaterialsStore::Impl {
  sqlite3* db= nullptr;
  fs::path root;
  fs::path db_path;
  fs::path files_path;
};

std::string
MaterialRecord::field (const std::string& name) const {
  for (const MaterialField& entry: fields)
    if (entry.name == name && entry.ordinal == 0) return entry.value;
  for (const MaterialField& entry: fields)
    if (entry.name == name) return entry.value;
  return {};
}

MaterialsStore::MaterialsStore (): impl (std::make_unique<Impl> ()) {}
MaterialsStore::~MaterialsStore () { close (); }
MaterialsStore::MaterialsStore (MaterialsStore&&) noexcept= default;
MaterialsStore& MaterialsStore::operator= (MaterialsStore&&) noexcept= default;

bool
MaterialsStore::open (const fs::path& vault_root,
                      const AthenaVaultfileInfo& vault_info,
                      std::string& error) {
  close ();
  if (!fs::exists (vault_root) || !fs::is_directory (vault_root)) {
    error= "Vault root is not a directory: " + vault_root.string ();
    return false;
  }
  fs::path db_path;
  fs::path files_path;
  if (!safe_vault_path (vault_root, vault_info.materials_db_path, db_path,
                        error) ||
      !safe_vault_path (vault_root, vault_info.materials_directory, files_path,
                        error))
    return false;
  if (db_path == files_path) {
    error= "Materials database and file directory must be separate paths";
    return false;
  }

  std::error_code ec;
  fs::create_directories (db_path.parent_path (), ec);
  if (ec) {
    error= "Could not create Materials database directory: " + ec.message ();
    return false;
  }
  fs::create_directories (files_path, ec);
  if (ec) {
    error= "Could not create Materials directory: " + ec.message ();
    return false;
  }
  fs::path checked_files;
  if (!safe_vault_path (vault_root, vault_info.materials_directory,
                        checked_files, error) || checked_files != files_path)
    return false;

  sqlite3* db= nullptr;
  if (sqlite3_open_v2 (db_path.string ().c_str (), &db,
                       SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE |
                       SQLITE_OPEN_FULLMUTEX, nullptr) != SQLITE_OK) {
    error= db == nullptr ? "Could not open Materials database"
                         : sqlite3_errmsg (db);
    if (db != nullptr) sqlite3_close (db);
    return false;
  }
  impl->db= db;
  impl->root= fs::weakly_canonical (vault_root);
  impl->db_path= db_path;
  impl->files_path= files_path;

  if (!exec_sql (db,
                 "PRAGMA foreign_keys=ON;PRAGMA busy_timeout=5000;"
                 "PRAGMA journal_mode=WAL;PRAGMA synchronous=NORMAL;",
                 error)) {
    close ();
    return false;
  }
  int schema_version= 0;
  {
    Statement version (db, "PRAGMA user_version;");
    if (!version || sqlite3_step (version.get ()) != SQLITE_ROW) {
      error= sqlite3_errmsg (db);
      close ();
      return false;
    }
    schema_version= sqlite3_column_int (version.get (), 0);
  }
  if (schema_version > 2) {
    error= "Materials database schema " + std::to_string (schema_version) +
           " is newer than this ATHENA build supports";
    close ();
    return false;
  }
  if (schema_version == 1 && !migrate_materials_v1_to_v2 (db, error)) {
    close ();
    return false;
  }

  const char* schema=
    "CREATE TABLE IF NOT EXISTS materials("
      "uuid TEXT PRIMARY KEY,"
      "item_type TEXT NOT NULL,"
      "review_state TEXT NOT NULL CHECK(review_state IN "
        "('ready','needs_review','unrecognized','error')),"
      "extra_json TEXT NOT NULL DEFAULT '{}',"
      "revision INTEGER NOT NULL DEFAULT 1,"
      "created_at INTEGER NOT NULL,"
      "updated_at INTEGER NOT NULL"
    ");"
    "CREATE TABLE IF NOT EXISTS material_fields("
      "material_uuid TEXT NOT NULL REFERENCES materials(uuid) ON DELETE CASCADE,"
      "name TEXT NOT NULL, value TEXT NOT NULL, language TEXT NOT NULL DEFAULT '',"
      "ordinal INTEGER NOT NULL DEFAULT 0,"
      "PRIMARY KEY(material_uuid,name,ordinal)"
    ");"
    "CREATE TABLE IF NOT EXISTS material_creators("
      "material_uuid TEXT NOT NULL REFERENCES materials(uuid) ON DELETE CASCADE,"
      "role TEXT NOT NULL, given_name TEXT NOT NULL DEFAULT '',"
      "family_name TEXT NOT NULL DEFAULT '', literal_name TEXT NOT NULL DEFAULT '',"
      "suffix TEXT NOT NULL DEFAULT '', ordinal INTEGER NOT NULL,"
      "PRIMARY KEY(material_uuid,role,ordinal)"
    ");"
    "CREATE TABLE IF NOT EXISTS material_identifiers("
      "material_uuid TEXT NOT NULL REFERENCES materials(uuid) ON DELETE CASCADE,"
      "scheme TEXT NOT NULL, value TEXT NOT NULL, normalized_value TEXT NOT NULL,"
      "PRIMARY KEY(material_uuid,scheme,normalized_value)"
    ");"
    "CREATE UNIQUE INDEX IF NOT EXISTS material_strong_identifiers "
      "ON material_identifiers(scheme,normalized_value) "
      "WHERE scheme IN ('doi','isbn','arxiv','pmid','pmcid');"
    "CREATE TABLE IF NOT EXISTS material_tags("
      "material_uuid TEXT NOT NULL REFERENCES materials(uuid) ON DELETE CASCADE,"
      "tag TEXT NOT NULL, PRIMARY KEY(material_uuid,tag)"
    ");"
    "CREATE TABLE IF NOT EXISTS material_relations("
      "subject_uuid TEXT NOT NULL REFERENCES materials(uuid) ON DELETE CASCADE,"
      "relation TEXT NOT NULL,"
      "object_uuid TEXT NOT NULL REFERENCES materials(uuid) ON DELETE CASCADE,"
      "PRIMARY KEY(subject_uuid,relation,object_uuid),"
      "CHECK(subject_uuid<>object_uuid)"
    ");"
    "CREATE TABLE IF NOT EXISTS material_aliases("
      "alias_uuid TEXT PRIMARY KEY,"
      "canonical_uuid TEXT NOT NULL REFERENCES materials(uuid) ON DELETE CASCADE,"
      "created_at INTEGER NOT NULL"
    ");"
    "CREATE TABLE IF NOT EXISTS material_attachments("
      "uuid TEXT PRIMARY KEY,"
      "material_uuid TEXT NOT NULL REFERENCES materials(uuid) ON DELETE CASCADE,"
      "role TEXT NOT NULL, stored_path TEXT NOT NULL UNIQUE,"
      "original_name TEXT NOT NULL, canonical_name TEXT NOT NULL,"
      "mime_type TEXT NOT NULL, sha256 TEXT NOT NULL UNIQUE,"
      "byte_size INTEGER NOT NULL, is_primary INTEGER NOT NULL DEFAULT 0,"
      "created_at INTEGER NOT NULL"
    ");"
    "CREATE UNIQUE INDEX IF NOT EXISTS material_one_primary_attachment "
      "ON material_attachments(material_uuid) WHERE is_primary=1;"
    "CREATE TABLE IF NOT EXISTS material_provenance("
      "id INTEGER PRIMARY KEY AUTOINCREMENT,"
      "material_uuid TEXT NOT NULL REFERENCES materials(uuid) ON DELETE CASCADE,"
      "field_name TEXT NOT NULL, source_kind TEXT NOT NULL,"
      "source_reference TEXT NOT NULL DEFAULT '', observed_value TEXT NOT NULL,"
      "confidence REAL NOT NULL, observed_at INTEGER NOT NULL"
    ");"
    "CREATE INDEX IF NOT EXISTS material_provenance_source "
      "ON material_provenance(source_kind,source_reference,material_uuid);"
    "CREATE TABLE IF NOT EXISTS material_landing_jobs("
      "uuid TEXT PRIMARY KEY, source_name TEXT NOT NULL, source_sha256 TEXT,"
      "state TEXT NOT NULL CHECK(state IN "
        "('staging','extracting','review','committing','completed','failed')),"
      "candidate_json TEXT NOT NULL DEFAULT '{}', error TEXT NOT NULL DEFAULT '',"
      "created_at INTEGER NOT NULL, updated_at INTEGER NOT NULL"
    ");"
    "CREATE TABLE IF NOT EXISTS material_search("
      "uuid TEXT PRIMARY KEY REFERENCES materials(uuid) ON DELETE CASCADE,"
      "title TEXT NOT NULL, creators TEXT NOT NULL, issued TEXT NOT NULL,"
      "identifiers TEXT NOT NULL, abstract_text TEXT NOT NULL, tags TEXT NOT NULL"
    ");"
    "CREATE VIRTUAL TABLE IF NOT EXISTS materials_fts USING fts5("
      "uuid UNINDEXED,title,creators,identifiers,abstract_text,tags,"
      "tokenize='unicode61 remove_diacritics 2'"
    ");"
    "PRAGMA user_version=2;";
  if (!exec_sql (db, schema, error)) {
    close ();
    return false;
  }
  return true;
}

void
MaterialsStore::close () {
  if (impl && impl->db != nullptr) sqlite3_close (impl->db);
  if (impl) {
    impl->db= nullptr;
    impl->root.clear ();
    impl->db_path.clear ();
    impl->files_path.clear ();
  }
}

bool MaterialsStore::is_open () const { return impl && impl->db != nullptr; }
const fs::path& MaterialsStore::vault_root () const { return impl->root; }
const fs::path& MaterialsStore::database_path () const { return impl->db_path; }
const fs::path& MaterialsStore::materials_directory () const {
  return impl->files_path;
}

bool
MaterialsStore::create (MaterialRecord& material, std::string& error) {
  if (!is_open ()) { error= "Materials database is not open"; return false; }
  if (material.item_type.empty ()) {
    error= "Material item type may not be empty";
    return false;
  }
  if (!json_object_valid (material.extra_json)) {
    error= "Material extra metadata must be a JSON object";
    return false;
  }
  if (material.uuid.empty ()) material.uuid= uuid_v4 ();
  if (!valid_uuid (material.uuid)) {
    error= "Material identifiers must be canonical UUIDs";
    return false;
  }
  material.created_at= material.updated_at= now_seconds ();
  material.revision= 1;

  if (!begin_transaction (impl->db, error)) return false;
  Statement insert (impl->db,
    "INSERT INTO materials(uuid,item_type,review_state,extra_json,revision,"
    "created_at,updated_at) VALUES(?,?,?,?,?,?,?);");
  if (!insert) {
    error= sqlite3_errmsg (impl->db);
    rollback_transaction (impl->db);
    return false;
  }
  bind_text (insert.get (), 1, material.uuid);
  bind_text (insert.get (), 2, material.item_type);
  bind_text (insert.get (), 3, material.review_state);
  bind_text (insert.get (), 4, material.extra_json);
  sqlite3_bind_int64 (insert.get (), 5, material.revision);
  sqlite3_bind_int64 (insert.get (), 6, material.created_at);
  sqlite3_bind_int64 (insert.get (), 7, material.updated_at);
  if (sqlite3_step (insert.get ()) != SQLITE_DONE ||
      !insert_record_children (impl->db, material, error) ||
      !rebuild_search_row (impl->db, material, error) ||
      !commit_transaction (impl->db, error)) {
    if (error.empty ()) error= sqlite3_errmsg (impl->db);
    rollback_transaction (impl->db);
    return false;
  }
  return true;
}

bool
MaterialsStore::update (MaterialRecord& material,
                        std::int64_t expected_revision, std::string& error) {
  if (!is_open ()) { error= "Materials database is not open"; return false; }
  if (!json_object_valid (material.extra_json)) {
    error= "Material extra metadata must be a JSON object";
    return false;
  }
  std::string canonical= resolve_uuid (material.uuid, error);
  if (canonical.empty ()) return false;
  material.uuid= canonical;
  material.updated_at= now_seconds ();

  if (!begin_transaction (impl->db, error)) return false;
  Statement update_main (impl->db,
    "UPDATE materials SET item_type=?,review_state=?,extra_json=?,"
    "revision=revision+1,updated_at=? WHERE uuid=? AND revision=?;");
  if (!update_main) {
    error= sqlite3_errmsg (impl->db);
    rollback_transaction (impl->db);
    return false;
  }
  bind_text (update_main.get (), 1, material.item_type);
  bind_text (update_main.get (), 2, material.review_state);
  bind_text (update_main.get (), 3, material.extra_json);
  sqlite3_bind_int64 (update_main.get (), 4, material.updated_at);
  bind_text (update_main.get (), 5, material.uuid);
  sqlite3_bind_int64 (update_main.get (), 6, expected_revision);
  if (sqlite3_step (update_main.get ()) != SQLITE_DONE ||
      sqlite3_changes (impl->db) != 1) {
    error= "Material was modified by another operation";
    rollback_transaction (impl->db);
    return false;
  }
  for (const char* table:
       {"material_fields", "material_creators", "material_identifiers",
        "material_tags", "material_provenance"}) {
    std::string sql= "DELETE FROM " + std::string (table) +
                     " WHERE material_uuid=?;";
    Statement remove (impl->db, sql.c_str ());
    if (!remove) {
      error= sqlite3_errmsg (impl->db);
      rollback_transaction (impl->db);
      return false;
    }
    bind_text (remove.get (), 1, material.uuid);
    if (sqlite3_step (remove.get ()) != SQLITE_DONE) {
      error= sqlite3_errmsg (impl->db);
      rollback_transaction (impl->db);
      return false;
    }
  }
  material.revision= expected_revision + 1;
  if (!insert_record_children (impl->db, material, error) ||
      !rebuild_search_row (impl->db, material, error) ||
      !commit_transaction (impl->db, error)) {
    rollback_transaction (impl->db);
    return false;
  }
  return true;
}

bool
MaterialsStore::remove (const std::string& uuid, bool remove_managed_files,
                        std::string& error) {
  std::string canonical= resolve_uuid (uuid, error);
  if (canonical.empty ()) return false;
  std::vector<MaterialAttachment> files= attachments (canonical, error);
  if (!error.empty ()) return false;

  struct StagedFile {
    fs::path original;
    fs::path staged;
  };
  std::vector<StagedFile> staged;
  if (remove_managed_files) {
    for (const MaterialAttachment& attachment: files) {
      fs::path original= impl->root / fs::u8path (attachment.stored_path);
      if (!fs::exists (original)) continue;
      fs::path temporary= impl->files_path /
        fs::u8path (".athena-material-delete-" + uuid_v4 () + ".part");
      std::error_code ec;
      fs::rename (original, temporary, ec);
      if (ec) {
        for (auto it= staged.rbegin (); it != staged.rend (); ++it) {
          std::error_code ignored;
          fs::rename (it->staged, it->original, ignored);
        }
        error= "Could not stage managed Material file for deletion: " +
               ec.message ();
        return false;
      }
      staged.push_back ({original, temporary});
    }
  }

  if (!begin_transaction (impl->db, error)) {
    for (auto it= staged.rbegin (); it != staged.rend (); ++it) {
      std::error_code ignored;
      fs::rename (it->staged, it->original, ignored);
    }
    return false;
  }
  Statement remove_fts (impl->db, "DELETE FROM materials_fts WHERE uuid=?;");
  Statement remove_material (impl->db, "DELETE FROM materials WHERE uuid=?;");
  if (!remove_fts || !remove_material) error= sqlite3_errmsg (impl->db);
  else {
    bind_text (remove_fts.get (), 1, canonical);
    bind_text (remove_material.get (), 1, canonical);
    if (sqlite3_step (remove_fts.get ()) != SQLITE_DONE ||
        sqlite3_step (remove_material.get ()) != SQLITE_DONE)
      error= sqlite3_errmsg (impl->db);
  }
  if (!error.empty () || !commit_transaction (impl->db, error)) {
    rollback_transaction (impl->db);
    for (auto it= staged.rbegin (); it != staged.rend (); ++it) {
      std::error_code ignored;
      fs::rename (it->staged, it->original, ignored);
    }
    return false;
  }
  for (const StagedFile& file: staged) {
    std::error_code ignored;
    fs::remove (file.staged, ignored);
  }
  return true;
}

std::optional<MaterialRecord>
MaterialsStore::get (const std::string& uuid, std::string& error) const {
  if (!is_open ()) { error= "Materials database is not open"; return {}; }
  std::string canonical= resolve_uuid (uuid, error);
  if (canonical.empty ()) return {};
  MaterialRecord material;
  if (!load_record (impl->db, canonical, material, error)) return {};
  return material;
}

std::vector<MaterialSearchHit>
MaterialsStore::list (int limit, int offset, std::string& error) const {
  std::vector<MaterialSearchHit> hits;
  if (!is_open ()) { error= "Materials database is not open"; return hits; }
  Statement query (impl->db,
    "SELECT m.uuid,m.item_type,s.title,s.creators,s.issued,m.review_state,0.0 "
    "FROM materials m JOIN material_search s ON s.uuid=m.uuid "
    "ORDER BY lower(s.creators),lower(s.title),m.uuid LIMIT ? OFFSET ?;");
  if (!query) { error= sqlite3_errmsg (impl->db); return hits; }
  sqlite3_bind_int (query.get (), 1, std::max (1, limit));
  sqlite3_bind_int (query.get (), 2, std::max (0, offset));
  int status;
  while ((status= sqlite3_step (query.get ())) == SQLITE_ROW)
    hits.push_back (search_hit (query.get ()));
  if (status != SQLITE_DONE) error= sqlite3_errmsg (impl->db);
  return hits;
}

std::vector<MaterialSearchHit>
MaterialsStore::search (const std::string& query_text, int limit,
                        std::string& error) const {
  if (trim_ascii (query_text).empty ()) return list (limit, 0, error);
  std::vector<MaterialSearchHit> hits;
  if (!is_open ()) { error= "Materials database is not open"; return hits; }
  std::string expression= fts_query (query_text);
  Statement query (impl->db,
    "SELECT m.uuid,m.item_type,s.title,s.creators,s.issued,m.review_state,"
    "bm25(materials_fts) FROM materials_fts "
    "JOIN materials m ON m.uuid=materials_fts.uuid "
    "JOIN material_search s ON s.uuid=m.uuid WHERE materials_fts MATCH ? "
    "ORDER BY bm25(materials_fts),lower(s.creators),lower(s.title) LIMIT ?;");
  if (!query) { error= sqlite3_errmsg (impl->db); return hits; }
  bind_text (query.get (), 1, expression);
  sqlite3_bind_int (query.get (), 2, std::max (1, limit));
  int status;
  while ((status= sqlite3_step (query.get ())) == SQLITE_ROW)
    hits.push_back (search_hit (query.get ()));
  if (status != SQLITE_DONE) error= sqlite3_errmsg (impl->db);
  return hits;
}

std::string
MaterialsStore::resolve_uuid (const std::string& uuid,
                              std::string& error) const {
  if (!is_open ()) { error= "Materials database is not open"; return {}; }
  std::string current= uuid;
  std::set<std::string> seen;
  for (int depth= 0; depth<64; ++depth) {
    if (!seen.insert (current).second) {
      error= "Cyclic Material UUID alias detected";
      return {};
    }
    Statement direct (impl->db, "SELECT 1 FROM materials WHERE uuid=?;");
    if (!direct) { error= sqlite3_errmsg (impl->db); return {}; }
    bind_text (direct.get (), 1, current);
    if (sqlite3_step (direct.get ()) == SQLITE_ROW) return current;

    Statement alias (impl->db,
                     "SELECT canonical_uuid FROM material_aliases "
                     "WHERE alias_uuid=?;");
    if (!alias) { error= sqlite3_errmsg (impl->db); return {}; }
    bind_text (alias.get (), 1, current);
    if (sqlite3_step (alias.get ()) != SQLITE_ROW) {
      error= "Unknown Material UUID: " + uuid;
      return {};
    }
    current= text_column (alias.get (), 0);
  }
  error= "Material UUID alias chain is too deep";
  return {};
}

bool
MaterialsStore::add_relation (const MaterialRelation& relation,
                              std::string& error) {
  std::string subject= resolve_uuid (relation.subject_uuid, error);
  if (subject.empty ()) return false;
  std::string object= resolve_uuid (relation.object_uuid, error);
  if (object.empty ()) return false;
  if (subject == object || relation.relation.empty ()) {
    error= "Material relation must connect two distinct materials";
    return false;
  }
  Statement insert (impl->db,
    "INSERT OR IGNORE INTO material_relations(subject_uuid,relation,"
    "object_uuid) VALUES(?,?,?);");
  if (!insert) { error= sqlite3_errmsg (impl->db); return false; }
  bind_text (insert.get (), 1, subject);
  bind_text (insert.get (), 2, relation.relation);
  bind_text (insert.get (), 3, object);
  if (sqlite3_step (insert.get ()) != SQLITE_DONE) {
    error= sqlite3_errmsg (impl->db);
    return false;
  }
  return true;
}

bool
MaterialsStore::merge (const std::string& canonical_uuid,
                       const std::string& duplicate_uuid,
                       std::string& error) {
  std::string canonical= resolve_uuid (canonical_uuid, error);
  if (canonical.empty ()) return false;
  std::string duplicate= resolve_uuid (duplicate_uuid, error);
  if (duplicate.empty ()) return false;
  if (canonical == duplicate) return true;
  if (!begin_transaction (impl->db, error)) return false;

  struct BoundOperation {
    const char* sql;
    std::vector<std::string> parameters;
  };
  std::string timestamp= std::to_string (now_seconds ());
  const std::vector<BoundOperation> operations= {
    {"INSERT OR IGNORE INTO material_fields "
       "SELECT ?,name,value,language,ordinal FROM material_fields "
       "WHERE material_uuid=?;", {canonical, duplicate}},
    {"INSERT OR IGNORE INTO material_creators "
       "SELECT ?,role,given_name,family_name,literal_name,suffix,ordinal "
       "FROM material_creators WHERE material_uuid=? AND NOT EXISTS("
       "SELECT 1 FROM material_creators WHERE material_uuid=?);",
       {canonical, duplicate, canonical}},
    {"INSERT OR IGNORE INTO material_identifiers "
       "SELECT ?,scheme,value,normalized_value FROM material_identifiers "
       "WHERE material_uuid=?;", {canonical, duplicate}},
    {"INSERT OR IGNORE INTO material_tags SELECT ?,tag FROM material_tags "
       "WHERE material_uuid=?;", {canonical, duplicate}},
    {"UPDATE material_attachments SET is_primary=0 WHERE material_uuid=? "
       "AND EXISTS(SELECT 1 FROM material_attachments "
       "WHERE material_uuid=? AND is_primary=1);", {duplicate, canonical}},
    {"UPDATE material_attachments SET material_uuid=? WHERE material_uuid=?;",
       {canonical, duplicate}},
    {"UPDATE material_provenance SET material_uuid=? WHERE material_uuid=?;",
       {canonical, duplicate}},
    {"INSERT OR IGNORE INTO material_relations(subject_uuid,relation,object_uuid) "
       "SELECT CASE WHEN subject_uuid=? THEN ? ELSE subject_uuid END,relation,"
       "CASE WHEN object_uuid=? THEN ? ELSE object_uuid END "
       "FROM material_relations WHERE subject_uuid=? OR object_uuid=?;",
       {duplicate, canonical, duplicate, canonical, duplicate, duplicate}},
    {"DELETE FROM material_relations WHERE subject_uuid=? OR object_uuid=?;",
       {duplicate, duplicate}},
    {"DELETE FROM material_relations WHERE subject_uuid=object_uuid;", {}},
    {"UPDATE material_aliases SET canonical_uuid=? WHERE canonical_uuid=?;",
       {canonical, duplicate}},
    {"INSERT INTO material_aliases(alias_uuid,canonical_uuid,created_at) "
       "VALUES(?,?,?);", {duplicate, canonical, timestamp}},
    {"DELETE FROM material_search WHERE uuid=?;", {duplicate}},
    {"DELETE FROM materials_fts WHERE uuid=?;", {duplicate}},
    {"DELETE FROM materials WHERE uuid=?;", {duplicate}},
    {"UPDATE materials SET revision=revision+1,updated_at=? WHERE uuid=?;",
       {timestamp, canonical}}
  };
  for (const BoundOperation& operation: operations)
    if (!exec_bound (impl->db, operation.sql, operation.parameters, error)) {
      rollback_transaction (impl->db);
      return false;
    }

  MaterialRecord merged;
  if (!load_record (impl->db, canonical, merged, error) ||
      !rebuild_search_row (impl->db, merged, error) ||
      !commit_transaction (impl->db, error)) {
    rollback_transaction (impl->db);
    return false;
  }
  return true;
}

std::optional<std::string>
MaterialsStore::material_for_identifier (const std::string& scheme,
                                         const std::string& value,
                                         std::string& error) const {
  if (!is_open ()) { error= "Materials database is not open"; return {}; }
  std::string normalized= normalize_identifier (scheme, value);
  Statement query (impl->db,
    "SELECT material_uuid FROM material_identifiers WHERE scheme=? AND "
    "normalized_value=? LIMIT 1;");
  if (!query) { error= sqlite3_errmsg (impl->db); return {}; }
  bind_text (query.get (), 1, lower_ascii (trim_ascii (scheme)));
  bind_text (query.get (), 2, normalized);
  int status= sqlite3_step (query.get ());
  if (status == SQLITE_ROW) return text_column (query.get (), 0);
  if (status != SQLITE_DONE) error= sqlite3_errmsg (impl->db);
  return {};
}

std::optional<std::string>
MaterialsStore::material_for_sha256 (const std::string& sha256,
                                     std::string& error) const {
  if (!is_open ()) { error= "Materials database is not open"; return {}; }
  Statement query (impl->db,
    "SELECT material_uuid FROM material_attachments WHERE sha256=? LIMIT 1;");
  if (!query) { error= sqlite3_errmsg (impl->db); return {}; }
  bind_text (query.get (), 1, lower_ascii (trim_ascii (sha256)));
  int status= sqlite3_step (query.get ());
  if (status == SQLITE_ROW) return text_column (query.get (), 0);
  if (status != SQLITE_DONE) error= sqlite3_errmsg (impl->db);
  return {};
}

std::optional<std::string>
MaterialsStore::material_for_source (const std::string& source_kind,
                                     const std::string& source_reference,
                                     std::string& error) const {
  if (!is_open ()) { error= "Materials database is not open"; return {}; }
  Statement query (impl->db,
    "SELECT material_uuid FROM material_provenance WHERE source_kind=? AND "
    "source_reference=? AND field_name='@record' ORDER BY id LIMIT 1;");
  if (!query) { error= sqlite3_errmsg (impl->db); return {}; }
  bind_text (query.get (), 1, source_kind);
  bind_text (query.get (), 2, source_reference);
  int status= sqlite3_step (query.get ());
  if (status == SQLITE_ROW) return text_column (query.get (), 0);
  if (status != SQLITE_DONE) error= sqlite3_errmsg (impl->db);
  return {};
}

bool
MaterialsStore::import_file (const std::string& material_uuid,
                             const fs::path& source, const std::string& role,
                             bool make_primary, MaterialImportResult& result,
                             std::string& error) {
  result= MaterialImportResult {};
  if (!fs::exists (source) || !fs::is_regular_file (source)) {
    error= "Material attachment is not a regular file: " + source.string ();
    return false;
  }
  std::string sha256;
  if (!file_sha256 (source, sha256, error)) return false;
  return import_file_with_sha256 (material_uuid, source, sha256, role,
                                  make_primary, result, error);
}

bool
MaterialsStore::import_material_file (
  MaterialRecord& material, const fs::path& source, const std::string& role,
  bool make_primary, MaterialImportResult& result, std::string& error) {
  result= MaterialImportResult {};
  if (!fs::exists (source) || !fs::is_regular_file (source)) {
    error= "Material attachment is not a regular file: " + source.string ();
    return false;
  }
  std::string sha256;
  if (!file_sha256 (source, sha256, error)) return false;

  std::optional<std::string> existing= material_for_sha256 (sha256, error);
  if (!error.empty ()) return false;
  if (existing) {
    if (!import_file_with_sha256 (*existing, source, sha256, role,
                                  make_primary, result, error))
      return false;
    std::optional<MaterialRecord> canonical= get (*existing, error);
    if (!canonical) return false;
    material= std::move (*canonical);
    return true;
  }

  if (!create (material, error)) return false;
  std::string created_uuid= material.uuid;
  bool imported= import_file_with_sha256 (
    created_uuid, source, sha256, role, make_primary, result, error);
  if (!imported || result.duplicate) {
    std::string cleanup_error;
    if (!remove (created_uuid, true, cleanup_error)) {
      if (!error.empty ()) error += "; ";
      error += "Could not remove incomplete Material " + created_uuid +
               ": " + cleanup_error;
      return false;
    }
  }
  if (!imported) return false;
  if (result.duplicate) {
    std::optional<MaterialRecord> canonical=
      get (result.existing_material_uuid, error);
    if (!canonical) return false;
    material= std::move (*canonical);
  }
  return true;
}

bool
MaterialsStore::import_file_with_sha256 (
  const std::string& material_uuid, const fs::path& source,
  const std::string& sha256, const std::string& role, bool make_primary,
  MaterialImportResult& result, std::string& error) {
  result= MaterialImportResult {};
  std::string canonical= resolve_uuid (material_uuid, error);
  if (canonical.empty ()) return false;
  std::optional<std::string> existing= material_for_sha256 (sha256, error);
  if (!error.empty ()) return false;
  if (existing) {
    Statement query (impl->db,
      "SELECT uuid,material_uuid,role,stored_path,original_name,canonical_name,"
      "mime_type,sha256,byte_size,is_primary,created_at "
      "FROM material_attachments WHERE sha256=?;");
    if (!query) { error= sqlite3_errmsg (impl->db); return false; }
    bind_text (query.get (), 1, sha256);
    if (sqlite3_step (query.get ()) != SQLITE_ROW) {
      error= sqlite3_errmsg (impl->db);
      return false;
    }
    result.attachment= {text_column (query.get (), 0),
                        text_column (query.get (), 1),
                        text_column (query.get (), 2),
                        text_column (query.get (), 3),
                        text_column (query.get (), 4),
                        text_column (query.get (), 5),
                        text_column (query.get (), 6),
                        text_column (query.get (), 7),
                        sqlite3_column_int64 (query.get (), 8),
                        sqlite3_column_int (query.get (), 9) != 0,
                        sqlite3_column_int64 (query.get (), 10)};
    result.duplicate= true;
    result.existing_material_uuid= *existing;
    return true;
  }

  std::optional<MaterialRecord> material= get (canonical, error);
  if (!material) return false;
  std::string filename= canonical_filename (*material, source);
  fs::path destination= unique_destination (impl->files_path, filename,
                                            canonical);
  fs::path temporary= impl->files_path /
    fs::u8path (".athena-material-import-" + uuid_v4 () + ".part");
  std::error_code ec;
  fs::copy_file (source, temporary, fs::copy_options::none, ec);
  if (ec) {
    error= "Could not copy material into the vault: " + ec.message ();
    return false;
  }
  fs::rename (temporary, destination, ec);
  if (ec) {
    fs::remove (temporary);
    error= "Could not publish material file: " + ec.message ();
    return false;
  }

  QMimeDatabase mime_database;
  std::string mime= ss (mime_database.mimeTypeForFile (
    qpath (destination), QMimeDatabase::MatchContent).name ());
  std::string attachment_uuid= uuid_v4 ();
  std::string stored_path= fs::relative (destination, impl->root).generic_string ();
  std::int64_t size= (std::int64_t) fs::file_size (destination, ec);
  if (ec) size= 0;

  if (!begin_transaction (impl->db, error)) {
    fs::remove (destination);
    return false;
  }
  Statement count (impl->db,
                   "SELECT count(*) FROM material_attachments "
                   "WHERE material_uuid=?;");
  if (!count) {
    error= sqlite3_errmsg (impl->db);
    rollback_transaction (impl->db);
    fs::remove (destination);
    return false;
  }
  bind_text (count.get (), 1, canonical);
  bool first= sqlite3_step (count.get ()) == SQLITE_ROW &&
              sqlite3_column_int (count.get (), 0) == 0;
  bool primary= make_primary || first;
  if (primary) {
    Statement clear (impl->db,
      "UPDATE material_attachments SET is_primary=0 WHERE material_uuid=?;");
    if (!clear) {
      error= sqlite3_errmsg (impl->db);
      rollback_transaction (impl->db);
      fs::remove (destination);
      return false;
    }
    bind_text (clear.get (), 1, canonical);
    if (sqlite3_step (clear.get ()) != SQLITE_DONE) {
      error= sqlite3_errmsg (impl->db);
      rollback_transaction (impl->db);
      fs::remove (destination);
      return false;
    }
  }
  Statement insert (impl->db,
    "INSERT INTO material_attachments(uuid,material_uuid,role,stored_path,"
    "original_name,canonical_name,mime_type,sha256,byte_size,is_primary,"
    "created_at) VALUES(?,?,?,?,?,?,?,?,?,?,?);");
  if (!insert) {
    error= sqlite3_errmsg (impl->db);
    rollback_transaction (impl->db);
    fs::remove (destination);
    return false;
  }
  bind_text (insert.get (), 1, attachment_uuid);
  bind_text (insert.get (), 2, canonical);
  bind_text (insert.get (), 3, role.empty () ? "document" : role);
  bind_text (insert.get (), 4, stored_path);
  bind_text (insert.get (), 5, source.filename ().u8string ());
  bind_text (insert.get (), 6, destination.filename ().u8string ());
  bind_text (insert.get (), 7, mime);
  bind_text (insert.get (), 8, sha256);
  sqlite3_bind_int64 (insert.get (), 9, size);
  sqlite3_bind_int (insert.get (), 10, primary ? 1 : 0);
  sqlite3_bind_int64 (insert.get (), 11, now_seconds ());
  if (sqlite3_step (insert.get ()) != SQLITE_DONE ||
      !commit_transaction (impl->db, error)) {
    if (error.empty ()) error= sqlite3_errmsg (impl->db);
    rollback_transaction (impl->db);
    fs::remove (destination);
    return false;
  }

  result.attachment= {attachment_uuid, canonical,
                      role.empty () ? "document" : role, stored_path,
                      source.filename ().u8string (),
                      destination.filename ().u8string (), mime, sha256, size,
                      primary, now_seconds ()};
  return true;
}

std::vector<MaterialAttachment>
MaterialsStore::attachments (const std::string& material_uuid,
                             std::string& error) const {
  std::vector<MaterialAttachment> result;
  std::string canonical= resolve_uuid (material_uuid, error);
  if (canonical.empty ()) return result;
  Statement query (impl->db,
    "SELECT uuid,material_uuid,role,stored_path,original_name,canonical_name,"
    "mime_type,sha256,byte_size,is_primary,created_at FROM material_attachments "
    "WHERE material_uuid=? ORDER BY is_primary DESC,created_at,uuid;");
  if (!query) { error= sqlite3_errmsg (impl->db); return result; }
  bind_text (query.get (), 1, canonical);
  int status;
  while ((status= sqlite3_step (query.get ())) == SQLITE_ROW)
    result.push_back ({text_column (query.get (), 0),
                       text_column (query.get (), 1),
                       text_column (query.get (), 2),
                       text_column (query.get (), 3),
                       text_column (query.get (), 4),
                       text_column (query.get (), 5),
                       text_column (query.get (), 6),
                       text_column (query.get (), 7),
                       sqlite3_column_int64 (query.get (), 8),
                       sqlite3_column_int (query.get (), 9) != 0,
                       sqlite3_column_int64 (query.get (), 10)});
  if (status != SQLITE_DONE) error= sqlite3_errmsg (impl->db);
  return result;
}

std::optional<MaterialAttachment>
MaterialsStore::primary_attachment (const std::string& material_uuid,
                                    std::string& error) const {
  std::vector<MaterialAttachment> all= attachments (material_uuid, error);
  if (!error.empty () || all.empty ()) return {};
  for (const MaterialAttachment& attachment: all)
    if (attachment.primary) return attachment;
  return all.front ();
}

std::string
MaterialsStore::normalize_identifier (const std::string& raw_scheme,
                                      const std::string& raw_value) {
  std::string scheme= lower_ascii (trim_ascii (raw_scheme));
  QString value= qs (trim_ascii (raw_value));
  if (scheme == "doi") {
    value.remove (QRegularExpression (
      "^(?:https?://(?:dx\\.)?doi\\.org/|doi:\\s*)",
      QRegularExpression::CaseInsensitiveOption));
    return ss (value.trimmed ().toLower ());
  }
  if (scheme == "isbn") {
    value.remove (QRegularExpression ("[^0-9Xx]"));
    return ss (value.toUpper ());
  }
  if (scheme == "arxiv") {
    value.remove (QRegularExpression (
      "^(?:https?://arxiv\\.org/(?:abs|pdf)/|arxiv:\\s*)",
      QRegularExpression::CaseInsensitiveOption));
    value.remove (QRegularExpression ("\\.pdf$",
                                      QRegularExpression::CaseInsensitiveOption));
    value.remove (QRegularExpression ("v[0-9]+$",
                                      QRegularExpression::CaseInsensitiveOption));
    return ss (value.trimmed ().toLower ());
  }
  if (scheme == "pmid" || scheme == "pmcid") {
    value.remove (QRegularExpression ("[^0-9A-Za-z]"));
    return ss (value.toUpper ());
  }
  return ss (value.trimmed ().toLower ());
}

std::string
MaterialsStore::canonical_filename (const MaterialRecord& material,
                                    const fs::path& source) {
  std::string creator= sanitize_filename_component (
    material_first_creator (material), "Unknown");
  std::string year= sanitize_filename_component (material_year (material),
                                                  "n.d.");
  std::string title= sanitize_filename_component (material_title (material),
                                                   "Untitled");
  std::string extension= lower_ascii (source.extension ().u8string ());
  if (extension.empty ()) extension= ".bin";
  return creator + " - " + year + " - " + title + extension;
}
