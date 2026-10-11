/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#include "ATHENA/Data/hodarium_worker.hpp"
#include "ATHENA/Data/vaultfile_json.hpp"
#include "ATHENA/Data/namespaces_schema.hpp"
#include "ATHENA/Data/hodarium_artifact_database.hpp"
#include "ATHENA/Hodarium/device_key_file.hpp"
#include "ATHENA/Hodarium/relay_credentials.hpp"
#include "tree_label.hpp"
#include "boot.hpp"
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QDateTime>
#include <QFile>
#include <QSaveFile>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QTimer>
#include <nlohmann/json.hpp>
#include <sqlite3.h>
#include <csignal>
#include <filesystem>
#include <iostream>
#include <memory>
#include <set>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

namespace {
using namespace athena::hodarium;
namespace fs= std::filesystem;
using json= nlohmann::json;
volatile std::sig_atomic_t stopping= 0;
void stop_signal (int) { stopping= 1; }

class exclusive_lock {
  int fd_= -1;
public:
  explicit exclusive_lock (const fs::path& path) {
    fd_= ::open (path.c_str (), O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd_ < 0) throw std::system_error (errno, std::generic_category (), "Open node lock");
    struct stat state{};
    if (::fstat (fd_, &state) != 0 || !S_ISREG (state.st_mode) ||
        state.st_uid != ::geteuid () || (state.st_mode & 077) != 0 ||
        ::flock (fd_, LOCK_EX | LOCK_NB) != 0) {
      ::close (fd_); fd_= -1;
      throw std::runtime_error ("Node state or Vault is locked or has unsafe lock permissions: " + path.string ());
    }
  }
  ~exclusive_lock () { if (fd_ >= 0) ::close (fd_); }
  exclusive_lock (const exclusive_lock&)= delete;
  exclusive_lock& operator= (const exclusive_lock&)= delete;
};

bool contains (const fs::path& parent, const fs::path& child) {
  auto relative= child.lexically_relative (parent);
  return !relative.empty () && !relative.is_absolute () && *relative.begin () != "..";
}
void separate_state (const fs::path& state, const fs::path& vault) {
  if (contains (state, vault) || contains (vault, state))
    throw std::invalid_argument ("Private node state and synchronized Vault must be separate directories");
}
const char* phase_name (profile_phase phase) {
  switch (phase) {
    case profile_phase::disabled: return "disabled";
    case profile_phase::pending: return "pending";
    case profile_phase::validating: return "validating";
    case profile_phase::authorized: return "authorized";
    case profile_phase::offline_valid: return "offline-valid";
    case profile_phase::expired: return "expired";
    case profile_phase::denied: return "denied";
    case profile_phase::error: return "error";
    case profile_phase::suspended: return "suspended";
  }
  throw std::logic_error ("Unknown membership phase");
}
json status_value (const profile_status& value) {
  json result{{"group", value.group}, {"phase", phase_name (value.phase)},
    {"diagnostic", value.diagnostic}, {"discovery_diagnostic", value.discovery_diagnostic},
    {"decision_diagnostic", value.decision_diagnostic}, {"routes", json::array ()}};
  for (const auto& route: value.routes)
    result["routes"].push_back ({{"member", route.member}, {"route", route.route},
      {"established", route.established}, {"rtt_ms", route.roundtrip_ms},
      {"bytes_per_second", route.transfer_bytes_per_second}});
  return result;
}
void write_status (const fs::path& path, const json& status) {
  QSaveFile file (QString::fromStdString (path.string ()));
  if (!file.open (QIODevice::WriteOnly)) throw std::runtime_error ("Cannot write node status");
  const auto bytes= status.dump () + "\n";
  if (file.write (bytes.data (), qint64 (bytes.size ())) != qint64 (bytes.size ()) || !file.commit ())
    throw std::runtime_error ("Cannot publish node status");
}
void initialize_vault (const fs::path& root) {
  if (fs::exists (root) && !fs::is_empty (root))
    throw std::invalid_argument ("init-vault requires an empty directory");
  fs::create_directories (root / ".athena");
  AthenaVaultfileInfo info;
  info.name= root.filename ().string ();
  info.node_model_version= 1;
  info.namespace_db_path= ".athena/namespaces.sqlite";
  info.artifacts_path= ".athena/artifacts.sqlite";
  info.enunciations_path= ".athena/enunciations.sqlite";
  info.bold_text_path= ".athena/bold-text.sqlite";
  info.rag_index_path= ".athena/rag.sqlite";
  info.materials_db_path= ".athena/materials.sqlite";
  sqlite3* database= nullptr;
  std::string error;
  if (!athena_namespace_database_open (root / info.namespace_db_path, true, database, error))
    throw std::runtime_error (error);
  sqlite3_close (database);
  if (!athena_vaultfile_write (root, info, error)) throw std::runtime_error (error);
}
}

