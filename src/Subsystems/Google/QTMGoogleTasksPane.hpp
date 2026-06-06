/******************************************************************************
* MODULE     : QTMGoogleTasksPane.hpp
* DESCRIPTION: Qt ADS pane for Google Tasks
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef QTMGOOGLETASKSPANE_HPP
#define QTMGOOGLETASKSPANE_HPP

#include "GoogleTasksClient.hpp"

#include <QSize>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QSizeGrip;
class QTreeWidget;
class QTreeWidgetItem;

class QTMGoogleTasksPane: public QWidget {
public:
  QTMGoogleTasksPane (QWidget* parent= nullptr);

  QSize sizeHint () const override;
  void  setFloatingResizeGripVisible (bool visible);
  void  refreshNow (bool automatic= false);

private:
  void updateConnectionStatus ();
  void connectGoogle ();
  void disconnectGoogle ();
  void refreshLists (bool automatic= false);
  void refreshTasks (bool automatic= false);
  void newTask ();
  void completeSelectedTask ();
  QString selectedListId () const;
  QString selectedTaskId () const;
  void setBusy (bool busy, const QString& status);
  void setStatus (const QString& status);
  void populateLists (const QVector<GoogleTaskList>& lists);
  void populateTasks (const QVector<GoogleTask>& tasks);

  QLabel*      statusLabel;
  QComboBox*   taskListCombo;
  QCheckBox*   showCompletedCheck;
  QPushButton* connectButton;
  QPushButton* disconnectButton;
  QPushButton* refreshButton;
  QPushButton* newButton;
  QPushButton* completeButton;
  QTreeWidget* taskTree;
  QSizeGrip*   floatingSizeGrip;
  bool          refreshRunning;
};

void google_tasks_show ();
void google_tasks_schedule_background_refresh ();

#endif // QTMGOOGLETASKSPANE_HPP
