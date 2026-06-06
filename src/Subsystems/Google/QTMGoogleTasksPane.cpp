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
#include "QTMMainTabWindow.hpp"
#include "boot.hpp"

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
#include <QPushButton>
#include <QSignalBlocker>
#include <QSizeGrip>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

static QTMGoogleTasksPane* google_tasks_widget= nullptr;
static ads::CDockWidget* google_tasks_dock= nullptr;

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

} // namespace

QTMGoogleTasksPane::QTMGoogleTasksPane (QWidget* parent)
  : QWidget (parent) {
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
           [this] () { refreshLists (); });
  connect (showCompletedCheck, &QCheckBox::toggled,
           [this] () { refreshTasks (); });
  connect (taskListCombo,
           static_cast<void (QComboBox::*) (int)> (
             &QComboBox::currentIndexChanged),
           [this] (int) { refreshTasks (); });
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
    if (ok) refreshLists ();
  });
}

void
QTMGoogleTasksPane::disconnectGoogle () {
  GoogleOAuth::instance ().forgetTokens ();
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
QTMGoogleTasksPane::refreshLists () {
  setBusy (true, "Loading Google task lists...");
  GoogleTasksClient::instance ().listTaskLists (
    [this] (const QVector<GoogleTaskList>& lists, const QString& error) {
      if (!error.isEmpty ()) {
        setBusy (false, error);
        return;
      }
      populateLists (lists);
      setBusy (false, lists.isEmpty ()? QString ("No task lists found."):
                                      QString ("Task lists loaded."));
      refreshTasks ();
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
QTMGoogleTasksPane::refreshTasks () {
  QString listId= selectedListId ();
  if (listId.isEmpty ()) {
    taskTree->clear ();
    return;
  }
  setBusy (true, "Loading tasks...");
  GoogleTasksClient::instance ().listTasks (
    listId, showCompletedCheck->isChecked (),
    [this] (const QVector<GoogleTask>& tasks, const QString& error) {
      if (!error.isEmpty ()) {
        setBusy (false, error);
        return;
      }
      populateTasks (tasks);
      setBusy (false, QString ("%1 task(s) loaded.").arg (tasks.size ()));
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
