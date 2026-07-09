/******************************************************************************
* MODULE     : rag_index.cpp
* DESCRIPTION: Continuous RAG SQLite index for ATHENA vaults
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "rag_index.hpp"
#include "rag_embedding.hpp"

#include "ATHENA/Data/vaultfile_json.hpp"
#include "convert.hpp"
#include "tm_ostream.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <thread>
#include <unordered_map>

#include <sqlite3.h>

#if defined(__unix__) || defined(__APPLE__)
#include <poll.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace athena::rag {
namespace {

class RagProgressDisplay {
public:
  void update_file (size_t current, size_t total, const std::string& phase,
                    const std::string& item) {
    update_slot (file_, current, total, phase, item);
  }

  void update_chunks (size_t current, size_t total, const std::string& phase,
                      const std::string& item) {
    update_slot (chunks_, current, total, phase, item);
  }

  void update (size_t current, size_t total, const std::string& phase,
               const std::string& item) {
    update_chunks (current, total, phase, item);
  }

  void clear_chunks () {
    chunks_.shown= false;
    draw ();
  }

  void log_info (const std::string& message) {
    bool was_active= active_;
    clear ();
    io_info << message.c_str () << "\n";
    if (was_active) draw ();
  }

  void log_warning (const std::string& message) {
    bool was_active= active_;
    clear ();
    std_warning << message.c_str () << "\n";
    if (was_active) draw ();
  }

  void finish () {
    if (!active_) return;
    std::cout << std::endl;
    active_= false;
    lines_drawn_= 0;
  }

private:
  struct Slot {
    bool shown= false;
    size_t current= 0;
    size_t total= 1;
    std::string phase;
    std::string item;
  };

  void update_slot (Slot& slot, size_t current, size_t total,
                    const std::string& phase, const std::string& item) {
    if (total == 0) total= 1;
    slot.shown= true;
    slot.current= current;
    slot.total= total;
    slot.phase= phase;
    slot.item= item;
    draw ();
  }

  std::string make_line (const Slot& slot) {
    int bar_width= 30;
    double progress= std::min (1.0, (double) slot.current /
                                    (double) slot.total);
    int pos= (int) (bar_width * progress);

    std::string shown= slot.item;
    if (shown.size () > 44) shown= "..." + shown.substr (shown.size () - 41);

    std::ostringstream line;
    line << "[";
    for (int i=0; i<bar_width; i++) {
      if (i < pos) line << "=";
      else if (i == pos) line << ">";
      else line << " ";
    }
    line << "] " << (int) (progress * 100.0) << "% "
         << "[" << slot.current << "/" << slot.total << "] "
         << slot.phase << ": " << shown;
    return line.str ();
  }

  std::vector<std::string> lines () {
    std::vector<std::string> out;
    if (file_.shown) out.push_back (make_line (file_));
    if (chunks_.shown) out.push_back (make_line (chunks_));
    return out;
  }

  void draw () {
    clear ();
    std::vector<std::string> next= lines ();
    if (next.empty ()) return;
    last_width_= 0;
    for (size_t i=0; i<next.size (); i++) {
      if (i != 0) std::cout << "\n";
      std::cout << next[i];
      last_width_= std::max (last_width_, next[i].size ());
    }
    std::cout << std::flush;
    active_= true;
    lines_drawn_= next.size ();
  }

  void clear () {
    if (!active_) return;
    if (lines_drawn_ > 1) {
      for (size_t i=1; i<lines_drawn_; i++) std::cout << "\r\033[1A";
    }
    for (size_t i=0; i<lines_drawn_; i++) {
      std::cout << "\r" << std::string (last_width_ + 8, ' ') << "\r";
      if (i + 1 < lines_drawn_) std::cout << "\033[1B";
    }
    if (lines_drawn_ > 1) {
      for (size_t i=1; i<lines_drawn_; i++) std::cout << "\r\033[1A";
    }
    std::cout << std::flush;
    active_= false;
    lines_drawn_= 0;
  }

  bool active_= false;
  size_t lines_drawn_= 0;
  size_t last_width_= 0;
  Slot file_;
  Slot chunks_;
};

static RagProgressDisplay rag_progress;

static std::string
progress_sanitize (std::string s) {
  for (char& c: s)
    if (c == '\t' || c == '\n' || c == '\r') c= ' ';
  return s;
}

static void
write_progress_event (int fd, const std::string& event) {
#if defined(__unix__) || defined(__APPLE__)
  if (fd < 0) return;
  std::string line= event + "\n";
  const char* p= line.c_str ();
  size_t left= line.size ();
  while (left > 0) {
    ssize_t n= write (fd, p, left);
    if (n <= 0) return;
    p += n;
    left -= size_t (n);
  }
#else
  (void) fd;
  (void) event;
#endif
}

static std::vector<std::string>
split_tabs (const std::string& line) {
  std::vector<std::string> parts;
  size_t start= 0;
  while (start <= line.size ()) {
    size_t pos= line.find ('\t', start);
    if (pos == std::string::npos) {
      parts.push_back (line.substr (start));
      break;
    }
    parts.push_back (line.substr (start, pos - start));
    start= pos + 1;
  }
  return parts;
}

static std::string
to_std (string s) {
  return std::string (as_charp (s), N(s));
}

static string
to_tm (const std::string& s) {
  return string (s.c_str ());
}

static bool
read_bytes (const fs::path& path, std::string& text) {
  std::ifstream in (path, std::ios::binary);
  if (!in) return false;
  std::ostringstream buf;
  buf << in.rdbuf ();
  text= buf.str ();
  return true;
}

static std::string
trim (std::string s) {
  auto is_space= [] (unsigned char c) { return std::isspace (c); };
  while (!s.empty () && is_space (s.front ())) s.erase (s.begin ());
  while (!s.empty () && is_space (s.back ())) s.pop_back ();
  return s;
}

static bool
starts_with (const std::string& s, const std::string& p) {
  return s.size () >= p.size () && s.compare (0, p.size (), p) == 0;
}

static bool
ends_with (const std::string& s, const std::string& p) {
  return s.size () >= p.size () &&
         s.compare (s.size () - p.size (), p.size (), p) == 0;
}

static std::string
fnv1a_hex (const std::string& s) {
  uint64_t h= 1469598103934665603ULL;
  for (unsigned char c: s) {
    h ^= c;
    h *= 1099511628211ULL;
  }
  std::ostringstream out;
  out << std::hex << std::setw (16) << std::setfill ('0') << h;
  return out.str ();
}

static int64_t
mtime_ns (const fs::path& path) {
  std::error_code ec;
  fs::file_time_type t= fs::last_write_time (path, ec);
  if (ec) return 0;
  return std::chrono::duration_cast<std::chrono::nanoseconds> (
           t.time_since_epoch ()).count ();
}

static bool
valid_vault_relative_path (const std::string& rel) {
  if (rel.empty ()) return false;
  fs::path p (rel);
  if (p.is_absolute ()) return false;
  for (const fs::path& part: p)
    if (part == "..") return false;
  return true;
}

static std::string
relative_path (const fs::path& root, const fs::path& path) {
  std::error_code ec;
  fs::path rel= fs::relative (path, root, ec);
  if (ec || rel.empty () || rel.is_absolute ()) return path.generic_string ();
  return rel.generic_string ();
}

static bool
shard_accepts (const std::string& rel, int shard_index, int shard_count) {
  if (shard_count <= 1) return true;
  if (shard_index < 0 || shard_index >= shard_count) return true;
  size_t h= std::hash<std::string>{} (rel);
  return int (h % size_t (shard_count)) == shard_index;
}

static std::vector<fs::path>
scan_ath_files (const fs::path& root) {
  std::vector<fs::path> out;
  std::error_code ec;
  fs::recursive_directory_iterator it (
    root, fs::directory_options::skip_permission_denied, ec);
  fs::recursive_directory_iterator end;
  for (; !ec && it != end; it.increment (ec)) {
    fs::path p= it->path ();
    if (it->is_directory (ec)) {
      std::string name= p.filename ().string ();
      if (name == ".backup" || name == ".git")
        it.disable_recursion_pending ();
      continue;
    }
    if (!it->is_regular_file (ec)) continue;
    if (p.extension () == ".ath") out.push_back (p);
  }
  std::sort (out.begin (), out.end ());
  return out;
}

static std::string
label_name (const tree& t) {
  if (is_atomic (t)) return to_std (t->label);
  return to_std (as_string (L(t)));
}

static std::string
plain_text (const tree& t);

static void
append_space (std::string& out) {
  if (!out.empty () && !std::isspace ((unsigned char) out.back ()))
    out.push_back (' ');
}

static void
plain_text_into (const tree& t, std::string& out) {
  if (is_atomic (t)) {
    std::string s= to_std (t->label);
    if (s.empty ()) return;
    if (!out.empty () && !std::isspace ((unsigned char) out.back ()) &&
        !std::isspace ((unsigned char) s.front ()))
      out.push_back (' ');
    out += s;
    return;
  }

  tree_label l= L(t);
  if (l == LABEL || l == REFERENCE || l == PAGEREF || l == IMAGE ||
      l == INCLUDE || l == WRITE || l == GET_ATTACHMENT)
    return;
  if ((l == WITH || l == STYLE_WITH || l == VAR_STYLE_WITH) && N(t) > 0) {
    plain_text_into (t[N(t) - 1], out);
    return;
  }
  if (l == ASSIGN || l == PROVIDE || l == DRD_PROPS || l == COLLECTION)
    return;

  for (int i=0; i<N(t); i++) {
    plain_text_into (t[i], out);
    if (l == DOCUMENT || l == PARA || l == CONCAT) append_space (out);
  }
}

static std::string
plain_text (const tree& t) {
  std::string out;
  plain_text_into (t, out);
  return trim (out);
}

static std::string
first_anchor (const tree& t) {
  if (is_atomic (t)) return "";
  if (is_func (t, LABEL, 1)) return plain_text (t[0]);
  for (int i=0; i<N(t); i++) {
    std::string a= first_anchor (t[i]);
    if (!a.empty ()) return a;
  }
  return "";
}

static std::string
path_string (const std::vector<int>& path) {
  std::string out;
  for (size_t i=0; i<path.size (); i++) {
    if (i != 0) out.push_back ('.');
    out += std::to_string (path[i]);
  }
  return out;
}

static std::string
strip_star (std::string s) {
  if (!s.empty () && s.back () == '*') s.pop_back ();
  return s;
}

static int
heading_level (const tree& t) {
  if (is_atomic (t)) return 0;
  std::string tag= strip_star (label_name (t));
  if (tag == "part") return 1;
  if (tag == "chapter") return 2;
  if (tag == "section") return 3;
  if (tag == "subsection") return 4;
  if (tag == "subsubsection") return 5;
  if (tag == "paragraph") return 6;
  if (tag == "subparagraph") return 7;
  return 0;
}

static bool
is_enunciation_tag (const std::string& raw) {
  static const std::set<std::string> tags= {
    "theorem", "lemma", "corollary", "proposition", "axiom",
    "definition", "notation", "convention", "conjecture", "law",
    "remark", "note", "example", "warning", "exercise", "problem",
    "question", "solution", "answer", "proof", "proof-variant",
    "quote-env", "disambiguation", "acknowledgments"
  };
  return tags.count (strip_star (raw)) != 0;
}

static std::string
heading_context (const std::vector<std::string>& headings) {
  std::string out;
  for (size_t i=0; i<headings.size (); i++) {
    if (headings[i].empty ()) continue;
    if (!out.empty ()) out += " / ";
    out += headings[i];
  }
  return out;
}

static bool
looks_like_link_or_transclusion (const tree& t) {
  if (is_atomic (t)) {
    std::string s= to_std (t->label);
    return s.find ("tmfs://wikilink/") != std::string::npos ||
           s.find ("tmfs://transclude/") != std::string::npos;
  }
  tree_label l= L(t);
  return l == HLINK || l == LINK || l == URL || l == TRANSCLUDE;
}

static void
collect_edges (const tree& t, std::vector<std::string>& edges) {
  if (looks_like_link_or_transclusion (t)) {
    std::string s= plain_text (t);
    if (!s.empty ()) edges.push_back (s);
  }
  if (is_atomic (t)) return;
  for (int i=0; i<N(t); i++) collect_edges (t[i], edges);
}

static std::string
snippet_from_text (const std::string& text) {
  if (text.size () <= 600) return text;
  return text.substr (0, 600) + "...";
}

static std::string
fts_query (const std::string& query) {
  std::vector<std::string> words;
  std::string cur;
  for (unsigned char c: query) {
    if (std::isalnum (c) || c >= 128 || c == '_' || c == '-') cur.push_back (c);
    else if (!cur.empty ()) {
      words.push_back (cur);
      cur.clear ();
    }
  }
  if (!cur.empty ()) words.push_back (cur);
  std::string out;
  for (const std::string& w: words) {
    if (!out.empty ()) out += " ";
    out += "\"";
    for (char c: w) {
      if (c == '"') out += "\"\"";
      else out.push_back (c);
    }
    out += "\"";
  }
  return out;
}

struct ChunkBuild {
  RagChunk chunk;
  std::vector<std::string> edges;
};

static void
add_chunk (std::vector<ChunkBuild>& chunks, const std::string& rel_path,
           const std::string& kind, const std::vector<int>& path,
           const tree& node, const std::vector<std::string>& headings,
           const std::string& explicit_title= "") {
  std::string text= plain_text (node);
  if (text.empty ()) return;
  RagChunk c;
  c.rel_path= rel_path;
  c.kind= kind;
  c.tree_path= path_string (path);
  c.anchor= first_anchor (node);
  c.title= explicit_title.empty ()? snippet_from_text (text): explicit_title;
  c.heading_path= heading_context (headings);
  c.text= text;
  c.source= snippet_from_text (text);
  c.chunk_id= fnv1a_hex (rel_path + "\n" + c.tree_path + "\n" + c.kind +
                         "\n" + c.anchor + "\n" + c.title);
  ChunkBuild build;
  build.chunk= c;
  collect_edges (node, build.edges);
  chunks.push_back (build);
}

static void
collect_nested_enunciations (std::vector<ChunkBuild>& chunks,
                             const std::string& rel_path, const tree& node,
                             std::vector<int> path,
                             const std::vector<std::string>& headings) {
  if (is_atomic (node)) return;
  std::string tag= label_name (node);
  if (is_enunciation_tag (tag))
    add_chunk (chunks, rel_path, strip_star (tag), path, node, headings);
  for (int i=0; i<N(node); i++) {
    std::vector<int> p= path;
    p.push_back (i);
    collect_nested_enunciations (chunks, rel_path, node[i], p, headings);
  }
}

static std::vector<ChunkBuild>
chunk_document (const std::string& rel_path, tree doc) {
  tree body= extract (doc, "body");
  if (is_atomic (body) && body == "") body= doc;
  std::vector<ChunkBuild> chunks;
  std::vector<std::string> headings;
  std::vector<int> root_path;

  if (!is_func (body, DOCUMENT)) {
    add_chunk (chunks, rel_path, "document", root_path, body, headings);
    collect_nested_enunciations (chunks, rel_path, body, root_path, headings);
    return chunks;
  }

  for (int i=0; i<N(body); i++) {
    const tree& child= body[i];
    std::vector<int> p= { i };
    int level= heading_level (child);
    if (level > 0) {
      std::string title= plain_text (child);
      if ((int) headings.size () < level) headings.resize (level);
      headings.resize (level);
      headings[level - 1]= title;
      add_chunk (chunks, rel_path, "heading", p, child, headings, title);
      continue;
    }

    std::string kind= is_atomic (child) ? "text" : strip_star (label_name (child));
    if (kind.empty () || kind == "document" || kind == "concat")
      kind= "block";
    add_chunk (chunks, rel_path, kind, p, child, headings);
    collect_nested_enunciations (chunks, rel_path, child, p, headings);
  }

  return chunks;
}

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

static bool
exec_sql (sqlite3* db, const char* sql, std::string& error) {
  char* msg= nullptr;
  int rc= sqlite3_exec (db, sql, nullptr, nullptr, &msg);
  if (rc == SQLITE_OK) return true;
  error= msg == nullptr ? sqlite3_errmsg (db) : msg;
  sqlite3_free (msg);
  return false;
}

static void
bind_text (sqlite3_stmt* st, int col, const std::string& s) {
  sqlite3_bind_text (st, col, s.c_str (), int (s.size ()), SQLITE_TRANSIENT);
}

static bool
should_embed_text (const std::string& text) {
  int useful= 0;
  for (unsigned char c: text) {
    if (std::isalnum (c)) useful++;
    if (useful >= 12) return true;
  }
  return false;
}

static RagChunk
chunk_from_stmt (sqlite3_stmt* st, int offset= 0) {
  auto col= [st] (int i) -> std::string {
    const unsigned char* text= sqlite3_column_text (st, i);
    return text == nullptr ? std::string () :
      std::string (reinterpret_cast<const char*> (text));
  };
  RagChunk c;
  c.chunk_id= col (offset + 0);
  c.rel_path= col (offset + 1);
  c.kind= col (offset + 2);
  c.tree_path= col (offset + 3);
  c.anchor= col (offset + 4);
  c.title= col (offset + 5);
  c.heading_path= col (offset + 6);
  c.text= col (offset + 7);
  c.source= col (offset + 8);
  return c;
}

static std::vector<float>
blob_to_vector (sqlite3_stmt* st, int col, int dim) {
  const void* blob= sqlite3_column_blob (st, col);
  int bytes= sqlite3_column_bytes (st, col);
  if (blob == nullptr || dim <= 0 || bytes != dim * int (sizeof (float)))
    return {};
  const float* ptr= reinterpret_cast<const float*> (blob);
  return std::vector<float> (ptr, ptr + dim);
}

static double
dot (const std::vector<float>& a, const std::vector<float>& b) {
  if (a.empty () || a.size () != b.size ()) return 0.0;
  double s= 0.0;
  for (size_t i=0; i<a.size (); i++) s += double (a[i]) * double (b[i]);
  return s;
}

} // namespace

struct RagIndex::Impl {
  RagConfig config;
  sqlite3* db= nullptr;
  RagEmbedder embedder;
  RagStatus status;
};

RagIndex::RagIndex ()
  : impl (new Impl) {}

RagIndex::~RagIndex () {
  if (impl->db != nullptr) sqlite3_close (impl->db);
  delete impl;
}

const fs::path&
RagIndex::vault_root () const {
  return impl->config.vault_root;
}

std::string
rag_read_vault_db_path (const fs::path& vault_root) {
  AthenaVaultfileInfo info;
  std::string error;
  if (!athena_vaultfile_read (vault_root, info, error))
    return "rag.sqlite";
  if (valid_vault_relative_path (info.rag_index_path))
    return info.rag_index_path.empty ()? "rag.sqlite": info.rag_index_path;
  return "rag.sqlite";
}

std::string
rag_default_db_path (const fs::path& vault_root) {
  return (vault_root / rag_read_vault_db_path (vault_root)).generic_string ();
}

bool
RagIndex::open (const RagConfig& config) {
  impl->config= config;
  impl->status= RagStatus ();
  impl->status.vault_root= config.vault_root.generic_string ();
  impl->status.db_path= config.db_path.generic_string ();
  impl->status.embedding_model= config.embedding_model.generic_string ();

  std::error_code ec;
  fs::create_directories (config.db_path.parent_path (), ec);
  if (sqlite3_open (config.db_path.string ().c_str (), &impl->db) !=
      SQLITE_OK) {
    impl->status.last_error= sqlite3_errmsg (impl->db);
    std_error << "rag index: failed to open "
              << impl->status.db_path.c_str ()
              << ": " << impl->status.last_error.c_str () << "\n";
    return false;
  }

  std::string error;
  const char* schema =
    "PRAGMA journal_mode=WAL;"
    "CREATE TABLE IF NOT EXISTS meta ("
    "  key TEXT PRIMARY KEY, value TEXT NOT NULL);"
    "CREATE TABLE IF NOT EXISTS documents ("
    "  rel_path TEXT PRIMARY KEY, abs_path TEXT NOT NULL,"
    "  size INTEGER NOT NULL, mtime_ns INTEGER NOT NULL,"
    "  content_hash TEXT NOT NULL, indexed_at INTEGER NOT NULL,"
    "  status TEXT NOT NULL, error TEXT NOT NULL);"
    "CREATE TABLE IF NOT EXISTS chunks ("
    "  chunk_id TEXT PRIMARY KEY, rel_path TEXT NOT NULL,"
    "  kind TEXT NOT NULL, tree_path TEXT NOT NULL, anchor TEXT,"
    "  title TEXT, heading_path TEXT, text TEXT NOT NULL, source TEXT,"
    "  embedding BLOB, embedding_dim INTEGER, embedding_model TEXT);"
    "CREATE TABLE IF NOT EXISTS edges ("
    "  src_chunk TEXT NOT NULL, relation TEXT NOT NULL,"
    "  target TEXT NOT NULL, label TEXT);"
    "CREATE VIRTUAL TABLE IF NOT EXISTS chunks_fts USING fts5("
    "  chunk_id UNINDEXED, rel_path, title, heading_path, text);";
  if (!exec_sql (impl->db, schema, error)) {
    impl->status.last_error= error;
    std_error << "rag index: schema initialization failed: "
              << error.c_str () << "\n";
    return false;
  }

  if (config.force_reindex) {
    exec_sql (impl->db,
              "DELETE FROM documents; DELETE FROM chunks; DELETE FROM edges;"
              "DELETE FROM chunks_fts;", error);
  }

  if (config.load_embedding_model && !config.embedding_model.empty ()) {
    if (impl->embedder.open (config.embedding_model.string (),
                             config.embedding_device,
                             config.embedding_threads)) {
      impl->status.embeddings_enabled= true;
    }
    else {
      impl->status.embedding_warning=
        "Embedding model could not be loaded; using FTS-only retrieval.";
    }
  }

  impl->status.open= true;
  return true;
}

static bool
document_is_current (sqlite3* db, const std::string& rel, int64_t size,
                     int64_t mtime, const std::string& hash) {
  Statement st (db, "SELECT size, mtime_ns, content_hash, status "
                    "FROM documents WHERE rel_path=?");
  if (st.get () == nullptr) return false;
  bind_text (st.get (), 1, rel);
  if (sqlite3_step (st.get ()) != SQLITE_ROW) return false;
  return sqlite3_column_int64 (st.get (), 0) == size &&
         sqlite3_column_int64 (st.get (), 1) == mtime &&
         hash == reinterpret_cast<const char*> (
                   sqlite3_column_text (st.get (), 2)) &&
         std::string (reinterpret_cast<const char*> (
           sqlite3_column_text (st.get (), 3))) == "ok";
}

static void
delete_document_rows (sqlite3* db, const std::string& rel) {
  Statement d1 (db, "DELETE FROM chunks WHERE rel_path=?");
  bind_text (d1.get (), 1, rel);
  sqlite3_step (d1.get ());
  Statement d2 (db, "DELETE FROM edges WHERE src_chunk NOT IN "
                    "(SELECT chunk_id FROM chunks)");
  sqlite3_step (d2.get ());
  Statement d3 (db, "DELETE FROM chunks_fts");
  sqlite3_step (d3.get ());
}

static void
rebuild_fts (sqlite3* db) {
  Statement del (db, "DELETE FROM chunks_fts");
  sqlite3_step (del.get ());
  Statement ins (db, "INSERT INTO chunks_fts "
                    "(chunk_id, rel_path, title, heading_path, text) "
                    "SELECT chunk_id, rel_path, title, heading_path, text "
                    "FROM chunks");
  sqlite3_step (ins.get ());
}

static std::string
worker_db_path (const fs::path& db_path, int worker) {
  fs::path p= db_path;
  std::string name= p.filename ().string ();
  p.replace_filename (name + ".worker-" + std::to_string (worker) +
                      ".sqlite");
  return p.generic_string ();
}

static bool
merge_worker_database (sqlite3* db, const std::string& path,
                       std::string& error) {
  sqlite3* worker= nullptr;
  if (sqlite3_open_v2 (path.c_str (), &worker, SQLITE_OPEN_READONLY,
                       nullptr) != SQLITE_OK) {
    error= worker == nullptr ? "failed to open worker database" :
           sqlite3_errmsg (worker);
    if (worker != nullptr) sqlite3_close (worker);
    return false;
  }

  auto text_col= [] (sqlite3_stmt* st, int col) -> const char* {
    const unsigned char* text= sqlite3_column_text (st, col);
    return text == nullptr ? "" : reinterpret_cast<const char*> (text);
  };

  {
    Statement src (worker, "SELECT rel_path, abs_path, size, mtime_ns, "
                           "content_hash, indexed_at, status, error "
                           "FROM documents");
    Statement dst (db, "INSERT OR REPLACE INTO documents "
                       "(rel_path, abs_path, size, mtime_ns, content_hash, "
                       " indexed_at, status, error) "
                       "VALUES (?, ?, ?, ?, ?, ?, ?, ?)");
    while (sqlite3_step (src.get ()) == SQLITE_ROW) {
      sqlite3_reset (dst.get ());
      sqlite3_clear_bindings (dst.get ());
      for (int i=0; i<2; i++)
        sqlite3_bind_text (dst.get (), i + 1, text_col (src.get (), i),
                           -1, SQLITE_TRANSIENT);
      sqlite3_bind_int64 (dst.get (), 3, sqlite3_column_int64 (src.get (), 2));
      sqlite3_bind_int64 (dst.get (), 4, sqlite3_column_int64 (src.get (), 3));
      sqlite3_bind_text (dst.get (), 5, text_col (src.get (), 4),
                         -1, SQLITE_TRANSIENT);
      sqlite3_bind_int64 (dst.get (), 6, sqlite3_column_int64 (src.get (), 5));
      sqlite3_bind_text (dst.get (), 7, text_col (src.get (), 6),
                         -1, SQLITE_TRANSIENT);
      sqlite3_bind_text (dst.get (), 8, text_col (src.get (), 7),
                         -1, SQLITE_TRANSIENT);
      if (sqlite3_step (dst.get ()) != SQLITE_DONE) {
        error= sqlite3_errmsg (db);
        sqlite3_close (worker);
        return false;
      }
    }
  }

  {
    Statement src (worker, "SELECT chunk_id, rel_path, kind, tree_path, "
                           "anchor, title, heading_path, text, source, "
                           "embedding, embedding_dim, embedding_model "
                           "FROM chunks");
    Statement dst (db, "INSERT OR REPLACE INTO chunks "
                       "(chunk_id, rel_path, kind, tree_path, anchor, title, "
                       " heading_path, text, source, embedding, "
                       " embedding_dim, embedding_model) "
                       "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
    while (sqlite3_step (src.get ()) == SQLITE_ROW) {
      sqlite3_reset (dst.get ());
      sqlite3_clear_bindings (dst.get ());
      for (int i=0; i<9; i++)
        sqlite3_bind_text (dst.get (), i + 1, text_col (src.get (), i),
                           -1, SQLITE_TRANSIENT);
      const void* blob= sqlite3_column_blob (src.get (), 9);
      int bytes= sqlite3_column_bytes (src.get (), 9);
      if (blob != nullptr && bytes > 0)
        sqlite3_bind_blob (dst.get (), 10, blob, bytes, SQLITE_TRANSIENT);
      else sqlite3_bind_null (dst.get (), 10);
      sqlite3_bind_int (dst.get (), 11, sqlite3_column_int (src.get (), 10));
      sqlite3_bind_text (dst.get (), 12, text_col (src.get (), 11),
                         -1, SQLITE_TRANSIENT);
      if (sqlite3_step (dst.get ()) != SQLITE_DONE) {
        error= sqlite3_errmsg (db);
        sqlite3_close (worker);
        return false;
      }
    }
  }

  {
    Statement src (worker, "SELECT src_chunk, relation, target, label "
                           "FROM edges");
    Statement dst (db, "INSERT INTO edges "
                       "(src_chunk, relation, target, label) "
                       "VALUES (?, ?, ?, ?)");
    while (sqlite3_step (src.get ()) == SQLITE_ROW) {
      sqlite3_reset (dst.get ());
      sqlite3_clear_bindings (dst.get ());
      for (int i=0; i<4; i++)
        sqlite3_bind_text (dst.get (), i + 1, text_col (src.get (), i),
                           -1, SQLITE_TRANSIENT);
      if (sqlite3_step (dst.get ()) != SQLITE_DONE) {
        error= sqlite3_errmsg (db);
        sqlite3_close (worker);
        return false;
      }
    }
  }
  sqlite3_close (worker);
  return true;
}

static void
upsert_document (sqlite3* db, const std::string& rel, const fs::path& abs,
                 int64_t size, int64_t mtime, const std::string& hash,
                 const std::string& status, const std::string& error) {
  Statement st (db, "INSERT OR REPLACE INTO documents "
                  "(rel_path, abs_path, size, mtime_ns, content_hash, "
                  " indexed_at, status, error) VALUES (?, ?, ?, ?, ?, ?, ?, ?)");
  bind_text (st.get (), 1, rel);
  bind_text (st.get (), 2, abs.generic_string ());
  sqlite3_bind_int64 (st.get (), 3, size);
  sqlite3_bind_int64 (st.get (), 4, mtime);
  bind_text (st.get (), 5, hash);
  sqlite3_bind_int64 (st.get (), 6, (sqlite3_int64) std::time (nullptr));
  bind_text (st.get (), 7, status);
  bind_text (st.get (), 8, error);
  sqlite3_step (st.get ());
}

static void
insert_chunk (sqlite3* db, const ChunkBuild& build,
              const std::vector<float>& embedding,
              const std::string& embedding_model) {
  const RagChunk& c= build.chunk;
  Statement st (db, "INSERT INTO chunks "
                  "(chunk_id, rel_path, kind, tree_path, anchor, title, "
                  " heading_path, text, source, embedding, embedding_dim, "
                  " embedding_model) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
  bind_text (st.get (), 1, c.chunk_id);
  bind_text (st.get (), 2, c.rel_path);
  bind_text (st.get (), 3, c.kind);
  bind_text (st.get (), 4, c.tree_path);
  bind_text (st.get (), 5, c.anchor);
  bind_text (st.get (), 6, c.title);
  bind_text (st.get (), 7, c.heading_path);
  bind_text (st.get (), 8, c.text);
  bind_text (st.get (), 9, c.source);
  if (!embedding.empty ())
    sqlite3_bind_blob (st.get (), 10, embedding.data (),
                       int (embedding.size () * sizeof (float)),
                       SQLITE_TRANSIENT);
  else
    sqlite3_bind_null (st.get (), 10);
  sqlite3_bind_int (st.get (), 11, int (embedding.size ()));
  bind_text (st.get (), 12, embedding.empty ()? "" : embedding_model);
  sqlite3_step (st.get ());

  for (const std::string& edge: build.edges) {
    Statement e (db, "INSERT INTO edges "
                    "(src_chunk, relation, target, label) VALUES (?, ?, ?, ?)");
    bind_text (e.get (), 1, c.chunk_id);
    bind_text (e.get (), 2, "mentions");
    bind_text (e.get (), 3, edge);
    bind_text (e.get (), 4, c.title);
    sqlite3_step (e.get ());
  }
}

bool
RagIndex::scan_once () {
  if (impl->db == nullptr) return false;
  std::vector<fs::path> files= scan_ath_files (impl->config.vault_root);
  std::vector<fs::path> work_files;
  work_files.reserve (files.size ());
  for (const fs::path& file: files) {
    std::string rel= relative_path (impl->config.vault_root, file);
    if (shard_accepts (rel, impl->config.shard_index,
                       impl->config.shard_count))
      work_files.push_back (file);
  }
  std::set<std::string> live;
  std::string error;
  exec_sql (impl->db, "BEGIN", error);
  auto log_file= [this] (const std::string& message, bool warning= false) {
    if (impl->config.progress_fd >= 0) {
      write_progress_event (
        impl->config.progress_fd,
        std::string (warning ? "W\t" : "L\t") + progress_sanitize (message));
      return;
    }
    if (warning) rag_progress.log_warning (message);
    else rag_progress.log_info (message);
  };
  auto progress_file_done= [this] () {
    if (impl->config.progress_fd >= 0)
      write_progress_event (impl->config.progress_fd, "F");
  };
  auto progress_chunks= [this] (const std::string& rel, size_t done,
                                size_t total) {
    if (impl->config.progress_fd >= 0) {
      write_progress_event (
        impl->config.progress_fd,
        "C\t" + std::to_string (done) + "\t" + std::to_string (total) +
        "\t" + progress_sanitize (rel));
      return;
    }
    if (impl->config.progress)
      rag_progress.update_chunks (done, total, "Embedding RAG chunks", rel);
  };

  for (size_t i=0; i<work_files.size (); i++) {
    const fs::path& file= work_files[i];
    std::string rel= relative_path (impl->config.vault_root, file);
    if (impl->config.progress) {
      rag_progress.clear_chunks ();
      rag_progress.update_file (i + 1, work_files.size (),
                                "Indexing RAG files", rel);
    }
    live.insert (rel);
    std::string text;
    if (!read_bytes (file, text)) {
      upsert_document (impl->db, rel, file, 0, 0, "", "error",
                       "failed to read file");
      log_file ("rag index: failed to read " + rel, true);
      progress_file_done ();
      continue;
    }

    int64_t size= int64_t (text.size ());
    int64_t mt= mtime_ns (file);
    std::string hash= fnv1a_hex (text);
    if (document_is_current (impl->db, rel, size, mt, hash)) {
      if (impl->config.progress || impl->config.progress_fd >= 0)
        log_file ("rag index: up-to-date " + rel);
      progress_file_done ();
      continue;
    }

    delete_document_rows (impl->db, rel);
    try {
      tree doc= texmacs_document_to_tree (to_tm (text));
      std::vector<ChunkBuild> chunks= chunk_document (rel, doc);
      std::vector<std::vector<float>> embeddings (chunks.size ());
      if (impl->embedder.available ()) {
        std::vector<std::string> texts;
        texts.reserve (chunks.size ());
        std::vector<size_t> map;
        map.reserve (chunks.size ());
        for (size_t j=0; j<chunks.size (); j++)
          if (should_embed_text (chunks[j].chunk.text)) {
            texts.push_back (chunks[j].chunk.text);
            map.push_back (j);
          }
        std::vector<std::vector<float>> batch= impl->embedder.embed_many (
          texts,
          [&progress_chunks, &rel] (size_t done, size_t total) {
            progress_chunks (rel, done, total);
          });
        for (size_t j=0; j<batch.size () && j<map.size (); j++)
          embeddings[map[j]]= std::move (batch[j]);
      }
      for (size_t j=0; j<chunks.size (); j++) {
        insert_chunk (impl->db, chunks[j], embeddings[j],
                      impl->embedder.model_fingerprint ());
      }
      upsert_document (impl->db, rel, file, size, mt, hash, "ok", "");
      size_t embedded= 0;
      for (const std::vector<float>& emb: embeddings)
        if (!emb.empty ()) embedded++;
      log_file ("rag index: indexed " + rel + " chunks=" +
                std::to_string (chunks.size ()) + ", embedded=" +
                std::to_string (embedded));
    }
    catch (...) {
      upsert_document (impl->db, rel, file, size, mt, hash, "error",
                       "failed to parse TeXmacs document");
      log_file ("rag index: malformed .ath file: " + rel, true);
    }
    progress_file_done ();
  }
  if (impl->config.progress) rag_progress.finish ();

  Statement docs (impl->db, "SELECT rel_path FROM documents");
  std::vector<std::string> stale;
  while (sqlite3_step (docs.get ()) == SQLITE_ROW) {
    std::string rel= reinterpret_cast<const char*> (
      sqlite3_column_text (docs.get (), 0));
    if (live.count (rel) == 0) stale.push_back (rel);
  }
  for (const std::string& rel: stale) {
    delete_document_rows (impl->db, rel);
    Statement del (impl->db, "DELETE FROM documents WHERE rel_path=?");
    bind_text (del.get (), 1, rel);
    sqlite3_step (del.get ());
  }
  rebuild_fts (impl->db);
  exec_sql (impl->db, "COMMIT", error);
  return true;
}

bool
RagIndex::parallel_reindex (int jobs) {
  if (impl->db == nullptr) return false;
  if (jobs <= 1) return scan_once ();

#if defined(__unix__) || defined(__APPLE__)
  size_t total_files= scan_ath_files (impl->config.vault_root).size ();
  if (total_files == 0) total_files= 1;
  std::vector<std::string> temp_dbs;
  temp_dbs.reserve (size_t (jobs));
  for (int i=0; i<jobs; i++) {
    std::string path= worker_db_path (impl->config.db_path, i);
    temp_dbs.push_back (path);
    std::error_code ec;
    fs::remove (path, ec);
    fs::remove (path + "-wal", ec);
    fs::remove (path + "-shm", ec);
  }

  std::vector<pid_t> pids;
  std::vector<int> progress_reads;
  pids.reserve (size_t (jobs));
  progress_reads.reserve (size_t (jobs));
  for (int i=0; i<jobs; i++) {
    int fds[2]= { -1, -1 };
    if (pipe (fds) != 0) {
      std_warning << "rag index: failed to create worker progress pipe" << "\n";
      return false;
    }
    pid_t pid= fork ();
    if (pid == 0) {
      close (fds[0]);
      bool ok= false;
      {
        RagConfig worker= impl->config;
        worker.db_path= temp_dbs[size_t (i)];
        worker.force_reindex= true;
        worker.load_embedding_model= true;
        worker.shard_index= i;
        worker.shard_count= jobs;
        worker.progress= false;
        worker.progress_fd= fds[1];
        unsigned hw= std::max (1u, std::thread::hardware_concurrency ());
        worker.embedding_threads= std::max (1u, hw / (unsigned) jobs);
        RagIndex idx;
        ok= idx.open (worker) && idx.scan_once ();
      }
      close (fds[1]);
      _exit (ok? 0: 1);
    }
    close (fds[1]);
    if (pid < 0) {
      close (fds[0]);
      std_warning << "rag index: failed to fork worker" << "\n";
      return false;
    }
    pids.push_back (pid);
    progress_reads.push_back (fds[0]);
  }

  int completed= 0;
  size_t processed_files= 0;
  size_t known_chunks= 0;
  size_t embedded_chunks= 0;
  std::unordered_map<std::string,size_t> chunk_totals;
  std::unordered_map<std::string,size_t> chunk_done;
  std::vector<std::string> progress_buffers (progress_reads.size ());
  bool ok= true;
  auto start= std::chrono::steady_clock::now ();
  auto process_worker_event= [&] (const std::string& line) {
    std::vector<std::string> parts= split_tabs (line);
    if (parts.empty () || parts[0].empty ()) return;
    if (parts[0] == "F") {
      processed_files++;
      return;
    }
    if ((parts[0] == "L" || parts[0] == "W") && parts.size () >= 2) {
      if (parts[0] == "W") rag_progress.log_warning (parts[1]);
      else rag_progress.log_info (parts[1]);
      return;
    }
    if (parts[0] == "C" && parts.size () >= 4) {
      size_t done= 0;
      size_t total= 0;
      try {
        done= (size_t) std::stoull (parts[1]);
        total= (size_t) std::stoull (parts[2]);
      }
      catch (...) {
        return;
      }
      const std::string& rel= parts[3];
      size_t old_total= chunk_totals[rel];
      if (total > old_total) {
        known_chunks += total - old_total;
        chunk_totals[rel]= total;
      }
      size_t old_done= chunk_done[rel];
      if (done > old_done) {
        embedded_chunks += done - old_done;
        chunk_done[rel]= done;
      }
      size_t total_for_bar= known_chunks == 0 ? embedded_chunks + 1 :
                            known_chunks;
      rag_progress.update_chunks (embedded_chunks, total_for_bar,
                                  "Embedding RAG chunks", rel);
    }
  };
  while (completed < jobs) {
    std::vector<pollfd> pfds (progress_reads.size ());
    for (size_t i=0; i<progress_reads.size (); i++)
      pfds[i]= {progress_reads[i], POLLIN | POLLHUP | POLLERR, 0};

    if (!pfds.empty ()) {
      int rc= poll (pfds.data (), pfds.size (), 1000);
      if (rc > 0) {
        for (size_t i=0; i<pfds.size (); i++) {
          pollfd& pfd= pfds[i];
          if (pfd.fd < 0) continue;
          if (pfd.revents & POLLIN) {
            char buf[256];
            ssize_t n= read (pfd.fd, buf, sizeof (buf));
            if (n > 0) {
              std::string& pending= progress_buffers[i];
              pending.append (buf, buf + n);
              while (true) {
                size_t nl= pending.find ('\n');
                if (nl == std::string::npos) break;
                std::string line= pending.substr (0, nl);
                pending.erase (0, nl + 1);
                process_worker_event (line);
              }
            }
          }
          if (pfd.revents & (POLLHUP | POLLERR | POLLNVAL)) {
            std::string& pending= progress_buffers[i];
            if (!pending.empty ()) {
              process_worker_event (pending);
              pending.clear ();
            }
            close (pfd.fd);
            progress_reads[i]= -1;
          }
        }
      }
    }

    while (true) {
      int status= 0;
      pid_t pid= waitpid (-1, &status, WNOHANG);
      if (pid == 0) break;
      if (pid < 0) break;
      completed++;
      if (!WIFEXITED (status) || WEXITSTATUS (status) != 0) ok= false;
    }

    auto now= std::chrono::steady_clock::now ();
    long long elapsed= std::chrono::duration_cast<std::chrono::seconds> (
      now - start).count ();
    if (processed_files > total_files) processed_files= total_files;
    if (known_chunks > 0)
      rag_progress.update_chunks (embedded_chunks, known_chunks,
                                  "Parallel RAG embedding",
                                  std::to_string (jobs - completed) +
                                  " workers active, " +
                                  std::to_string (elapsed) + "s elapsed");
    rag_progress.update_file (processed_files, total_files,
                              "Parallel RAG files",
                              std::to_string (jobs - completed) +
                              " workers active, " +
                              std::to_string (elapsed) + "s elapsed");
  }
  rag_progress.finish ();
  for (int fd: progress_reads)
    if (fd >= 0) close (fd);
  if (!ok) return false;

  std::string error;
  exec_sql (impl->db, "BEGIN", error);
  exec_sql (impl->db, "DELETE FROM documents; DELETE FROM chunks; "
                      "DELETE FROM edges; DELETE FROM chunks_fts;", error);
  for (const std::string& path: temp_dbs) {
    if (!merge_worker_database (impl->db, path, error)) {
      exec_sql (impl->db, "ROLLBACK", error);
      std_warning << "rag index: failed to merge worker database: "
                  << error.c_str () << "\n";
      return false;
    }
  }
  rebuild_fts (impl->db);
  exec_sql (impl->db, "COMMIT", error);

  for (const std::string& path: temp_dbs) {
    std::error_code ec;
    fs::remove (path, ec);
    fs::remove (path + "-wal", ec);
    fs::remove (path + "-shm", ec);
  }
  return true;
#else
  std_warning << "rag index: process parallelization is unavailable on this "
              << "platform; using serial indexing" << "\n";
  return scan_once ();
#endif
}

void
RagIndex::set_progress_enabled (bool enabled) {
  impl->config.progress= enabled;
}

RagStatus
RagIndex::status () const {
  RagStatus s= impl->status;
  if (impl->db == nullptr) return s;
  Statement docs (impl->db, "SELECT count(*) FROM documents WHERE status='ok'");
  if (sqlite3_step (docs.get ()) == SQLITE_ROW)
    s.document_count= sqlite3_column_int (docs.get (), 0);
  Statement chunks (impl->db, "SELECT count(*) FROM chunks");
  if (sqlite3_step (chunks.get ()) == SQLITE_ROW)
    s.chunk_count= sqlite3_column_int (chunks.get (), 0);
  Statement bad (impl->db, "SELECT count(*) FROM documents WHERE status!='ok'");
  if (sqlite3_step (bad.get ()) == SQLITE_ROW)
    s.malformed_count= sqlite3_column_int (bad.get (), 0);
  return s;
}

std::optional<RagChunk>
RagIndex::read_chunk (const std::string& chunk_id) const {
  if (impl->db == nullptr) return std::nullopt;
  Statement st (impl->db, "SELECT chunk_id, rel_path, kind, tree_path, anchor, "
                         "title, heading_path, text, source "
                         "FROM chunks WHERE chunk_id=?");
  bind_text (st.get (), 1, chunk_id);
  if (sqlite3_step (st.get ()) != SQLITE_ROW) return std::nullopt;
  return chunk_from_stmt (st.get ());
}

std::string
RagIndex::read_document (const std::string& rel_path) const {
  if (!valid_vault_relative_path (rel_path)) return "";
  std::string text;
  if (!read_bytes (impl->config.vault_root / rel_path, text)) return "";
  return text;
}

std::vector<RagChunk>
RagIndex::list_chunks (int limit) const {
  if (impl->db == nullptr) return {};
  limit= std::max (1, std::min (limit, 200));
  Statement st (impl->db, "SELECT chunk_id, rel_path, kind, tree_path, anchor, "
                         "title, heading_path, text, source "
                         "FROM chunks ORDER BY rel_path, tree_path LIMIT ?");
  sqlite3_bind_int (st.get (), 1, limit);
  std::vector<RagChunk> out;
  while (sqlite3_step (st.get ()) == SQLITE_ROW)
    out.push_back (chunk_from_stmt (st.get ()));
  return out;
}

std::vector<RagChunk>
RagIndex::search (const std::string& query, int limit) {
  if (impl->db == nullptr) return {};
  limit= std::max (1, std::min (limit, 50));
  std::string q= fts_query (query);
  std::vector<float> qemb;
  if (impl->embedder.available ()) qemb= impl->embedder.embed (query);

  std::vector<RagChunk> out;
  if (!q.empty ()) {
    Statement st (
      impl->db,
      "SELECT c.chunk_id, c.rel_path, c.kind, c.tree_path, c.anchor, "
      "c.title, c.heading_path, c.text, c.source, bm25(chunks_fts), "
      "c.embedding, c.embedding_dim, c.embedding_model "
      "FROM chunks_fts JOIN chunks c ON c.chunk_id=chunks_fts.chunk_id "
      "WHERE chunks_fts MATCH ? ORDER BY bm25(chunks_fts) LIMIT ?");
    bind_text (st.get (), 1, q);
    sqlite3_bind_int (st.get (), 2, std::max (limit * 5, 30));
    while (sqlite3_step (st.get ()) == SQLITE_ROW) {
      RagChunk c= chunk_from_stmt (st.get ());
      double bm25= sqlite3_column_double (st.get (), 9);
      c.score= -bm25;
      std::vector<float> emb= blob_to_vector (st.get (), 10,
                                              sqlite3_column_int (st.get (), 11));
      const unsigned char* model_text= sqlite3_column_text (st.get (), 12);
      std::string model= model_text == nullptr ? std::string ():
        std::string (reinterpret_cast<const char*> (model_text));
      if (!qemb.empty () && !emb.empty () &&
          model == impl->embedder.model_fingerprint ())
        c.score += dot (qemb, emb);
      out.push_back (c);
    }
  }

  if (out.empty () && !query.empty ()) {
    std::string like= "%" + query + "%";
    Statement st (impl->db,
      "SELECT chunk_id, rel_path, kind, tree_path, anchor, title, "
      "heading_path, text, source FROM chunks "
      "WHERE text LIKE ? OR title LIKE ? OR heading_path LIKE ? "
      "ORDER BY rel_path, tree_path LIMIT ?");
    bind_text (st.get (), 1, like);
    bind_text (st.get (), 2, like);
    bind_text (st.get (), 3, like);
    sqlite3_bind_int (st.get (), 4, limit);
    while (sqlite3_step (st.get ()) == SQLITE_ROW) {
      RagChunk c= chunk_from_stmt (st.get ());
      c.score= 0.0;
      out.push_back (c);
    }
  }

  std::sort (out.begin (), out.end (),
             [] (const RagChunk& a, const RagChunk& b) {
               return a.score > b.score;
             });
  if ((int) out.size () > limit) out.resize (limit);
  return out;
}

std::vector<RagChunk>
RagIndex::related (const std::string& chunk_id, int limit) const {
  std::optional<RagChunk> c= read_chunk (chunk_id);
  if (!c) return {};
  limit= std::max (1, std::min (limit, 50));
  Statement st (impl->db, "SELECT chunk_id, rel_path, kind, tree_path, anchor, "
                         "title, heading_path, text, source "
                         "FROM chunks WHERE rel_path=? AND chunk_id!=? "
                         "ORDER BY tree_path LIMIT ?");
  bind_text (st.get (), 1, c->rel_path);
  bind_text (st.get (), 2, chunk_id);
  sqlite3_bind_int (st.get (), 3, limit);
  std::vector<RagChunk> out;
  while (sqlite3_step (st.get ()) == SQLITE_ROW)
    out.push_back (chunk_from_stmt (st.get ()));
  return out;
}

std::vector<RagChunk>
RagIndex::backlinks (const std::string& target, int limit) const {
  limit= std::max (1, std::min (limit, 50));
  Statement st (impl->db,
    "SELECT c.chunk_id, c.rel_path, c.kind, c.tree_path, c.anchor, c.title, "
    "c.heading_path, c.text, c.source "
    "FROM edges e JOIN chunks c ON c.chunk_id=e.src_chunk "
    "WHERE e.target LIKE ? ORDER BY c.rel_path, c.tree_path LIMIT ?");
  bind_text (st.get (), 1, "%" + target + "%");
  sqlite3_bind_int (st.get (), 2, limit);
  std::vector<RagChunk> out;
  while (sqlite3_step (st.get ()) == SQLITE_ROW)
    out.push_back (chunk_from_stmt (st.get ()));
  return out;
}

} // namespace athena::rag
