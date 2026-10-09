/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#include "hodarium_resources.hpp"
#include "buffer_name_catalog.hpp"
#include "background_workers.hpp"
#include <QCryptographicHash>
#include <QUuid>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <mutex>
#include <set>
#include <stdexcept>
#include <system_error>

namespace athena::hodarium {
namespace {
constexpr std::size_t maximum_blob= 512ULL * 1024 * 1024;
constexpr std::size_t maximum_avd= 64ULL * 1024 * 1024;
const char* format (resource_kind kind) {
  switch (kind) {
    case resource_kind::avd: return "ath-resource-avd-v1";
    case resource_kind::attachment: return "ath-resource-blob-v1";
    case resource_kind::style: return "ath-resource-style-v1";
    case resource_kind::sorter: return "ath-resource-sorter-v1";
  }
  throw std::invalid_argument ("Unknown Hodarium resource kind");
}
std::string fingerprint (std::string_view bytes) {
  return QCryptographicHash::hash (
    QByteArrayView (bytes.data (), qsizetype (bytes.size ())),
    QCryptographicHash::Sha256).toHex ().toStdString ();
}
resource_kind require_path (const std::string& path, const resource_attachment_paths& declared) {
  auto kind= resource_path_kind (path, declared);
  if (!kind) throw std::invalid_argument ("Unsupported Hodarium resource path: " + path);
  return *kind;
}
resource_kind validate_revision (const revision& value, const resource_attachment_paths& declared) {
  auto kind= require_path (value.relative_path, declared);
  if (value.format != format (kind) || value.semantic_version != 1)
    throw std::invalid_argument ("Hodarium resource path and format disagree");
  const auto id= QString::fromStdString (value.object);
  const QUuid uuid (id);
  if (uuid.isNull () || uuid.toString (QUuid::WithoutBraces) != id)
    throw std::invalid_argument ("Hodarium resource identity is not a canonical UUID");
  return kind;
}
struct observed {
  filesystem::entry file;
  filesystem::metadata stat;
  std::string bytes;
};
std::optional<observed> observe (filesystem::confined_root& root,
                               const std::string& path, const resource_attachment_paths& declared) {
  std::optional<filesystem::entry> file;
  try { file= root.open (path); }
  catch (const std::system_error& e) {
    if (e.code () != std::errc::no_such_file_or_directory) throw;
    (void) root.open (".");
    return {};
  }
  auto stat= file->stat ();
  if (stat.directory) throw std::runtime_error ("Resource path is a directory");
  auto bytes= file->read (require_path (path, declared) == resource_kind::avd ? maximum_avd : maximum_blob);
  if (!filesystem::same_revision (stat, file->stat ()))
    throw std::runtime_error ("Resource changed while reading");
  return observed{*file, stat, std::move (bytes)};
}
void same_entry (filesystem::confined_root& root, const std::string& path,
                 const observed& source) {
  auto current= root.open (path);
  if (!current.same_object (source.file) ||
      !filesystem::same_revision (current.stat (), source.stat))
    throw std::runtime_error ("Resource changed before journal capture");
}
bool exists (filesystem::confined_root& root, const std::string& path) {
  try { (void) root.open (path); return true; }
  catch (const std::system_error& e) {
    if (e.code () != std::errc::no_such_file_or_directory) throw;
    (void) root.open ("."); return false;
  }
}
std::string portable_vault (const std::string& path) {
  std::filesystem::path result;
  for (const auto& component: std::filesystem::path (path).parent_path ()) {
    (void) component; result /= "..";
  }
  return result.empty () ? "." : result.generic_string ();
}
struct deferred {};
std::map<std::string, std::size_t>& resource_views () {
  static auto* views= new std::map<std::string, std::size_t>;
  return *views;
}
bool operational_name (const std::string& name) {
  if (name == "Vaultfile.json" || name == "lock.mdb" || name == "data.mdb") return true;
  for (const auto* suffix: {".sqlite", ".sqlite3", ".db", ".mdb"}) {
    auto at= name.rfind (suffix);
    if (at != std::string::npos &&
        (at + std::char_traits<char>::length (suffix) == name.size () ||
         name[at + std::char_traits<char>::length (suffix)] == '-')) return true;
  }
  return false;
}
}

std::shared_ptr<void> hold_resource_view (const std::filesystem::path& path) {
  std::lock_guard<std::recursive_mutex> lock (document_publication_mutex ());
  const auto canonical= std::filesystem::canonical (path).generic_string ();
  auto token= std::make_shared<std::string> (canonical);
  ++resource_views ()[canonical];
  return std::shared_ptr<void> (token.get (), [token] (void*) {
    std::lock_guard<std::recursive_mutex> release (document_publication_mutex ());
    auto it= resource_views ().find (*token);
    if (--it->second == 0) resource_views ().erase (it);
  });
}
bool resource_view_open (const std::filesystem::path& path) {
  std::lock_guard<std::recursive_mutex> lock (document_publication_mutex ());
  return resource_views ().count (path.lexically_normal ().generic_string ()) != 0;
}

std::string initial_resource_identity (const std::string& vault, const std::string& path,
    const resource_attachment_paths& declared) {
  require_path (path, declared);
  if (vault.empty ()) throw std::invalid_argument ("Resource import requires a Vault identity");
  const auto name= nlohmann::json::array ({"ATHENA-HODARIUM-RESOURCE-IDENTITY-v1", vault, path}).dump ();
  return QUuid::createUuidV5 (QUuid (QStringLiteral ("e9c24ef8-d1a2-5184-9f78-7a537493a07b")),
    QByteArray::fromStdString (name)).toString (QUuid::WithoutBraces).toStdString ();
}

std::optional<resource_kind> resource_path_kind (const std::string& path,
    const resource_attachment_paths& declared) {
  if (!history::valid_relative_document_path (path) || path.find ('\\') != std::string::npos)
    return {};
  const std::filesystem::path file (path);
  if (file.lexically_normal ().generic_string () != path) return {};
  for (const auto& component: file) {
    auto name= component.string ();
    if (name.empty () || name.front () == '.' || name.back () == '~' || operational_name (name)) return {};
  }
  const auto ext= file.extension ().string ();
  if (ext == ".avd") return resource_kind::avd;
  if (ext == ".ts") return resource_kind::style;
  if (ext == ".luau") return resource_kind::sorter;
  static const std::set<std::string> attachments {
    ".png", ".jpg", ".jpeg", ".gif", ".webp", ".tif", ".tiff",
    ".bmp", ".svg", ".pdf", ".eps", ".ps", ".bib", ".mp3", ".wav",
    ".ogg", ".flac", ".mp4", ".webm", ".mov"};
  if (attachments.count (ext)) return resource_kind::attachment;
  static const std::set<std::string> forbidden {
    ".ath", ".tm", ".scm", ".go", ".pem", ".key", ".p12", ".pfx", ".keystore",
    ".gpg", ".asc", ".sqlite", ".sqlite3", ".db", ".mdb", ".lock"};
  auto lower= ext;
  std::transform (lower.begin (), lower.end (), lower.begin (), [] (unsigned char c) {
    return c >= 'A' && c <= 'Z' ? char (c - 'A' + 'a') : char (c);
  });
  if (lower == ".avd") return resource_kind::avd;
  if (lower == ".ts") return resource_kind::style;
  if (lower == ".luau") return resource_kind::sorter;
  auto basename= file.filename ().string ();
  std::transform (basename.begin (), basename.end (), basename.begin (), [] (unsigned char c) {
    return c >= 'A' && c <= 'Z' ? char (c - 'A' + 'a') : char (c);
  });
  if (forbidden.count (lower) || operational_name (basename) || basename == "vaultfile.json" || basename == "credentials" ||
      basename == "secrets" || basename == "authorized_keys" || basename == "id_rsa" ||
      basename == "id_ed25519" || basename == "id_ecdsa") return {};
  if (declared.count (path)) return resource_kind::attachment;
  return {};
}
bool resource_revision_format (const std::string& value) {
  return value == format (resource_kind::avd) || value == format (resource_kind::attachment) ||
         value == format (resource_kind::style) || value == format (resource_kind::sorter);
}

resource_inventory inventory_resources (const std::filesystem::path& path,
    const std::atomic<bool>& cancelled, std::size_t maximum_entries,
    background::source_watch* watch, const resource_attachment_paths& declared) {
  if (!maximum_entries || maximum_entries > 1000000)
    throw std::invalid_argument ("Invalid resource inventory budget");
  filesystem::confined_root root (path);
  resource_inventory result;
  std::vector<std::string> pending {"."};
  std::size_t visited= 0;
  while (!pending.empty ()) {
    if (cancelled.load ()) return result;
    auto parent= std::move (pending.back ()); pending.pop_back ();
    try {
      auto directory= root.open (parent);
      if (watch) watch->directory (directory.path ());
      const auto before= directory.stat ();
      for (const auto& name: directory.names ()) {
        if (cancelled.load ()) return result;
        if (++visited > maximum_entries) {
          result.errors.push_back ("Resource inventory entry budget exceeded"); return result;
        }
        if (name.empty () || name.front () == '.' || name.back () == '~' || operational_name (name)) continue;
        const auto relative= parent == "." ? name : parent + "/" + name;
        try {
          auto file= root.open (relative);
          const auto stat= file.stat ();
          if (stat.directory) pending.push_back (relative);
          else if (auto kind= resource_path_kind (relative, declared)) {
            if (stat.size > (*kind == resource_kind::avd ? maximum_avd : maximum_blob))
              throw std::length_error ("Resource exceeds transfer byte budget");
            result.files.push_back ({relative, *kind, stat});
          }
        }
        catch (const std::exception& e) { result.errors.push_back (relative + ": " + e.what ()); }
      }
      if (!filesystem::same_revision (before, directory.stat ()))
        result.errors.push_back (parent + ": directory changed during inventory");
    }
    catch (const std::exception& e) { result.errors.push_back (parent + ": " + e.what ()); }
  }
  std::sort (result.files.begin (), result.files.end (),
    [] (const auto& a, const auto& b) { return a.path < b.path; });
  result.complete= result.errors.empty () && !cancelled.load ();
  return result;
}

resource_bindings applied_resources (revision_store& journal, const std::string& vault,
    const resource_attachment_paths& declared) {
  resource_bindings result;
  std::string cursor;
  for (;;) {
    auto page= journal.applied_page (vault, cursor, 256);
    if (page.empty ()) return result;
    for (const auto& value: page) {
      cursor= value.object;
      if (!resource_revision_format (value.format) || value.deleted) continue;
      // Removing a Material declaration withdraws unknown-file authorization;
      // it is not permission to delete that file or erase its journal identity.
      if (!resource_path_kind (value.relative_path, declared)) continue;
      validate_revision (value, declared);
      if (!result.emplace (value.relative_path, value).second)
        throw std::runtime_error ("Multiple Hodarium resource identities occupy " + value.relative_path);
    }
  }
}

std::string resource_payload (const std::filesystem::path& root,
    const std::string& path, std::string_view bytes, const resource_attachment_paths& declared) {
  const auto kind= require_path (path, declared);
  if (bytes.size () > (kind == resource_kind::avd ? maximum_avd : maximum_blob))
    throw std::length_error ("Resource exceeds transfer byte budget");
  if (bytes.substr (0, 16) == std::string_view ("SQLite format 3\0", 16))
    throw std::invalid_argument ("A SQLite database is not a Hodarium source resource");
  if (kind != resource_kind::avd) return std::string (bytes);
  using json= nlohmann::json;
  std::vector<std::set<std::string>> keys;
  auto value= json::parse (bytes.begin (), bytes.end (),
    [&] (int depth, json::parse_event_t event, json& item) {
      if (depth > 32) throw std::invalid_argument ("AVD JSON nesting exceeds budget");
      if (event == json::parse_event_t::object_start) keys.emplace_back ();
      if (event == json::parse_event_t::key && !keys.back ().insert (item.get<std::string> ()).second)
        throw std::invalid_argument ("Duplicate AVD JSON key");
      if (event == json::parse_event_t::object_end) keys.pop_back ();
      return true;
    });
  if (!value.is_object () || value.value ("format", std::string ()) != "athena-avd" ||
      !value.contains ("version") || value["version"] != 1)
    throw std::invalid_argument ("Unsupported AVD format or version");
  for (auto it= value.begin (); it != value.end (); ++it)
    if (it.key () != "format" && it.key () != "version" && it.key () != "namespace" &&
        it.key () != "vault" && it.key () != "counter_cache")
      throw std::invalid_argument ("Unsupported AVD definition field: " + it.key ());
  const auto namespace_id= value.at ("namespace").get<std::string> ();
  const auto uuid_text= QString::fromStdString (namespace_id);
  const QUuid uuid (uuid_text);
  if (uuid.isNull () || uuid.toString (QUuid::WithoutBraces) != uuid_text)
    throw std::invalid_argument ("AVD namespace must be a canonical UUID");
  const auto vault= value.at ("vault").get<std::string> ();
  if (vault.empty () || vault.find ('\0') != std::string::npos || vault.find ('\\') != std::string::npos)
    throw std::invalid_argument ("Invalid AVD Vault path");
  // Lexical comparison deliberately does not follow a descriptor-supplied link
  // outside this Vault. The root was captured by confined_root at the boundary.
  auto selected= (root / std::filesystem::path (path).parent_path () / vault).lexically_normal ();
  if (selected.filename ().empty ()) selected= selected.parent_path ();
  auto scope= root.lexically_normal ();
  if (scope.filename ().empty ()) scope= scope.parent_path ();
  if (selected != scope)
    throw std::invalid_argument ("AVD selects another Vault");
  value["vault"]= portable_vault (path);
  value.erase ("counter_cache");
  return value.dump () + "\n";
}

std::optional<std::string> capture_resource (const std::filesystem::path& path,
    revision_store& journal, const std::string& vault, const std::string& member,
    const resource_file& file, const std::optional<revision>& previous,
    const std::function<bool()>& permitted, const resource_attachment_paths& declared) {
  if (permitted && !permitted ()) return {};
  const auto kind= require_path (file.path, declared);
  if (kind != file.kind) throw std::invalid_argument ("Resource inventory kind changed");
  filesystem::confined_root root (path);
  if (previous && (validate_revision (*previous, declared) != kind || previous->vault != vault || previous->deleted))
    throw std::invalid_argument ("Resource capture has an incompatible predecessor");
  auto source= observe (root, file.path, declared);
  if (!source || !filesystem::same_revision (file.observed, source->stat))
    throw std::runtime_error ("Resource changed since inventory");
  const bool renamed= previous && previous->relative_path != file.path;
  if (renamed && exists (root, previous->relative_path))
    throw std::runtime_error ("Explicit resource move still has an occupied old path");
  revision snapshot;
  snapshot.vault= vault; snapshot.origin_member= member;
  snapshot.object= previous ? previous->object : initial_resource_identity (vault, file.path, declared);
  if (!previous) {
    auto existing= journal.applied (vault, snapshot.object);
    if (!existing) snapshot.origin_member= "saved-resource-baseline-v1";
    else {
      auto old= journal.offer (*existing);
      if (!old->metadata.deleted && old->metadata.relative_path == file.path)
        throw std::invalid_argument ("Resource capture omitted its existing applied binding");
      snapshot.object= QUuid::createUuid ().toString (QUuid::WithoutBraces).toStdString ();
    }
  }
  snapshot.relative_path= file.path; snapshot.format= format (kind);
  snapshot.payload= resource_payload (root.path (), file.path, source->bytes, declared);
  same_entry (root, file.path, *source);
  if (permitted && !permitted ()) return {};
  return journal.capture_saved (std::move (snapshot), previous ? std::optional<std::string> (previous->id) : std::nullopt);
}

void validate_resource_revision (const std::filesystem::path& root, const revision& value,
    const resource_attachment_paths& declared) {
  validate_revision (value, declared);
  if (value.deleted) {
    if (!value.payload.empty ()) throw std::invalid_argument ("Resource tombstone carries content");
  }
  else if (resource_payload (root, value.relative_path, value.payload, declared) != value.payload)
    throw std::invalid_argument ("Incoming resource is not in canonical portable form");
}

std::optional<std::string> capture_resource_deletion (const std::filesystem::path& path,
    revision_store& journal, const std::string& member, const revision& previous,
    const std::function<bool()>& permitted, const resource_attachment_paths& declared) {
  validate_revision (previous, declared);
  if (!permitted || !permitted ()) return {};
  if (previous.deleted) throw std::invalid_argument ("Resource is already deleted");
  filesystem::confined_root root (path);
  if (exists (root, previous.relative_path))
    throw std::runtime_error ("Cannot capture deletion of an existing resource");
  revision snapshot= previous;
  snapshot.id.clear (); snapshot.parents.clear (); snapshot.payload.clear ();
  snapshot.deleted= true; snapshot.origin_member= member;
  if (!permitted ()) return {};
  return journal.capture_saved (std::move (snapshot), previous.id);
}

bool apply_closed_resource (const std::filesystem::path& path,
    revision_store& journal, history::document_history_store& history,
    const std::string& revision_id,
    const std::function<bool(resource_kind, const std::string&)>& permitted,
    const resource_attachment_paths& declared) {
  auto target= journal.get (revision_id);
  if (!target) throw std::invalid_argument ("Unknown Hodarium resource revision");
  const auto kind= validate_revision (*target, declared);
  if (!permitted || !permitted (kind, target->relative_path)) return false;
  filesystem::confined_root root (path);
  if (history.vault_root () != root.path ())
    throw std::invalid_argument ("Resource history belongs to another Vault");
  validate_resource_revision (root.path (), *target, declared);
  const auto operation= "hodarium-resource-" + target->id;
  auto intent= journal.application (operation);
  if (intent && intent->completed) return true;
  auto expected= intent ? intent->expected_revision : journal.applied (target->vault, target->object);
  if (!intent && expected == target->id) return true;
  auto previous= expected ? journal.offer (*expected) : std::optional<revision_offer> ();
  if (previous && validate_revision (previous->metadata, declared) != kind)
    throw std::invalid_argument ("Resource changes its format contract");
  const auto source_path= previous ? previous->metadata.relative_path : target->relative_path;
  if (target->deleted && source_path != target->relative_path)
    throw std::invalid_argument ("Resource deletion cannot also rename its target");
  const auto allowed= [&] {
    for (const auto& name: {source_path, target->relative_path})
      if (!permitted (kind, name) || published_buffer_actor_id ((root.path () / name).generic_string ()) ||
          resource_view_open (root.path () / name))
        throw deferred {};
  };
  bool prepared_here= false, published= false;
  try {
    allowed ();
    if (!intent) {
      if (journal.heads (target->vault, target->object) != std::vector<std::string>{target->id})
        throw std::runtime_error ("Resource requires manual conflict resolution");
      auto source= observe (root, source_path, declared);
      const bool existing= previous && !previous->metadata.deleted;
      if (bool (source) != existing || (source &&
          fingerprint (resource_payload (root.path (), source_path, source->bytes, declared)) != journal.payload_fingerprint (*expected)))
        throw std::runtime_error ("Resource differs from its applied revision");
      if (source_path != target->relative_path && exists (root, target->relative_path))
        throw std::runtime_error ("Resource rename destination is occupied");
      apply_intent prepared;
      prepared.operation= operation; prepared.revision_id= target->id;
      prepared.expected_revision= expected; prepared.source_path= source_path;
      if (source) {
        prepared.source_fingerprint= fingerprint (source->bytes);
        std::string error;
        if (!history.protect (source_path, source->bytes, operation, prepared.protected_history_version, error))
          throw std::runtime_error (error);
      }
      prepared_here= journal.prepare_apply (prepared); intent= prepared;
    }
    if (journal.applied (target->vault, target->object) != intent->expected_revision)
      throw std::runtime_error ("Resource application base changed");
    if (intent->source_fingerprint) {
      std::string preimage, error;
      if (!history.reconstruct (intent->protected_history_version, preimage, error))
        throw std::runtime_error (error);
      if (fingerprint (preimage) != *intent->source_fingerprint)
        throw std::runtime_error ("Resource protected history does not match its intent");
    }
    std::unique_lock<std::recursive_mutex> publication (document_publication_mutex (), std::defer_lock);
    const auto gate= [&] {
      if (!publication.owns_lock () && !publication.try_lock ()) throw deferred {};
      allowed ();
    };
    auto source= observe (root, source_path, declared);
    const bool renamed= source_path != target->relative_path;
    auto rename_destination= renamed ? observe (root, target->relative_path, declared) : std::optional<observed> ();
    const auto& destination= renamed ? rename_destination : source;
    const auto hash= journal.payload_fingerprint (target->id);
    const bool installed= !target->deleted && destination && fingerprint (destination->bytes) == hash;
    if (source && !(installed && !renamed) &&
        (!intent->source_fingerprint || fingerprint (source->bytes) != *intent->source_fingerprint))
      throw std::runtime_error ("Resource preimage changed during application");
    if (!source && intent->source_fingerprint && !target->deleted && !installed)
      throw std::runtime_error ("Resource disappeared before installation");
    if (!target->deleted && !installed) {
      if (renamed && destination) throw std::runtime_error ("Resource rename destination is occupied");
      root.ensure_parents (target->relative_path, gate);
      const auto written= source && !renamed ?
        root.replace (source_path, source->file, source->stat, target->payload, gate) :
        root.create (target->relative_path, target->payload, gate);
      published= true;
      if (!written.directory_synced) throw std::runtime_error ("Resource installation directory sync failed");
      publication.unlock ();
    }
    if ((target->deleted || renamed) && source) {
      const bool durable= root.remove (source_path, source->file, source->stat, gate);
      published= true;
      if (!durable) throw std::runtime_error ("Resource deletion directory sync failed");
      publication.unlock ();
    }
    gate ();
    if (!renamed || intent->source_fingerprint) root.sync_parent (source_path);
    if (renamed) root.sync_parent (target->relative_path);
    auto final= observe (root, target->relative_path, declared);
    if (renamed && exists (root, source_path)) throw std::runtime_error ("Resource rename source reappeared");
    const auto actual= final ? std::optional<std::string> (fingerprint (final->bytes)) : std::nullopt;
    if (target->deleted ? bool (actual) : (!actual || *actual != hash))
      throw std::runtime_error ("Installed resource differs from its revision");
    journal.finish_apply (operation, actual);
    return true;
  }
  catch (const deferred&) {
    if (prepared_here && !published) journal.abandon_unpublished_apply (operation);
    return false;
  }
}
}
