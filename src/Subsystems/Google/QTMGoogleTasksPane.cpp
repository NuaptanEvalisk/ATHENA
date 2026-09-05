/******************************************************************************
* MODULE     : QTMGoogleTasksPane.cpp
* DESCRIPTION: Qt ADS pane for Google Tasks
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "QTMGoogleTasksPane.hpp"

#include "GoogleOAuth.hpp"
#include "GoogleCloudTodo.hpp"
#include "QTMMainTabWindow.hpp"
#include "QTMToast.hpp"
#include "boot.hpp"
#include "qt_utilities.hpp"

#include <DockAreaWidget.h>
#include <DockSplitter.h>
#include <DockWidget.h>
#include <QApplication>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QSet>
#include <QSharedPointer>
#include <QSignalBlocker>
#include <QSizeGrip>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

static QTMGoogleTasksPane* google_tasks_widget= nullptr;
static ads::CDockWidget* google_tasks_dock= nullptr;
static QTimer* google_tasks_refresh_timer= nullptr;
static bool google_tasks_refresh_scheduled= false;
static bool google_tasks_connection_toast_shown= false;
static bool google_tasks_snapshot_ready= false;
static bool google_tasks_monitor_running= false;
static QSet<QString> google_tasks_known_ids;

namespace {

static void
set_google_tasks_area_width (ads::CDockWidget* dock) {
  if (dock == nullptr || dock->isInFloatingContainer ()) return;
  ads::CDockAreaWidget* area= dock->dockAreaWidget ();
  if (area == nullptr) return;
  ads::CDockSplitter* splitter= area->parentSplitter ();
  if (splitter == nullptr) return;
  QList<int> sizes= splitter->sizes ();
  if (sizes.size () < 2) return;
  int total= 0;
  for (int size: sizes) total += size;
  if (total <= 0) return;
  sizes[sizes.size () - 1]= qMax (360, qMin (520, total / 3));
  splitter->setSizes (sizes);
}

static void
update_google_tasks_floating_state (ads::CDockWidget* dock, bool floating) {
  if (google_tasks_widget == nullptr) return;
  google_tasks_widget->setFloatingResizeGripVisible (floating);
  if (floating && dock != nullptr) dock->resize (520, 680);
}

static void
google_tasks_toast (const QString& title, const QString& body) {
  qtm_show_toast (from_qstring (body), from_qstring (title));
}

struct GoogleTaskMonitorState {
  int pending= 0;
  int newCount= 0;
  QString firstNewTitle;
  QSet<QString> seen;
};

static void
google_tasks_monitor_active_tasks () {
  if (google_tasks_monitor_running ||
      GoogleOAuth::instance ().clientId ().trimmed ().isEmpty () ||
      !GoogleOAuth::instance ().hasRefreshToken ())
    return;

  google_tasks_monitor_running= true;
  GoogleTasksClient::instance ().listTaskLists (
    [] (const QVector<GoogleTaskList>& lists, const QString& error) {
      if (!error.isEmpty () || lists.isEmpty ()) {
        google_tasks_monitor_running= false;
        if (!google_tasks_snapshot_ready) google_tasks_snapshot_ready= true;
        return;
      }

      QSharedPointer<GoogleTaskMonitorState> state (
        new GoogleTaskMonitorState);
      state->pending= lists.size ();
      for (const GoogleTaskList& list: lists) {
        GoogleTasksClient::instance ().listTasks (
          list.id, false,
          [state] (const QVector<GoogleTask>& tasks, const QString&) {
            for (const GoogleTask& task: tasks) {
              if (task.id.isEmpty ()) continue;
              state->seen.insert (task.id);
              if (google_tasks_snapshot_ready &&
                  !google_tasks_known_ids.contains (task.id)) {
                state->newCount++;
                if (state->firstNewTitle.isEmpty ())
                  state->firstNewTitle= task.title;
              }
            }

            state->pending--;
            if (state->pending > 0) return;

            google_tasks_known_ids= state->seen;
            google_tasks_monitor_running= false;
            if (!google_tasks_snapshot_ready) {
              google_tasks_snapshot_ready= true;
              return;
            }

            if (state->newCount == 1)
              google_tasks_toast (
                "Google Tasks", "New task: " + state->firstNewTitle);
            else if (state->newCount > 1)
              google_tasks_toast (
                "Google Tasks",
                QString ("%1 new tasks").arg (state->newCount));
          });
      }
    });
}

} // namespace

QTMGoogleTasksPane::QTMGoogleTasksPane (QWidget* parent)
  : QWidget (parent), refreshRunning (false) {
  QVBoxLayout* outer= new QVBoxLayout (this);
  outer->setContentsMargins (14, 12, 14, 12);
  outer->setSpacing (10);

  statusLabel= new QLabel (this);
  statusLabel->setWordWrap (true);
  outer->addWidget (statusLabel);

  QHBoxLayout* authRow= new QHBoxLayout;
  connectButton= new QPushButton ("Connect", this);
  disconnectButton= new QPushButton ("Disconnect", this);
  authRow->addWidget (connectButton);
  authRow->addWidget (disconnectButton);
  authRow->addStretch (1);
  outer->addLayout (authRow);

  QHBoxLayout* listRow= new QHBoxLayout;
  taskListCombo= new QComboBox (this);
  taskListCombo->setSizePolicy (QSizePolicy::Expanding,
                                QSizePolicy::Preferred);
  refreshButton= new QPushButton ("Refresh", this);
  listRow->addWidget (new QLabel ("List:", this));
  listRow->addWidget (taskListCombo, 1);
  listRow->addWidget (refreshButton);
  outer->addLayout (listRow);

  showCompletedCheck= new QCheckBox ("Show completed tasks", this);
  outer->addWidget (showCompletedCheck);

  taskTree= new QTreeWidget (this);
  taskTree->setColumnCount (3);
  taskTree->setHeaderLabels (QStringList () << "Task" << "Status" << "Due");
  taskTree->header ()->setStretchLastSection (false);
  taskTree->header ()->setSectionResizeMode (0, QHeaderView::Stretch);
  taskTree->header ()->setSectionResizeMode (1, QHeaderView::ResizeToContents);
  taskTree->header ()->setSectionResizeMode (2, QHeaderView::ResizeToContents);
  taskTree->setRootIsDecorated (false);
  taskTree->setAlternatingRowColors (true);
  outer->addWidget (taskTree, 1);

  QHBoxLayout* actionRow= new QHBoxLayout;
  newButton= new QPushButton ("New task", this);
  completeButton= new QPushButton ("Complete", this);
  floatingSizeGrip= new QSizeGrip (this);
  floatingSizeGrip->setVisible (false);
  actionRow->addWidget (newButton);
  actionRow->addWidget (completeButton);
  actionRow->addStretch (1);
  actionRow->addWidget (floatingSizeGrip);
  outer->addLayout (actionRow);

  connect (connectButton, &QPushButton::clicked,
           [this] () { connectGoogle (); });
  connect (disconnectButton, &QPushButton::clicked,
           [this] () { disconnectGoogle (); });
  connect (refreshButton, &QPushButton::clicked,
           [this] () { refreshLists (false); });
  connect (showCompletedCheck, &QCheckBox::toggled,
           [this] () { refreshTasks (false); });
  connect (taskListCombo,
           static_cast<void (QComboBox::*) (int)> (
             &QComboBox::currentIndexChanged),
           [this] (int) { refreshTasks (false); });
  connect (newButton, &QPushButton::clicked, [this] () { newTask (); });
  connect (completeButton, &QPushButton::clicked,
           [this] () { completeSelectedTask (); });

  updateConnectionStatus ();
}

QSize
QTMGoogleTasksPane::sizeHint () const {
  return QSize (500, 680);
}

void
QTMGoogleTasksPane::setFloatingResizeGripVisible (bool visible) {
  if (floatingSizeGrip != nullptr) floatingSizeGrip->setVisible (visible);
}

void
QTMGoogleTasksPane::refreshNow (bool automatic) {
  refreshLists (automatic);
}

void
QTMGoogleTasksPane::setBusy (bool busy, const QString& status) {
  connectButton->setEnabled (!busy);
  disconnectButton->setEnabled (!busy);
  refreshButton->setEnabled (!busy);
  newButton->setEnabled (!busy);
  completeButton->setEnabled (!busy);
  taskListCombo->setEnabled (!busy);
  showCompletedCheck->setEnabled (!busy);
  setStatus (status);
}

void
QTMGoogleTasksPane::setStatus (const QString& status) {
  statusLabel->setText (status);
}

void
QTMGoogleTasksPane::updateConnectionStatus () {
  if (GoogleOAuth::instance ().clientId ().trimmed ().isEmpty ())
    setStatus ("Set a Google OAuth desktop client ID in Preferences -> Other -> Connectivity.");
  else if (GoogleOAuth::instance ().hasRefreshToken ())
    setStatus ("Connected to Google Tasks.");
  else
    setStatus ("Google Tasks is not connected.");
}

void
QTMGoogleTasksPane::connectGoogle () {
  setBusy (true, "Opening browser for Google authorization...");
  GoogleOAuth::instance ().authorizeTasks (this, [this] (bool ok,
                                                        const QString& msg) {
    setBusy (false, msg);
    google_tasks_toast ("Google Tasks", msg);
    if (ok) {
      google_tasks_snapshot_ready= false;
      google_tasks_known_ids.clear ();
    }
    if (ok) refreshLists (false);
  });
}

void
QTMGoogleTasksPane::disconnectGoogle () {
  GoogleOAuth::instance ().forgetTokens ();
  google_tasks_snapshot_ready= false;
  google_tasks_known_ids.clear ();
  taskListCombo->clear ();
  taskTree->clear ();
  updateConnectionStatus ();
}

QString
QTMGoogleTasksPane::selectedListId () const {
  int index= taskListCombo->currentIndex ();
  if (index < 0) return QString ();
  return taskListCombo->itemData (index).toString ();
}

QString
QTMGoogleTasksPane::selectedTaskId () const {
  QTreeWidgetItem* item= taskTree->currentItem ();
  if (item == nullptr) return QString ();
  return item->data (0, Qt::UserRole).toString ();
}

void
QTMGoogleTasksPane::refreshLists (bool automatic) {
  if (refreshRunning) return;
  if (GoogleOAuth::instance ().clientId ().trimmed ().isEmpty () ||
      !GoogleOAuth::instance ().hasRefreshToken ()) {
    if (!automatic) updateConnectionStatus ();
    return;
  }
  refreshRunning= true;
  if (!automatic) setBusy (true, "Loading Google task lists...");
  QPointer<QTMGoogleTasksPane> self (this);
  GoogleTasksClient::instance ().listTaskLists (
    [self, automatic] (const QVector<GoogleTaskList>& lists,
                       const QString& error) {
      if (self.isNull ()) return;
      self->refreshRunning= false;
      if (!error.isEmpty ()) {
        if (!automatic) self->setBusy (false, error);
        else self->setStatus (error);
        return;
      }
      self->populateLists (lists);
      if (!automatic)
        self->setBusy (false, lists.isEmpty ()? QString ("No task lists found."):
                                               QString ("Task lists loaded."));
      self->refreshTasks (automatic);
    });
}

void
QTMGoogleTasksPane::populateLists (const QVector<GoogleTaskList>& lists) {
  QSignalBlocker blocker (taskListCombo);
  QString old= selectedListId ();
  taskListCombo->clear ();
  int restore= -1;
  for (const GoogleTaskList& list: lists) {
    int index= taskListCombo->count ();
    taskListCombo->addItem (list.title.isEmpty ()? list.id: list.title,
                            list.id);
    if (list.id == old) restore= index;
  }
  if (restore >= 0) taskListCombo->setCurrentIndex (restore);
}

void
QTMGoogleTasksPane::refreshTasks (bool automatic) {
  QString listId= selectedListId ();
  if (listId.isEmpty ()) {
    taskTree->clear ();
    return;
  }
  if (!automatic) setBusy (true, "Loading tasks...");
  QPointer<QTMGoogleTasksPane> self (this);
  GoogleTasksClient::instance ().listTasks (
    listId, showCompletedCheck->isChecked (),
    [self, automatic] (const QVector<GoogleTask>& tasks,
                       const QString& error) {
      if (self.isNull ()) return;
      if (!error.isEmpty ()) {
        if (!automatic) self->setBusy (false, error);
        else self->setStatus (error);
        return;
      }
      self->populateTasks (tasks);
      self->setBusy (false, QString ("%1 task(s) loaded.").arg (tasks.size ()));
    });
}

void
QTMGoogleTasksPane::populateTasks (const QVector<GoogleTask>& tasks) {
  taskTree->clear ();
  for (const GoogleTask& task: tasks) {
    QTreeWidgetItem* item= new QTreeWidgetItem (taskTree);
    item->setText (0, task.title);
    item->setText (1, task.status);
    item->setText (2, task.due.left (10));
    item->setData (0, Qt::UserRole, task.id);
    if (task.status == "completed")
      item->setForeground (0, QColor (120, 120, 120));
  }
}

void
QTMGoogleTasksPane::newTask () {
  bool ok= false;
  QString title= QInputDialog::getText (this, "New Google task", "Title:",
                                        QLineEdit::Normal, QString (), &ok);
  if (!ok || title.trimmed ().isEmpty ()) return;
  setBusy (true, "Creating task...");
  GoogleTasksClient::instance ().insertTask (
    selectedListId (), title,
    [this] (bool ok2, const QString& message) {
      setBusy (false, message);
      if (ok2) refreshTasks ();
    });
}

void
QTMGoogleTasksPane::completeSelectedTask () {
  QString taskId= selectedTaskId ();
  if (taskId.isEmpty ()) {
    QMessageBox::information (this, "Google Tasks",
                              "Select a task to complete.");
    return;
  }
  setBusy (true, "Completing task...");
  GoogleTasksClient::instance ().completeTask (
    selectedListId (), taskId,
    [this] (bool ok, const QString& message) {
      setBusy (false, message);
      if (ok) refreshTasks ();
    });
}

void
google_tasks_show () {
  if (headless_mode) return;
  if (qt_defer_to_main_thread (google_tasks_show)) return;
  QTMMainTabWindow* win= QTMMainTabWindow::topTabWindow ();
  if (win == nullptr || win->dockManager () == nullptr) {
    QMessageBox::warning (QApplication::activeWindow (), "Google Tasks",
                          "No active ATHENA window.");
    return;
  }

  if (google_tasks_widget == nullptr) {
    google_tasks_widget= new QTMGoogleTasksPane ();
    google_tasks_widget->resize (520, 680);
    QObject::connect (google_tasks_widget, &QObject::destroyed, [] () {
      google_tasks_widget= nullptr;
      google_tasks_dock= nullptr;
    });
  }

  bool freshDock= google_tasks_dock == nullptr;
  if (freshDock) {
    google_tasks_dock= new ads::CDockWidget ("Google Tasks");
    google_tasks_dock->setObjectName ("athena-google-tasks");
    google_tasks_dock->resize (520, 680);
    google_tasks_dock->setWidget (
      google_tasks_widget, ads::CDockWidget::ForceNoScrollArea);
    google_tasks_dock->setFeature (
      ads::CDockWidget::DockWidgetDeleteOnClose, false);
    ads::CDockWidget* dock= google_tasks_dock;
    QObject::connect (dock, &ads::CDockWidget::topLevelChanged,
                      google_tasks_widget, [dock] (bool topLevel) {
      update_google_tasks_floating_state (dock, topLevel);
      if (topLevel)
        QTimer::singleShot (0, dock, [dock] () {
          update_google_tasks_floating_state (dock, true);
        });
    });
    QObject::connect (google_tasks_dock, &QObject::destroyed, [] () {
      google_tasks_dock= nullptr;
    });
  }

  win->showAdsDockWidget (google_tasks_dock, ads::RightDockWidgetArea);
  update_google_tasks_floating_state (
    google_tasks_dock, google_tasks_dock->isInFloatingContainer ());
  set_google_tasks_area_width (google_tasks_dock);
  QTimer::singleShot (0, win, [] () {
    set_google_tasks_area_width (google_tasks_dock);
  });
  google_tasks_widget->setFocus ();
}

void
google_tasks_schedule_background_refresh () {
  if (headless_mode || google_tasks_refresh_scheduled) return;
  QObject* context= QApplication::instance ();
  if (context == nullptr) return;
  google_tasks_refresh_scheduled= true;

  auto refresh= [] () {
    QString clientId= GoogleOAuth::instance ().clientId ().trimmed ();
    bool hasToken= GoogleOAuth::instance ().hasRefreshToken ();
    if (clientId.isEmpty () || !hasToken) {
      if (!google_tasks_connection_toast_shown) {
        google_tasks_connection_toast_shown= true;
        google_tasks_toast (
          "Google Tasks",
          clientId.isEmpty ()?
            "Not connected: configure the OAuth client ID in Preferences.":
            "Not connected: sign in from Preferences or Tools -> Google Tasks.");
      }
      return;
    }
    GoogleOAuth::instance ().getAccessToken (
      [] (const QString& token, const QString& error) {
        if (!google_tasks_connection_toast_shown) {
          google_tasks_connection_toast_shown= true;
          google_tasks_toast (
            "Google Tasks",
            error.isEmpty () && !token.isEmpty ()?
              "Connected and background refresh is enabled.":
              "Not connected: " + error);
        }
        if (error.isEmpty () && !token.isEmpty ()) {
          google_tasks_monitor_active_tasks ();
          google_cloud_todo_sync_open_buffers (false);
        }
      });
    if (google_tasks_widget != nullptr)
      google_tasks_widget->refreshNow (true);
  };

  QTimer::singleShot (8000, context, [refresh] () {
    refresh ();
    if (google_tasks_refresh_timer == nullptr) {
      google_tasks_refresh_timer= new QTimer (QApplication::instance ());
      QObject::connect (google_tasks_refresh_timer, &QTimer::timeout,
                        [refresh] () { refresh (); });
      google_tasks_refresh_timer->start (60000);
    }
  });
}
