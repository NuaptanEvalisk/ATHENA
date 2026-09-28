/******************************************************************************
* MODULE     : vault_format_upgrade.cpp
* DESCRIPTION: Validate, stage and atomically publish an offline XML vault upgrade
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#include "vault_format_upgrade.hpp"
#include "document_file_codec.hpp"
#include "document_upgrade_file.hpp"
#include "vault_directory_lease.hpp"
#include "ATHENA/Data/artifacts.hpp"
#include "ATHENA/Data/document_node_copy.hpp"
#include "ATHENA/Data/document_node_model.hpp"
#include "ATHENA/Data/enunciation_model.hpp"
#include "ATHENA/Data/heading_word_count.hpp"
#include "ATHENA/Data/vault_map_sqlite.hpp"
#include "ATHENA/Data/vaultfile_json.hpp"
#include "new_style.hpp"
#include "drd_std.hpp"
#include "node_metadata.hpp"
#include "unicode_text.hpp"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QUrl>
#include <QUuid>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <future>
#include <iomanip>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <sqlite3.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
#ifdef __linux__
#include <linux/fs.h>
#endif

namespace athena::document {
namespace fs= std::filesystem;
namespace {
struct descriptor {
  int value;
  explicit descriptor (int fd): value (fd) {
    if (fd < 0) throw std::system_error (errno, std::generic_category (), "Open upgrade file");
  }
  ~descriptor () { ::close (value); }
  descriptor (const descriptor&)= delete;
};
void require (bool ok, const std::string& message) {
  if (!ok) throw std::runtime_error (message);
}
void sync_fd (int fd) {
  if (::fsync (fd) != 0)
    throw std::system_error (errno, std::generic_category (), "Sync vault upgrade");
}
struct record {
  struct stat info {};
  std::string digest, link;
};
using inventory= std::map<fs::path,record>;
bool same_time (timespec a, timespec b) {
  return a.tv_sec == b.tv_sec && a.tv_nsec == b.tv_nsec;
}
bool same_revision (const struct stat& a, const struct stat& b) {
  return a.st_dev == b.st_dev && a.st_ino == b.st_ino &&
    a.st_mode == b.st_mode && a.st_uid == b.st_uid && a.st_gid == b.st_gid &&
    a.st_size == b.st_size && a.st_nlink == b.st_nlink &&
    same_time (a.st_mtim, b.st_mtim) && same_time (a.st_ctim, b.st_ctim);
}
record inspect (const fs::path& path, bool flush= false, bool hash_content= true,
                const std::atomic<bool>* stop= nullptr) {
  record r;
  if (::lstat (path.c_str (), &r.info) != 0)
    throw std::system_error (errno, std::generic_category (), path.string ());
  if (S_ISLNK (r.info.st_mode)) r.link= fs::read_symlink (path).native ();
  else if (S_ISREG (r.info.st_mode) && (flush || hash_content)) {
    descriptor fd (::open (path.c_str (), O_RDONLY | O_NOFOLLOW | O_CLOEXEC));
    struct stat opened {};
    require (::fstat (fd.value, &opened) == 0 && same_revision (r.info, opened),
             "File changed while reading: " + path.string ());
    QCryptographicHash hash (QCryptographicHash::Sha256);
    char buffer[128 * 1024];
    while (hash_content) {
      if (stop && stop->load ()) throw std::runtime_error ("Inventory cancelled");
      ssize_t count= ::read (fd.value, buffer, sizeof buffer);
      if (count < 0 && errno == EINTR) continue;
      require (count >= 0, "Cannot read " + path.string ());
      if (count == 0) break;
      hash.addData (QByteArrayView (buffer, count));
    }
    if (flush) sync_fd (fd.value);
    require (::fstat (fd.value, &opened) == 0 && same_revision (r.info, opened),
             "File changed while hashing: " + path.string ());
    r.digest= hash.result ().toHex ().toStdString ();
  }
  else require (S_ISDIR (r.info.st_mode) || S_ISREG (r.info.st_mode),
                "Special file in vault: " + path.string ());
  return r;
}
void report (const vault_upgrade_progress& progress, const char* phase,
             std::size_t done, std::size_t total, const fs::path& path= {}) {
  if (progress) progress (phase, done, total, path.generic_string ());
}
inventory scan (const fs::path& root, const vault_upgrade_progress& progress,
                const char* phase, bool flush= false, bool hash_content= true) {
  inventory result;
  result.emplace (fs::path ("."), inspect (root));
  const auto device= result.begin ()->second.info.st_dev;
  for (const auto& entry: fs::recursive_directory_iterator (root)) {
    require (result.size () < 1000000, "Vault exceeds one million filesystem entries");
    auto relative= entry.path ().lexically_relative (root);
    text::require_utf8 (relative.native ());
    auto r= inspect (entry.path (), false, false);
    require (r.info.st_dev == device, "Nested mount in vault: " + relative.string ());
    result.emplace (relative, std::move (r));
    report (progress, phase, result.size () - 1, 0, relative);
  }
  // Bound parallel I/O; each worker owns its hash and writes a distinct record.
  // Progress callbacks (including cancellation) remain on the caller thread.
  std::vector<std::pair<fs::path, record*>> files;
  for (auto& [path, r]: result)
    if (S_ISREG (r.info.st_mode)) files.emplace_back (path, &r);
  std::atomic<std::size_t> next {0}, done {0};
  std::atomic<bool> stop {false};
  std::vector<std::future<void>> workers;
  try {
    const auto count= std::min<std::size_t> (files.size (),
      std::min (4u, std::max (1u, std::thread::hardware_concurrency ())));
    for (std::size_t i= 0; i < count; ++i)
      workers.push_back (std::async (std::launch::async, [&] {
        while (!stop.load ()) {
          const auto index= next.fetch_add (1);
          if (index >= files.size ()) break;
          const auto& [path, initial]= files[index];
          auto checked= inspect (root / path, flush, hash_content, &stop);
          require (same_revision (initial->info, checked.info),
                   "File changed during inventory: " + path.string ());
          *initial= std::move (checked);
          done.fetch_add (1);
        }
      }));
    for (auto& worker: workers) {
      while (worker.wait_for (std::chrono::milliseconds (100)) != std::future_status::ready)
        report (progress, phase, done.load (), files.size ());
      worker.get ();
    }
    report (progress, phase, done.load (), files.size ());
  }
  catch (...) {
    stop.store (true);
    for (auto& worker: workers) if (worker.valid ()) worker.wait ();
    throw;
  }
  if (flush) {
    // Flush children before their parents, including directory entries copied
    // by coreutils. File data alone is insufficient before the exchange.
    for (auto it= result.rbegin (); it != result.rend (); ++it)
      if (S_ISDIR (it->second.info.st_mode)) {
        descriptor fd (::open ((root / it->first).c_str (), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
        sync_fd (fd.value);
      }
  }
  return result;
}
void compare (const inventory& expected, const inventory& actual, bool original) {
  require (expected.size () == actual.size (), "Vault entries changed during upgrade");
  for (const auto& [path, before]: expected) {
    auto it= actual.find (path);
    require (it != actual.end (), "Missing snapshot entry: " + path.string ());
    const auto& after= it->second;
    require (before.digest == after.digest && before.link == after.link &&
      before.info.st_mode == after.info.st_mode && before.info.st_uid == after.info.st_uid &&
      before.info.st_gid == after.info.st_gid &&
      (S_ISDIR (before.info.st_mode) || same_time (before.info.st_mtim, after.info.st_mtim)) &&
      (!original || same_revision (before.info, after.info)),
      "Snapshot mismatch or external modification: " + path.string ());
  }
}
bool is_document_file (const fs::path& path) {
  const auto first= *path.begin ();
  // Archives remain exact originals; they are not live vault documents.
  return first != ".backup" && first != ".git" && first != ".hg" &&
    first != ".svn" && path.extension () == ".ath";
}
std::string bytes (const fs::path& root, const fs::path& path) {
  filesystem::confined_root storage (root);
  return storage.open (path).read (codec_limits ().output_bytes);
}
struct document_record {
  fs::path path;
  std::string semantic;
  bool legacy;
};
legacy_import_limits limits () {
  legacy_import_limits out;
  out.codec.input_bytes= out.codec.output_bytes;
  return out;
}
void clone (const fs::path& source, const fs::path& target,
             const vault_upgrade_progress& progress) {
  const auto cp= QStandardPaths::findExecutable ("cp");
  require (!cp.isEmpty (), "GNU coreutils cp is required for metadata-preserving vault snapshots");
  QProcess process;
  process.setProcessChannelMode (QProcess::MergedChannels);
  process.start (cp, {"--archive", "--reflink=auto", "--", QString::fromStdString ((source / ".").string ()),
                     QString::fromStdString (target.string ())});
  require (process.waitForStarted (), "Cannot start vault snapshot copy");
  QByteArray diagnostics;
  try {
    while (!process.waitForFinished (200)) {
      diagnostics += process.readAll ();
      if (diagnostics.size () > 8192) diagnostics= diagnostics.right (8192);
      report (progress, "Copy snapshot", 0, 0);
    }
  }
  catch (...) { process.kill (); process.waitForFinished (-1); throw; }
  diagnostics += process.readAll ();
  require (process.exitStatus () == QProcess::NormalExit && process.exitCode () == 0,
           "Snapshot copy failed: " + diagnostics.right (8192).toStdString ());
}

using node_path= athena::document_node::source_path;

struct node_model_document {
  fs::path path;
  tree document;
  std::size_t assigned= 0;
  std::size_t converted_enunciations= 0;
};

struct map_resolution {
  AthenaVaultMapNode source;
  node_model_document* document= nullptr;
  std::optional<node_path> direct;
  std::vector<node_path> range_roots;
  std::vector<node_path> generated_labels;
  std::vector<std::string> targets;
  std::string diagnostic;
};

struct artifact_binding_update {
  std::string artifact_uuid;
  std::string source_uuid;
  std::string role;
  std::vector<std::string> source_nodes;
};

struct stale_artifact_record {
  std::string artifact_uuid;
  std::string relative_path;
  std::string reason;
};

std::string native_text (string value) {
  return std::string (as_charp (value), (std::size_t) N(value));
}

std::string source_path_text (const node_path& value) {
  std::string out;
  for (int index: value) out += "/" + std::to_string (index);
  return out.empty () ? "/" : out;
}

tree& tree_at (tree& root, const node_path& where) {
  tree* current= &root;
  for (int index: where) {
    require (is_compound (*current) && index >= 0 && index < N(*current),
             "Node-model migration path no longer exists: " +
             source_path_text (where));
    current= &(*current)[index];
  }
  return *current;
}

const tree& tree_at (const tree& root, const node_path& where) {
  const tree* current= &root;
  for (int index: where) {
    require (is_compound (*current) && index >= 0 && index < N(*current),
             "Node-model migration path no longer exists: " +
             source_path_text (where));
    current= &(*current)[index];
  }
  return *current;
}

tree& document_body_ref (tree& document) {
  for (int i=0; i<N(document); ++i)
    if (is_compound (document[i], "body", 1) &&
        is_func (document[i][0], DOCUMENT))
      return document[i][0];
  throw std::runtime_error ("Native XML document has no DOCUMENT body");
}

const tree& document_body_ref (const tree& document) {
  for (int i=0; i<N(document); ++i)
    if (is_compound (document[i], "body", 1) &&
        is_func (document[i][0], DOCUMENT))
      return document[i][0];
  throw std::runtime_error ("Native XML document has no DOCUMENT body");
}

bool whitespace_only (const tree& value) {
  return is_atomic (value) &&
    QString::fromUtf8 (as_charp (value->label), N(value->label)).trimmed ().isEmpty ();
}

bool label_node (const tree& value, std::string* text= nullptr) {
  if (!is_func (value, LABEL, 1) || !is_atomic (value[0])) return false;
  if (text) *text= native_text (value[0]->label);
  return true;
}

void find_labels (const tree& value, const std::string& label, node_path where,
                  std::vector<node_path>& out) {
  std::string text;
  if (label_node (value, &text) && text == label) out.push_back (where);
  if (!is_compound (value)) return;
  for (int i=0; i<N(value); ++i) {
    node_path child= where;
    child.push_back (i);
    find_labels (value[i], label, std::move (child), out);
  }
}

node_path unique_label (const tree& body, const std::string& label) {
  std::vector<node_path> matches;
  find_labels (body, label, {}, matches);
  require (matches.size () == 1,
           matches.empty () ?
             "Legacy anchor is missing: " + label :
             "Legacy anchor is ambiguous: " + label);
  return matches.front ();
}

node_path parent_path (node_path path) {
  require (!path.empty (), "Legacy anchor cannot be the document body");
  path.pop_back ();
  return path;
}

std::optional<node_path> next_substantive_sibling (
    const tree& body, const node_path& where) {
  node_path parent= parent_path (where);
  const tree& container= tree_at (body, parent);
  const int start= where.back () + 1;
  for (int i=start; i<N(container); ++i) {
    if (whitespace_only (container[i])) continue;
    node_path result= parent;
    result.push_back (i);
    return result;
  }
  return std::nullopt;
}

bool ends_with (const std::string& value, const char* suffix) {
  const std::size_t n= std::strlen (suffix);
  return value.size () >= n &&
    value.compare (value.size () - n, n, suffix) == 0;
}

std::string wrapper_base (const std::string& value, const char* suffix) {
  return ends_with (value, suffix) ?
    value.substr (0, value.size () - std::strlen (suffix)) : std::string ();
}

std::string generated_heading_label (const tree& heading) {
  const int level= athena_heading_level (heading);
  if (level <= 0) return {};
  const std::string title= native_text (athena_heading_title (heading));
  return title.empty () ? std::string () :
    "H" + std::to_string (level) + " " + title;
}

bool generated_enunciation_stem (
    const std::unordered_set<std::string>& stems, const std::string& stem) {
  return !stem.empty () && stems.find (stem) != stems.end ();
}

std::optional<node_path> following_generated_lower (
    const tree& body, const node_path& upper, const node_path& source,
    const std::string& stem) {
  if (parent_path (upper) != parent_path (source)) return std::nullopt;
  node_path parent= parent_path (source);
  const tree& container= tree_at (body, parent);
  for (int i=source.back () + 1; i<N(container); ++i) {
    if (whitespace_only (container[i])) continue;
    std::string label;
    if (!label_node (container[i], &label) || label != stem + " }")
      return std::nullopt;
    node_path lower= parent;
    lower.push_back (i);
    return lower;
  }
  return std::nullopt;
}

map_resolution resolve_map_node (
    const AthenaVaultMapNode& node,
    std::unordered_map<std::string,node_model_document*>& documents,
    const std::unordered_map<std::string,std::unordered_set<std::string>>&
      enunciation_stems) {
  map_resolution result;
  result.source= node;
  const fs::path relative= fs::path (node.path).lexically_normal ();
  if (relative.empty () || relative.is_absolute ()) {
    result.diagnostic= "Map target path is not vault-relative";
    return result;
  }
  for (const auto& part: relative)
    if (part == "..") {
      result.diagnostic= "Map target path escapes the vault";
      return result;
    }
  auto document= documents.find (relative.generic_string ());
  if (document == documents.end ()) {
    result.diagnostic= "Map target document is missing: " + relative.generic_string ();
    return result;
  }
  result.document= document->second;
  const tree& body= document_body_ref (result.document->document);
  try {
    if (node.anchor_begin.empty () && node.anchor_end.empty ()) {
      result.direct= node_path {};
      return result;
    }

    const std::string probe=
      !node.anchor_begin.empty () ? node.anchor_begin : node.anchor_end;
    node_path first= unique_label (body, probe);

    // Generated heading anchors and generated enunciation upper anchors both
    // identify the following source object exactly. User labels remain
    // first-class explicit targets rather than guessing a containing paragraph.
    if (node.anchor_begin.empty () || node.anchor_begin == node.anchor_end) {
      auto next= next_substantive_sibling (body, first);
      if (next) {
        const tree& target= tree_at (body, *next);
        const bool heading= athena_heading_level (target) > 0;
        const bool wrapper= ends_with (probe, " {");
        if (heading || wrapper) {
          result.direct= *next;
          if (heading && probe == generated_heading_label (target))
            result.generated_labels.push_back (first);
          if (wrapper && athena::enunciation::is_canonical (target)) {
            const std::string stem= wrapper_base (probe, " {");
            auto stems= enunciation_stems.find (relative.generic_string ());
            if (stems != enunciation_stems.end () &&
                generated_enunciation_stem (stems->second, stem)) {
              auto lower= following_generated_lower (body, first, *next, stem);
              if (lower) {
                result.generated_labels.push_back (first);
                result.generated_labels.push_back (*lower);
              }
            }
          }
          return result;
        }
      }
      result.direct= first;
      return result;
    }

    node_path last= unique_label (body, node.anchor_end);
    const node_path first_parent= parent_path (first);
    const node_path last_parent= parent_path (last);
    require (first_parent == last_parent,
             "Legacy range anchors do not share a structural parent");
    require (first.back () < last.back (),
             "Legacy range anchors are reversed");
    const tree& container= tree_at (body, first_parent);

    const std::string upper= wrapper_base (node.anchor_begin, " {");
    const std::string lower= wrapper_base (node.anchor_end, " }");
    if (!upper.empty () && upper == lower) {
      int substantive= -1, count= 0;
      for (int i=first.back () + 1; i<last.back (); ++i)
        if (!whitespace_only (container[i])) { substantive= i; ++count; }
      require (count == 1,
               "Generated enunciation anchors do not enclose exactly one source object");
      node_path target= first_parent;
      target.push_back (substantive);
      result.direct= std::move (target);
      const tree& source= tree_at (body, *result.direct);
      auto stems= enunciation_stems.find (relative.generic_string ());
      if (athena::enunciation::is_canonical (source) &&
          stems != enunciation_stems.end () &&
          generated_enunciation_stem (stems->second, upper)) {
        result.generated_labels.push_back (first);
        result.generated_labels.push_back (last);
      }
      return result;
    }

    for (int i=first.back () + 1; i<last.back (); ++i) {
      if (whitespace_only (container[i])) continue;
      node_path target= first_parent;
      target.push_back (i);
      result.range_roots.push_back (std::move (target));
    }
    require (!result.range_roots.empty (),
             "Legacy transclusion range contains no source objects");
  }
  catch (const std::exception& error) { result.diagnostic= error.what (); }
  return result;
}

std::string role_name (athena::document_node::identity_role role) {
  using role_t= athena::document_node::identity_role;
  switch (role) {
  case role_t::body: return "body";
  case role_t::paragraph: return "paragraph";
  case role_t::heading: return "heading";
  case role_t::enunciation: return "enunciation";
  }
  return "unknown";
}

std::string deterministic_uuid (
    const std::string& relative, const std::string& purpose,
    const node_path& where, const std::string& category= {}) {
  QByteArray seed ("athena-node-model-migration-v1\0", 31);
  seed.append (relative.data (), (qsizetype) relative.size ());
  seed.append ('\0');
  seed.append (purpose.data (), (qsizetype) purpose.size ());
  seed.append ('\0');
  seed.append (category.data (), (qsizetype) category.size ());
  seed.append ('\0');
  for (int index: where) {
    const std::string item= std::to_string (index);
    seed.append (item.data (), (qsizetype) item.size ());
    seed.append ('/');
  }
  QByteArray digest= QCryptographicHash::hash (seed, QCryptographicHash::Sha256);
  unsigned char bytes[16];
  std::memcpy (bytes, digest.constData (), sizeof bytes);
  bytes[6]= (bytes[6] & 0x0f) | 0x50;
  bytes[8]= (bytes[8] & 0x3f) | 0x80;
  static constexpr char hex[]= "0123456789abcdef";
  std::string out;
  out.reserve (36);
  for (int i=0; i<16; ++i) {
    if (i == 4 || i == 6 || i == 8 || i == 10) out.push_back ('-');
    out.push_back (hex[bytes[i] >> 4]);
    out.push_back (hex[bytes[i] & 15]);
  }
  return out;
}

athena::enunciation::conversion_options migration_enunciation_options (
    const fs::path& root, const AthenaVaultfileInfo& info) {
  athena::enunciation::conversion_options options;
  bool number_solutions= true;
  fs::path relative= info.preferences_path.empty () ?
    fs::path ("vprefs.json") : fs::path (info.preferences_path);
  if (!relative.empty () && !relative.is_absolute ()) {
    bool safe= true;
    for (const auto& part: relative) if (part == "..") safe= false;
    require (safe, "Vault preferences path escapes the vault");
    const fs::path file= root / relative;
    if (fs::exists (file)) {
      require (relative.extension () == ".json",
               "Node-model migration requires JSON vault preferences");
      const std::string text= bytes (root, relative);
      QJsonParseError parse;
      QJsonDocument json= QJsonDocument::fromJson (
        QByteArray (text.data (), (qsizetype) text.size ()), &parse);
      require (parse.error == QJsonParseError::NoError && json.isObject (),
               "Invalid vault preferences JSON");
      const QJsonObject object= json.object ();
      require (object.value ("format").toString () == "athena-preferences" &&
               object.value ("version").toInt () == 1 &&
               object.value ("preferences").isObject (),
               "Unsupported vault preferences JSON");
      const QJsonValue value=
        object.value ("preferences").toObject ().value ("number solutions");
      if (value.isString ()) {
        const QString setting= value.toString ();
        require (setting == "on" || setting == "off",
                 "Invalid 'number solutions' vault preference");
        number_solutions= setting == "on";
      }
    }
  }
  else require (relative.empty (), "Vault preferences path must be vault-relative");
  options.numbering_preferences["number solutions"]= number_solutions;
  return options;
}

void assign_id (tree& node, const std::string& id) {
  require (athena::node::valid_id (id), "Migration allocator produced invalid UUID");
  athena::node::metadata metadata;
  if (const auto* current= athena::node::get (node)) metadata= *current;
  if (!metadata.id.empty ()) {
    require (metadata.id == id, "Conflicting source identity during migration");
    return;
  }
  metadata.id= id;
  athena::node::set (node, metadata);
}

void collect_top_level_ids (const tree& value, std::vector<std::string>& out) {
  const std::string id= athena::node::id (value);
  if (!id.empty ()) {
    out.push_back (id);
    return;
  }
  if (!is_compound (value)) return;
  for (int i=0; i<N(value); ++i) collect_top_level_ids (value[i], out);
}

void dedupe_ids (std::vector<std::string>& ids) {
  std::unordered_set<std::string> seen;
  std::vector<std::string> out;
  for (const std::string& id: ids)
    if (seen.insert (id).second) out.push_back (id);
  ids= std::move (out);
}

void collect_ids (const tree& value, const std::string& owner,
                  std::unordered_map<std::string,std::string>& global) {
  const std::string id= athena::node::id (value);
  if (!id.empty ()) {
    auto inserted= global.emplace (id, owner);
    require (inserted.second,
             "Duplicate migrated UUID " + id + " in " + owner +
             " and " + inserted.first->second);
  }
  if (!is_compound (value)) return;
  for (int i=0; i<N(value); ++i) collect_ids (value[i], owner, global);
}

tree remove_migration_paths (
    const tree& source, const std::set<node_path>& removed,
    node_path where= {}) {
  if (is_atomic (source)) return copy (source);
  tree result (L(source));
  athena::node::copy_metadata (source, result);
  for (int i=0; i<N(source); ++i) {
    node_path child= where;
    child.push_back (i);
    if (removed.find (child) != removed.end ()) continue;
    result << remove_migration_paths (source[i], removed, std::move (child));
  }
  return result;
}

enum class tmfs_rewrite_result { not_applicable, resolved, unresolved };

tmfs_rewrite_result rewrite_tmfs_uuid (
    tree& destination,
    const std::unordered_map<std::string,std::vector<std::string>>& aliases,
    const char* prefix, std::size_t& changed) {
  if (!is_atomic (destination)) return tmfs_rewrite_result::not_applicable;
  std::string value= native_text (destination->label);
  const std::string head (prefix);
  if (value.rfind (head, 0) != 0) return tmfs_rewrite_result::not_applicable;
  std::size_t end= value.find_first_of ("/?#", head.size ());
  const std::string old= value.substr (
    head.size (), end == std::string::npos ? std::string::npos : end-head.size ());
  // Historical path-only wikilinks use a deliberately empty UUID component,
  // e.g. tmfs://wikilink//Linear%20Algebra.Scope/.  There is no legacy source
  // identity to rewrite in that form; keep the compatibility locator intact.
  // Nonempty UUIDs that cannot be resolved are reported to the caller so the
  // enclosing link can be degraded without aborting the whole vault migration.
  if (old.empty ()) return tmfs_rewrite_result::resolved;
  auto found= aliases.find (old);
  if (found == aliases.end () || found->second.size () != 1)
    return tmfs_rewrite_result::unresolved;
  const std::string& next= found->second.front ();
  if (next != old) {
    value.replace (head.size (), old.size (), next);
    destination->label= string (value.data (), (int) value.size ());
    ++changed;
  }
  return tmfs_rewrite_result::resolved;
}

void replace_with_display_text (tree& value) {
  tree replacement= copy (value[0]);
  athena::node::copy_metadata (value, replacement);
  value= std::move (replacement);
}

std::string legacy_transclusion_fallback (const tree& value) {
  std::string locator= "tmfs://transclude/";
  if (N(value) > 0 && is_atomic (value[0]))
    locator += native_text (value[0]->label);
  std::string text=
    "a transclusion was once here but is already lost (original link: " + locator;
  if (N(value) > 1 && is_atomic (value[1]) && N(value[1]->label) != 0)
    text += "; file hint: " + native_text (value[1]->label);
  if (N(value) > 2 && is_atomic (value[2]) && N(value[2]->label) != 0)
    text += "; begin anchor: " + native_text (value[2]->label);
  if (N(value) > 3 && is_atomic (value[3]) && N(value[3]->label) != 0)
    text += "; end anchor: " + native_text (value[3]->label);
  return text + ")";
}

void replace_with_lost_transclusion (tree& value, const std::string& text) {
  tree replacement (string (text.data (), (int) text.size ()));
  athena::node::copy_metadata (value, replacement);
  value= std::move (replacement);
}

void rewrite_references (
    tree& value,
    const std::unordered_map<std::string,std::vector<std::string>>& aliases,
    const std::string& context, std::size_t& changed) {
  if (is_atomic (value)) return;
  if ((is_func (value, HLINK, 2) || is_compound (value, "hlink", 2))) {
    const std::string original_destination=
      is_atomic (value[1]) ? native_text (value[1]->label) : "<non-atomic target>";
    auto result= rewrite_tmfs_uuid (
      value[1], aliases, "tmfs://wikilink/", changed);
    if (result == tmfs_rewrite_result::not_applicable)
      result= rewrite_tmfs_uuid (
        value[1], aliases, "tmfs://Wikilink/", changed);
    if (result == tmfs_rewrite_result::not_applicable)
      result= rewrite_tmfs_uuid (
        value[1], aliases, "tmfs://transclude/", changed);
    if (result == tmfs_rewrite_result::unresolved) {
      const std::string display=
        is_atomic (value[0]) ? native_text (value[0]->label) : "<structured display>";
      std::cerr
        << "WARNING: node-model migration degraded an unresolved legacy link in "
        << context << ": target=" << std::quoted (original_destination)
        << ", display=" << std::quoted (display)
        << "; the link wrapper was removed and only its display content was kept.\n";
      replace_with_display_text (value);
      ++changed;
      rewrite_references (value, aliases, context, changed);
      return;
    }
  }
  if ((is_func (value, TRANSCLUDE, 4) ||
       is_compound (value, "transclude", 4)) && is_atomic (value[0])) {
    const std::string old= native_text (value[0]->label);
    auto found= aliases.find (old);
    if (old.empty () || found == aliases.end () || found->second.empty ()) {
      const std::string fallback= legacy_transclusion_fallback (value);
      std::cerr
        << "WARNING: node-model migration degraded an unresolved legacy "
        << "transclusion in " << context << ": " << fallback
        << "; the transclusion was replaced by this plain-text recovery marker.\n";
      replace_with_lost_transclusion (value, fallback);
      ++changed;
      return;
    }
    tree ids (TUPLE);
    for (const std::string& id: found->second)
      ids << string (id.data (), (int) id.size ());
    tree replacement (TRANSCLUDE, ids);
    athena::node::copy_metadata (value, replacement);
    value= std::move (replacement);
    ++changed;
    return;
  }
  if (is_func (value, TRANSCLUDE, 1) && is_tuple (value[0])) {
    std::vector<std::string> ids;
    bool mapped= false;
    std::string fallback_links;
    for (int i=0; i<N(value[0]); ++i) {
      require (is_atomic (value[0][i]),
               "Malformed canonical transclusion in " + context);
      if (!fallback_links.empty ()) fallback_links += ", ";
      fallback_links += "tmfs://transclude/" + native_text (value[0][i]->label);
    }
    for (int i=0; i<N(value[0]); ++i) {
      require (is_atomic (value[0][i]),
               "Malformed canonical transclusion in " + context);
      const std::string old= native_text (value[0][i]->label);
      auto found= aliases.find (old);
      if (found == aliases.end ()) {
        const std::string fallback=
          "a transclusion was once here but is already lost (original link: " +
          fallback_links + ")";
        std::cerr
          << "WARNING: node-model migration degraded an unresolved canonical "
          << "transclusion in " << context << ": unresolved UUID="
          << std::quoted (old) << ", original links="
          << std::quoted (fallback_links)
          << "; the transclusion was replaced by a plain-text recovery marker.\n";
        replace_with_lost_transclusion (value, fallback);
        ++changed;
        return;
      }
      else {
        ids.insert (ids.end (), found->second.begin (), found->second.end ());
        mapped|= found->second.size () != 1 || found->second.front () != old;
      }
    }
    dedupe_ids (ids);
    if (mapped) {
      tree tuple_ids (TUPLE);
      for (const std::string& id: ids)
        tuple_ids << string (id.data (), (int) id.size ());
      value[0]= std::move (tuple_ids);
      ++changed;
    }
  }
  for (int i=0; i<N(value); ++i)
    rewrite_references (value[i], aliases, context, changed);
}

node_path native_to_source_path (path value) {
  node_path reversed;
  for (; !is_nil (value); value= value->next) reversed.push_back (value->item);
  // Native path iteration is root-to-leaf; preserve that order.
  return reversed;
}

void update_artifact_binding_database (
    const fs::path& root, const AthenaVaultfileInfo& info,
    const std::vector<artifact_binding_update>& bindings,
    const std::vector<stale_artifact_record>& stale) {
  if (bindings.empty () && stale.empty ()) return;
  const fs::path relative (info.artifacts_path);
  require (!relative.empty () && !relative.is_absolute (),
           "Artifact database path must be vault-relative");
  for (const auto& part: relative)
    require (part != "..", "Artifact database path escapes the vault");
  if (!fs::exists (root / relative)) return;
  filesystem::confined_root confined (root);
  (void) confined.open (relative);
  sqlite3* db= nullptr;
  require (sqlite3_open_v2 (
    (root / relative).c_str (), &db,
    SQLITE_OPEN_READWRITE | SQLITE_OPEN_NOFOLLOW, nullptr) == SQLITE_OK,
    "Could not open staged Artifact database");
  const auto close= [&] { if (db) sqlite3_close (db); db= nullptr; };
  try {
    auto exec= [&] (const char* sql) {
      char* message= nullptr;
      int code= sqlite3_exec (db, sql, nullptr, nullptr, &message);
      std::string detail= message ? message : "";
      sqlite3_free (message);
      require (code == SQLITE_OK,
               detail.empty () ? "Artifact database update failed" : detail);
    };
    auto has_column= [&] (const char* table, const char* name) {
      sqlite3_stmt* st= nullptr;
      require (sqlite3_prepare_v2 (
        db, (std::string ("SELECT 1 FROM pragma_table_info('") + table +
             "') WHERE name=?1;").c_str (), -1, &st, nullptr) == SQLITE_OK,
        sqlite3_errmsg (db));
      sqlite3_bind_text (st, 1, name, -1, SQLITE_STATIC);
      const bool found= sqlite3_step (st) == SQLITE_ROW;
      sqlite3_finalize (st);
      return found;
    };
    exec ("BEGIN IMMEDIATE;");
    if (!has_column ("artifacts", "source_uuid"))
      exec ("ALTER TABLE artifacts ADD COLUMN source_uuid TEXT NOT NULL DEFAULT '';");
    if (!has_column ("artifacts", "source_role"))
      exec ("ALTER TABLE artifacts ADD COLUMN source_role TEXT NOT NULL DEFAULT '';");
    if (!has_column ("artifacts", "source_nodes"))
      exec ("ALTER TABLE artifacts ADD COLUMN source_nodes TEXT NOT NULL DEFAULT '';");
    sqlite3_stmt* update= nullptr;
    require (sqlite3_prepare_v2 (
      db, "UPDATE artifacts SET source_uuid=?1,source_role=?2,source_nodes=?4 WHERE uuid=?3;",
      -1, &update, nullptr) == SQLITE_OK, sqlite3_errmsg (db));
    for (const auto& binding: bindings) {
      sqlite3_reset (update);
      sqlite3_clear_bindings (update);
      sqlite3_bind_text (update, 1, binding.source_uuid.c_str (), -1, SQLITE_TRANSIENT);
      sqlite3_bind_text (update, 2, binding.role.c_str (), -1, SQLITE_TRANSIENT);
      sqlite3_bind_text (update, 3, binding.artifact_uuid.c_str (), -1, SQLITE_TRANSIENT);
      QJsonArray nodes;
      for (const auto& id: binding.source_nodes) nodes.append (QString::fromStdString (id));
      const auto encoded= QJsonDocument (nodes).toJson (QJsonDocument::Compact);
      sqlite3_bind_text (update, 4, encoded.constData (), encoded.size (), SQLITE_TRANSIENT);
      require (sqlite3_step (update) == SQLITE_DONE, sqlite3_errmsg (db));
      require (sqlite3_changes (db) == 1,
               "Artifact disappeared during staged migration: " +
               binding.artifact_uuid);
    }
    sqlite3_finalize (update);
    if (!stale.empty ()) {
      sqlite3_stmt* delete_names= nullptr;
      sqlite3_stmt* delete_artifact= nullptr;
      sqlite3_stmt* mark_document= nullptr;
      require (sqlite3_prepare_v2 (
        db, "DELETE FROM artifact_names WHERE artifact_uuid=?1;",
        -1, &delete_names, nullptr) == SQLITE_OK, sqlite3_errmsg (db));
      require (sqlite3_prepare_v2 (
        db, "DELETE FROM artifacts WHERE uuid=?1;",
        -1, &delete_artifact, nullptr) == SQLITE_OK, sqlite3_errmsg (db));
      const bool can_mark_document=
        has_column ("documents", "locator_contract");
      if (can_mark_document)
        require (sqlite3_prepare_v2 (
          db, "UPDATE documents SET locator_contract='' WHERE path=?1;",
          -1, &mark_document, nullptr) == SQLITE_OK, sqlite3_errmsg (db));
      for (const auto& item: stale) {
        sqlite3_reset (delete_names);
        sqlite3_clear_bindings (delete_names);
        sqlite3_bind_text (
          delete_names, 1, item.artifact_uuid.c_str (), -1, SQLITE_TRANSIENT);
        require (sqlite3_step (delete_names) == SQLITE_DONE, sqlite3_errmsg (db));

        sqlite3_reset (delete_artifact);
        sqlite3_clear_bindings (delete_artifact);
        sqlite3_bind_text (
          delete_artifact, 1, item.artifact_uuid.c_str (), -1, SQLITE_TRANSIENT);
        require (sqlite3_step (delete_artifact) == SQLITE_DONE, sqlite3_errmsg (db));
        require (sqlite3_changes (db) == 1,
                 "Stale Artifact disappeared during staged migration: " +
                 item.artifact_uuid);

        if (can_mark_document) {
          sqlite3_reset (mark_document);
          sqlite3_clear_bindings (mark_document);
          sqlite3_bind_text (
            mark_document, 1, item.relative_path.c_str (), -1, SQLITE_TRANSIENT);
          require (sqlite3_step (mark_document) == SQLITE_DONE, sqlite3_errmsg (db));
        }
      }
      sqlite3_finalize (delete_names);
      sqlite3_finalize (delete_artifact);
      if (mark_document) sqlite3_finalize (mark_document);
    }
    exec ("CREATE UNIQUE INDEX IF NOT EXISTS artifacts_source_binding_idx "
          "ON artifacts(source_uuid,source_role) "
          "WHERE source_uuid<>'' AND source_role<>'';");
    exec ("COMMIT;");
    sqlite3_stmt* check= nullptr;
    require (sqlite3_prepare_v2 (db, "PRAGMA quick_check;", -1, &check, nullptr) ==
             SQLITE_OK, sqlite3_errmsg (db));
    require (sqlite3_step (check) == SQLITE_ROW &&
             std::string ((const char*) sqlite3_column_text (check, 0)) == "ok",
             "Artifact database failed quick_check after migration");
    sqlite3_finalize (check);
    exec ("PRAGMA wal_checkpoint(TRUNCATE);");
    close ();
  }
  catch (...) {
    if (db) sqlite3_exec (db, "ROLLBACK;", nullptr, nullptr, nullptr);
    close ();
    throw;
  }
}

volatile std::sig_atomic_t interrupted= 0;
void cancel_signal (int) { interrupted= 1; }
} // namespace

vault_upgrade_result upgrade_vault_format (const fs::path& requested,
                                            const vault_upgrade_progress& progress) {
#ifndef __linux__
  throw std::runtime_error ("Atomic vault format upgrade requires Linux renameat2(RENAME_EXCHANGE)");
#else
  const auto absolute= fs::absolute (requested).lexically_normal ();
  require (!fs::is_symlink (fs::symlink_status (absolute)), "Vault root must not be a symlink");
  const auto root= fs::canonical (absolute);
  text::require_utf8 (root.native ());
  require (root != root.root_path (), "Cannot upgrade filesystem root");
  filesystem::vault_directory_lease lease (root, true);
  descriptor parent (::open (root.parent_path ().c_str (), O_RDONLY | O_DIRECTORY | O_CLOEXEC));
  struct stat parent_info {}, root_info {};
  require (::fstat (parent.value, &parent_info) == 0 &&
    ::fstat (lease.descriptor (), &root_info) == 0 && parent_info.st_dev == root_info.st_dev,
    "Vault must not be a mount point; atomic sibling exchange requires the same filesystem");
  AthenaVaultfileInfo info;
  std::string error;
  require (athena_vaultfile_read (root, info, error), error);
  const auto original= scan (root, progress, "Inventory");
  std::vector<document_record> documents;
  std::vector<vault_upgrade_revision> revisions;
  vault_upgrade_result result;
  std::size_t total= 0;
  for (const auto& [path, r]: original) if (is_document_file (path)) ++total;
  for (const auto& [path, r]: original) {
    if (!is_document_file (path)) continue;
    require (S_ISREG (r.info.st_mode) && r.info.st_nlink == 1,
             "Document must be a regular, unlinked file: " + path.string ());
    const auto source= bytes (root, path);
    try {
      auto decoded= decode_document_bytes (source, root / path, limits ());
      const auto semantic= semantic_document_fingerprint (decoded.document);
      if (decoded.legacy ()) {
        require ((r.info.st_mode & 0222) != 0, "Read-only document: " + path.string ());
        ++result.converted;
      }
      else ++result.already_xml;
      documents.push_back ({path, semantic, decoded.legacy ()});
      revisions.push_back ({path.generic_string (), semantic,
        static_cast<long long> (source.size ()),
        static_cast<long long> (fs::last_write_time (root / path).time_since_epoch ().count ())});
    }
    catch (const std::exception& e) {
      throw std::runtime_error ("Invalid document " + path.string () + ": " + e.what ());
    }
    report (progress, "Validate input", documents.size (), total, path);
  }
  if (result.converted == 0) return result;
  const auto workspace= root.parent_path () / ("." + root.filename ().string () +
    ".format-upgrade-" + QUuid::createUuid ().toString (QUuid::WithoutBraces).toStdString ());
  require (::mkdir (workspace.c_str (), 0700) == 0, "Cannot create private upgrade workspace");
  const auto staged= workspace / "vault";
  bool exchanged= false;
  try {
    fs::create_directory (staged);
    // Advertise the recovery location before copying or changing any bytes.
    report (progress, "Recovery directory", 0, 0, workspace);
    clone (root, staged, progress);
    filesystem::vault_directory_lease staged_lease (staged, true);
    compare (original, scan (staged, progress, "Verify snapshot"), false);
    prepare_vault_upgrade_indexes (staged, revisions, progress);
    QJsonArray manifest;
    std::size_t done= 0;
    filesystem::confined_root storage (staged);
    for (const auto& doc: documents) {
      report (progress, "Convert", done, documents.size (), doc.path);
      if (doc.legacy) {
        auto file= storage.open (doc.path);
        auto revision= file.stat ();
        auto source= file.read (limits ().codec.input_bytes);
        auto decoded= decode_document_bytes (source, staged / doc.path, limits ());
        require (semantic_document_fingerprint (decoded.document) == doc.semantic,
                 "Snapshot style context changed: " + doc.path.string ());
        const auto xml= write_xml (decoded.document);
        require (read_xml (xml, xml_kind::document, limits ().codec) == decoded.document,
                 "XML round-trip mismatch: " + doc.path.string ());
        auto replacement= storage.replace (doc.path, file, revision, xml);
        require (replacement.directory_synced, "Cannot sync converted document");
      }
      manifest.append (QJsonObject {{"path", QString::fromStdString (doc.path.generic_string ())},
        {"semantic_sha256", QString::fromStdString (doc.semantic)}, {"was_legacy", doc.legacy}});
      report (progress, "Convert", ++done, documents.size (), doc.path);
    }
    done= 0;
    for (const auto& doc: documents) {
      const auto xml= bytes (staged, doc.path);
      const auto decoded= read_xml (xml, xml_kind::document, limits ().codec);
      require (semantic_document_fingerprint (decoded) == doc.semantic,
               "Post-conversion semantic mismatch: " + doc.path.string ());
      report (progress, "Validate XML", ++done, documents.size (), doc.path);
    }
    scan (staged, progress, "Sync snapshot", true, false);
    QJsonObject receipt {{"version", 1}, {"source", QString::fromStdString (root.string ())},
      {"commit", "atomic-directory-exchange"}, {"documents", manifest}};
    filesystem::confined_root journal (workspace);
    const auto json= QJsonDocument (receipt).toJson (QJsonDocument::Indented);
    journal.preserve ("manifest.json", std::string_view (json.constData (), json.size ()));
    sync_fd (parent.value);
    // All original files, including databases and style resources, must still
    // match the snapshot. Offline exclusion remains required for old binaries
    // and unrelated writers that do not participate in our directory lease.
    report (progress, "Commit", 0, 1, root);
    compare (original, scan (root, progress, "Check external changes", true), true);
    descriptor workspace_fd (::open (workspace.c_str (), O_RDONLY | O_DIRECTORY | O_CLOEXEC));
    result.backup= staged;
    if (::syscall (SYS_renameat2, parent.value, root.filename ().c_str (),
                   AT_FDCWD, staged.c_str (), RENAME_EXCHANGE) != 0)
      throw std::system_error (errno, std::generic_category (), "Atomic vault directory exchange");
    exchanged= true;
    // No throwing callbacks, allocations or rollback after successful rename.
    const int first= ::fsync (workspace_fd.value);
    const int second= ::fsync (parent.value);
    result.durable= first == 0 && second == 0;
    return result;
  }
  catch (...) {
    if (!exchanged) {
      std::error_code ignored;
      fs::remove_all (workspace, ignored);
    }
    throw;
  }
#endif
}

vault_node_model_upgrade_result
upgrade_vault_node_model (
    const fs::path& requested, const vault_upgrade_progress& progress) {
#ifndef __linux__
  throw std::runtime_error (
    "Atomic vault node-model upgrade requires Linux renameat2(RENAME_EXCHANGE)");
#else
  const auto absolute= fs::absolute (requested).lexically_normal ();
  require (!fs::is_symlink (fs::symlink_status (absolute)),
           "Vault root must not be a symlink");
  const auto root= fs::canonical (absolute);
  text::require_utf8 (root.native ());
  require (root != root.root_path (), "Cannot upgrade filesystem root");
  filesystem::vault_directory_lease lease (root, true);
  descriptor parent (
    ::open (root.parent_path ().c_str (), O_RDONLY | O_DIRECTORY | O_CLOEXEC));
  struct stat parent_info {}, root_info {};
  require (::fstat (parent.value, &parent_info) == 0 &&
           ::fstat (lease.descriptor (), &root_info) == 0 &&
           parent_info.st_dev == root_info.st_dev,
           "Vault must not be a mount point; atomic sibling exchange requires "
           "the same filesystem");

  AthenaVaultfileInfo info;
  std::string error;
  require (athena_vaultfile_read (root, info, error), error);
  require (info.node_model_version == 0 || info.node_model_version == 1,
           "Unsupported vault node-model version " +
           std::to_string (info.node_model_version));

  const auto original= scan (root, progress, "Inventory");
  std::vector<fs::path> document_paths;
  for (const auto& [path, record]: original) {
    if (!is_document_file (path)) continue;
    require (S_ISREG (record.info.st_mode) && record.info.st_nlink == 1,
             "Document must be a regular, unlinked file: " + path.string ());
    const auto source= bytes (root, path);
    try {
      const auto decoded= decode_document_bytes (source, root / path, limits ());
      if (info.node_model_version == 0) {
        require (
          decoded.format == document_source_format::xml_v1,
          decoded.legacy () ?
            "Node-model migration requires UTF-8 XML input; run "
            "--upgrade-vault-format first: " + path.string () :
            "Unmarked vault contains XML v2 document: " + path.string ());
        require ((record.info.st_mode & 0222) != 0,
                 "Read-only document: " + path.string ());
      }
      else {
        require (decoded.format == document_source_format::xml_v2,
                 "node_model_version=1 vault contains a non-v2 document: " +
                 path.string ());
        athena::document_node::source_identity_state identities;
        const tree& body= document_body_ref (decoded.document);
        const auto diagnostics= identities.initialize_complete (
          body, get_offline_document_drd (
                  decoded.document, url ((root / path).string ().c_str ())),
          athena::document_node::standard_source_role);
        require (diagnostics.empty (),
                 "Invalid migrated identity baseline in " + path.string () +
                 (diagnostics.empty () ? "" : ": " +
                  diagnostics.front ().detail));
      }
    }
    catch (const std::exception& failure) {
      throw std::runtime_error (
        "Invalid document " + path.string () + ": " + failure.what ());
    }
    document_paths.push_back (path);
    report (progress, "Validate node-model input",
            document_paths.size (), 0, path);
  }

  vault_node_model_upgrade_result result;
  if (info.node_model_version == 1) {
    std::unordered_map<std::string,std::string> ids;
    for (const fs::path& path: document_paths) {
      auto decoded= decode_document_bytes (
        bytes (root, path), root / path, limits ());
      collect_ids (
        document_body_ref (decoded.document), path.generic_string (), ids);
    }
    result.already_v2= document_paths.size ();
    return result;
  }

  const auto workspace= root.parent_path () /
    ("." + root.filename ().string () + ".node-model-upgrade-" +
     QUuid::createUuid ().toString (QUuid::WithoutBraces).toStdString ());
  require (::mkdir (workspace.c_str (), 0700) == 0,
           "Cannot create private node-model upgrade workspace");
  const auto staged= workspace / "vault";
  bool exchanged= false;
  try {
    fs::create_directory (staged);
    report (progress, "Recovery directory", 0, 0, workspace);
    clone (root, staged, progress);
    filesystem::vault_directory_lease staged_lease (staged, true);
    compare (original, scan (staged, progress, "Verify snapshot"), false);

    AthenaVaultfileInfo staged_info;
    require (athena_vaultfile_read (staged, staged_info, error), error);
    require (staged_info.node_model_version == 0,
             "Snapshot node-model version changed before migration");
    const auto conversion_options=
      migration_enunciation_options (staged, staged_info);

    std::vector<AthenaArtifactRecord> artifacts;
    error.clear ();
    require (athena_artifacts_query (
      staged, artifacts, error, true), error.empty () ?
      "Could not read staged Artifact index" : error);
    std::unordered_map<std::string,std::vector<AthenaArtifactRecord>>
      artifacts_by_document;
    std::unordered_map<std::string,std::unordered_set<std::string>>
      enunciation_stems;
    for (const auto& artifact: artifacts)
    {
      const std::string relative=
        fs::path (artifact.relative_path).lexically_normal ().generic_string ();
      artifacts_by_document[relative].push_back (artifact);
      if (artifact.origin == "enunciation" && !artifact.anchor_stem.empty ())
        enunciation_stems[relative].insert (artifact.anchor_stem);
    }

    std::vector<AthenaVaultMapNode> map_nodes;
    const fs::path map_relative (staged_info.map_path);
    if (fs::exists (staged / map_relative)) {
      require (!map_relative.empty () && !map_relative.is_absolute (),
               "Vault map path must be vault-relative");
      for (const auto& part: map_relative)
        require (part != "..", "Vault map path escapes the vault");
      filesystem::confined_root confined (staged);
      (void) confined.open (map_relative);
      AthenaVaultMapSqlite map;
      require (map.open_read_only (staged / map_relative, error), error);
      require (map.read_all (map_nodes, error), error);
      map.close ();
    }

    std::vector<node_model_document> documents;
    documents.reserve (document_paths.size ());
    std::unordered_map<std::string,node_model_document*> by_path;
    std::size_t done= 0;
    for (const fs::path& path: document_paths) {
      report (progress, "Canonicalize source", done, document_paths.size (), path);
      const auto decoded= decode_document_bytes (
        bytes (staged, path), staged / path, limits ());
      require (decoded.format == document_source_format::xml_v1,
               "Snapshot is no longer XML v1: " + path.string ());
      tree document= copy (decoded.document);
      tree& body= document_body_ref (document);
      auto converted= athena::enunciation::convert_detached_source (
        body, conversion_options);
      require (converted.diagnostics.empty (),
               "Cannot canonicalize " + path.string () + ": " +
               (converted.diagnostics.empty () ? std::string () :
                converted.diagnostics.front ().detail));
      body= std::move (converted.source);
      documents.push_back (
        {path, std::move (document), 0, converted.converted});
      by_path[path.generic_string ()]= &documents.back ();
      report (progress, "Canonicalize source", ++done,
              document_paths.size (), path);
    }

    std::vector<map_resolution> resolutions;
    resolutions.reserve (map_nodes.size ());
    std::unordered_map<std::string,std::map<node_path,std::vector<std::string>>>
      direct_aliases;
    for (const auto& node: map_nodes) {
      map_resolution resolution= resolve_map_node (
        node, by_path, enunciation_stems);
      if (resolution.document && resolution.direct)
        direct_aliases[resolution.document->path.generic_string ()]
                      [*resolution.direct].push_back (node.uuid);
      resolutions.push_back (std::move (resolution));
    }

    std::unordered_map<std::string,std::set<node_path>> generated_labels;
    for (const map_resolution& resolution: resolutions) {
      if (!resolution.document || !resolution.diagnostic.empty ()) continue;
      auto& paths=
        generated_labels[resolution.document->path.generic_string ()];
      paths.insert (
        resolution.generated_labels.begin (), resolution.generated_labels.end ());
    }

    std::unordered_map<std::string,std::map<node_path,std::string>> preferred;
    for (auto& [relative, paths]: direct_aliases)
      for (auto& [where, aliases]: paths) {
        std::sort (aliases.begin (), aliases.end ());
        for (const std::string& alias: aliases)
          if (athena::node::valid_id (alias)) {
            preferred[relative][where]= alias;
            break;
          }
      }

    done= 0;
    for (node_model_document& document: documents) {
      report (progress, "Assign source identities", done,
              documents.size (), document.path);
      tree& body= document_body_ref (document.document);
      const std::string relative= document.path.generic_string ();
      auto identities= athena::document_node::assign_detached_source_ids (
        body, get_offline_document_drd (
                document.document,
                url ((staged / document.path).string ().c_str ())),
        athena::document_node::standard_source_role,
        [&] (const athena::document_node::identity_request& request) {
          auto preferred_path= preferred[relative].find (request.where);
          if (preferred_path != preferred[relative].end ())
            return preferred_path->second;
          return deterministic_uuid (
            relative, role_name (request.role), request.where,
            request.category);
        });
      require (identities.ok (),
               "Could not assign source identities in " + relative +
               (identities.diagnostics.empty () ? "" : ": " +
                 identities.diagnostics.front ().detail + " at " +
                 source_path_text (identities.diagnostics.front ().where)));
      body= std::move (*identities.body);
      document.assigned= identities.assigned.size ();

      auto direct= direct_aliases.find (relative);
      if (direct != direct_aliases.end ())
        for (const auto& [where, aliases]: direct->second) {
          tree& target= tree_at (body, where);
          if (!athena::node::id (target).empty ()) continue;
          auto choice= preferred[relative].find (where);
          assign_id (
            target, choice != preferred[relative].end () ?
              choice->second :
              deterministic_uuid (relative, "legacy-target", where));
          ++document.assigned;
        }
      report (progress, "Assign source identities", ++done,
              documents.size (), document.path);
    }

    std::vector<artifact_binding_update> artifact_updates;
    std::vector<stale_artifact_record> stale_artifacts;
    QJsonArray stale_artifact_manifest;
    auto prune_artifact= [&] (const AthenaArtifactRecord& artifact,
                              const std::string& relative,
                              const std::string& reason) {
      stale_artifacts.push_back ({artifact.uuid, relative, reason});
      stale_artifact_manifest.append (QJsonObject {
        {"uuid", QString::fromStdString (artifact.uuid)},
        {"path", QString::fromStdString (relative)},
        {"origin", QString::fromStdString (artifact.origin)},
        {"reason", QString::fromStdString (reason)}});
      ++result.stale_artifacts_pruned;
    };
    for (node_model_document& document: documents) {
      const std::string relative= document.path.generic_string ();
      auto found= artifacts_by_document.find (relative);
      if (found == artifacts_by_document.end ()) continue;
      tree& body= document_body_ref (document.document);
      const auto source_drd= get_offline_document_drd (
        document.document, url ((staged / document.path).string ().c_str ()));
      for (const AthenaArtifactRecord& artifact: found->second) {
        path native;
        std::string locate_error;
        if (!athena_artifact_locate_source (
              document.document, artifact, native, locate_error)) {
          // Artifact indexes are derived state and may legitimately lag the
          // source vault.  Do not invent a new source match for a stale row;
          // prune it from the staged index and force the owning document to be
          // reconsidered by the next normal Artifact build.
          prune_artifact (artifact, relative, locate_error);
          continue;
        }
        node_path where= native_to_source_path (native);
        const auto classification= athena::document_node::classify_source_path (
          body, where, source_drd,
          athena::document_node::standard_source_role);
        require (classification.ok (),
                 "Could not classify Artifact source " + artifact.uuid + " in " +
                 relative + (classification.diagnostics.empty () ? "" : ": " +
                   classification.diagnostics.front ().detail));
        if (!classification.content) {
          prune_artifact (
            artifact, relative,
            "Artifact source lies in generated or non-source data");
          continue;
        }
        const std::string role=
          artifact.origin == "enunciation" ? "enunciation" :
          artifact.origin == "bold-text" ? "bold-text-definition" :
          std::string ();
        require (!role.empty (),
                 "Unsupported Artifact origin during node-model migration: " +
                 artifact.origin);
        const tree& source= tree_at (body, where);
        std::string source_uuid= athena::node::id (source);
        if (source_uuid.empty ())
          source_uuid= deterministic_uuid (
            relative, "artifact:" + role, where);
        auto prepared= athena::document_node::prepare_artifact_binding (
          body, where, role, artifact.uuid, source_uuid);
        require (prepared.ok (),
                 "Could not bind Artifact " + artifact.uuid + " in " +
                 relative + (prepared.diagnostics.empty () ? "" : ": " +
                   prepared.diagnostics.front ().detail));
        if (prepared.change) ::apply (body, *prepared.change);
        auto range= artifact;
        if (artifact.origin == "bold-text")
          require (athena_artifact_freeze_source_nodes (
            document.document, range, locate_error),
            "Cannot freeze Artifact range " + artifact.uuid + ": " + locate_error);
        artifact_updates.push_back (
          {artifact.uuid, prepared.id, role, range.source_nodes});
        ++result.artifact_bindings;
      }
    }

    std::unordered_map<std::string,std::string> global_ids;
    for (node_model_document& document: documents)
      collect_ids (
        document_body_ref (document.document),
        document.path.generic_string (), global_ids);

    std::unordered_map<std::string,std::vector<std::string>> aliases;
    for (map_resolution& resolution: resolutions) {
      if (!resolution.document || !resolution.diagnostic.empty ()) continue;
      const tree& body= document_body_ref (resolution.document->document);
      if (resolution.direct) {
        const std::string id= athena::node::id (
          tree_at (body, *resolution.direct));
        if (id.empty ())
          resolution.diagnostic=
            "Resolved legacy target has no source UUID";
        else resolution.targets= {id};
      }
      else {
        for (const node_path& root_path: resolution.range_roots)
          collect_top_level_ids (
            tree_at (body, root_path), resolution.targets);
        dedupe_ids (resolution.targets);
        if (resolution.targets.empty ())
          resolution.diagnostic=
            "Resolved legacy range has no source objects";
      }
      if (resolution.diagnostic.empty ())
        aliases[resolution.source.uuid]= resolution.targets;
    }

    done= 0;
    for (node_model_document& document: documents) {
      report (progress, "Rewrite node references", done,
              documents.size (), document.path);
      rewrite_references (
        document.document, aliases, document.path.generic_string (),
        result.references_rewritten);
      report (progress, "Rewrite node references", ++done,
              documents.size (), document.path);
    }

    // Historical generated labels are consumed only as migration evidence.
    // Once every map/reference/Artifact target has become a persistent UUID,
    // remove the conservatively proven labels before publishing XML v2. User
    // labels never enter generated_labels and therefore survive untouched.
    for (node_model_document& document: documents) {
      auto found= generated_labels.find (document.path.generic_string ());
      if (found == generated_labels.end () || found->second.empty ()) continue;
      tree& body= document_body_ref (document.document);
      body= remove_migration_paths (body, found->second);
      result.generated_labels_removed += found->second.size ();
    }

    // Keep the old map as a compatibility locator until the later bare-wikilink
    // cutover. Single-target entries are re-keyed to the migrated source UUID;
    // duplicate aliases collapse deterministically. Multi-target range rows are
    // retained only for old external readers; in-document transclusions are now
    // canonical UUID lists and no longer depend on them.
    if (fs::exists (staged / map_relative)) {
      std::vector<AthenaVaultMapNode> migrated_map;
      std::unordered_set<std::string> seen;
      for (const map_resolution& resolution: resolutions) {
        AthenaVaultMapNode node= resolution.source;
        if (resolution.diagnostic.empty () &&
            resolution.targets.size () == 1) {
          node.uuid= resolution.targets.front ();
          if (!resolution.generated_labels.empty ()) {
            node.anchor_begin.clear ();
            node.anchor_end.clear ();
          }
        }
        if (seen.insert (node.uuid).second)
          migrated_map.push_back (std::move (node));
      }
      AthenaVaultMapSqlite map;
      require (map.open (staged / map_relative, false, error), error);
      require (map.replace_all (migrated_map, error), error);
      require (map.integrity_check (error), error);
      map.close ();
    }

    filesystem::confined_root storage (staged);
    QJsonArray manifest;
    done= 0;
    for (node_model_document& document: documents) {
      report (progress, "Write XML v2", done, documents.size (), document.path);
      tree& body= document_body_ref (document.document);
      athena::document_node::source_identity_state identities;
      const auto diagnostics= identities.initialize_complete (
        body, get_offline_document_drd (
                document.document,
                url ((staged / document.path).string ().c_str ())),
        athena::document_node::standard_source_role);
      require (diagnostics.empty (),
               "Migrated identity baseline is incomplete in " +
               document.path.generic_string () +
               (diagnostics.empty () ? "" : ": " +
                diagnostics.front ().detail));
      const std::string xml= write_xml_v2 (document.document);
      require (read_xml_v2 (
                 xml, xml_kind::document, limits ().codec) ==
               document.document,
               "XML v2 round-trip mismatch: " + document.path.string ());
      auto file= storage.open (document.path);
      auto revision= file.stat ();
      auto replacement= storage.replace (
        document.path, file, revision, xml);
      require (replacement.directory_synced,
               "Cannot sync migrated document " + document.path.string ());
      manifest.append (QJsonObject {
        {"path", QString::fromStdString (document.path.generic_string ())},
        {"assigned_ids", (qint64) document.assigned},
        {"converted_enunciations", (qint64) document.converted_enunciations}});
      ++result.migrated;
      report (progress, "Write XML v2", ++done,
              documents.size (), document.path);
    }

    update_artifact_binding_database (
      staged, staged_info, artifact_updates, stale_artifacts);
    staged_info.node_model_version= 1;
    require (athena_vaultfile_write (staged, staged_info, error), error);

    // Re-open every staged source from bytes after all database/Vaultfile
    // mutations. This is the acceptance boundary before the directory exchange.
    std::unordered_map<std::string,std::string> validated_ids;
    done= 0;
    for (const fs::path& path: document_paths) {
      const auto decoded= decode_document_bytes (
        bytes (staged, path), staged / path, limits ());
      require (decoded.format == document_source_format::xml_v2,
               "Migrated document is not XML v2: " + path.string ());
      const tree& body= document_body_ref (decoded.document);
      athena::document_node::source_identity_state identities;
      const auto diagnostics= identities.initialize_complete (
        body, get_offline_document_drd (
                decoded.document, url ((staged / path).string ().c_str ())),
        athena::document_node::standard_source_role);
      require (diagnostics.empty (),
               "Post-write identity validation failed: " + path.string ());
      collect_ids (body, path.generic_string (), validated_ids);
      report (progress, "Validate node model", ++done,
              document_paths.size (), path);
    }
    AthenaVaultfileInfo verified_info;
    require (athena_vaultfile_read (staged, verified_info, error), error);
    require (verified_info.node_model_version == 1,
             "Staged Vaultfile did not record node-model version");

    scan (staged, progress, "Sync snapshot", true, false);
    QJsonObject receipt {
      {"version", 1},
      {"migration", "utf8-xml-v1-to-node-model-v1"},
      {"source", QString::fromStdString (root.string ())},
      {"commit", "atomic-directory-exchange"},
      {"references_rewritten", (qint64) result.references_rewritten},
      {"artifact_bindings", (qint64) result.artifact_bindings},
      {"stale_artifacts_pruned", (qint64) result.stale_artifacts_pruned},
      {"stale_artifacts", stale_artifact_manifest},
      {"generated_labels_removed", (qint64) result.generated_labels_removed},
      {"documents", manifest}};
    filesystem::confined_root journal (workspace);
    const auto json= QJsonDocument (receipt).toJson (QJsonDocument::Indented);
    journal.preserve (
      "manifest.json",
      std::string_view (json.constData (), (std::size_t) json.size ()));
    sync_fd (parent.value);

    report (progress, "Commit", 0, 1, root);
    compare (
      original, scan (root, progress, "Check external changes", true), true);
    descriptor workspace_fd (
      ::open (workspace.c_str (), O_RDONLY | O_DIRECTORY | O_CLOEXEC));
    result.backup= staged;
    if (::syscall (
          SYS_renameat2, parent.value, root.filename ().c_str (),
          AT_FDCWD, staged.c_str (), RENAME_EXCHANGE) != 0)
      throw std::system_error (
        errno, std::generic_category (),
        "Atomic vault node-model directory exchange");
    exchanged= true;
    const int first= ::fsync (workspace_fd.value);
    const int second= ::fsync (parent.value);
    result.durable= first == 0 && second == 0;
    return result;
  }
  catch (...) {
    if (!exchanged) {
      std::error_code ignored;
      fs::remove_all (workspace, ignored);
    }
    throw;
  }
#endif
}

int upgrade_vault_format_cli (const fs::path& root) {
  interrupted= 0;
  const auto old_int= std::signal (SIGINT, cancel_signal);
  const auto old_term= std::signal (SIGTERM, cancel_signal);
  int code= 1;
  try {
    init_std_drd ();
    std::cerr << "Offline vault upgrade. Close all users of this vault before continuing.\n";
    std::string previous;
    auto last= std::chrono::steady_clock::now ();
    const bool terminal= ::isatty (STDERR_FILENO);
    auto progress= [&] (const char* phase, std::size_t done, std::size_t total, const std::string& path) {
      if (interrupted) throw std::runtime_error ("Upgrade cancelled before commit");
      const auto now= std::chrono::steady_clock::now ();
      const bool complete= total != 0 && done == total;
      if (previous == phase && !complete &&
          now - last < std::chrono::milliseconds (terminal ? 200 : 2000)) return;
      if (terminal) std::cerr << '\r' << "\033[K";
      if (total) {
        const auto filled= 24 * done / total;
        std::cerr << '[' << std::string (filled, '=') << std::string (24 - filled, ' ') << "] ";
      }
      std::cerr << phase;
      if (done || total) std::cerr << " " << done;
      else std::cerr << " ...";
      if (total) std::cerr << '/' << total;
      if (previous != phase && !path.empty ()) std::cerr << " " << std::quoted (path);
      if (!terminal || complete) std::cerr << '\n';
      std::cerr << std::flush;
      previous= phase; last= now;
    };
    const auto result= upgrade_vault_format (root, progress);
    std::cerr << "\nConverted " << result.converted << "; already XML " << result.already_xml << ".\n";
    if (!result.backup.empty ()) std::cerr << "Original vault backup: " << result.backup << '\n';
    if (!result.durable) std::cerr << "COMMITTED, but directory durability could not be confirmed. Do not delete the backup.\n";
    code= result.durable ? 0 : 2;
  }
  catch (const std::exception& e) { std::cerr << "\nVault upgrade failed: " << e.what () << '\n'; }
  catch (const string& e) { std::cerr << "\nVault upgrade failed: " << std::string (e.data (), N(e)) << '\n'; }
  std::signal (SIGINT, old_int);
  std::signal (SIGTERM, old_term);
  return interrupted && code != 0 ? 130 : code;
}

int upgrade_vault_node_model_cli (const fs::path& root) {
  interrupted= 0;
  const auto old_int= std::signal (SIGINT, cancel_signal);
  const auto old_term= std::signal (SIGTERM, cancel_signal);
  int code= 1;
  try {
    init_std_drd ();
    std::cerr
      << "Offline node-model migration for an existing UTF-8 XML vault. "
      << "Close all users of this vault before continuing.\n";
    std::string previous;
    auto last= std::chrono::steady_clock::now ();
    const bool terminal= ::isatty (STDERR_FILENO);
    auto progress= [&] (
        const char* phase, std::size_t done, std::size_t total,
        const std::string& path) {
      if (interrupted)
        throw std::runtime_error ("Node-model migration cancelled before commit");
      const auto now= std::chrono::steady_clock::now ();
      const bool complete= total != 0 && done == total;
      if (previous == phase && !complete &&
          now - last < std::chrono::milliseconds (terminal ? 200 : 2000))
        return;
      if (terminal) std::cerr << '\r' << "\033[K";
      if (total) {
        const auto filled= 24 * done / total;
        std::cerr << '[' << std::string (filled, '=')
                  << std::string (24 - filled, ' ') << "] ";
      }
      std::cerr << phase;
      if (done || total) std::cerr << " " << done;
      else std::cerr << " ...";
      if (total) std::cerr << '/' << total;
      if (previous != phase && !path.empty ())
        std::cerr << " " << std::quoted (path);
      if (!terminal || complete) std::cerr << '\n';
      std::cerr << std::flush;
      previous= phase;
      last= now;
    };
    const auto result= upgrade_vault_node_model (root, progress);
    if (result.backup.empty () && result.already_v2 != 0)
      std::cerr << "\nVault already uses node model v1; validated "
                << result.already_v2 << " XML v2 document(s).\n";
    else
      std::cerr << "\nMigrated " << result.migrated
                 << " document(s); rewrote " << result.references_rewritten
                 << " reference(s); persisted " << result.artifact_bindings
                 << " Artifact binding(s); pruned "
                 << result.stale_artifacts_pruned
                 << " stale Artifact row(s); removed "
                 << result.generated_labels_removed
                << " generated identity label(s).\n";
    if (!result.backup.empty ())
      std::cerr << "Original UTF-8 XML vault backup: "
                << result.backup << '\n';
    if (!result.durable)
      std::cerr
        << "COMMITTED, but directory durability could not be confirmed. "
        << "Do not delete the backup.\n";
    code= result.durable ? 0 : 2;
  }
  catch (const std::exception& e) {
    std::cerr << "\nVault node-model migration failed: " << e.what () << '\n';
  }
  catch (const string& e) {
    std::cerr << "\nVault node-model migration failed: "
              << std::string (e.data (), N(e)) << '\n';
  }
  std::signal (SIGINT, old_int);
  std::signal (SIGTERM, old_term);
  return interrupted && code != 0 ? 130 : code;
}
} // namespace athena::document
