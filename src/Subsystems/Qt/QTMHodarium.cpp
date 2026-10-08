/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#include "QTMHodarium.hpp"
#include "QTMSystemPowerMonitor.hpp"
#include "ATHENA/Hodarium/profile_session.hpp"
#include "ATHENA/Hodarium/control_http.hpp"
#include "qt_utilities.hpp"
#include "tm_ostream.hpp"
#include <QApplication>
#include <QFileInfo>
#include <QPointer>
#include <QThread>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QDateTime>
#include <QSysInfo>
#include <QDesktopServices>
#include <filesystem>
#include <map>

namespace {
using namespace athena::hodarium;
struct profile_row { client_profile profile; profile_status status; };
using rows= std::vector<profile_row>;

class worker: public QObject {
public:
  std::unique_ptr<client_settings> settings;
  std::map<std::string, std::unique_ptr<profile_session>> sessions;
  std::function<void (profile_status)> report;
  std::filesystem::path root;
  std::unique_ptr<control_http> discovery;

  void add_session (const client_profile& profile) {
    auto group= profile.pin.group;
    auto session= std::make_unique<profile_session> (*settings, profile,
      root / ("membership-" + group + ".sqlite"), report);
    auto* active= session.get ();
    sessions.emplace (group, std::move (session));
    active->start ();
  }
  rows snapshot () {
    rows result;
    if (settings) for (const auto& profile: settings->profiles ()) {
      auto session= sessions.find (profile.pin.group);
      if (session != sessions.end ()) result.push_back ({profile, session->second->status ()});
    }
    return result;
  }
  void join (QUrl origin, std::string name) {
    if (!settings) throw std::runtime_error ("Hodarium settings unavailable");
    if (discovery) throw std::runtime_error ("Authority discovery already in progress");
    if (name.empty () || name.size () > 128)
      throw std::invalid_argument ("Device name must be 1-128 UTF-8 bytes");
    discovery= std::make_unique<control_http> (origin, nullptr);
    discover_authority (*discovery, [this, origin, name] (control_result result, authority_description info) {
      // Destroy the transport outside its reply callback.
      discovery.release ()->deleteLater ();
      try {
        if (result.failure != control_failure::none) throw std::runtime_error (result.diagnostic);
        if (!info.initialized) throw std::runtime_error ("Authority administrator setup is incomplete");
        if (settings->find (info.pin.group))
          throw std::runtime_error ("Hodarium already configured; resume its existing membership instead");
        client_profile profile{origin, info.pin, info.recovery_public_key,
          create_device_identity (), name, "", false};
        settings->add_pending (profile);
        add_session (profile);
        sessions.at (profile.pin.group)->join ();
      }
      catch (const std::exception& e) { report ({"", profile_phase::error, e.what (), "", 0}); }
    });
  }

