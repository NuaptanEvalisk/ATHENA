/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#include "client_settings.hpp"
#include "control_http.hpp"
#include "vault_registration.hpp"
#include "recovery.hpp"
#include "sqlite_internal.hpp"
#include <sodium.h>
#include <stdexcept>
#include <QUuid>

namespace athena::hodarium {
namespace {
using namespace detail;
void identifier (const std::string& value) {
  unsigned char bytes[32]; std::size_t size;
  if (value.size () != 43 || sodium_base642bin (bytes, sizeof bytes,
    value.data (), value.size (), nullptr, &size, nullptr,
    sodium_base64_VARIANT_URLSAFE_NO_PADDING) != 0 || size != sizeof bytes)
    throw std::invalid_argument ("Invalid Hodarium settings identity");
}
void validate (const client_profile& p) {
  validate_authority_origin (p.origin);
  identifier (p.pin.group); identifier (p.pin.public_key); identifier (p.pin.generation);
  identifier (p.recovery_public_key); identifier (p.device.handle); identifier (p.device.public_key);
  if (!p.member.empty ()) identifier (p.member);
  if (p.name.empty () || p.name.size () > 128 ||
      QString::fromUtf8 (p.name.data (), p.name.size ()).toUtf8 ().toStdString () != p.name)
    throw std::invalid_argument ("Invalid Hodarium device name");
}
client_profile read (const statement& st) {
  client_profile p;
  p.origin= QUrl (QString::fromStdString (st.bytes (0)));
  p.pin= {st.bytes (1), st.bytes (2), st.bytes (3)};
  p.recovery_public_key= st.bytes (4);
  p.device= {st.bytes (5), st.bytes (6)};
  p.name= st.bytes (7); p.member= st.bytes (8);
  p.enabled= sqlite3_column_int (st.value, 9) != 0;
  validate (p);
  return p;
}
constexpr auto columns= "SELECT origin,hodarium,authority,generation,recovery_key,"
  "key_handle,device_key,name,member,enabled FROM profiles";
}
client_settings::client_settings (const std::filesystem::path& database) {
  using namespace detail;
  try {
    check (db_, sqlite3_open_v2 (database.string ().c_str (), &db_,
      SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOMUTEX, nullptr));
    check (db_, sqlite3_busy_timeout (db_, 5000));
    int version;
    { statement st (db_, "PRAGMA user_version"); st.row (); version= sqlite3_column_int (st.value, 0); }
    if (version > 5) throw std::runtime_error ("Unsupported Hodarium settings version");
    sql (db_, "PRAGMA foreign_keys=ON; PRAGMA journal_mode=WAL; PRAGMA synchronous=FULL;");
    if (version == 0) {
      transaction tx (db_);
      sql (db_, "CREATE TABLE profiles("
        "origin TEXT NOT NULL,hodarium TEXT PRIMARY KEY,authority TEXT NOT NULL,"
        "generation TEXT NOT NULL,recovery_key TEXT NOT NULL,key_handle TEXT NOT NULL,"
        "device_key TEXT NOT NULL,name TEXT NOT NULL,member TEXT NOT NULL DEFAULT '',"
        "enabled INTEGER NOT NULL DEFAULT 0 CHECK(enabled IN (0,1)));"
        "PRAGMA user_version=1;");
      tx.commit ();
    }
    if (version < 2) {
      transaction tx (db_);
      sql (db_, "CREATE TABLE vault_bindings("
        "hodarium TEXT NOT NULL REFERENCES profiles(hodarium),"
        "vault TEXT NOT NULL,root TEXT NOT NULL UNIQUE,"
        "enabled INTEGER NOT NULL CHECK(enabled IN(0,1)),PRIMARY KEY(hodarium,vault));"
        "PRAGMA user_version=2;");
      tx.commit ();
    }
    if (version < 3) {
      transaction tx (db_);
      sql (db_, "CREATE TABLE vault_secrets("
        "hodarium TEXT NOT NULL REFERENCES profiles(hodarium),generation TEXT NOT NULL,"
        "vault TEXT NOT NULL,commitment TEXT NOT NULL,key_handle TEXT NOT NULL UNIQUE,"
        "PRIMARY KEY(hodarium,generation,vault,commitment));"
        "PRAGMA user_version=3;");
      tx.commit ();
    }
    if (version < 4) {
      transaction tx (db_);
      sql (db_, "CREATE TABLE vault_secret_authority("
        "hodarium TEXT NOT NULL REFERENCES profiles(hodarium),generation TEXT NOT NULL,"
        "vault TEXT NOT NULL,commitment TEXT NOT NULL,authority TEXT NOT NULL,receipt TEXT NOT NULL,"
        "PRIMARY KEY(hodarium,generation,vault)); PRAGMA user_version=4;");
      tx.commit ();
    }
    if (version < 5) {
      transaction tx (db_);
      sql (db_, "CREATE TABLE accepted_generations("
        "hodarium TEXT NOT NULL REFERENCES profiles(hodarium),generation TEXT NOT NULL,"
        "authority TEXT NOT NULL,legacy_path INTEGER NOT NULL,receipt TEXT NOT NULL,"
        "PRIMARY KEY(hodarium,generation));"
        "INSERT INTO accepted_generations SELECT hodarium,generation,authority,1,'' FROM profiles;"
        "CREATE TABLE relay_bindings("
        "hodarium TEXT NOT NULL REFERENCES profiles(hodarium),origin TEXT NOT NULL,"
        "credential_handle TEXT NOT NULL,PRIMARY KEY(hodarium,origin));"
        "ALTER TABLE vault_bindings ADD COLUMN allow_code_resources INTEGER NOT NULL DEFAULT 0 CHECK(allow_code_resources IN(0,1));"
        "PRAGMA user_version=5;");
      tx.commit ();
    }
  }
  catch (...) { sqlite3_close (db_); db_= nullptr; throw; }
}
client_settings::~client_settings () { sqlite3_close (db_); }
std::vector<client_profile> client_settings::profiles () const {
  detail::statement st (db_, (std::string (columns) + " ORDER BY hodarium").c_str ());
  std::vector<client_profile> result;
  while (st.row ()) result.push_back (read (st));
  return result;
}
std::optional<client_profile> client_settings::find (const std::string& group) const {
  detail::statement st (db_, (std::string (columns) + " WHERE hodarium=?").c_str ());
  st.text (1, group);
  if (!st.row ()) return std::nullopt;
  return read (st);
}
void client_settings::add_pending (const client_profile& p) {
  using namespace detail;
  validate (p);
  if (!p.member.empty () || p.enabled)
    throw std::invalid_argument ("New Hodarium enrollment must start pending and disabled");
  transaction tx (db_);
  statement st (db_, "INSERT INTO profiles(origin,hodarium,authority,generation,recovery_key,"
    "key_handle,device_key,name) VALUES(?,?,?,?,?,?,?,?)");
  st.text (1, p.origin.toString (QUrl::FullyEncoded).toStdString ());
  st.text (2, p.pin.group); st.text (3, p.pin.public_key); st.text (4, p.pin.generation);
  st.text (5, p.recovery_public_key); st.text (6, p.device.handle);
  st.text (7, p.device.public_key); st.text (8, p.name); st.row ();
  statement generation (db_, "INSERT INTO accepted_generations VALUES(?,?,?,1,'')");
  generation.text (1, p.pin.group); generation.text (2, p.pin.generation);
  generation.text (3, p.pin.public_key); generation.row ();
  tx.commit ();
}

client_profile client_settings::accept_recovery (recovery_candidate&& candidate) {
  using namespace detail;
  if (candidate.consumed_ || std::chrono::steady_clock::now () >= candidate.deadline_)
    throw std::invalid_argument ("Recovery approval expired or was already consumed");
  transaction tx (db_);
  const auto& before= candidate.previous_;
  auto current= find (before.pin.group);
  if (!current || current->origin != before.origin || current->pin.public_key != before.pin.public_key ||
      current->pin.generation != before.pin.generation || current->recovery_public_key != before.recovery_public_key ||
      current->device.handle != before.device.handle || current->device.public_key != before.device.public_key)
    throw std::invalid_argument ("Recovery approval no longer matches local trust");
  auto next= verify_current_recovery (current->pin, current->recovery_public_key, candidate.envelope_, candidate.nonce_);
  statement known (db_, "SELECT 1 FROM accepted_generations WHERE hodarium=? AND generation=?");
  known.text (1, next.group); known.text (2, next.generation);
  if (known.row ()) throw std::invalid_argument ("Recovery cannot return to a previously accepted generation");
  statement record (db_, "INSERT INTO accepted_generations VALUES(?,?,?,0,?)");
  record.text (1, next.group); record.text (2, next.generation); record.text (3, next.public_key);
  record.text (4, candidate.envelope_.toStdString ()); record.row ();
  statement update (db_, "UPDATE profiles SET authority=?,generation=?,member='',enabled=0 WHERE hodarium=?");
  update.text (1, next.public_key); update.text (2, next.generation); update.text (3, next.group); update.row ();
  tx.commit ();
  candidate.consumed_= true;
  current->pin= std::move (next); current->member.clear (); current->enabled= false;
  return *current;
}

std::filesystem::path client_settings::generation_database_path (const std::filesystem::path& directory,
  const std::string& group, const char* kind) const {
  auto profile= find (group);
  if (!profile) throw std::invalid_argument ("Unknown Hodarium profile");
  detail::statement st (db_, "SELECT legacy_path FROM accepted_generations WHERE hodarium=? AND generation=? AND authority=?");
  st.text (1, group); st.text (2, profile->pin.generation); st.text (3, profile->pin.public_key);
  if (!st.row ()) throw std::invalid_argument ("Missing accepted Hodarium generation");
  const bool legacy= sqlite3_column_int (st.value, 0) != 0;
  return directory / (std::string (kind) + "-" + group + (legacy ? "" : "-" + profile->pin.generation) + ".sqlite");
}

std::filesystem::path client_settings::trust_database_path (const std::filesystem::path& directory,
  const std::string& group) const {
  return generation_database_path (directory, group, "membership");
}

std::filesystem::path client_settings::revision_database_path (const std::filesystem::path& directory,
  const std::string& group) const {
  return generation_database_path (directory, group, "revisions");
}

namespace {
std::string relay_origin (QUrl origin) {
  validate_authority_origin (origin);
  // Default TLS port denotes the same origin, not a ninth route or credential.
  if (origin.port () == 443) origin.setPort (-1);
  return origin.toString (QUrl::FullyEncoded).toStdString ();
}
}
std::vector<relay_binding> client_settings::relays (const std::string& group) const {
  detail::statement st (db_, "SELECT origin,credential_handle FROM relay_bindings WHERE hodarium=? ORDER BY origin");
  st.text (1, group);
  std::vector<relay_binding> result;
  while (st.row ()) result.push_back ({group, QUrl (QString::fromStdString (st.bytes (0))), st.bytes (1)});
  return result;
}
void client_settings::set_relay (const relay_binding& binding) {
  using namespace detail;
  auto origin= relay_origin (binding.origin);
  identifier (binding.credential_handle);
  transaction tx (db_);
  if (!find (binding.group)) throw std::invalid_argument ("Relay requires an existing Hodarium profile");
  statement count (db_, "SELECT count(*) FROM relay_bindings WHERE hodarium=? AND origin<>?");
  count.text (1, binding.group); count.text (2, origin); count.row ();
  if (sqlite3_column_int (count.value, 0) >= 8)
    throw std::invalid_argument ("A Hodarium profile supports at most eight Relays");
  statement st (db_, "INSERT INTO relay_bindings VALUES(?,?,?) ON CONFLICT(hodarium,origin) "
    "DO UPDATE SET credential_handle=excluded.credential_handle");
  st.text (1, binding.group); st.text (2, origin); st.text (3, binding.credential_handle); st.row ();
  tx.commit ();
}
void client_settings::remove_relay (const std::string& group, const QUrl& origin) {
  detail::statement st (db_, "DELETE FROM relay_bindings WHERE hodarium=? AND origin=?");
  st.text (1, group); st.text (2, relay_origin (origin)); st.row ();
}
void client_settings::complete_admission (const std::string& group,
  const std::string& handle, const std::string& public_key, const std::string& member,
  const std::string& generation) {
  using namespace detail;
  identifier (member);
  transaction tx (db_);
  auto profile= find (group);
  if (!profile) throw std::invalid_argument ("Unknown Hodarium admission profile");
  if (generation.empty ()) {
    // Old callers are valid only before any recovery. A delayed pre-recovery
    // admission callback must not restore an expelled member afterward.
    statement initial (db_, "SELECT 1 FROM accepted_generations WHERE hodarium=? AND generation=? AND legacy_path=1");
    initial.text (1, group); initial.text (2, profile->pin.generation);
    if (!initial.row ()) throw std::invalid_argument ("Recovered admission requires its explicit authority generation");
  }
  else if (generation != profile->pin.generation)
    throw std::invalid_argument ("Admission belongs to a previous authority generation");
  statement st (db_, "UPDATE profiles SET member=? WHERE hodarium=? AND key_handle=? "
    "AND device_key=? AND (member='' OR member=?)");
  st.text (1, member); st.text (2, group); st.text (3, handle);
  st.text (4, public_key); st.text (5, member); st.row ();
  if (sqlite3_changes (db_) != 1)
    throw std::invalid_argument ("Hodarium admission no longer matches persisted device identity");
  tx.commit ();
}
void client_settings::set_enabled (const std::string& group, bool enabled) {
  using namespace detail;
  transaction tx (db_);
  statement st (db_, "UPDATE profiles SET enabled=? WHERE hodarium=? AND (?=0 OR member<>'')");
  check (db_, sqlite3_bind_int (st.value, 1, enabled)); st.text (2, group);
  check (db_, sqlite3_bind_int (st.value, 3, enabled)); st.row ();
  if (sqlite3_changes (db_) != 1)
    throw std::invalid_argument ("Only an admitted Hodarium profile can be enabled");
  tx.commit ();
}
std::vector<vault_binding> client_settings::vaults (const std::string& group) const {
  detail::statement st (db_, "SELECT vault,root,enabled,allow_code_resources FROM vault_bindings WHERE hodarium=? ORDER BY vault");
  st.text (1, group);
  std::vector<vault_binding> result;
  while (st.row ()) result.push_back ({group, st.bytes (0), std::filesystem::u8path (st.bytes (1)),
    sqlite3_column_int (st.value, 2) != 0, sqlite3_column_int (st.value, 3) != 0});
  return result;
}
void client_settings::bind_vault (const vault_binding& binding) {
  using namespace detail;
  auto id= QString::fromStdString (binding.vault);
  if (QUuid (id).isNull () || QUuid (id).toString (QUuid::WithoutBraces) != id || !binding.root.is_absolute ())
    throw std::invalid_argument ("Vault binding requires a canonical UUID and absolute local directory");
  auto root= std::filesystem::canonical (binding.root);
  if (!std::filesystem::is_directory (root)) throw std::invalid_argument ("Vault binding is not a directory");
  transaction tx (db_);
  auto profile= find (binding.group);
  if (!profile || profile->member.empty ()) throw std::invalid_argument ("Vault binding requires an admitted device");
  statement previous (db_, "SELECT root FROM vault_bindings");
  while (previous.row ()) {
    auto path= std::filesystem::u8path (previous.bytes (0));
    auto a= root.begin (), b= path.begin ();
    while (a != root.end () && b != path.end () && *a == *b) { ++a; ++b; }
    if (a == root.end () || b == path.end ())
      throw std::invalid_argument ("Vault binding overlaps an existing local binding");
  }
  statement st (db_, "INSERT INTO vault_bindings(hodarium,vault,root,enabled,allow_code_resources) VALUES(?,?,?,?,?)");
  st.text (1, binding.group); st.text (2, binding.vault); st.text (3, root.u8string ());
  check (db_, sqlite3_bind_int (st.value, 4, binding.enabled));
  check (db_, sqlite3_bind_int (st.value, 5, binding.allow_code_resources)); st.row (); tx.commit ();
}
void client_settings::set_vault_code_resources (const std::string& group, const std::string& vault, bool allowed) {
  using namespace detail;
  transaction tx (db_);
  statement st (db_, "UPDATE vault_bindings SET allow_code_resources=? WHERE hodarium=? AND vault=?");
  check (db_, sqlite3_bind_int (st.value, 1, allowed)); st.text (2, group); st.text (3, vault); st.row ();
  if (sqlite3_changes (db_) != 1) throw std::invalid_argument ("Unknown Vault binding");
  tx.commit ();
}
void client_settings::set_vault_enabled (const std::string& group, const std::string& vault, bool enabled) {
  using namespace detail;
  transaction tx (db_);
  statement st (db_, "UPDATE vault_bindings SET enabled=? WHERE hodarium=? AND vault=?");
  check (db_, sqlite3_bind_int (st.value, 1, enabled)); st.text (2, group); st.text (3, vault); st.row ();
  if (sqlite3_changes (db_) != 1) throw std::invalid_argument ("Unknown Vault binding");
  tx.commit ();
}
void client_settings::unbind_vault (const std::string& group, const std::string& vault) {
  detail::transaction tx (db_);
  detail::statement st (db_, "DELETE FROM vault_bindings WHERE hodarium=? AND vault=?");
  st.text (1, group); st.text (2, vault); st.row (); tx.commit ();
}
std::optional<vault_secret> client_settings::find_vault_secret (const std::string& group,
  const std::string& generation, const std::string& vault, const std::string& commitment) const {
  detail::statement st (db_, "SELECT key_handle FROM vault_secrets WHERE hodarium=? AND generation=? AND vault=? AND commitment=?");
  st.text (1, group); st.text (2, generation); st.text (3, vault); st.text (4, commitment);
  if (!st.row ()) return std::nullopt;
  return vault_secret{st.bytes (0), group, vault, commitment};
}
void client_settings::remember_vault_secret (const std::string& generation, const vault_secret& secret) {
  using namespace detail;
  identifier (generation); identifier (secret.commitment);
  // Secret Service may block: do not hold a SQLite write transaction while
  // waiting for it. Recheck the local enrollment and binding afterward.
  verify_vault_secret (secret);
  transaction tx (db_);
  auto profile= find (secret.group);
  if (!profile || profile->member.empty () || profile->pin.generation != generation)
    throw std::invalid_argument ("Vault secret does not match the enrolled authority generation");
  statement binding (db_, "SELECT 1 FROM vault_bindings WHERE hodarium=? AND vault=?");
  binding.text (1, secret.group); binding.text (2, secret.vault);
  if (!binding.row ()) throw std::invalid_argument ("Vault secret requires an explicit local Vault binding");
  auto previous= find_vault_secret (secret.group, generation, secret.vault, secret.commitment);
  if (previous) {
    if (previous->handle != secret.handle)
      throw std::invalid_argument ("Protected Vault secret handle cannot be replaced implicitly");
  }
  else {
    statement st (db_, "INSERT INTO vault_secrets VALUES(?,?,?,?,?)");
    st.text (1, secret.group); st.text (2, generation); st.text (3, secret.vault);
    st.text (4, secret.commitment); st.text (5, secret.handle); st.row ();
  }
  tx.commit ();
}
std::optional<vault_secret> client_settings::vault_secret_candidate (const std::string& group,
  const std::string& generation, const std::string& vault) const {
  detail::statement st (db_, "SELECT commitment,key_handle FROM vault_secrets WHERE hodarium=? AND generation=? AND vault=? ORDER BY commitment LIMIT 1");
  st.text (1, group); st.text (2, generation); st.text (3, vault);
  if (!st.row ()) return std::nullopt;
  return vault_secret{st.bytes (1), group, vault, st.bytes (0)};
}
std::optional<std::string> client_settings::vault_secret_commitment (const std::string& group,
  const std::string& generation, const std::string& vault) const {
  detail::statement st (db_, "SELECT commitment,authority FROM vault_secret_authority WHERE hodarium=? AND generation=? AND vault=?");
  st.text (1, group); st.text (2, generation); st.text (3, vault);
  if (!st.row ()) return std::nullopt;
  auto profile= find (group);
  if (!profile || profile->pin.generation != generation || profile->pin.public_key != st.bytes (1))
    throw std::invalid_argument ("Stored Vault registration requires explicit trust re-establishment");
  return st.bytes (0);
}
std::optional<vault_secret_registration> client_settings::accept_vault_registration (
  const std::string& group, const std::string& vault, const std::string& response,
  const std::string& epoch, const std::string& nonce, const std::string& subject) {
  using namespace detail;
  transaction tx (db_);
  auto profile= find (group);
  if (!profile || profile->member.empty ()) throw std::invalid_argument ("Vault registration requires admitted identity");
  statement binding (db_, "SELECT 1 FROM vault_bindings WHERE hodarium=? AND vault=? AND enabled=1");
  binding.text (1, group); binding.text (2, vault);
  if (!binding.row ()) throw std::invalid_argument ("Vault registration requires an enabled local binding");
  auto result= verify_vault_registration (profile->pin, response, epoch, nonce, subject, vault_registration_slot (group, vault));
  auto previous= vault_secret_commitment (group, profile->pin.generation, vault);
  if (previous && (!result || result->commitment != *previous))
    throw std::invalid_argument ("Authority rolled back or replaced the canonical Vault secret");
  if (result && !previous) {
    statement st (db_, "INSERT INTO vault_secret_authority VALUES(?,?,?,?,?,?)");
    st.text (1, group); st.text (2, profile->pin.generation); st.text (3, vault);
    st.text (4, result->commitment); st.text (5, profile->pin.public_key); st.text (6, response); st.row ();
  }
  tx.commit ();
  return result;
}
} // namespace athena::hodarium
