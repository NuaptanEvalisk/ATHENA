/******************************************************************************
* MODULE     : rag_delegation_patch.cpp
* DESCRIPTION: SQLite patch helpers for delegated RAG embedding jobs
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "rag_delegation_patch.hpp"
#include "rag_delegation_crypto.hpp"
#include "rag_embedding.hpp"
#include "rag_storage.hpp"

#include "ATHENA/Data/vaultfile_json.hpp"
#include "Data/Convert/Xml/document_file_codec.hpp"
#include "Data/Convert/Xml/document_upgrade_file.hpp"

#include <sqlite3.h>

#include <algorithm>
#include <chrono>
#include <fstream>
#include <set>
#include <sstream>
#include <system_error>

namespace fs = std::filesystem;

namespace athena::rag::delegation {
namespace {

class Statement {
public:
  Statement (sqlite3* db, const char* sql): stmt (nullptr) {
    sqlite3_prepare_v2 (db, sql, -1, &stmt, nullptr);
  }
  ~Statement () { if (stmt != nullptr) sqlite3_finalize (stmt); }
  sqlite3_stmt* get () const { return stmt; }
private:
  sqlite3_stmt* stmt;
};

bool
exec_sql (sqlite3* db, const char* sql, std::string& error) {
  char* msg= nullptr;
  int rc= sqlite3_exec (db, sql, nullptr, nullptr, &msg);
  if (rc == SQLITE_OK) return true;
  error= msg == nullptr ? sqlite3_errmsg (db) : msg;
  sqlite3_free (msg);
  return false;
}

void
bind_text (sqlite3_stmt* st, int col, const std::string& s) {
  sqlite3_bind_text (st, col, s.c_str (), int (s.size ()), SQLITE_TRANSIENT);
}

const char*
text_col (sqlite3_stmt* st, int col) {
  const unsigned char* text= sqlite3_column_text (st, col);
  return text == nullptr ? "" : reinterpret_cast<const char*> (text);
}

bool read_meta (sqlite3* db, const std::string& key, std::string& value);
bool write_meta (sqlite3* db, const std::string& key, const std::string& value,
                  std::string& error);

struct CachedRevision {
  bool found= false;
  int64_t size= 0;
  int64_t mtime= 0;
  std::string storage_revision;
  std::string semantic_revision;
  std::string status;
};

CachedRevision
document_revision (sqlite3* db, const std::string& rel) {
  CachedRevision result;
  Statement st (db, "SELECT size,mtime_ns,storage_revision,semantic_revision,status "
                    "FROM documents WHERE rel_path=?");
  if (st.get () == nullptr) return result;
  bind_text (st.get (), 1, rel);
  if (sqlite3_step (st.get ()) != SQLITE_ROW) return result;
  result.found= true;
  result.size= sqlite3_column_int64 (st.get (), 0);
  result.mtime= sqlite3_column_int64 (st.get (), 1);
  result.storage_revision= text_col (st.get (), 2);
  result.semantic_revision= text_col (st.get (), 3);
  result.status= text_col (st.get (), 4);
  return result;
}

bool
update_document_revision (sqlite3* db, const std::string& rel,
                          const fs::path& absolute, int64_t size, int64_t mtime,
                           const std::string& storage_revision,
                           const std::string& semantic_revision,
                          std::string& error) {
  Statement st (db, "UPDATE documents SET abs_path=?,size=?,mtime_ns=?,"
                    "storage_revision=?,semantic_revision=? WHERE rel_path=?");
  if (st.get () == nullptr) { error= sqlite3_errmsg (db); return false; }
  bind_text (st.get (), 1, absolute.generic_string ());
  sqlite3_bind_int64 (st.get (), 2, size);
  sqlite3_bind_int64 (st.get (), 3, mtime);
  bind_text (st.get (), 4, storage_revision);
  bind_text (st.get (), 5, semantic_revision);
  bind_text (st.get (), 6, rel);
  if (sqlite3_step (st.get ()) != SQLITE_DONE) {
    error= sqlite3_errmsg (db);
    return false;
  }
  return true;
}

bool
read_local_documents (sqlite3* db, std::set<std::string>& docs) {
  Statement st (db, "SELECT rel_path FROM documents");
  if (st.get () == nullptr) return false;
  while (sqlite3_step (st.get ()) == SQLITE_ROW)
    docs.insert (text_col (st.get (), 0));
  return true;
}

bool
read_stale_embedding_documents (sqlite3* db,
                                 const std::string& expected_model,
                                 std::set<std::string>& stale) {
  if (expected_model.empty ()) return true;
  Statement st (db, "SELECT c.rel_path,c.text,c.embedding_space,e.embedding "
                    "FROM chunks c LEFT JOIN embeddings e "
                    "ON e.space_id=? AND e.input_hash=c.embedding_input_hash");
  if (st.get () == nullptr) return false;
  bind_text (st.get (), 1, expected_model);
  while (sqlite3_step (st.get ()) == SQLITE_ROW) {
    std::string text= text_col (st.get (), 1);
    if (!athena::rag::rag_text_requires_embedding (text)) continue;
    if (expected_model != text_col (st.get (), 2) ||
        sqlite3_column_bytes (st.get (), 3) == 0)
      stale.insert (text_col (st.get (), 0));
  }
  return true;
}

bool
read_meta (sqlite3* db, const std::string& key, std::string& value) {
  Statement st (db, "SELECT value FROM meta WHERE key=?");
  if (st.get () == nullptr) return false;
  bind_text (st.get (), 1, key);
  if (sqlite3_step (st.get ()) != SQLITE_ROW) return false;
  value= text_col (st.get (), 0);
  return true;
}

bool
write_meta (sqlite3* db, const std::string& key, const std::string& value,
            std::string& error) {
  Statement st (db, "INSERT OR REPLACE INTO meta (key, value) VALUES (?, ?)");
  if (st.get () == nullptr) {
    error= sqlite3_errmsg (db);
    return false;
  }
  bind_text (st.get (), 1, key);
  bind_text (st.get (), 2, value);
  if (sqlite3_step (st.get ()) == SQLITE_DONE) return true;
  error= sqlite3_errmsg (db);
  return false;
}

bool
restore_job_document_metadata (const fs::path& patch_db,
                               const DelegatedJob& job,
                               std::string& error) {
  sqlite3* db= nullptr;
  if (sqlite3_open (patch_db.string ().c_str (), &db) != SQLITE_OK) {
    error= db == nullptr ? "failed to reopen delegated patch database":
                           sqlite3_errmsg (db);
    if (db != nullptr) sqlite3_close (db);
    return false;
  }
  bool ok= true;
  {
    Statement update (db, "UPDATE documents SET size=?, mtime_ns=?, "
                           "storage_revision=?,semantic_revision=? WHERE rel_path=?");
    if (update.get () == nullptr) {
      error= sqlite3_errmsg (db);
      ok= false;
    }
    for (const DelegatedFile& file: job.files) {
      if (!ok) break;
      sqlite3_reset (update.get ());
      sqlite3_clear_bindings (update.get ());
      sqlite3_bind_int64 (update.get (), 1, file.size);
      sqlite3_bind_int64 (update.get (), 2, file.mtime_ns);
      bind_text (update.get (), 3, file.storage_revision);
      bind_text (update.get (), 4, file.semantic_revision);
      bind_text (update.get (), 5, file.rel_path);
      if (sqlite3_step (update.get ()) != SQLITE_DONE ||
          sqlite3_changes (db) != 1) {
        error= "failed to restore delegated metadata for " + file.rel_path;
        ok= false;
      }
    }
  }
  sqlite3_close (db);
  return ok;
}

bool
delete_document_rows (sqlite3* db, const std::string& rel,
                      std::string& error) {
  Statement fts (db, "DELETE FROM chunks_fts WHERE rel_path=?");
  bind_text (fts.get (), 1, rel);
  if (sqlite3_step (fts.get ()) != SQLITE_DONE) {
    error= sqlite3_errmsg (db);
    return false;
  }
  Statement d1 (db, "DELETE FROM chunks WHERE rel_path=?");
  bind_text (d1.get (), 1, rel);
  if (sqlite3_step (d1.get ()) != SQLITE_DONE) {
    error= sqlite3_errmsg (db);
    return false;
  }
  Statement d2 (db, "DELETE FROM documents WHERE rel_path=?");
  bind_text (d2.get (), 1, rel);
  if (sqlite3_step (d2.get ()) != SQLITE_DONE) {
    error= sqlite3_errmsg (db);
    return false;
  }
  return true;
}

bool
copy_patch_documents (sqlite3* local, sqlite3* patch,
                       const fs::path& vault_root, std::string& error) {
  Statement src (patch, "SELECT rel_path,size,mtime_ns,storage_revision,semantic_revision,"
                         "indexed_at,status,error FROM documents");
  Statement dst (local, "INSERT OR REPLACE INTO documents "
                       "(rel_path,abs_path,size,mtime_ns,storage_revision,semantic_revision,"
                       "indexed_at,status,error) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)");
  while (sqlite3_step (src.get ()) == SQLITE_ROW) {
    sqlite3_reset (dst.get ());
    sqlite3_clear_bindings (dst.get ());
    std::string rel= text_col (src.get (), 0);
    bind_text (dst.get (), 1, rel);
    bind_text (dst.get (), 2, (vault_root / rel).generic_string ());
    sqlite3_bind_int64 (dst.get (), 3, sqlite3_column_int64 (src.get (), 1));
    sqlite3_bind_int64 (dst.get (), 4, sqlite3_column_int64 (src.get (), 2));
    bind_text (dst.get (), 5, text_col (src.get (), 3));
    bind_text (dst.get (), 6, text_col (src.get (), 4));
    sqlite3_bind_int64 (dst.get (), 7, sqlite3_column_int64 (src.get (), 5));
    bind_text (dst.get (), 8, text_col (src.get (), 6));
    bind_text (dst.get (), 9, text_col (src.get (), 7));
    if (sqlite3_step (dst.get ()) != SQLITE_DONE) {
      error= sqlite3_errmsg (local);
      return false;
    }
  }
  return true;
}

bool
copy_patch_chunks (sqlite3* local, sqlite3* patch, std::string& error) {
  Statement src (patch, "SELECT chunk_id, rel_path, kind, tree_path, "
                         "anchor, title, heading_path, text, source, "
                         "embedding_input_hash, embedding_space "
                         "FROM chunks");
  Statement dst (local, "INSERT OR REPLACE INTO chunks "
                       "(chunk_id, rel_path, kind, tree_path, anchor, title, "
                       " heading_path, text, source, embedding_input_hash, "
                       " embedding_space) "
                       "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
  while (sqlite3_step (src.get ()) == SQLITE_ROW) {
    sqlite3_reset (dst.get ());
    sqlite3_clear_bindings (dst.get ());
    for (int i=0; i<11; i++)
      bind_text (dst.get (), i + 1, text_col (src.get (), i));
    if (sqlite3_step (dst.get ()) != SQLITE_DONE) {
      error= sqlite3_errmsg (local);
      return false;
    }
  }
  return true;
}

bool
copy_patch_embeddings (sqlite3* local, sqlite3* patch, std::string& error) {
  Statement src (patch, "SELECT space_id,input_hash,embedding,embedding_dim "
                        "FROM embeddings");
  Statement dst (local, "INSERT OR REPLACE INTO embeddings "
                       "(space_id,input_hash,embedding,embedding_dim) "
                       "VALUES (?,?,?,?)");
  if (src.get () == nullptr || dst.get () == nullptr) {
    error= sqlite3_errmsg (local);
    return false;
  }
  while (sqlite3_step (src.get ()) == SQLITE_ROW) {
    sqlite3_reset (dst.get ());
    sqlite3_clear_bindings (dst.get ());
    bind_text (dst.get (), 1, text_col (src.get (), 0));
    bind_text (dst.get (), 2, text_col (src.get (), 1));
    const void* blob= sqlite3_column_blob (src.get (), 2);
    int bytes= sqlite3_column_bytes (src.get (), 2);
    if (blob != nullptr && bytes > 0)
      sqlite3_bind_blob (dst.get (), 3, blob, bytes, SQLITE_TRANSIENT);
    else sqlite3_bind_null (dst.get (), 3);
    sqlite3_bind_int (dst.get (), 4, sqlite3_column_int (src.get (), 3));
    if (sqlite3_step (dst.get ()) != SQLITE_DONE) {
      error= sqlite3_errmsg (local);
      return false;
    }
  }
  return true;
}

bool
copy_patch_embedding_spaces (sqlite3* local, sqlite3* patch,
                             std::string& error) {
  Statement src (patch, "SELECT space_id,dimension,backend,model,contract "
                        "FROM embedding_spaces");
  Statement dst (local, "INSERT OR REPLACE INTO embedding_spaces "
                       "(space_id,dimension,backend,model,contract) "
                       "VALUES (?,?,?,?,?)");
  if (src.get () == nullptr || dst.get () == nullptr) {
    error= sqlite3_errmsg (local);
    return false;
  }
  while (sqlite3_step (src.get ()) == SQLITE_ROW) {
    sqlite3_reset (dst.get ());
    sqlite3_clear_bindings (dst.get ());
    bind_text (dst.get (), 1, text_col (src.get (), 0));
    sqlite3_bind_int (dst.get (), 2, sqlite3_column_int (src.get (), 1));
    bind_text (dst.get (), 3, text_col (src.get (), 2));
    bind_text (dst.get (), 4, text_col (src.get (), 3));
    bind_text (dst.get (), 5, text_col (src.get (), 4));
    if (sqlite3_step (dst.get ()) != SQLITE_DONE) {
      error= sqlite3_errmsg (local);
      return false;
    }
  }
  return true;
}

bool
copy_patch_fts (sqlite3* local, sqlite3* patch, std::string& error) {
  Statement src (patch, "SELECT chunk_id, rel_path, title, heading_path, text "
                        "FROM chunks");
  Statement dst (local, "INSERT INTO chunks_fts "
                       "(chunk_id, rel_path, title, heading_path, text) "
                       "VALUES (?, ?, ?, ?, ?)");
  while (sqlite3_step (src.get ()) == SQLITE_ROW) {
    sqlite3_reset (dst.get ());
    sqlite3_clear_bindings (dst.get ());
    for (int i=0; i<5; i++)
      bind_text (dst.get (), i + 1, text_col (src.get (), i));
    if (sqlite3_step (dst.get ()) != SQLITE_DONE) {
      error= sqlite3_errmsg (local);
      return false;
    }
  }
  return true;
}

bool
copy_patch_edges (sqlite3* local, sqlite3* patch, std::string& error) {
  Statement src (patch, "SELECT src_chunk, relation, target, label FROM edges");
  Statement dst (local, "INSERT INTO edges "
                       "(src_chunk, relation, target, label) "
                       "VALUES (?, ?, ?, ?)");
  while (sqlite3_step (src.get ()) == SQLITE_ROW) {
    sqlite3_reset (dst.get ());
    sqlite3_clear_bindings (dst.get ());
    for (int i=0; i<4; i++)
      bind_text (dst.get (), i + 1, text_col (src.get (), i));
    if (sqlite3_step (dst.get ()) != SQLITE_DONE) {
      error= sqlite3_errmsg (local);
      return false;
    }
  }
  return true;
}

} // namespace

bool
valid_delegated_rel_path (const std::string& rel) {
  if (rel.empty () || rel[0] == '/' || rel.find ("..") != std::string::npos)
    return false;
  fs::path p (rel);
  for (const fs::path& part: p) {
    std::string s= part.string ();
    if (s.empty () || s == "." || s == "..") return false;
    if (s == ".backup" || s == ".athena" || s == ".git" || s == "assets")
      return false;
  }
  return p.extension () == ".ath";
}

std::vector<fs::path>
scan_delegation_ath_files (const fs::path& vault_root) {
  std::vector<fs::path> out;
  fs::path maintenance_root;
  AthenaVaultfileInfo info;
  std::string vault_error;
  if (athena_vaultfile_read (vault_root, info, vault_error)) {
    fs::path configured= fs::path (info.maintenance_summary_path)
                           .lexically_normal ();
    bool valid= !configured.empty () && !configured.is_absolute ();
    for (const fs::path& part: configured)
      valid= valid && part != "." && part != "..";
    if (valid) maintenance_root= vault_root / configured;
  }
  std::error_code ec;
  if (!fs::exists (vault_root, ec)) return out;
  fs::recursive_directory_iterator it (
    vault_root, fs::directory_options::skip_permission_denied, ec);
  fs::recursive_directory_iterator end;
  for (; !ec && it != end; it.increment (ec)) {
    const fs::path p= it->path ();
    std::string name= p.filename ().string ();
    if (it->is_directory (ec)) {
      if (name == ".backup" || name == ".athena" || name == ".git" ||
          name == "assets" || (!name.empty () && name[0] == '.') ||
          (!maintenance_root.empty () &&
           p.lexically_normal () == maintenance_root))
        it.disable_recursion_pending ();
      continue;
    }
    if (!it->is_regular_file (ec)) continue;
    if (p.extension () != ".ath") continue;
    std::string rel= relative_vault_path (vault_root, p);
    if (valid_delegated_rel_path (rel)) out.push_back (p);
  }
  std::sort (out.begin (), out.end ());
  return out;
}

std::string
relative_vault_path (const fs::path& vault_root, const fs::path& file) {
  std::error_code ec;
  fs::path rel= fs::relative (file, vault_root, ec);
  if (ec) rel= file.filename ();
  return rel.generic_string ();
}

bool
read_file_bytes (const fs::path& file, std::string& bytes) {
  std::ifstream in (file, std::ios::binary);
  if (!in) return false;
  std::ostringstream ss;
  ss << in.rdbuf ();
  bytes= ss.str ();
  return true;
}

int64_t
file_mtime_ns (const fs::path& file) {
  std::error_code ec;
  fs::file_time_type mt= fs::last_write_time (file, ec);
  if (ec) return 0;
  return std::chrono::duration_cast<std::chrono::nanoseconds> (
    mt.time_since_epoch ()).count ();
}

std::string
storage_revision (const std::string& bytes) {
  return athena::document::storage_bytes_fingerprint (bytes);
}

bool
cached_embedding_space_id (const fs::path& local_db,
                           const fs::path& embedding_model,
                           std::string& space_id,
                           std::string& error) {
  space_id.clear ();
  if (embedding_model.empty ()) return true;
  std::error_code ec;
  if (!fs::is_regular_file (embedding_model, ec)) return true;
  int64_t size= int64_t (fs::file_size (embedding_model, ec));
  if (ec) {
    error= "failed to inspect embedding model: " + ec.message ();
    return false;
  }
  int64_t mtime= file_mtime_ns (embedding_model);
  std::string path= embedding_model.generic_string ();
  std::string cached_path, cached_size, cached_mtime, cached_space;

  if (!athena::rag::storage::prepare_database_path (local_db, error))
    return false;

  sqlite3* db= nullptr;
  if (sqlite3_open (local_db.string ().c_str (), &db) != SQLITE_OK) {
    error= db == nullptr ? "failed to open local RAG database":
                           sqlite3_errmsg (db);
    if (db != nullptr) sqlite3_close (db);
    return false;
  }
  bool schema_ok= athena::rag::storage::ensure_schema (db, error);
  bool cache_hit= schema_ok &&
    read_meta (db, "delegation_model_path", cached_path) &&
    read_meta (db, "delegation_model_size", cached_size) &&
    read_meta (db, "delegation_model_mtime_ns", cached_mtime) &&
    read_meta (db, "delegation_model_space_id", cached_space) &&
    cached_path == path && cached_size == std::to_string (size) &&
    cached_mtime == std::to_string (mtime) && !cached_space.empty ();
  if (!schema_ok) {
    sqlite3_close (db);
    return false;
  }
  if (cache_hit) {
    space_id= cached_space;
    sqlite3_close (db);
    return true;
  }

  space_id= athena::rag::rag_embedding_space_id_for_model (path);
  if (space_id.empty ()) {
    error= "failed to fingerprint embedding model " + path;
    sqlite3_close (db);
    return false;
  }
  bool ok=
    write_meta (db, "delegation_model_path", path, error) &&
    write_meta (db, "delegation_model_size", std::to_string (size), error) &&
    write_meta (db, "delegation_model_mtime_ns", std::to_string (mtime), error) &&
    write_meta (db, "delegation_model_space_id", space_id, error);
  sqlite3_close (db);
  return ok;
}

bool
collect_delegated_job (const fs::path& vault_root, const fs::path& local_db,
                        const std::string& expected_embedding_model,
                        DelegatedJob& job, std::string& error) {
  job= DelegatedJob ();
  if (!athena::rag::storage::prepare_database_path (local_db, error))
    return false;
  sqlite3* db= nullptr;
  bool have_db= sqlite3_open (local_db.string ().c_str (), &db) == SQLITE_OK;
  if (have_db && !athena::rag::storage::ensure_schema (db, error)) {
    sqlite3_close (db);
    return false;
  }
  std::set<std::string> known;
  if (have_db) read_local_documents (db, known);
  std::set<std::string> staleEmbeddings;
  if (have_db && !read_stale_embedding_documents (
        db, expected_embedding_model, staleEmbeddings)) {
    error= sqlite3_errmsg (db);
    sqlite3_close (db);
    return false;
  }
  std::set<std::string> live;

  for (const fs::path& file: scan_delegation_ath_files (vault_root)) {
    std::string rel= relative_vault_path (vault_root, file);
    live.insert (rel);
    std::error_code ec;
    int64_t observedSize= int64_t (fs::file_size (file, ec));
    if (ec) {
      error= "failed to inspect " + file.generic_string () + ": " +
             ec.message ();
      if (db != nullptr) sqlite3_close (db);
      return false;
    }
    int64_t observedMtime= file_mtime_ns (file);
    CachedRevision cached= have_db ? document_revision (db, rel) : CachedRevision {};
    const bool embeddings_current= staleEmbeddings.count (rel) == 0;
    if (cached.found && cached.status == "ok" && embeddings_current &&
        cached.size == observedSize && cached.mtime == observedMtime &&
        !cached.semantic_revision.empty ()) continue;

    std::string bytes;
    if (!read_file_bytes (file, bytes)) {
      error= "failed to read " + file.generic_string ();
      if (db != nullptr) sqlite3_close (db);
      return false;
    }
    int64_t size= int64_t (bytes.size ());
    int64_t mt= file_mtime_ns (file);
    std::string storage_rev= storage_revision (bytes);
    std::string semantic_revision;
    try {
      tree document= athena::document::decode_document_bytes (bytes, file).document;
      semantic_revision= athena::document::semantic_document_fingerprint (document);
    }
    catch (const std::exception& e) {
      error= "failed to decode " + file.generic_string () + ": " + e.what ();
      if (db != nullptr) sqlite3_close (db);
      return false;
    }
    if (cached.found && cached.status == "ok" && embeddings_current &&
        (cached.semantic_revision == semantic_revision ||
         (cached.semantic_revision.empty () && cached.size == size &&
          cached.mtime == mt && cached.storage_revision == storage_rev))) {
      if (!update_document_revision (
            db, rel, file, size, mt, storage_rev, semantic_revision, error)) {
        if (db != nullptr) sqlite3_close (db);
        return false;
      }
      continue;
    }
    DelegatedFile f;
    f.rel_path= rel;
    f.content= std::move (bytes);
    f.size= size;
    f.mtime_ns= mt;
    f.storage_revision= std::move (storage_rev);
    f.semantic_revision= std::move (semantic_revision);
    job.files.push_back (std::move (f));
  }
  for (const std::string& rel: known)
    if (live.count (rel) == 0 && valid_delegated_rel_path (rel))
      job.deleted.push_back (rel);
  if (db != nullptr) sqlite3_close (db);
  return true;
}

bool
build_patch_for_job (const DelegatedJob& job, const fs::path& patch_db,
                     const fs::path& temp_parent, const RagConfig& config,
                     std::string& error) {
  fs::path temp_root= temp_parent /
    ("athena-rag-delegation-" + random_hex_id (12));
  std::error_code ec;
  fs::create_directories (temp_root, ec);
  if (ec) {
    error= "failed to create temporary RAG job root: " + ec.message ();
    return false;
  }
  auto cleanup= [&] () { fs::remove_all (temp_root, ec); };
  for (const DelegatedFile& file: job.files) {
    if (!valid_delegated_rel_path (file.rel_path)) {
      error= "invalid delegated path: " + file.rel_path;
      cleanup ();
      return false;
    }
    fs::path out= temp_root / file.rel_path;
    fs::create_directories (out.parent_path (), ec);
    if (ec) {
      error= "failed to create " + out.parent_path ().generic_string ();
      cleanup ();
      return false;
    }
    std::ofstream f (out, std::ios::binary | std::ios::trunc);
    if (!f) {
      error= "failed to write delegated file " + out.generic_string ();
      cleanup ();
      return false;
    }
    f.write (file.content.data (), std::streamsize (file.content.size ()));
  }
  fs::remove (patch_db, ec);
  RagConfig patch_config= config;
  patch_config.vault_root= temp_root;
  patch_config.db_path= patch_db;
  patch_config.force_reindex= true;
  patch_config.progress= false;
  {
    RagIndex index;
    if (!index.open (patch_config)) {
      error= "failed to open delegated patch index";
      cleanup ();
      return false;
    }
    if (!index.scan_once ()) {
      error= "failed to build delegated patch index";
      cleanup ();
      return false;
    }
  }
  if (!restore_job_document_metadata (patch_db, job, error)) {
    cleanup ();
    return false;
  }
  cleanup ();
  return true;
}

bool
apply_patch_database (const fs::path& vault_root, const fs::path& local_db,
                       const fs::path& patch_db,
                       const std::vector<std::string>& deleted,
                       std::string& error) {
  if (!athena::rag::storage::prepare_database_path (local_db, error))
    return false;
  sqlite3* local= nullptr;
  if (sqlite3_open (local_db.string ().c_str (), &local) != SQLITE_OK) {
    error= local == nullptr ? "failed to open local RAG database" :
           sqlite3_errmsg (local);
    if (local != nullptr) sqlite3_close (local);
    return false;
  }
  if (!athena::rag::storage::ensure_schema (local, error)) {
    sqlite3_close (local);
    return false;
  }
  sqlite3* patch= nullptr;
  if (sqlite3_open_v2 (patch_db.string ().c_str (), &patch,
                       SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
    error= patch == nullptr ? "failed to open delegated patch database" :
           sqlite3_errmsg (patch);
    sqlite3_close (local);
    if (patch != nullptr) sqlite3_close (patch);
    return false;
  }

  std::set<std::string> affected (deleted.begin (), deleted.end ());
  Statement docs (patch, "SELECT rel_path FROM documents");
  while (sqlite3_step (docs.get ()) == SQLITE_ROW)
    affected.insert (text_col (docs.get (), 0));

  if (!exec_sql (local, "BEGIN IMMEDIATE", error)) {
    sqlite3_close (patch);
    sqlite3_close (local);
    return false;
  }
  for (const std::string& rel: affected) {
    if (!valid_delegated_rel_path (rel)) {
      error= "invalid delegated patch path: " + rel;
      exec_sql (local, "ROLLBACK", error);
      sqlite3_close (patch);
      sqlite3_close (local);
      return false;
    }
    if (!delete_document_rows (local, rel, error)) {
      exec_sql (local, "ROLLBACK", error);
      sqlite3_close (patch);
      sqlite3_close (local);
      return false;
    }
  }
  if (!exec_sql (local,
      "DELETE FROM edges WHERE src_chunk NOT IN (SELECT chunk_id FROM chunks)",
      error) ||
      !copy_patch_documents (local, patch, vault_root, error) ||
      !copy_patch_chunks (local, patch, error) ||
      !copy_patch_embeddings (local, patch, error) ||
      !copy_patch_embedding_spaces (local, patch, error) ||
      !copy_patch_edges (local, patch, error) ||
      !copy_patch_fts (local, patch, error)) {
    exec_sql (local, "ROLLBACK", error);
    sqlite3_close (patch);
    sqlite3_close (local);
    return false;
  }
  if (!exec_sql (local, "COMMIT", error)) {
    sqlite3_close (patch);
    sqlite3_close (local);
    return false;
  }
  sqlite3_close (patch);
  sqlite3_close (local);
  return true;
}

} // namespace athena::rag::delegation
