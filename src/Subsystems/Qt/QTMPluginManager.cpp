/******************************************************************************
* MODULE     : QTMPluginManager.cpp
* DESCRIPTION: Launch-scoped AUDMAP plugin IPC with asynchronous package management
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#include "QTMPluginManager.hpp"
#include "identity.hpp"
#include "subscription.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QUuid>
#include <algorithm>
#include <csignal>
#include <deque>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>

using namespace athena::interop;
using namespace athena::plugins;
namespace fs = std::filesystem;
namespace {
value policy_json (const QTMPluginPolicy& p) {
  value jail= value::array ();
  for (const auto& grant: p.jailGrants) jail.push_back (grant);
  return {{"version", 2}, {"startup", static_cast<int> (p.startup)},
    {"delay_seconds", p.delaySeconds}, {"trust", static_cast<int> (p.trust)},
    {"jail_grants", std::move (jail)}, {"audmap_grants", p.audmapGrants}};
}
QTMPluginPolicy policy_from_json (const value& v, const manifest& manifest) {
  QTMPluginPolicy p;
  for (const char* field: {"startup", "trust", "delay_seconds"})
    if (!v.at (field).is_number_integer () || v.at (field) < 0 || v.at (field) > 86400)
      throw std::invalid_argument ("Plugin policy modes and delay must be bounded integers");
  const int startup = v.at ("startup").get<int> ();
  const int trust = v.at ("trust").get<int> ();
  if (startup < 0 || startup > 2 ||
      trust < static_cast<int> (trust_mode::full_access) || trust > static_cast<int> (trust_mode::confirm_requests))
    throw std::invalid_argument ("Invalid plugin policy mode");
  p.startup = static_cast<QTMPluginPolicy::Startup> (startup);
  p.trust = static_cast<trust_mode> (trust);
  p.delaySeconds = v.at ("delay_seconds").get<int> ();
  if (p.delaySeconds < 1 || p.delaySeconds > 86400) throw std::invalid_argument ("Delay must be 1 through 86400 seconds");
  std::set<std::string> requested_jail;
  for (const auto& request: manifest.jail_permissions)
    requested_jail.insert (jail_permission_id (request));
  std::map<std::string, std::set<std::string>> requested_audmap;
  for (const auto& request: manifest.audmap_permissions)
    requested_audmap[request.resource]= request.actions;

  if (v.value ("version", 1) == 2) {
    if (!v.contains ("jail_grants") || !v.at ("jail_grants").is_array () ||
        !v.contains ("audmap_grants") || !v.at ("audmap_grants").is_object ())
      throw std::invalid_argument ("Invalid plugin policy grant sets");
    for (const auto& grant: v.at ("jail_grants")) {
      if (!grant.is_string ()) throw std::invalid_argument ("Invalid jail grant");
      const auto id= grant.get<std::string> ();
      if (requested_jail.count (id)) p.jailGrants.insert (id);
    }
    const auto stored= v.at ("audmap_grants").get<std::map<std::string, std::set<std::string>>> ();
    for (const auto& [resource, actions]: stored) {
      auto requested= requested_audmap.find (resource);
      if (requested == requested_audmap.end ()) continue;
      for (const auto& action: actions)
        if (requested->second.count (action)) p.audmapGrants[resource].insert (action);
    }
  }
  else if (v.contains ("access")) {
    const int access= v.at ("access").get<int> ();
    if (access < 0 || access > 2) throw std::invalid_argument ("Invalid legacy plugin access mode");
    std::map<std::string, std::set<std::string>> commands;
    if (v.contains ("commands"))
      commands= v.at ("commands").get<std::map<std::string, std::set<std::string>>> ();
    for (const auto& [resource, actions]: requested_audmap) {
      for (const auto& action: actions) {
        bool grant= access == 1;
        if (access == 0) grant= action == "resolve" || action == "get" || action == "inspect";
        if (access == 2) {
          auto found= commands.find (resource);
          if (found == commands.end ()) found= commands.find ("*");
          grant= action == "resolve" || (found != commands.end () && found->second.count (action));
        }
        if (grant) p.audmapGrants[resource].insert (action);
      }
    }
  }
  return p;
}
value read_settings (const fs::path& path) {
  QFile file (QString::fromStdString (path.string ()));
  if (!file.exists ()) return value::object ();
  if (QFileInfo (file).isSymLink () || file.size () > 1024 * 1024 || !file.open (QIODevice::ReadOnly))
    throw std::runtime_error ("Cannot read plugin settings");
  auto json = value::parse (file.readAll ().toStdString ());
  if (!json.is_object ()) throw std::runtime_error ("Invalid plugin settings");
  return json;
}
void write_private_json (const fs::path& path, const value& data) {
  const auto bytes= data.dump (2);
  QSaveFile file (QString::fromStdString (path.string ()));
  file.setDirectWriteFallback (false);
  if (!file.open (QIODevice::WriteOnly) ||
      !file.setPermissions (QFile::ReadOwner | QFile::WriteOwner) ||
      file.write (bytes.data (), static_cast<qint64> (bytes.size ())) !=
        static_cast<qint64> (bytes.size ()) || !file.commit ())
    throw std::runtime_error ("Cannot write private plugin launch file");
}
void save_setting (const fs::path& path, const std::string& id, const value& policy) {
  QLockFile lock (QString::fromStdString (path.string () + ".lock"));
  if (!lock.tryLock (0)) throw std::runtime_error ("Plugin settings are being changed by another ATHENA instance");
  auto json = read_settings (path);
  if (policy.is_null ()) json.erase (id); else json[id] = policy;
  const auto bytes = json.dump (2);
  if (bytes.size () > 1024 * 1024) throw std::runtime_error ("Plugin settings exceed size limit");
  QSaveFile file (QString::fromStdString (path.string ()));
  file.setDirectWriteFallback (false);
  if (!file.open (QIODevice::WriteOnly) || !file.setPermissions (QFile::ReadOwner | QFile::WriteOwner) ||
      file.write (bytes.data (), bytes.size ()) != static_cast<qint64> (bytes.size ()) || !file.commit ())
    throw std::runtime_error ("Cannot save plugin settings");
}
} // namespace

struct QTMPluginManager::impl {
  struct entry {
    QTMPluginInfo info;
    fs::path directory;
    std::unique_ptr<QProcess> process;
    std::unique_ptr<QTemporaryDir> identity;
    std::shared_ptr<subscription> channel;
    std::string key;
    std::uint64_t generation = 0, schedule = 0;
    qint64 group = 0;
    bool stopping = false, restart = false, force = false;
    bool userLaunchPending = false, restartUserInitiated = false;
    bool launchFailureReported = false;
  };
  struct inbox {
    std::mutex mutex;
    std::deque<std::function<void ()>> events;
  };
  QTMPluginManager* owner;
  fs::path home, endpoint, settings;
  package_store packages;
  std::shared_ptr<const resolver_registry> registry;
  std::function<void (std::string)> revoke;
  std::function<std::optional<fs::path> ()> currentVaultRoot;
  std::map<std::string, std::unique_ptr<entry>> entries;
  std::shared_ptr<prepared_plugin> pendingInstall;
  std::map<std::string, std::pair<std::string, std::uint64_t>> identities;
  std::map<std::string, std::uint64_t> deferred_starts;
  std::shared_ptr<inbox> incoming = std::make_shared<inbox> ();
  resolution_workers jobs {1};
  std::uint64_t serial = 0;
  bool busy = false, closing = false, dirty = false;
  std::optional<fs::path> observedVaultRoot;
  QTimer timer;

  impl (QTMPluginManager* owner, fs::path home, fs::path endpoint,
        std::shared_ptr<const resolver_registry> registry, std::function<void (std::string)> revoke,
        std::function<std::optional<fs::path> ()> currentVaultRoot):
    owner (owner), home (std::move (home)), endpoint (std::move (endpoint)),
    settings (this->home / "plugins" / ".settings.json"), packages (this->home / "plugins"),
    registry (std::move (registry)), revoke (std::move (revoke)),
    currentVaultRoot (std::move (currentVaultRoot)) {
    observedVaultRoot= this->currentVaultRoot ? this->currentVaultRoot () : std::optional<fs::path> {};
    const auto policies = read_settings (settings);
    for (const auto& path: fs::directory_iterator (packages.directory ())) {
      const auto id = path.path ().filename ().string ();
      if (!valid_plugin_id (id)) continue;
      auto e = std::make_unique<entry> ();
      e->directory = path.path (); e->info.manifest.id = id; e->info.manifest.name = id;
      e->info.state = "Stopped";
      try {
        if (!fs::is_directory (path.symlink_status ())) throw std::runtime_error ("Plugin directory is not a regular directory");
        e->info.manifest = read_manifest (path.path ());
        if (e->info.manifest.id != id) throw std::runtime_error ("Installed plugin ID does not match its directory");
        if (policies.contains (id)) e->info.policy = policy_from_json (policies.at (id), e->info.manifest);
      }
      catch (const std::exception& ex) { e->info.error = QString::fromUtf8 (ex.what ()); e->info.state = "Invalid"; }
      entries.emplace (id, std::move (e));
    }
    QObject::connect (&timer, &QTimer::timeout, owner, [this] {
      std::deque<std::function<void ()>> events;
      { std::lock_guard<std::mutex> lock (incoming->mutex); events.swap (incoming->events); }
      for (auto& event: events) event ();
      if (!busy) {
        auto ready = std::move (deferred_starts); deferred_starts.clear ();
        for (const auto& [id, token]: ready) launch_scheduled (id, token);
      }
      bool changed = dirty; dirty = false;
      for (auto& [id, e]: entries) if (e->channel) {
        const auto replies = e->channel->take_responses ();
        if (!replies.empty ()) { e->info.lastResult = replies.back (); changed = true; }
      }
      const auto vault_now= this->currentVaultRoot ? this->currentVaultRoot () :
                                                    std::optional<fs::path> {};
      if (vault_now != observedVaultRoot) {
        observedVaultRoot= vault_now;
        for (auto& [id, e]: entries) {
          if (!needs_vault (e->info.manifest, e->info.policy)) continue;
          if (e->info.running) {
            const bool restart_after= e->info.policy.startup != QTMPluginPolicy::Startup::Manual;
            this->owner->stop (id);
            e->restart= restart_after;
          }
          else schedule (id);
        }
      }
      if (changed) emit this->owner->changed ();
    });
    timer.start (100);
    for (auto& [id, e]: entries) schedule (id);
  }
  void check () const { Q_ASSERT (QThread::currentThread () == owner->thread ()); }
  entry& at (const std::string& id) {
    check ();
    auto found = entries.find (id);
    if (found == entries.end ()) throw std::invalid_argument ("Unknown plugin");
    return *found->second;
  }
  void schedule (const std::string& id) {
    auto& e = at (id);
    const auto token = e.schedule = ++serial;
    if (e.info.state == "Invalid" || e.info.running) return;
    if (e.info.policy.startup == QTMPluginPolicy::Startup::Manual) {
      if (e.info.state == "Scheduled") e.info.state = "Stopped";
      return;
    }
    const int ms = e.info.policy.startup == QTMPluginPolicy::Startup::Delayed ? e.info.policy.delaySeconds * 1000 : 0;
    e.info.state = ms ? "Scheduled" : "Stopped";
    QTimer::singleShot (ms, owner, [this, id, token] { launch_scheduled (id, token); });
  }
  void launch_scheduled (const std::string& id, std::uint64_t token) {
    auto found = entries.find (id);
    if (closing || found == entries.end () || found->second->schedule != token || found->second->info.running) return;
    if (busy) { deferred_starts[id] = token; return; }
    try { owner->start (id, false); }
    catch (const std::exception& ex) {
      found->second->info.error = QString::fromUtf8 (ex.what ()); found->second->info.state = "Failed";
      emit owner->changed ();
    }
  }
  void report_launch_failure (entry& e) {
    if (!e.userLaunchPending || e.launchFailureReported) return;
    e.launchFailureReported = true;
    emit owner->launchFailed (
      QString::fromUtf8 (e.info.manifest.name.data (), e.info.manifest.name.size ()),
      e.info.error);
  }
  void invalidate (entry& e) {
    if (e.channel) e.channel->close ();
    if (!e.key.empty ()) revoke (e.key);
    e.info.connected = false;
  }
  void signal_group (entry& e, int signal) {
    if (e.group > 1) ::kill (-static_cast<pid_t> (e.group), signal);
    if (e.process && e.process->state () != QProcess::NotRunning) {
      if (signal == SIGKILL) e.process->kill (); else e.process->terminate ();
    }
  }
  void finished (const std::string& id, std::uint64_t generation, int code, QProcess::ExitStatus status) {
    auto found = entries.find (id);
    if (found == entries.end () || found->second->generation != generation) return;
    auto& e = *found->second;
    if (e.process) {
      e.info.log += QString::fromUtf8 (e.process->readAllStandardOutput ());
      e.info.log = e.info.log.right (65536);
    }
    invalidate (e);
    // A plugin must not leave its worker children behind when its leader exits.
    signal_group (e, SIGKILL); e.group = 0;
    e.info.running = false; e.info.pid = 0;
    const bool restart = e.restart;
    const bool restartUserInitiated = e.restartUserInitiated;
    e.restart = false; e.restartUserInitiated = false;
    const bool failedDuringManualLaunch= e.userLaunchPending && !e.info.connected && !e.stopping;
    if (e.stopping || (!failedDuringManualLaunch && code == 0 && status == QProcess::NormalExit)) {
      e.info.state = "Stopped"; e.info.error.clear ();
    }
    else {
      e.info.state = "Failed";
      e.info.error = failedDuringManualLaunch && code == 0 && status == QProcess::NormalExit ?
        QStringLiteral ("Process exited before connecting to ATHENA") :
        QString ("Process exited: %1").arg (code);
      const QStringList lines= e.info.log.trimmed ().split ('\n', Qt::SkipEmptyParts);
      if (!lines.isEmpty ()) e.info.error += "\n" + lines.back ().trimmed ();
      report_launch_failure (e);
    }
    e.stopping = false;
    emit owner->changed ();
    if (restart && !closing) QTimer::singleShot (0, owner, [this, id, restartUserInitiated] {
      try { owner->start (id, restartUserInitiated); }
      catch (const std::exception& ex) {
        const QString error= QString::fromUtf8 (ex.what ());
        if (restartUserInitiated) {
          auto& e= at (id);
          e.info.state= "Failed"; e.info.error= error;
          emit owner->changed ();
          emit owner->launchFailed (
            QString::fromUtf8 (e.info.manifest.name.data (), e.info.manifest.name.size ()), error);
        }
        else emit owner->managementFinished (error);
      }
    });
  }
  template<class Job> void background (Job job) {
    if (busy) throw std::runtime_error ("Another plugin management operation is in progress");
    busy = true; emit owner->changed ();
    std::weak_ptr<inbox> weak = incoming;
    if (!jobs.post ([weak, job = std::move (job)] () mutable {
      auto done = job ();
      if (auto queue = weak.lock ()) { std::lock_guard<std::mutex> lock (queue->mutex); queue->events.push_back (std::move (done)); }
    })) { busy = false; throw std::runtime_error ("Plugin management queue is full"); }
  }
  ~impl () {
    closing = true; timer.stop (); incoming.reset ();
    for (auto& [id, e]: entries) {
      if (e->process) QObject::disconnect (e->process.get (), nullptr, owner, nullptr);
      invalidate (*e); signal_group (*e, SIGTERM);
    }
    // Shutdown only: bounded per-process grace, then group termination/reaping.
    for (auto& [id, e]: entries) if (e->process && e->info.running) {
      if (!e->process->waitForFinished (100)) signal_group (*e, SIGKILL);
      e->process->waitForFinished (1000);
      signal_group (*e, SIGKILL);
    }
  }
  bool needs_vault (const manifest& m, const QTMPluginPolicy& p) const {
    return std::any_of (m.jail_permissions.begin (), m.jail_permissions.end (),
      [&] (const jail_permission& permission) {
        return permission.permission != jail_permission_kind::network &&
          (permission.required || p.jailGrants.count (jail_permission_id (permission)));
      });
  }
  void validate_policy (const manifest& m, const QTMPluginPolicy& p) const {
    std::set<std::string> requested_jail;
    for (const auto& request: m.jail_permissions) requested_jail.insert (jail_permission_id (request));
    for (const auto& grant: p.jailGrants)
      if (!requested_jail.count (grant)) throw std::invalid_argument ("Jail grant is not requested by the manifest");
    std::map<std::string, std::set<std::string>> requested;
    for (const auto& permission: m.audmap_permissions) requested[permission.resource]= permission.actions;
    for (const auto& [resource, actions]: p.audmapGrants) {
      auto found= requested.find (resource);
      if (found == requested.end ()) throw std::invalid_argument ("AUDMAP resource grant is not requested by the manifest");
      for (const auto& action: actions)
        if (!found->second.count (action)) throw std::invalid_argument ("AUDMAP action grant is not requested by the manifest");
    }
  }
  void require_permissions (const manifest& m, const QTMPluginPolicy& p) const {
    validate_policy (m, p);
    for (const auto& request: m.jail_permissions)
      if (request.required && !p.jailGrants.count (jail_permission_id (request)))
        throw std::runtime_error ("Plugin is missing a required system permission");
    for (const auto& request: m.audmap_permissions) if (request.required) {
      auto found= p.audmapGrants.find (request.resource);
      for (const auto& action: request.actions)
        if (found == p.audmapGrants.end () || !found->second.count (action))
          throw std::runtime_error ("Plugin is missing a required ATHENA permission");
    }
    if (needs_vault (m, p) && (!currentVaultRoot || !currentVaultRoot ()))
      throw std::runtime_error ("Plugin requires access to the current vault, but no vault is open");
  }
  value sandbox_plan (entry& e, const fs::path& data, const fs::path& identity_file) const {
    value mounts= value::array ();
    bool network= false;
    for (const auto& permission: e.info.manifest.jail_permissions) {
      if (!e.info.policy.jailGrants.count (jail_permission_id (permission))) continue;
      if (permission.permission == jail_permission_kind::network) { network= true; continue; }
      mounts.push_back ({{"path", permission.path.generic_string ()},
        {"scope", permission.scope == filesystem_scope::file ? "file" : "tree"},
        {"writable", permission.permission == jail_permission_kind::filesystem_write}});
    }
    value arguments= value::array ();
    for (const auto& argument: e.info.manifest.arguments) arguments.push_back (argument);
    value plan {{"version", 1}, {"plugin_dir", fs::absolute (e.directory).string ()},
      {"data_dir", fs::absolute (data).string ()},
      {"identity_file", fs::absolute (identity_file).string ()},
      {"connection_file", fs::absolute (endpoint).string ()},
      {"audmap_socket", fs::absolute (endpoint.parent_path () / "socket").string ()},
      {"plugin_id", e.info.manifest.id}, {"subscription_guid", e.channel->guid ()},
      {"executable", e.info.manifest.executable.generic_string ()},
      {"arguments", std::move (arguments)}, {"network", network},
      {"vault_access", std::move (mounts)}};
    if (needs_vault (e.info.manifest, e.info.policy)) {
      const auto root= currentVaultRoot ? currentVaultRoot () : std::optional<fs::path> {};
      if (!root) throw std::runtime_error ("Current vault disappeared before plugin launch");
      plan["vault_root"]= fs::absolute (*root).string ();
    }
    return plan;
  }
};

QTMPluginManager::QTMPluginManager (fs::path home, fs::path endpoint,
    std::shared_ptr<const resolver_registry> registry, std::function<void (std::string)> revoke,
    std::function<std::optional<fs::path> ()> currentVaultRoot, QObject* parent):
  QObject (parent), implementation (std::make_unique<impl> (this, std::move (home), std::move (endpoint),
    std::move (registry), std::move (revoke), std::move (currentVaultRoot))) {}
QTMPluginManager::~QTMPluginManager () = default;
std::vector<QTMPluginInfo> QTMPluginManager::plugins () const {
  implementation->check ();
  std::vector<QTMPluginInfo> result;
  for (const auto& [id, e]: implementation->entries) result.push_back (e->info);
  return result;
}
bool QTMPluginManager::busy () const { implementation->check (); return implementation->busy; }
void QTMPluginManager::start (const std::string& id, bool userInitiated) {
  auto& s = *implementation; auto& e = s.at (id);
  if (s.busy) throw std::runtime_error ("Plugin installation or removal is in progress");
  if (e.info.running) return;
  if (e.info.state == "Invalid") throw std::runtime_error (e.info.error.toStdString ());
  auto metadata = read_manifest (e.directory);
  if (metadata.id != id) throw std::runtime_error ("Installed plugin identity changed");
  e.info.manifest = std::move (metadata);
  s.require_permissions (e.info.manifest, e.info.policy);
  const QString helper= QDir (QCoreApplication::applicationDirPath ()).filePath ("athena-plugin-sandbox");
  if (!QFileInfo::exists (helper) || !QFileInfo (helper).isExecutable ())
    throw std::runtime_error ("ATHENA plugin sandbox helper is unavailable");
  e.schedule = ++s.serial; e.generation = ++s.serial;
  e.userLaunchPending = userInitiated;
  e.launchFailureReported = false;
  e.identity = std::make_unique<QTemporaryDir> ();
  if (!e.identity->isValid ()) throw std::runtime_error ("Cannot create private plugin launch identity");
  const auto keyfile = e.identity->filePath ("identity.json").toStdString ();
  const auto identity = load_client_identity (keyfile);
  e.key = identity.public_key;
  e.channel = std::make_shared<subscription> (QUuid::createUuid ().toString (QUuid::WithoutBraces).toStdString (), id);
  const auto data = s.home / "plugins-data" / id;
  fs::create_directories (data); fs::permissions (data, fs::perms::owner_all);
  const fs::path policy_file= fs::path (e.identity->path ().toStdString ()) / "sandbox.json";
  write_private_json (policy_file, s.sandbox_plan (e, data, keyfile));
  s.identities[e.key] = {id, e.generation};
  e.process = std::make_unique<QProcess> ();
  auto* process = e.process.get ();
  process->setUnixProcessParameters (QProcess::UnixProcessFlag::CreateNewSession |
    QProcess::UnixProcessFlag::CloseFileDescriptors | QProcess::UnixProcessFlag::ResetSignalHandlers);
  process->setProcessEnvironment (QProcessEnvironment::systemEnvironment ());
  process->setProgram (helper);
  process->setArguments ({"--policy", QString::fromStdString (policy_file.string ())});
  process->setWorkingDirectory (e.identity->path ());
  process->setProcessChannelMode (QProcess::MergedChannels);
  const auto generation = e.generation;
  connect (process, &QProcess::started, this, [this, id, generation] {
    auto& s = *implementation; auto& e = s.at (id);
    if (e.generation != generation) return;
    e.group = e.process->processId (); e.info.pid = e.group;
    if (e.stopping) s.signal_group (e, e.force ? SIGKILL : SIGTERM); else e.info.state = "Running";
    emit changed ();
  });
  connect (process, &QProcess::readyReadStandardOutput, this, [this, id, generation] {
    auto& e = implementation->at (id);
    if (e.generation != generation) return;
    e.info.log += QString::fromUtf8 (e.process->readAllStandardOutput ());
    e.info.log = e.info.log.right (65536);
    implementation->dirty = true;
  });
  connect (process, &QProcess::finished, this, [this, id, generation] (int code, QProcess::ExitStatus status) {
    implementation->finished (id, generation, code, status);
  });
  connect (process, &QProcess::errorOccurred, this, [this, id, generation] (QProcess::ProcessError error) {
    auto& s = *implementation; auto& e = s.at (id);
    if (e.generation != generation) return;
    if (!e.stopping) e.info.error = e.process->errorString ();
    if (error == QProcess::FailedToStart) {
      s.invalidate (e); e.info.running = false; e.info.state = "Failed"; e.info.pid = 0; e.restart = false;
      s.report_launch_failure (e);
    }
    emit changed ();
  });
  e.info.error.clear (); e.info.lastResult = nullptr; e.stopping = false; e.restart = false; e.force = false;
  e.info.running = true; e.info.connected = false; e.info.state = "Starting";
  process->start (); emit changed ();
}
void QTMPluginManager::stop (const std::string& id, bool force) {
  auto& s = *implementation; auto& e = s.at (id);
  e.schedule = ++s.serial; e.restart = false;
  if (!e.info.running) { e.info.state = "Stopped"; emit changed (); return; }
  e.stopping = true; e.force = force; e.info.state = "Stopping"; s.invalidate (e);
  s.signal_group (e, force ? SIGKILL : SIGTERM);
  const auto generation = e.generation;
  QTimer::singleShot (2000, this, [this, id, generation] {
    auto& s = *implementation; auto found = s.entries.find (id);
    if (found != s.entries.end () && found->second->generation == generation && found->second->stopping)
      s.signal_group (*found->second, SIGKILL);
  });
  emit changed ();
}
void QTMPluginManager::restart (const std::string& id, bool userInitiated) {
  if (!implementation->at (id).info.running) { start (id, userInitiated); return; }
  stop (id);
  implementation->at (id).restart = true;
  implementation->at (id).restartUserInitiated = userInitiated;
}
std::uint64_t QTMPluginManager::command (const std::string& id, const std::string& name) {
  auto& e = implementation->at (id);
  if (!e.info.running || e.stopping || !e.channel) throw std::runtime_error ("Plugin is not running");
  for (const auto& c: e.info.manifest.commands) if (c.id == name) return e.channel->enqueue (c.id, c.parameters);
  throw std::invalid_argument ("Unknown manifest command");
}
bool QTMPluginManager::authorize (const std::string& key, std::optional<connection_grant>& grant, std::string* display_name) {
  auto& s = *implementation; s.check (); grant.reset ();
  const auto known = s.identities.find (key);
  if (known == s.identities.end ()) return false;
  const auto found = s.entries.find (known->second.first);
  if (found == s.entries.end ()) return true;
  auto& e = *found->second;
  if (display_name) *display_name = e.info.manifest.name + " [" + e.info.manifest.id + "]";
  if (!e.info.running || e.stopping || e.key != key || e.generation != known->second.second) return true;
  connection_grant policy (e.info.policy.trust);
  auto registry = std::make_shared<resolver_registry> (*s.registry);
  registry->push_back (subscription_resolver (e.channel));
  policy.registry = registry;
  capability_mask deny;
  deny.enforced= true; deny.resolve= false;
  policy.capabilities["*"]= deny;
  for (const auto& request: e.info.manifest.audmap_permissions) {
    capability_mask mask;
    mask.enforced= true; mask.resolve= false;
    auto granted= e.info.policy.audmapGrants.find (request.resource);
    if (granted != e.info.policy.audmapGrants.end ()) {
      mask.resolve= granted->second.count ("resolve") != 0;
      for (const auto& action: granted->second)
        if (action != "resolve") mask.commands.insert (action);
    }
    policy.capabilities[request.resource]= std::move (mask);
  }
  capability_mask subscription_mask;
  subscription_mask.enforced= true; subscription_mask.resolve= true;
  subscription_mask.commands= {"get", "reply", "inspect"};
  subscription_mask.confirmation_required= false;
  policy.capabilities["subscription"]= std::move (subscription_mask);
  grant = std::move (policy);
  e.info.connected = true;
  e.userLaunchPending = false;
  emit changed ();
  return true;
}
void QTMPluginManager::disconnected (const std::string& key) {
  auto& s = *implementation; s.check ();
  const auto known = s.identities.find (key);
  if (known == s.identities.end ()) return;
  auto found = s.entries.find (known->second.first);
  if (found != s.entries.end () && found->second->key == key && found->second->generation == known->second.second) {
    found->second->info.connected = false; emit changed ();
  }
}
void QTMPluginManager::configure (const std::string& id, const QTMPluginPolicy& policy) {
  auto& s = *implementation; auto& e = s.at (id);
  if (s.busy) throw std::runtime_error ("Plugin management operation is in progress");
  s.validate_policy (e.info.manifest, policy);
  const auto validated = policy_from_json (policy_json (policy), e.info.manifest);
  save_setting (s.settings, id, policy_json (validated));
  const bool running = e.info.running;
  e.info.policy = validated;
  // Retire the old grant; do not leave a live process using the previous policy.
  if (running) stop (id);
  else s.schedule (id);
  emit changed ();
}
void QTMPluginManager::install (const fs::path& source) {
  auto& s = *implementation; s.check ();
  if (s.pendingInstall) throw std::runtime_error ("A plugin is already waiting for installation review");
  const auto store = s.packages.directory (), settings = s.settings;
  s.background ([this, source, store, settings] {
    (void) settings;
    QString error; std::shared_ptr<prepared_plugin> prepared;
    try {
      prepared= std::make_shared<prepared_plugin> (package_store (store).prepare (source));
    }
    catch (const std::exception& e) { error = QString::fromUtf8 (e.what ()); }
    return [this, prepared = std::move (prepared), error] {
      auto& s = *implementation;
      if (!prepared) {
        s.busy = false;
        emit changed (); emit managementFinished (error);
        return;
      }
      s.pendingInstall= prepared;
      // Keep management busy until review is accepted or cancelled so another
      // package operation cannot invalidate the private staging directory.
      s.busy= true;
      emit changed (); emit installPrepared ();
    };
  });
}

std::optional<QTMPluginPendingInstall> QTMPluginManager::pendingInstall () const {
  auto& s= *implementation; s.check ();
  if (!s.pendingInstall) return {};
  return QTMPluginPendingInstall {s.pendingInstall->metadata,
                                  s.pendingInstall->license_file ()};
}

void QTMPluginManager::acceptInstall (const QTMPluginPolicy& policy) {
  auto& s= *implementation; s.check ();
  if (!s.pendingInstall) throw std::runtime_error ("No plugin is waiting for installation review");
  s.validate_policy (s.pendingInstall->metadata, policy);
  const auto validated= policy_from_json (policy_json (policy), s.pendingInstall->metadata);
  auto prepared= std::move (s.pendingInstall);
  const auto store= s.packages.directory (), settings= s.settings;
  s.busy= false;
  s.background ([this, prepared = std::move (prepared), validated, store, settings] {
    QString error; std::optional<installed_plugin> installed;
    try {
      const std::string id= prepared->metadata.id;
      save_setting (settings, id, policy_json (validated));
      try { installed= package_store (store).publish (std::move (*prepared)); }
      catch (...) { save_setting (settings, id, nullptr); throw; }
    }
    catch (const std::exception& e) { error= QString::fromUtf8 (e.what ()); }
    return [this, installed = std::move (installed), validated, error] {
      auto& s= *implementation; s.busy= false;
      if (installed) {
        auto e= std::make_unique<impl::entry> ();
        e->info.manifest= installed->metadata; e->info.policy= validated;
        e->directory= installed->directory; e->info.state= "Stopped";
        s.entries[installed->metadata.id]= std::move (e);
      }
      emit changed (); emit managementFinished (error);
    };
  });
}

void QTMPluginManager::cancelInstall () {
  auto& s= *implementation; s.check ();
  if (!s.pendingInstall) return;
  s.pendingInstall.reset ();
  s.busy= false;
  emit changed (); emit managementFinished (QString ());
}
void QTMPluginManager::uninstall (const std::string& id) {
  auto& s = *implementation; auto& e = s.at (id);
  if (e.info.running) throw std::runtime_error ("Stop the plugin before uninstalling");
  e.schedule = ++s.serial;
  const auto store = s.packages.directory (), settings = s.settings;
  s.background ([this, id, store, settings] {
    QString error; bool removed = false;
    try { package_store (store).uninstall (id); removed = true; save_setting (settings, id, nullptr); }
    catch (const std::exception& e) { error = QString::fromUtf8 (e.what ()); }
    return [this, id, error, removed] {
      implementation->busy = false;
      if (removed) implementation->entries.erase (id);
      emit changed (); emit managementFinished (error);
    };
  });
}
