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
#include "ATHENA/Data/background_workers.hpp"
#include "rag_embedding.hpp"
#include "rag_storage.hpp"

#include "ATHENA/Data/vaultfile_json.hpp"
#include "Data/Convert/Xml/document_file_codec.hpp"
#include "Data/Convert/Xml/document_upgrade_file.hpp"
#include "convert.hpp"
#include "tm_ostream.hpp"
#include "confined_filesystem.hpp"
#include <nlohmann/json.hpp>

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
    athena_spdlog_info (message);
    if (was_active) draw ();
  }

  void log_warning (const std::string& message) {
    bool was_active= active_;
    clear ();
    athena_spdlog_warning (message);
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

std::vector<fs::path>
scan_ath_files (const fs::path& root,
                    const std::function<bool ()>& current,
                    athena::background::source_watch* watch) {
  std::vector<fs::path> out;
  if (watch) watch->directory (root);
  fs::path maintenance_root;
  AthenaVaultfileInfo info;
  std::string vault_error;
  if (athena_vaultfile_read (root, info, vault_error)) {
    fs::path configured= fs::path (info.maintenance_summary_path)
                           .lexically_normal ();
    bool valid= !configured.empty () && !configured.is_absolute ();
    for (const fs::path& part: configured)
      valid= valid && part != "." && part != "..";
    if (valid) maintenance_root= root / configured;
  }
  std::error_code ec;
  fs::recursive_directory_iterator it (
    root, fs::directory_options::skip_permission_denied, ec);
  fs::recursive_directory_iterator end;
  for (; !ec && it != end; it.increment (ec)) {
    if (current && !current ()) return {};
    fs::path p= it->path ();
    const auto status= it->symlink_status (ec);
    if (ec == std::errc::no_such_file_or_directory) {
      ec.clear ();
      continue;
    }
    if (ec) throw fs::filesystem_error ("Read RAG inventory entry", p, ec);
    if (fs::is_symlink (status)) {
      it.disable_recursion_pending ();
      continue;
    }
    if (fs::is_directory (status)) {
      std::string name= p.filename ().string ();
      if (name == ".athena" || name == ".backup" || name == ".git" ||
          (!maintenance_root.empty () &&
           p.lexically_normal () == maintenance_root))
        it.disable_recursion_pending ();
      else if (watch) watch->directory (p);
      continue;
    }
    if (!fs::is_regular_file (status)) continue;
    if (p.extension () == ".ath") out.push_back (p);
  }
  if (ec) throw fs::filesystem_error ("Read RAG inventory", root, ec);
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

static bool should_embed_text (const std::string& text);

struct ChunkBuild {
  RagChunk chunk;
  std::string embedding_input_hash;
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
  if (should_embed_text (c.text))
    build.embedding_input_hash= athena::document::storage_bytes_fingerprint (
      std::string ("athena-rag-embedding-input-v1\n") + c.text);
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
    // The recursive collector includes its root when it is an enunciation.
    // Do not emit that same (path, kind) chunk twice.
    if (!is_enunciation_tag (kind))
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

std::vector<fs::path>
rag_document_files (const fs::path& root,
                    const std::function<bool ()>& current,
                    athena::background::source_watch* watch) {
  return scan_ath_files (root, current, watch);
}

bool
rag_text_requires_embedding (const std::string& text) {
  return should_embed_text (text);
}

struct RagIndex::Impl {
  RagConfig config;
  sqlite3* db= nullptr;
  RagEmbedder owned_embedder;
  std::shared_ptr<RagEmbedder> shared_embedder;
  RagStatus status;

  RagEmbedder& embedder () {
    return shared_embedder ? *shared_embedder : owned_embedder;
  }
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
  impl->shared_embedder= config.embedding_runtime;
  impl->status= RagStatus ();
  impl->status.vault_root= config.vault_root.generic_string ();
  impl->status.db_path= config.db_path.generic_string ();
  impl->status.embedding_model= config.embedding_model.generic_string ();

  std::error_code ec;
  fs::create_directories (config.db_path.parent_path (), ec);
  std::string reset_error;
  if (!storage::prepare_database_path (config.db_path, reset_error)) {
    impl->status.last_error= reset_error;
    return false;
  }
  if (sqlite3_open (config.db_path.string ().c_str (), &impl->db) !=
      SQLITE_OK) {
    impl->status.last_error= sqlite3_errmsg (impl->db);
    athena_spdlog_error (
      "rag index: failed to open " + impl->status.db_path + ": " +
      impl->status.last_error);
    return false;
  }
  std::string error;
  if (!storage::ensure_schema (impl->db, error)) {
    impl->status.last_error= error;
    athena_spdlog_error (
      "rag index: schema initialization failed: " + error);
    return false;
  }

  if (config.force_reindex) {
    exec_sql (impl->db,
               "DELETE FROM documents; DELETE FROM chunks; DELETE FROM edges;"
               "DELETE FROM chunks_fts; DELETE FROM embeddings;"
               "DELETE FROM embedding_spaces;", error);
  }

  if (config.load_embedding_model && !config.embedding_model.empty ()) {
    if (impl->embedder ().available () ||
        impl->embedder ().open (config.embedding_model.string (),
                                config.embedding_device,
                                config.embedding_threads)) {
      impl->status.embeddings_enabled= true;
      impl->status.embedding_space= impl->embedder ().space_id ();
    }
    else {
      impl->status.embedding_warning=
        "Embedding model could not be loaded; using FTS-only retrieval.";
    }
  }

  impl->status.open= true;
  return true;
}

struct CachedDocumentRevision {
  bool found= false;
  int64_t size= 0;
  int64_t mtime= 0;
  std::string storage_revision;
  std::string semantic_revision;
  std::string status;
};

static CachedDocumentRevision
document_revision (sqlite3* db, const std::string& rel) {
  CachedDocumentRevision result;
  Statement st (db, "SELECT size,mtime_ns,storage_revision,semantic_revision,status "
                    "FROM documents WHERE rel_path=?");
  if (st.get () == nullptr) return result;
  bind_text (st.get (), 1, rel);
  if (sqlite3_step (st.get ()) != SQLITE_ROW) return result;
  result.found= true;
  result.size= sqlite3_column_int64 (st.get (), 0);
  result.mtime= sqlite3_column_int64 (st.get (), 1);
  auto column= [&] (int index) {
    const unsigned char* text= sqlite3_column_text (st.get (), index);
    return text == nullptr ? std::string () :
      std::string (reinterpret_cast<const char*> (text));
  };
  result.storage_revision= column (2);
  result.semantic_revision= column (3);
  result.status= column (4);
  return result;
}

static bool
delete_document_rows (sqlite3* db, const std::string& rel) {
  Statement d0 (db, "DELETE FROM chunks_fts WHERE rel_path=?");
  if (d0.get () == nullptr) return false;
  bind_text (d0.get (), 1, rel);
  if (sqlite3_step (d0.get ()) != SQLITE_DONE) return false;
  Statement d1 (db, "DELETE FROM chunks WHERE rel_path=?");
  if (d1.get () == nullptr) return false;
  bind_text (d1.get (), 1, rel);
  if (sqlite3_step (d1.get ()) != SQLITE_DONE) return false;
  Statement d2 (db, "DELETE FROM edges WHERE src_chunk NOT IN "
                    "(SELECT chunk_id FROM chunks)");
  return d2.get () != nullptr && sqlite3_step (d2.get ()) == SQLITE_DONE;
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
                           "storage_revision, semantic_revision, indexed_at, status, error "
                           "FROM documents");
    Statement dst (db, "INSERT OR REPLACE INTO documents "
                       "(rel_path, abs_path, size, mtime_ns, storage_revision, semantic_revision, "
                       " indexed_at, status, error) "
                       "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)");
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
      sqlite3_bind_text (dst.get (), 6, text_col (src.get (), 5),
                         -1, SQLITE_TRANSIENT);
      sqlite3_bind_int64 (dst.get (), 7, sqlite3_column_int64 (src.get (), 6));
      sqlite3_bind_text (dst.get (), 8, text_col (src.get (), 7),
                         -1, SQLITE_TRANSIENT);
      sqlite3_bind_text (dst.get (), 9, text_col (src.get (), 8),
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
                           "embedding_input_hash, embedding_space "
                           "FROM chunks");
    Statement dst (db, "INSERT OR REPLACE INTO chunks "
                       "(chunk_id, rel_path, kind, tree_path, anchor, title, "
                       " heading_path, text, source, embedding_input_hash, "
                       " embedding_space) "
                       "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
    while (sqlite3_step (src.get ()) == SQLITE_ROW) {
      sqlite3_reset (dst.get ());
      sqlite3_clear_bindings (dst.get ());
      for (int i=0; i<11; i++)
        sqlite3_bind_text (dst.get (), i + 1, text_col (src.get (), i),
                           -1, SQLITE_TRANSIENT);
      if (sqlite3_step (dst.get ()) != SQLITE_DONE) {
        error= sqlite3_errmsg (db);
        sqlite3_close (worker);
        return false;
      }
    }
  }

  {
    Statement src (worker, "SELECT space_id,input_hash,embedding,embedding_dim "
                           "FROM embeddings");
    Statement dst (db, "INSERT OR REPLACE INTO embeddings "
                       "(space_id,input_hash,embedding,embedding_dim) "
                       "VALUES (?,?,?,?)");
    while (sqlite3_step (src.get ()) == SQLITE_ROW) {
      sqlite3_reset (dst.get ());
      sqlite3_clear_bindings (dst.get ());
      sqlite3_bind_text (dst.get (), 1, text_col (src.get (), 0), -1,
                         SQLITE_TRANSIENT);
      sqlite3_bind_text (dst.get (), 2, text_col (src.get (), 1), -1,
                         SQLITE_TRANSIENT);
      const void* blob= sqlite3_column_blob (src.get (), 2);
      int bytes= sqlite3_column_bytes (src.get (), 2);
      if (blob != nullptr && bytes > 0)
        sqlite3_bind_blob (dst.get (), 3, blob, bytes, SQLITE_TRANSIENT);
      else sqlite3_bind_null (dst.get (), 3);
      sqlite3_bind_int (dst.get (), 4, sqlite3_column_int (src.get (), 3));
      if (sqlite3_step (dst.get ()) != SQLITE_DONE) {
        error= sqlite3_errmsg (db);
        sqlite3_close (worker);
        return false;
      }
    }
  }

  {
    Statement src (worker, "SELECT space_id,dimension,backend,model,contract "
                           "FROM embedding_spaces");
    Statement dst (db, "INSERT OR REPLACE INTO embedding_spaces "
                       "(space_id,dimension,backend,model,contract) "
                       "VALUES (?,?,?,?,?)");
    while (sqlite3_step (src.get ()) == SQLITE_ROW) {
      sqlite3_reset (dst.get ());
      sqlite3_clear_bindings (dst.get ());
      sqlite3_bind_text (dst.get (), 1, text_col (src.get (), 0), -1,
                         SQLITE_TRANSIENT);
      sqlite3_bind_int (dst.get (), 2, sqlite3_column_int (src.get (), 1));
      for (int i=2; i<5; ++i)
        sqlite3_bind_text (dst.get (), i + 1, text_col (src.get (), i), -1,
                           SQLITE_TRANSIENT);
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

static bool
upsert_document (sqlite3* db, const std::string& rel, const fs::path& abs,
                  int64_t size, int64_t mtime,
                  const std::string& storage_revision,
                  const std::string& semantic_revision,
                  const std::string& status, const std::string& error) {
  Statement st (db, "INSERT OR REPLACE INTO documents "
                  "(rel_path, abs_path, size, mtime_ns, storage_revision, semantic_revision, "
                  " indexed_at, status, error) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)");
  bind_text (st.get (), 1, rel);
  bind_text (st.get (), 2, abs.generic_string ());
  sqlite3_bind_int64 (st.get (), 3, size);
  sqlite3_bind_int64 (st.get (), 4, mtime);
  bind_text (st.get (), 5, storage_revision);
  bind_text (st.get (), 6, semantic_revision);
  sqlite3_bind_int64 (st.get (), 7, (sqlite3_int64) std::time (nullptr));
  bind_text (st.get (), 8, status);
  bind_text (st.get (), 9, error);
  return st.get () != nullptr && sqlite3_step (st.get ()) == SQLITE_DONE;
}

static bool
insert_chunk (sqlite3* db, const ChunkBuild& build,
               const std::string& embedding_space) {
  const RagChunk& c= build.chunk;
  Statement st (db, "INSERT INTO chunks "
                   "(chunk_id, rel_path, kind, tree_path, anchor, title, "
                   " heading_path, text, source, embedding_input_hash, "
                   " embedding_space) "
                   "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
  bind_text (st.get (), 1, c.chunk_id);
  bind_text (st.get (), 2, c.rel_path);
  bind_text (st.get (), 3, c.kind);
  bind_text (st.get (), 4, c.tree_path);
  bind_text (st.get (), 5, c.anchor);
  bind_text (st.get (), 6, c.title);
  bind_text (st.get (), 7, c.heading_path);
  bind_text (st.get (), 8, c.text);
  bind_text (st.get (), 9, c.source);
  bind_text (st.get (), 10, build.embedding_input_hash);
  bind_text (st.get (), 11, embedding_space);
  if (st.get () == nullptr || sqlite3_step (st.get ()) != SQLITE_DONE)
    return false;

  Statement fts (db, "INSERT INTO chunks_fts "
                     "(chunk_id,rel_path,title,heading_path,text) "
                     "VALUES (?,?,?,?,?)");
  bind_text (fts.get (), 1, c.chunk_id);
  bind_text (fts.get (), 2, c.rel_path);
  bind_text (fts.get (), 3, c.title);
  bind_text (fts.get (), 4, c.heading_path);
  bind_text (fts.get (), 5, c.text);
  if (fts.get () == nullptr || sqlite3_step (fts.get ()) != SQLITE_DONE)
    return false;

  for (const std::string& edge: build.edges) {
    Statement e (db, "INSERT INTO edges "
                    "(src_chunk, relation, target, label) VALUES (?, ?, ?, ?)");
    bind_text (e.get (), 1, c.chunk_id);
    bind_text (e.get (), 2, "mentions");
    bind_text (e.get (), 3, edge);
    bind_text (e.get (), 4, c.title);
    if (e.get () == nullptr || sqlite3_step (e.get ()) != SQLITE_DONE)
      return false;
  }
  return true;
}

static std::vector<float>
cached_embedding (sqlite3* db, const std::string& space_id,
                  const std::string& input_hash) {
  if (space_id.empty () || input_hash.empty ()) return {};
  Statement st (db, "SELECT embedding,embedding_dim FROM embeddings "
                    "WHERE space_id=? AND input_hash=?");
  if (st.get () == nullptr) return {};
  bind_text (st.get (), 1, space_id);
  bind_text (st.get (), 2, input_hash);
  if (sqlite3_step (st.get ()) != SQLITE_ROW) return {};
  return blob_to_vector (st.get (), 0, sqlite3_column_int (st.get (), 1));
}

static bool
store_embedding (sqlite3* db, const std::string& space_id,
                 const std::string& input_hash,
                 const std::vector<float>& embedding) {
  if (space_id.empty () || input_hash.empty () || embedding.empty ()) return true;
  Statement st (db, "INSERT INTO embeddings "
                    "(space_id,input_hash,embedding,embedding_dim) VALUES (?,?,?,?) "
                    "ON CONFLICT(space_id,input_hash) DO NOTHING");
  if (st.get () == nullptr) return false;
  bind_text (st.get (), 1, space_id);
  bind_text (st.get (), 2, input_hash);
  sqlite3_bind_blob (st.get (), 3, embedding.data (),
                     int (embedding.size () * sizeof (float)), SQLITE_TRANSIENT);
  sqlite3_bind_int (st.get (), 4, int (embedding.size ()));
  return sqlite3_step (st.get ()) == SQLITE_DONE;
}

static bool
document_has_complete_space (sqlite3* db, const std::string& rel,
                             const std::string& space_id) {
  if (space_id.empty ()) return true;
  Statement st (db,
    "SELECT count(*) FROM chunks c LEFT JOIN embeddings e "
    "ON e.space_id=? AND e.input_hash=c.embedding_input_hash "
    "WHERE c.rel_path=? AND c.embedding_input_hash!='' AND "
    "(c.embedding_space!=? OR e.input_hash IS NULL)");
  if (st.get () == nullptr) return false;
  bind_text (st.get (), 1, space_id);
  bind_text (st.get (), 2, rel);
  bind_text (st.get (), 3, space_id);
  return sqlite3_step (st.get ()) == SQLITE_ROW && sqlite3_column_int64 (st.get (), 0) == 0;
}

static std::string
physical_file_revision (const athena::filesystem::metadata& value) {
  // Exclude atime: reading a document must not invalidate this proof. Include
  // inode and ctime so atomic replacement or preserved mtime cannot hide edits.
  return nlohmann::json::array ({1, value.device, value.inode, value.size,
    value.modified.seconds, value.modified.nanoseconds,
    value.changed.seconds, value.changed.nanoseconds}).dump ();
}

static bool
file_check_matches (sqlite3* db, const std::string& rel,
                    const std::string& physical, const std::string& storage) {
  Statement st (db, "SELECT 1 FROM document_file_checks WHERE rel_path=? "
                    "AND file_revision=? AND storage_revision=?");
  if (!st.get ()) return false;
  bind_text (st.get (), 1, rel);
  bind_text (st.get (), 2, physical);
  bind_text (st.get (), 3, storage);
  return sqlite3_step (st.get ()) == SQLITE_ROW;
}

static bool
remember_file_check (sqlite3* db, const std::string& rel,
                     const std::string& physical, const std::string& storage) {
  Statement st (db, "INSERT INTO document_file_checks VALUES (?,?,?) "
                    "ON CONFLICT(rel_path) DO UPDATE SET "
                    "file_revision=excluded.file_revision, "
                    "storage_revision=excluded.storage_revision "
                    "WHERE file_revision!=excluded.file_revision "
                    "OR storage_revision!=excluded.storage_revision");
  if (!st.get ()) return false;
  bind_text (st.get (), 1, rel);
  bind_text (st.get (), 2, physical);
  bind_text (st.get (), 3, storage);
  return sqlite3_step (st.get ()) == SQLITE_DONE;
}

bool
RagIndex::prepare_document (const std::string& rel_path,
                            const std::string& expected_storage_revision,
                            const std::string& embedding_space,
                            RagPreparedDocument& prepared) {
  prepared= RagPreparedDocument ();
  impl->status.last_error.clear ();
  impl->status.revision_superseded= false;
  if (impl->db == nullptr || !valid_vault_relative_path (rel_path)) {
    impl->status.last_error= "invalid RAG document path";
    return false;
  }

  fs::path absolute= impl->config.vault_root / fs::path (rel_path);
  CachedDocumentRevision cached= document_revision (impl->db, rel_path);
  prepared.rel_path= rel_path;
  prepared.absolute_path= absolute;
  prepared.embedding_space= embedding_space;
  std::string bytes;
  std::string physical;
  try {
    athena::filesystem::confined_root root (impl->config.vault_root);
    auto entry= root.open (rel_path);
    const auto before= entry.stat ();
    if (before.directory) throw std::runtime_error ("Source is a directory");
    physical= physical_file_revision (before);
    if (cached.found && cached.status == "ok" &&
        !cached.storage_revision.empty () && !cached.semantic_revision.empty () &&
        (expected_storage_revision.empty () || expected_storage_revision == cached.storage_revision) &&
        file_check_matches (impl->db, rel_path, physical, cached.storage_revision) &&
        document_has_complete_space (impl->db, rel_path, embedding_space) &&
        athena::filesystem::same_revision (before, root.open (rel_path).stat ())) {
      prepared.size= cached.size;
      prepared.mtime_ns= cached.mtime;
      prepared.storage_revision= cached.storage_revision;
      prepared.semantic_revision= cached.semantic_revision;
      prepared.unchanged= true;
      return true;
    }
    bytes= entry.read (athena::document::codec_limits ().input_bytes);
    if (!athena::filesystem::same_revision (before, root.open (rel_path).stat ())) {
      impl->status.revision_superseded= true;
      return false;
    }
  }
  catch (const std::exception& error) {
    impl->status.last_error= "failed to read RAG document " + rel_path + ": " + error.what ();
    return false;
  }
  const std::string storage_revision=
    athena::document::storage_bytes_fingerprint (bytes);
  if (!expected_storage_revision.empty () &&
      storage_revision != expected_storage_revision) {
    impl->status.revision_superseded= true;
    return false;
  }

  prepared.storage_revision= storage_revision;
  prepared.size= static_cast<std::int64_t> (bytes.size ());
  prepared.mtime_ns= mtime_ns (absolute);

  if (!remember_file_check (impl->db, rel_path, physical, storage_revision)) {
    impl->status.last_error= sqlite3_errmsg (impl->db);
    return false;
  }
  const bool storage_same= cached.found && cached.status == "ok" &&
    cached.storage_revision == storage_revision;
  if (storage_same && !cached.semantic_revision.empty () &&
      document_has_complete_space (impl->db, rel_path, embedding_space)) {
    prepared.semantic_revision= cached.semantic_revision;
    prepared.unchanged= true;
    return true;
  }

  tree document;
  try {
    document= athena::document::decode_document_bytes (bytes, absolute).document;
  }
  catch (const std::exception& error) {
    impl->status.last_error= "failed to decode RAG document " + rel_path +
                             ": " + error.what ();
    return false;
  }
  prepared.semantic_revision=
    athena::document::semantic_document_fingerprint (document);

  if (cached.found && cached.status == "ok" &&
      cached.semantic_revision == prepared.semantic_revision &&
      document_has_complete_space (impl->db, rel_path, embedding_space)) {
    prepared.metadata_only= true;
    return true;
  }

  std::vector<ChunkBuild> built= chunk_document (rel_path, document);
  prepared.chunks.reserve (built.size ());
  for (std::size_t i=0; i<built.size (); ++i) {
    RagPreparedChunk item;
    item.chunk= built[i].chunk;
    item.embedding_input_hash= built[i].embedding_input_hash;
    item.edges= std::move (built[i].edges);
    if (!embedding_space.empty () && should_embed_text (item.chunk.text)) {
      item.cached_embedding= cached_embedding (
        impl->db, embedding_space, item.embedding_input_hash);
      if (item.cached_embedding.empty ()) {
        item.needs_embedding= true;
        prepared.missing_embedding_indices.push_back (i);
      }
    }
    prepared.chunks.push_back (std::move (item));
  }
  return true;
}

bool
RagIndex::commit_document (
  const RagPreparedDocument& prepared,
  const std::vector<std::vector<float>>& computed_embeddings,
  const std::function<bool ()>& generation_is_current) {
  impl->status.last_error.clear ();
  impl->status.revision_superseded= false;
  auto superseded= [&] {
    impl->status.revision_superseded= true;
    return false;
  };
  if (impl->db == nullptr) {
    impl->status.last_error= "RAG index is not open";
    return false;
  }
  if (prepared.unchanged) return true;
  if (generation_is_current && !generation_is_current ()) return superseded ();
  if (prepared.missing_embedding_indices.size () != computed_embeddings.size ()) {
    impl->status.last_error= "RAG computed embedding count does not match prepared work";
    return false;
  }

  // Re-read the atomically saved file immediately before taking the SQLite
  // write lock.  Long parsing and inference have already happened outside the
  // transaction; a superseded save must not publish stale document rows.
  std::string current_bytes;
  if (!read_bytes (prepared.absolute_path, current_bytes)) {
    impl->status.last_error= "Could not read RAG source before commit: " +
                             prepared.absolute_path.string ();
    return false;
  }
  if (athena::document::storage_bytes_fingerprint (current_bytes) !=
      prepared.storage_revision) return superseded ();
  if (generation_is_current && !generation_is_current ()) return superseded ();

  std::string error;
  if (!exec_sql (impl->db, "BEGIN IMMEDIATE", error)) {
    impl->status.last_error= error;
    return false;
  }
  auto rollback= [&] (const std::string& message) {
    std::string ignored;
    (void) exec_sql (impl->db, "ROLLBACK", ignored);
    impl->status.last_error= message;
    return false;
  };
  auto rollback_superseded= [&] {
    rollback ("");
    return superseded ();
  };

  if (generation_is_current && !generation_is_current ())
    return rollback_superseded ();

  if (prepared.metadata_only) {
    if (!upsert_document (
          impl->db, prepared.rel_path, prepared.absolute_path, prepared.size,
          prepared.mtime_ns, prepared.storage_revision,
          prepared.semantic_revision, "ok", ""))
      return rollback (sqlite3_errmsg (impl->db));
    if (generation_is_current && !generation_is_current ())
      return rollback_superseded ();
    if (!exec_sql (impl->db, "COMMIT", error))
      return rollback (error);
    return true;
  }

  if (!delete_document_rows (impl->db, prepared.rel_path))
    return rollback (sqlite3_errmsg (impl->db));

  std::size_t computed_cursor= 0;
  int space_dimension= 0;
  for (const RagPreparedChunk& item: prepared.chunks) {
    std::vector<float> embedding= item.cached_embedding;
    if (item.needs_embedding) {
      embedding= computed_embeddings[computed_cursor++];
    }
    if (!embedding.empty ()) {
      space_dimension= int (embedding.size ());
      if (!store_embedding (
            impl->db, prepared.embedding_space, item.embedding_input_hash,
            embedding))
        return rollback (sqlite3_errmsg (impl->db));
    }
    ChunkBuild build;
    build.chunk= item.chunk;
    build.embedding_input_hash= item.embedding_input_hash;
    build.edges= item.edges;
    if (!insert_chunk (
          impl->db, build,
          embedding.empty () ? std::string () : prepared.embedding_space))
      return rollback (sqlite3_errmsg (impl->db));
  }

  if (!prepared.embedding_space.empty ()) {
    Statement space (impl->db,
      "INSERT INTO embedding_spaces(space_id,dimension) VALUES (?,?) "
      "ON CONFLICT(space_id) DO UPDATE SET dimension="
      "CASE WHEN excluded.dimension>0 THEN excluded.dimension ELSE dimension END");
    if (space.get () == nullptr)
      return rollback (sqlite3_errmsg (impl->db));
    bind_text (space.get (), 1, prepared.embedding_space);
    sqlite3_bind_int (space.get (), 2, space_dimension);
    if (sqlite3_step (space.get ()) != SQLITE_DONE)
      return rollback (sqlite3_errmsg (impl->db));
  }

  if (!upsert_document (
        impl->db, prepared.rel_path, prepared.absolute_path, prepared.size,
        prepared.mtime_ns, prepared.storage_revision,
        prepared.semantic_revision, "ok", ""))
    return rollback (sqlite3_errmsg (impl->db));

  if (generation_is_current && !generation_is_current ())
    return rollback_superseded ();
  if (!exec_sql (impl->db, "COMMIT", error))
    return rollback (error);
  return true;
}

bool
RagIndex::delete_document (const std::string& rel_path) {
  if (impl->db == nullptr || !valid_vault_relative_path (rel_path)) return false;
  std::string error;
  if (!exec_sql (impl->db, "BEGIN IMMEDIATE", error)) return false;
  if (!delete_document_rows (impl->db, rel_path)) {
    std::string ignored;
    (void) exec_sql (impl->db, "ROLLBACK", ignored);
    return false;
  }
  Statement del (impl->db, "DELETE FROM documents WHERE rel_path=?");
  bind_text (del.get (), 1, rel_path);
  if (del.get () == nullptr || sqlite3_step (del.get ()) != SQLITE_DONE) {
    std::string ignored;
    (void) exec_sql (impl->db, "ROLLBACK", ignored);
    return false;
  }
  return exec_sql (impl->db, "COMMIT", error);
}

bool
RagIndex::scan_once () {
  if (impl->db == nullptr) return false;
  std::vector<fs::path> files= rag_document_files (impl->config.vault_root);
  std::vector<fs::path> work_files;
  work_files.reserve (files.size ());
  for (const fs::path& file: files) {
    std::string rel= relative_path (impl->config.vault_root, file);
    if (shard_accepts (rel, impl->config.shard_index,
                       impl->config.shard_count))
      work_files.push_back (file);
  }
  std::set<std::string> live;
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

  const std::string embedding_space=
    impl->embedder ().available () ? impl->embedder ().space_id () :
                                     std::string ();

  for (size_t i=0; i<work_files.size (); i++) {
    const fs::path& file= work_files[i];
    std::string rel= relative_path (impl->config.vault_root, file);
    if (impl->config.progress) {
      rag_progress.clear_chunks ();
      rag_progress.update_file (i + 1, work_files.size (),
                                "Indexing RAG files", rel);
    }
    live.insert (rel);
    RagPreparedDocument prepared;
    if (!prepare_document (rel, "", embedding_space, prepared)) {
      log_file ("rag index: failed to prepare " + rel + ": " +
                impl->status.last_error, true);
      progress_file_done ();
      continue;
    }
    if (prepared.unchanged) {
      if (impl->config.progress || impl->config.progress_fd >= 0)
        log_file ("rag index: up-to-date " + rel);
      progress_file_done ();
      continue;
    }

    std::vector<std::string> texts;
    texts.reserve (prepared.missing_embedding_indices.size ());
    for (std::size_t index: prepared.missing_embedding_indices)
      texts.push_back (prepared.chunks[index].chunk.text);
    std::vector<std::vector<float>> computed;
    if (!texts.empty () && impl->embedder ().available ()) {
      computed= impl->embedder ().embed_many (
        texts,
        [&progress_chunks, &rel] (size_t done, size_t total) {
          progress_chunks (rel, done, total);
        });
    }
    else computed.resize (texts.size ());

    if (!commit_document (prepared, computed)) {
      log_file ("rag index: document changed during indexing or commit failed " +
                rel, true);
      progress_file_done ();
      continue;
    }
    log_file ("rag index: indexed " + rel + " chunks=" +
              std::to_string (prepared.chunks.size ()) + ", embedded-new=" +
              std::to_string (computed.size ()) + ", reused=" +
              std::to_string (
                prepared.chunks.size () >= computed.size () ?
                  prepared.chunks.size () - computed.size () : 0));
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
  for (const std::string& rel: stale) (void) delete_document (rel);
  return true;
}

bool
RagIndex::parallel_reindex (int jobs) {
  if (impl->db == nullptr) return false;
  if (jobs <= 1) return scan_once ();

#if defined(__unix__) || defined(__APPLE__)
  size_t total_files= rag_document_files (impl->config.vault_root).size ();
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
      athena_spdlog_warning (
        "rag index: failed to create worker progress pipe");
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
      athena_spdlog_warning ("rag index: failed to fork worker");
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
                      "DELETE FROM edges; DELETE FROM chunks_fts; "
                      "DELETE FROM embeddings; DELETE FROM embedding_spaces;", error);
  for (const std::string& path: temp_dbs) {
    if (!merge_worker_database (impl->db, path, error)) {
      exec_sql (impl->db, "ROLLBACK", error);
      athena_spdlog_warning (
        "rag index: failed to merge worker database: " + error);
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
  athena_spdlog_warning (
    "rag index: process parallelization is unavailable on this platform; "
    "using serial indexing");
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
  if (s.embedding_space.empty ()) {
    Statement spaces (
      impl->db, "SELECT count(DISTINCT embedding_space), "
                "min(embedding_space) FROM chunks WHERE embedding_space!=''");
    if (sqlite3_step (spaces.get ()) == SQLITE_ROW) {
      const int count= sqlite3_column_int (spaces.get (), 0);
      if (count == 1) {
        const unsigned char* text= sqlite3_column_text (spaces.get (), 1);
        if (text != nullptr)
          s.embedding_space= reinterpret_cast<const char*> (text);
      }
      else if (count > 1) s.embedding_space= "multiple";
    }
  }
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
  if (impl->embedder ().available ()) qemb= impl->embedder ().embed (query);

  std::vector<RagChunk> out;
  if (!q.empty ()) {
    Statement st (
      impl->db,
      "SELECT c.chunk_id, c.rel_path, c.kind, c.tree_path, c.anchor, "
      "c.title, c.heading_path, c.text, c.source, bm25(chunks_fts), "
      "e.embedding, e.embedding_dim, c.embedding_space "
      "FROM chunks_fts JOIN chunks c ON c.chunk_id=chunks_fts.chunk_id "
      "LEFT JOIN embeddings e ON e.space_id=c.embedding_space "
      "AND e.input_hash=c.embedding_input_hash "
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
          model == impl->embedder ().space_id ())
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
