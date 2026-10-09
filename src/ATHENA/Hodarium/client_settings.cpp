/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#include "client_settings.hpp"
#include "control_http.hpp"
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
    if (version > 2) throw std::runtime_error ("Unsupported Hodarium settings version");
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
  tx.commit ();
}
void client_settings::complete_admission (const std::string& group,
  const std::string& handle, const std::string& public_key, const std::string& member) {
  using namespace detail;
  identifier (member);
  transaction tx (db_);
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
  detail::statement st (db_, "SELECT vault,root,enabled FROM vault_bindings WHERE hodarium=? ORDER BY vault");
  st.text (1, group);
  std::vector<vault_binding> result;
  while (st.row ()) result.push_back ({group, st.bytes (0), std::filesystem::u8path (st.bytes (1)),
    sqlite3_column_int (st.value, 2) != 0});
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
  statement st (db_, "INSERT INTO vault_bindings VALUES(?,?,?,?)");
  st.text (1, binding.group); st.text (2, binding.vault); st.text (3, root.u8string ());
  check (db_, sqlite3_bind_int (st.value, 4, binding.enabled)); st.row (); tx.commit ();
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
} // namespace athena::hodarium
