/******************************************************************************
* MODULE     : GoogleTasksClient.hpp
* DESCRIPTION: Google Tasks REST client
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef GOOGLETASKSCLIENT_HPP
#define GOOGLETASKSCLIENT_HPP

#include <QObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QString>
#include <QVector>
#include <functional>

class QNetworkAccessManager;

struct GoogleTaskList {
  QString id;
  QString title;
};

struct GoogleTask {
  QString id;
  QString title;
  QString notes;
  QString status;
  QString due;
};

class GoogleTasksClient: public QObject {
public:
  using ListsCallback= std::function<void(const QVector<GoogleTaskList>&,
                                          const QString&)>;
  using TasksCallback= std::function<void(const QVector<GoogleTask>&,
                                          const QString&)>;
  using DoneCallback= std::function<void(bool, const QString&)>;

  static GoogleTasksClient& instance ();

  void listTaskLists (ListsCallback callback);
  void listTasks (const QString& taskListId, bool showCompleted,
                  TasksCallback callback);
  void insertTask (const QString& taskListId, const QString& title,
                   DoneCallback callback);
  void completeTask (const QString& taskListId, const QString& taskId,
                     DoneCallback callback);

private:
  GoogleTasksClient ();

  void authorizedRequest (std::function<void(const QString&)> body,
                          DoneCallback errorCallback);
  QNetworkRequest jsonRequest (const QUrl& url, const QString& token) const;
  QString replyError (QNetworkReply* reply, const QByteArray& body,
                      const QString& fallback) const;

  QNetworkAccessManager* manager;
};

#endif // GOOGLETASKSCLIENT_HPP
