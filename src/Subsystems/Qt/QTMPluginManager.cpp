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
  return {{"startup", static_cast<int> (p.startup)}, {"access", static_cast<int> (p.access)},
    {"delay_seconds", p.delaySeconds}, {"trust", static_cast<int> (p.trust)}, {"commands", p.commands}};
}
QTMPluginPolicy policy_from_json (const value& v) {
  QTMPluginPolicy p;
  for (const char* field: {"startup", "access", "trust", "delay_seconds"})
    if (!v.at (field).is_number_integer () || v.at (field) < 0 || v.at (field) > 86400)
      throw std::invalid_argument ("Plugin policy modes and delay must be bounded integers");
  const int startup = v.at ("startup").get<int> (), access = v.at ("access").get<int> ();
  const int trust = v.at ("trust").get<int> ();
  if (startup < 0 || startup > 2 || access < 0 || access > 2 ||
      trust < static_cast<int> (trust_mode::full_access) || trust > static_cast<int> (trust_mode::confirm_requests))
    throw std::invalid_argument ("Invalid plugin policy mode");
  p.startup = static_cast<QTMPluginPolicy::Startup> (startup);
  p.access = static_cast<QTMPluginPolicy::Access> (access);
  p.trust = static_cast<trust_mode> (trust);
  p.delaySeconds = v.at ("delay_seconds").get<int> ();
  if (p.delaySeconds < 1 || p.delaySeconds > 86400) throw std::invalid_argument ("Delay must be 1 through 86400 seconds");
  p.commands = v.at ("commands").get<std::map<std::string, std::set<std::string>>> ();
  if (p.commands.size () > 128) throw std::invalid_argument ("Too many permission rules");
  for (const auto& [type, commands]: p.commands) {
    if (type.empty () || type.size () > 128 || commands.size () > 128)
      throw std::invalid_argument ("Invalid permission rule");
    for (const auto& command: commands) if (command.empty () || command.size () > 128)
      throw std::invalid_argument ("Invalid permitted command");
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
  std::map<std::string, std::unique_ptr<entry>> entries;
  std::map<std::string, std::pair<std::string, std::uint64_t>> identities;
  std::map<std::string, std::uint64_t> deferred_starts;
  std::shared_ptr<inbox> incoming = std::make_shared<inbox> ();
  resolution_workers jobs {1};
  std::uint64_t serial = 0;
  bool busy = false, closing = false, dirty = false;
  QTimer timer;

  impl (QTMPluginManager* owner, fs::path home, fs::path endpoint,
        std::shared_ptr<const resolver_registry> registry, std::function<void (std::string)> revoke):
    owner (owner), home (std::move (home)), endpoint (std::move (endpoint)),
    settings (this->home / "plugins" / ".settings.json"), packages (this->home / "plugins"),
    registry (std::move (registry)), revoke (std::move (revoke)) {
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
        if (policies.contains (id)) e->info.policy = policy_from_json (policies.at (id));
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
    try { owner->start (id); }
    catch (const std::exception& ex) {
      found->second->info.error = QString::fromUtf8 (ex.what ()); found->second->info.state = "Failed";
      emit owner->changed ();
    }
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
    invalidate (e);
    // A plugin must not leave its worker children behind when its leader exits.
    signal_group (e, SIGKILL); e.group = 0;
    e.info.running = false; e.info.pid = 0;
    const bool restart = e.restart; e.restart = false;
    if (e.stopping || (code == 0 && status == QProcess::NormalExit)) {
      e.info.state = "Stopped"; e.info.error.clear ();
    }
    else { e.info.state = "Failed"; e.info.error = QString ("Process exited: %1").arg (code); }
    e.stopping = false;
    emit owner->changed ();
    if (restart && !closing) QTimer::singleShot (0, owner, [this, id] {
      try { owner->start (id); }
      catch (const std::exception& ex) { emit owner->managementFinished (QString::fromUtf8 (ex.what ())); }
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
};

QTMPluginManager::QTMPluginManager (fs::path home, fs::path endpoint,
    std::shared_ptr<const resolver_registry> registry, std::function<void (std::string)> revoke, QObject* parent):
  QObject (parent), implementation (std::make_unique<impl> (this, std::move (home), std::move (endpoint),
    std::move (registry), std::move (revoke))) {}
QTMPluginManager::~QTMPluginManager () = default;
std::vector<QTMPluginInfo> QTMPluginManager::plugins () const {
  implementation->check ();
  std::vector<QTMPluginInfo> result;
  for (const auto& [id, e]: implementation->entries) result.push_back (e->info);
  return result;
}
bool QTMPluginManager::busy () const { implementation->check (); return implementation->busy; }
void QTMPluginManager::start (const std::string& id) {
  auto& s = *implementation; auto& e = s.at (id);
  if (s.busy) throw std::runtime_error ("Plugin installation or removal is in progress");
  if (e.info.running) return;
  if (e.info.state == "Invalid") throw std::runtime_error (e.info.error.toStdString ());
  auto metadata = read_manifest (e.directory);
  if (metadata.id != id) throw std::runtime_error ("Installed plugin identity changed");
  e.info.manifest = std::move (metadata);
  e.schedule = ++s.serial; e.generation = ++s.serial;
  e.identity = std::make_unique<QTemporaryDir> ();
  if (!e.identity->isValid ()) throw std::runtime_error ("Cannot create private plugin launch identity");
  const auto keyfile = e.identity->filePath ("identity.json").toStdString ();
  const auto identity = load_client_identity (keyfile);
  e.key = identity.public_key;
  e.channel = std::make_shared<subscription> (QUuid::createUuid ().toString (QUuid::WithoutBraces).toStdString (), id);
  s.identities[e.key] = {id, e.generation};
  const auto data = s.home / "plugins-data" / id;
  fs::create_directories (data); fs::permissions (data, fs::perms::owner_all);
  e.process = std::make_unique<QProcess> ();
  auto* process = e.process.get ();
  process->setUnixProcessParameters (QProcess::UnixProcessFlag::CreateNewSession |
    QProcess::UnixProcessFlag::CloseFileDescriptors | QProcess::UnixProcessFlag::ResetSignalHandlers);
  auto environment = QProcessEnvironment::systemEnvironment ();
  environment.insert ("ATHENA_AUDMAP_ENDPOINT", QString::fromStdString (s.endpoint.string ()));
  environment.insert ("ATHENA_AUDMAP_IDENTITY", QString::fromStdString (keyfile));
  environment.insert ("ATHENA_SUBSCRIPTION_GUID", QString::fromStdString (e.channel->guid ()));
  environment.insert ("ATHENA_PLUGIN_ID", QString::fromStdString (id));
  environment.insert ("ATHENA_PLUGIN_DATA_DIR", QString::fromStdString (data.string ()));
  process->setProcessEnvironment (environment);
  process->setProgram (QString::fromStdString ((e.directory / e.info.manifest.executable).string ()));
  QStringList arguments;
  for (const auto& arg: e.info.manifest.arguments) arguments.push_back (QString::fromStdString (arg));
  process->setArguments (arguments);
  process->setWorkingDirectory (QString::fromStdString (e.directory.string ()));
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
void QTMPluginManager::restart (const std::string& id) {
  if (!implementation->at (id).info.running) { start (id); return; }
  stop (id); implementation->at (id).restart = true;
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
  if (e.info.policy.access == QTMPluginPolicy::Access::ReadOnly)
    policy.capabilities["*"] = {true, {"get", "inspect"}};
  else if (e.info.policy.access == QTMPluginPolicy::Access::Custom) {
    policy.capabilities["*"] = {true, {}};
    for (const auto& [type, commands]: e.info.policy.commands) policy.capabilities[type] = {true, commands};
  }
  policy.capabilities["subscription"] = {true, {"get", "reply", "inspect"}, false};
  grant = std::move (policy); e.info.connected = true; emit changed (); return true;
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
  const auto validated = policy_from_json (policy_json (policy));
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
  const auto store = s.packages.directory (), settings = s.settings;
  s.background ([this, source, store, settings] {
    QString error; std::optional<installed_plugin> installed;
    try {
      installed = package_store (store).install (source);
      try { save_setting (settings, installed->metadata.id, policy_json (QTMPluginPolicy {})); }
      catch (...) { package_store (store).uninstall (installed->metadata.id); installed.reset (); throw; }
    }
    catch (const std::exception& e) { error = QString::fromUtf8 (e.what ()); }
    return [this, installed = std::move (installed), error] {
      auto& s = *implementation; s.busy = false;
      if (installed) {
        auto e = std::make_unique<impl::entry> (); e->info.manifest = installed->metadata;
        e->directory = installed->directory; e->info.state = "Stopped";
        s.entries[installed->metadata.id] = std::move (e);
      }
      emit changed (); emit managementFinished (error);
    };
  });
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