int main (int argc, char** argv) {
  ::umask (0077);
  headless_mode= true;
  QCoreApplication application (argc, argv);
  QCoreApplication::setApplicationName ("athena-hodarium-node");
  QCoreApplication::setApplicationVersion (ATHENA_APP_VERSION);
  QCommandLineParser parser;
  parser.setApplicationDescription ("Persistent headless ATHENA Hodarium member");
  parser.addHelpOption (); parser.addVersionOption ();
  parser.addPositionalArgument ("command", "serve, enroll, status, init-vault, bind, unbind, enable, disable, relay-add, relay-remove, conflicts");
  parser.addOptions ({
    {{"s", "state"}, "Private persistent node state directory (required)", "directory"},
    {"server", "Hodarium authority HTTPS origin", "url"},
    {"ca-file", "Additional trusted PEM CA certificates for private HTTPS services", "path"},
    {"name", "Name for this independently enrolled device", "name"},
    {"group", "Hodarium group identifier", "id"},
    {"vault", "Shared Vault UUID", "uuid"},
    {"root", "Local Vault directory", "directory"},
    {"relay", "Relay HTTPS origin", "url"},
    {"token-file", "Read Relay access token from a private file", "path"},
    {"allow-code-resources", "Replicate style/sorter files as inert data; never execute them"}
  });
  parser.process (application);
  const auto arguments= parser.positionalArguments ();
  if (arguments.size () != 1 || !parser.isSet ("state")) parser.showHelp (2);
  auto required= [&] (const char* name) {
    auto value= parser.value (name).toStdString ();
    if (value.empty ()) throw std::invalid_argument (std::string ("Missing --") + name);
    return value;
  };
  try {
    init_standard_tree_labels ();
    if (parser.isSet ("ca-file")) {
      QFile file (QString::fromStdString (required ("ca-file")));
      if (!file.open (QIODevice::ReadOnly)) throw std::runtime_error ("Cannot read trusted CA file");
      auto certificates= QSslCertificate::fromData (file.readAll (), QSsl::Pem);
      if (certificates.empty ()) throw std::invalid_argument ("CA file contains no certificates");
      auto configuration= QSslConfiguration::defaultConfiguration ();
      auto authorities= configuration.caCertificates ();
      authorities.append (certificates);
      configuration.setCaCertificates (authorities);
      QSslConfiguration::setDefaultConfiguration (configuration);
    }
    const auto command= arguments.front ().toStdString ();
    const auto state= fs::absolute (fs::u8path (required ("state"))).lexically_normal ();
    if (command == "status") {
      QFile file (QString::fromStdString ((state / "status.json").string ()));
      if (!file.open (QIODevice::ReadOnly)) throw std::runtime_error ("No daemon status is available");
      const auto status= json::parse (file.readAll ().toStdString ());
      std::cout << status.dump (2) << '\n';
      return status.value ("running", false) &&
        QDateTime::currentSecsSinceEpoch () - status.at ("timestamp").get<std::int64_t> () < 15 ? 0 : 1;
    }
    fs::create_directories (state);
    struct stat permissions{};
    if (::lstat (state.c_str (), &permissions) != 0 || !S_ISDIR (permissions.st_mode) ||
        permissions.st_uid != ::geteuid () || (permissions.st_mode & 077) != 0)
      throw std::runtime_error ("Node state must be an owned private directory with mode 0700");
    exclusive_lock state_lock (state / "node.lock");
    fs::create_directories (state / "keys");
    configure_private_key_directory ((state / "keys").string ());
    if (command == "init-vault") {
      auto root= fs::weakly_canonical (fs::absolute (fs::u8path (required ("root"))));
      separate_state (fs::canonical (state), root);
      initialize_vault (root);
      std::cout << "Initialized " << root << '\n';
      return 0;
    }
    if (command != "serve" && command != "enroll") {
      client_settings settings (state / "device.sqlite");
      const auto group= required ("group");
      auto profile= settings.find (group);
      if (!profile) throw std::invalid_argument ("Unknown Hodarium group");
      if (command == "bind") {
        const auto root= fs::canonical (fs::u8path (required ("root")));
        separate_state (fs::canonical (state), root);
        AthenaVaultfileInfo info; std::string error;
        if (!athena_vaultfile_read (root, info, error)) throw std::invalid_argument (error);
        if (info.node_model_version != 1) throw std::invalid_argument ("Vault must use the native node model");
        settings.bind_vault ({group, required ("vault"), root, true, parser.isSet ("allow-code-resources")});
      }
      else if (command == "unbind") settings.unbind_vault (group, required ("vault"));
      else if (command == "enable" || command == "disable") settings.set_enabled (group, command == "enable");
      else if (command == "relay-add") {
        const QUrl origin (QString::fromStdString (required ("relay")));
        struct token_descriptor {
          int fd;
          ~token_descriptor () { if (fd >= 0) ::close (fd); }
        } descriptor{::open (required ("token-file").c_str (),
          O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK)};
        if (descriptor.fd < 0) throw std::runtime_error ("Cannot open Relay token file");
        struct stat permissions{};
        if (::fstat (descriptor.fd, &permissions) != 0 || !S_ISREG (permissions.st_mode) ||
            permissions.st_uid != ::geteuid () || (permissions.st_mode & 07777) != 0600 ||
            permissions.st_size < 0 || permissions.st_size > 256)
          throw std::runtime_error ("Relay token file must be an owned regular file with mode 0600");
        QFile token;
        if (!token.open (descriptor.fd, QIODevice::ReadOnly, QFileDevice::DontCloseHandle))
          throw std::runtime_error ("Cannot read Relay token file");
        auto bytes= token.read (257);
        if (token.error () != QFileDevice::NoError || bytes.size () > 256) {
          bytes.fill ('\0');
          throw std::runtime_error ("Cannot read bounded Relay token file");
        }
        bytes= bytes.trimmed ();
        const auto handle= store_relay_token (origin, bytes.toStdString ());
        bytes.fill ('\0');
        settings.set_relay ({group, origin, handle});
      }
      else if (command == "relay-remove") settings.remove_relay (group, QUrl (QString::fromStdString (required ("relay"))));
      else if (command == "conflicts") {
        revision_store journal (settings.revision_database_path (state, group));
        std::string cursor;
        for (;;) {
          auto entries= journal.conflicts (required ("vault"), cursor, 64);
          if (entries.empty ()) break;
          for (const auto& entry: entries) {
            std::cout << json ({{"object", entry.object}, {"path", entry.representative_path},
              {"branches", entry.heads}}).dump () << '\n';
            cursor= entry.object;
          }
        }
      }
      else throw std::invalid_argument ("Unknown command: " + command);
      return 0;
    }
    std::vector<std::unique_ptr<exclusive_lock>> vault_locks;
    std::set<std::string> existing_groups;
    {
      client_settings settings (state / "device.sqlite");
      for (const auto& profile: settings.profiles ()) {
        existing_groups.insert (profile.pin.group);
        for (const auto& binding: settings.vaults (profile.pin.group)) {
          const auto root= fs::canonical (binding.root);
          separate_state (fs::canonical (state), root);
          fs::create_directories (root / ".athena");
          vault_locks.push_back (std::make_unique<exclusive_lock> (root / ".athena/hodarium-node.lock"));
        }
      }
    }
    std::recursive_mutex publication;
    sync_worker worker;
    worker.application_hooks.mode= document_application_hooks::ownership::exclusive_vault;
    worker.application_hooks.publication_mutex= &publication;
    worker.application_hooks.rename_artifacts= [] (const document_path_change& change, std::string& error) {
      return athena_artifacts_apply_path_rename (change.root, change.old_path, change.new_path, false, error);
    };
    bool enrollment_done= false, enrollment_failed= false, enrollment_started= false;
    std::string enrollment_group= command == "enroll" && parser.isSet ("group") ? required ("group") : "";
    bool starting= false, startup_failed= false;
    std::string startup_error;
    worker.report= [&] (profile_status status) {
      json message= status_value (status);
      message["timestamp"]= QDateTime::currentSecsSinceEpoch ();
      if (command == "enroll" && !status.code.empty ()) {
        message["admission_code"]= status.code;
        message["code_expires"]= status.code_expires;
      }
      std::cout << message.dump () << std::endl;
      if (starting) {
        if (status.phase == profile_phase::error) {
          if (!startup_failed) startup_error= status.diagnostic;
          startup_failed= true;
        }
        return;
      }
      if (command != "enroll" || !enrollment_started || enrollment_failed) return;
      if (status.group.empty () && status.phase == profile_phase::error) {
        enrollment_failed= true;
        QTimer::singleShot (0, &application, [&] { application.exit (1); });
        return;
      }
      // Discovery creates one new profile; reports from startup profiles cannot
      // complete a new enrollment, even if those profiles become authorized.
      if (enrollment_group.empty () && !status.group.empty () && !existing_groups.count (status.group))
        enrollment_group= status.group;
      if (!enrollment_group.empty () && status.group == enrollment_group &&
          status.phase == profile_phase::authorized) {
        enrollment_done= true;
        QTimer::singleShot (0, &application, [&] { application.exit (enrollment_failed ? 1 : 0); });
      }
    };
    worker.identity_conflict= [] (vault_binding binding, source_inventory::identity_conflict conflict) {
      std::cerr << json ({{"error", "duplicate-source-identity"}, {"vault", binding.vault},
        {"uuid", conflict.object}, {"paths", conflict.paths}}).dump () << std::endl;
    };
    struct stop_worker {
      sync_worker& owner;
      ~stop_worker () { owner.stop (); }
    } cleanup{worker};
    starting= true;
    worker.start (state);
    starting= false;
    if (startup_failed) throw std::runtime_error ("Node startup failed: " + startup_error);
    if (!worker.settings) throw std::runtime_error ("Node startup failed");
    if (command == "enroll") {
      enrollment_started= true;
      if (parser.isSet ("group")) {
        auto found= worker.sessions.find (enrollment_group);
        if (found == worker.sessions.end ()) throw std::invalid_argument ("Unknown pending group");
        found->second->join ();
      }
      else worker.join (QUrl (QString::fromStdString (required ("server"))), required ("name"));
    }
    auto snapshot= [&] (bool running) {
      json result{{"running", running}, {"timestamp", QDateTime::currentSecsSinceEpoch ()},
        {"profiles", json::array ()}};
      for (const auto& row: worker.snapshot ()) {
        auto value= status_value (row.status);
        value["member"]= row.profile.member;
        value["vaults"]= json::array ();
        for (const auto& binding: row.vaults) {
          json vault{{"id", binding.vault}, {"root", binding.root.string ()}, {"enabled", binding.enabled}};
          const auto track= worker.inventories.find ({binding.group, binding.vault});
          if (track != worker.inventories.end ()) {
            vault["inventoried"]= track->second->inventoried;
            vault["application_error"]= track->second->application_error;
            vault["recovering"]= track->second->recovery_baseline_pending;
          }
          const auto journal= worker.journals.find (binding.group);
          if (journal != worker.journals.end ()) {
            vault["has_conflicts"]= !journal->second->conflicts (binding.vault, {}, 1).empty ();
            vault["has_pending_application"]= !journal->second->pending_applications (binding.vault, {}, 1).empty () ||
              !journal->second->application_candidates (binding.vault, {}, 1).empty ();
          }
          value["vaults"].push_back (std::move (vault));
        }
        result["profiles"].push_back (std::move (value));
      }
      write_status (state / "status.json", result);
    };
    std::signal (SIGINT, stop_signal); std::signal (SIGTERM, stop_signal);
    QTimer stop_poll, heartbeat;
    QObject::connect (&stop_poll, &QTimer::timeout, &application, [&] { if (stopping) application.quit (); });
    QObject::connect (&heartbeat, &QTimer::timeout, &application, [&] {
      try { snapshot (true); }
      catch (const std::exception& e) { std::cerr << e.what () << std::endl; application.exit (1); }
    });
    stop_poll.start (200); heartbeat.start (5000); snapshot (true);
    const auto result= application.exec ();
    snapshot (false);
    return command == "enroll" && (!enrollment_done || enrollment_failed) ? 1 : result;
  }
  catch (const std::exception& e) {
    std::cerr << json ({{"error", e.what ()}}).dump () << std::endl;
    return 1;
  }
}
