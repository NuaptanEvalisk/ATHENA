/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#include "QTMHodarium.hpp"
#include "QTMSystemPowerMonitor.hpp"
#include "ATHENA/Hodarium/profile_session.hpp"
#include "qt_utilities.hpp"
#include "tm_ostream.hpp"
#include <QApplication>
#include <QFileInfo>
#include <QPointer>
#include <QThread>
#include <filesystem>
#include <map>

namespace {
using namespace athena::hodarium;

class worker: public QObject {
public:
  std::unique_ptr<client_settings> settings;
  std::map<std::string, std::unique_ptr<profile_session>> sessions;
  std::function<void (profile_status)> report;

  void start (const std::filesystem::path& root) {
    try {
      settings= std::make_unique<client_settings> (root / "device.sqlite");
      for (auto& profile: settings->profiles ()) {
        auto group= profile.pin.group;
        auto session= std::make_unique<profile_session> (*settings, profile,
          root / ("membership-" + group + ".sqlite"), report);
        auto* active= session.get ();
        sessions.emplace (group, std::move (session));
        active->start ();
      }
    }
    catch (const std::exception& e) {
      report ({"", profile_phase::error, e.what (), "", 0});
    }
  }
  void suspended (bool value) {
    for (const auto& entry: sessions) {
      try {
        if (value) entry.second->suspend ();
        else entry.second->resume ();
      }
      catch (const std::exception& e) {
        report ({entry.first, profile_phase::error, e.what (), "", 0});
      }
    }
  }
  void stop () { sessions.clear (); settings.reset (); }
};

class service: public QObject {
  QThread thread_;
  worker* worker_;
  QTMSystemPowerMonitor power_;
  bool stopping_= false;
  bool suspended_= false;
public:
  explicit service (const std::filesystem::path& root): QObject (qApp),
    worker_ (new worker) {
    worker_->moveToThread (&thread_);
    worker_->report= [this] (profile_status status) {
      QMetaObject::invokeMethod (this, [this, status= std::move (status)] {
        if (stopping_ || suspended_ || status.diagnostic.empty ()) return;
        if (status.phase == profile_phase::error || status.phase == profile_phase::denied)
          std_error << "Hodarium: " << string (status.diagnostic.c_str ()) << LF;
        else if (status.phase == profile_phase::expired || status.phase == profile_phase::offline_valid)
          std_warning << "Hodarium: " << string (status.diagnostic.c_str ()) << LF;
      }, Qt::QueuedConnection);
    };
    connect (&thread_, &QThread::finished, worker_, &QObject::deleteLater);
    connect (&thread_, &QThread::started, worker_, [w= worker_, root] { w->start (root); });
    connect (qApp, &QCoreApplication::aboutToQuit, this, [this] { stop (); });
    connect (qApp, &QGuiApplication::applicationStateChanged, this,
      [this] (Qt::ApplicationState state) {
#if defined(Q_OS_IOS)
        suspended (state != Qt::ApplicationActive);
#else
        if (state == Qt::ApplicationSuspended) suspended (true);
        else if (state == Qt::ApplicationActive) suspended (false);
#endif
      });
    connect (&power_, &QTMSystemPowerMonitor::sleepChanged, this,
      [this] (bool asleep) { suspended (asleep); });
    thread_.setObjectName ("Hodarium control");
    thread_.start ();
  }
  ~service () override { stop (); }
  void suspended (bool value) {
    if (stopping_ || suspended_ == value) return;
    suspended_= value;
    if (value) cancel_device_key_operations ();
    QMetaObject::invokeMethod (worker_, [w= worker_, value] {
      w->suspended (value);
    }, Qt::QueuedConnection);
  }
  void stop () {
    if (stopping_) return;
    stopping_= true;
    cancel_device_key_operations ();
    QMetaObject::invokeMethod (worker_, [w= worker_] { w->stop (); }, Qt::BlockingQueuedConnection);
    thread_.quit (); thread_.wait ();
  }
};
QPointer<service> active_service;
}

void qtm_hodarium_initialize () {
  if (active_service) return;
  const QString home= qEnvironmentVariable ("ATHENA_HOME_PATH");
  if (home.isEmpty ()) return;
  const QString root= home + "/system/hodarium";
  // No database creation, secret-store access or network activity before setup.
  if (!QFileInfo::exists (root + "/device.sqlite")) return;
  active_service= new service (std::filesystem::path (root.toUtf8 ().constData ()));
}
void qtm_hodarium_reload () {
  delete active_service.data ();
  qtm_hodarium_initialize ();
}