  void start (const std::filesystem::path& root) {
    try {
      this->root= root;
      std::filesystem::create_directories (root);
      std::filesystem::permissions (root, std::filesystem::perms::owner_all);
      settings= std::make_unique<client_settings> (root / "device.sqlite");
      for (auto& profile: settings->profiles ()) add_session (profile);
    }
    catch (const std::exception& e) {
      report ({"", profile_phase::error, e.what (), "", 0});
    }
  }
  void suspended (bool value) {
    if (value) discovery.reset ();
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
  void stop () { discovery.reset (); sessions.clear (); settings.reset (); }
};

class service: public QObject {
  QThread thread_;
  worker* worker_;
  QTMSystemPowerMonitor power_;
  bool stopping_= false;
  bool suspended_= false;
public:
  QPointer<QObject> observer;
  std::function<void (QString)> changed;
  explicit service (const std::filesystem::path& root): QObject (qApp),
    worker_ (new worker) {
    worker_->moveToThread (&thread_);
    worker_->report= [this] (profile_status status) {
      QMetaObject::invokeMethod (this, [this, status= std::move (status)] {
        if (observer && changed) changed (QString::fromStdString (status.diagnostic));
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
  void perform (std::function<void (worker&)> operation) {
    if (stopping_ || suspended_) return;
    QMetaObject::invokeMethod (worker_, [w= worker_, operation= std::move (operation)] {
      try { operation (*w); }
      catch (const std::exception& e) { w->report ({"", profile_phase::error, e.what (), "", 0}); }
    }, Qt::QueuedConnection);
  }
  void inspect (QObject* context, std::function<void (rows)> completed) {
    if (stopping_) { completed ({}); return; }
    QPointer<QObject> receiver= context;
    QMetaObject::invokeMethod (worker_, [this, receiver, completed= std::move (completed)] {
      rows state;
      try { state= worker_->snapshot (); }
      catch (const std::exception& e) { worker_->report ({"", profile_phase::error, e.what (), "", 0}); }
      QMetaObject::invokeMethod (this, [receiver, completed, state= std::move (state)] {
        if (receiver) completed (state);
      }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
  }
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
QString phase_name (profile_phase phase) {
  switch (phase) {
    case profile_phase::disabled: return QObject::tr ("Paused");
    case profile_phase::pending: return QObject::tr ("Awaiting admission");
    case profile_phase::validating: return QObject::tr ("Validating membership");
    case profile_phase::authorized: return QObject::tr ("Membership valid");
    case profile_phase::offline_valid: return QObject::tr ("Offline membership valid");
    case profile_phase::expired: return QObject::tr ("Membership expired");
    case profile_phase::denied: return QObject::tr ("Membership denied");
    case profile_phase::error: return QObject::tr ("Error");
    case profile_phase::suspended: return QObject::tr ("Suspended");
  }
  return {};
}
class manager: public QDialog {
  QPointer<service> service_;
  QTableWidget* table_;
  QLabel* diagnostic_;
  QPushButton* resume_;
  QPushButton* pause_;
  QPushButton* admin_;
  rows rows_;
  bool inspecting_= false;
  void refresh () {
    if (!service_ || inspecting_) return;
    inspecting_= true;
    service_->inspect (this, [this] (rows state) {
      inspecting_= false;
      std::string selected;
      int row= table_->currentRow ();
      if (row >= 0 && row < int (rows_.size ())) selected= rows_[row].profile.pin.group;
      rows_= std::move (state);
      table_->setRowCount (int (rows_.size ()));
      for (int i= 0; i < int (rows_.size ()); ++i) {
        const auto& r= rows_[i];
        QString code= QString::fromStdString (r.status.code);
        if (r.status.code_expires <= QDateTime::currentSecsSinceEpoch ()) code.clear ();
        QStringList cells{r.profile.origin.toString (), QString::fromStdString (r.profile.name),
          phase_name (r.status.phase), code};
        for (int j= 0; j < cells.size (); ++j) {
          auto* item= new QTableWidgetItem (cells[j]);
          item->setToolTip (QString::fromStdString (r.status.diagnostic));
          table_->setItem (i, j, item);
        }
        if (r.profile.pin.group == selected) table_->selectRow (i);
      }
      if (selected.empty () && !rows_.empty ()) table_->selectRow (0);
      selection ();
    });
  }
  void selection () {
    int i= table_->currentRow ();
    bool valid= i >= 0 && i < int (rows_.size ());
    resume_->setEnabled (valid); admin_->setEnabled (valid);
    pause_->setEnabled (valid && rows_[i].profile.enabled);
  }
  void action (bool enable) {
    int i= table_->currentRow ();
    if (!service_ || i < 0 || i >= int (rows_.size ())) return;
    auto profile= rows_[i].profile;
    service_->perform ([profile, enable] (worker& w) {
      auto& session= *w.sessions.at (profile.pin.group);
      if (enable && profile.member.empty ()) session.join ();
      else session.set_enabled (enable);
    });
  }
public:
  explicit manager (service* controller): service_ (controller) {
    setAttribute (Qt::WA_DeleteOnClose);
    setWindowTitle (tr ("ATHENA Hodarium")); resize (850, 450);
    auto* layout= new QVBoxLayout (this);
    auto* form= new QFormLayout;
    auto* origin= new QLineEdit (this);
    origin->setPlaceholderText ("https://");
    auto* name= new QLineEdit (QSysInfo::machineHostName (), this);
    form->addRow (tr ("Server"), origin); form->addRow (tr ("Device name"), name);
    layout->addLayout (form);
    auto* join= new QPushButton (tr ("Request admission"), this);
    layout->addWidget (join, 0, Qt::AlignLeft);
    table_= new QTableWidget (0, 4, this);
    table_->setHorizontalHeaderLabels ({tr ("Server"), tr ("Device"), tr ("Membership"), tr ("Admission code")});
    table_->horizontalHeader ()->setSectionResizeMode (QHeaderView::ResizeToContents);
    table_->horizontalHeader ()->setStretchLastSection (true);
    table_->setSelectionBehavior (QAbstractItemView::SelectRows);
    table_->setSelectionMode (QAbstractItemView::SingleSelection);
    table_->setEditTriggers (QAbstractItemView::NoEditTriggers);
    layout->addWidget (table_);
    diagnostic_= new QLabel (this); diagnostic_->setWordWrap (true);
    diagnostic_->setTextInteractionFlags (Qt::TextSelectableByMouse);
    layout->addWidget (diagnostic_);
    auto* buttons= new QDialogButtonBox (QDialogButtonBox::Close, this);
    resume_= buttons->addButton (tr ("Resume / retry"), QDialogButtonBox::ActionRole);
    pause_= buttons->addButton (tr ("Pause"), QDialogButtonBox::ActionRole);
    admin_= buttons->addButton (tr ("Administration"), QDialogButtonBox::ActionRole);
    layout->addWidget (buttons);
    connect (buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
    connect (table_, &QTableWidget::itemSelectionChanged, this, [this] { selection (); });
    connect (resume_, &QPushButton::clicked, this, [this] { action (true); });
    connect (pause_, &QPushButton::clicked, this, [this] { action (false); });
    connect (admin_, &QPushButton::clicked, this, [this] {
      int i= table_->currentRow ();
      if (i >= 0 && i < int (rows_.size ())) QDesktopServices::openUrl (rows_[i].profile.origin);
    });
    connect (join, &QPushButton::clicked, this, [this, origin, name] {
      if (!service_) return;
      auto address= QUrl (origin->text ().trimmed (), QUrl::StrictMode);
      if (address.path () == "/") address.setPath ({});
      auto device= name->text ().trimmed ().toUtf8 ().toStdString ();
      diagnostic_->clear ();
      service_->perform ([address, device] (worker& w) { w.join (address, device); });
    });
    controller->observer= this;
    controller->changed= [this] (QString diagnostic) {
      if (!diagnostic.isEmpty ()) diagnostic_->setText (diagnostic);
      refresh ();
    };
    auto* timer= new QTimer (this); timer->setInterval (1000);
    connect (timer, &QTimer::timeout, this, [this] {
      for (int i= 0; i < int (rows_.size ()); ++i)
        if (rows_[i].status.code_expires <= QDateTime::currentSecsSinceEpoch ())
          table_->item (i, 3)->setText ({});
    });
    timer->start (); selection (); refresh ();
  }
};
QPointer<manager> active_manager;
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
  delete active_manager.data ();
  delete active_service.data ();
  qtm_hodarium_initialize ();
}
void qtm_hodarium_show () {
  if (active_manager) { active_manager->show (); active_manager->raise (); active_manager->activateWindow (); return; }
  if (!active_service) {
    const QString home= qEnvironmentVariable ("ATHENA_HOME_PATH");
    if (home.isEmpty ()) { std_error << "Hodarium: application storage is unavailable" << LF; return; }
    active_service= new service (std::filesystem::path ((home + "/system/hodarium").toUtf8 ().constData ()));
  }
  active_manager= new manager (active_service);
  active_manager->show ();
}
