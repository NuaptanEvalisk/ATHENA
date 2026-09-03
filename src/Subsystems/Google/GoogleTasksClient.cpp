/******************************************************************************
* MODULE     : GoogleTasksClient.cpp
* DESCRIPTION: Google Tasks REST client
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "GoogleTasksClient.hpp"

#include "GoogleAsyncDispatch.hpp"
#include "GoogleOAuth.hpp"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSharedPointer>
#include <QThread>
#include <QUrl>
#include <QUrlQuery>

GoogleTasksClient&
GoogleTasksClient::instance () {
  static GoogleTasksClient client;
  return client;
}

GoogleTasksClient::GoogleTasksClient ()
  = default;

QNetworkAccessManager*
GoogleTasksClient::networkManager () {
  QCoreApplication* app= QCoreApplication::instance ();
  Q_ASSERT (app != nullptr && QThread::currentThread () == app->thread ());
  if (manager == nullptr) manager= new QNetworkAccessManager (app);
  return manager;
}

void
GoogleTasksClient::authorizedRequest (
  std::function<void(const QString&)> body, DoneResult errorCallback) {
  GoogleOAuth::instance ().getAccessToken (
    [body, errorCallback] (const QString& token, const QString& error) {
      if (!error.isEmpty ()) {
        errorCallback (false, error);
        return;
      }
      body (token);
    });
}

QNetworkRequest
GoogleTasksClient::jsonRequest (const QUrl& url, const QString& token) const {
  QNetworkRequest request (url);
  request.setRawHeader ("Authorization",
                        QByteArray ("Bearer ") + token.toUtf8 ());
  request.setHeader (QNetworkRequest::ContentTypeHeader,
                     "application/json; charset=utf-8");
  return request;
}

QString
GoogleTasksClient::replyError (QNetworkReply* reply, const QByteArray& body,
                               const QString& fallback) const {
  QJsonDocument doc= QJsonDocument::fromJson (body);
  QJsonObject root= doc.object ();
  QJsonObject error= root.value ("error").toObject ();
  QString message= error.value ("message").toString ();
  if (!message.isEmpty ()) return message;
  return fallback;
}

void
GoogleTasksClient::listTaskLists (ListsCallback callback) {
  GoogleAsyncOrigin origin= google_async_origin ();
  google_dispatch_to_qt (
    [this, origin, callback= std::move (callback)] () mutable {
      listTaskListsOnQt (
        [origin, callback= std::move (callback)] (
          QVector<GoogleTaskList> lists, QString error) mutable {
          google_dispatch_to_origin (
            origin,
            [callback= std::move (callback), lists= std::move (lists),
             error= std::move (error)] () mutable {
              callback (lists, error);
            });
        });
    });
}

void
GoogleTasksClient::listTaskListsOnQt (ListsResult callback) {
  authorizedRequest ([=] (const QString& token) {
    QUrl url ("https://tasks.googleapis.com/tasks/v1/users/@me/lists");
    QNetworkReply* reply= networkManager ()->get (jsonRequest (url, token));
    QObject::connect (reply, &QNetworkReply::finished, reply, [=] () {
      QByteArray body= reply->readAll ();
      if (reply->error () != QNetworkReply::NoError) {
        QString error= replyError (reply, body, reply->errorString ());
        reply->deleteLater ();
        callback ({}, error);
        return;
      }
      QVector<GoogleTaskList> lists;
      QJsonArray items= QJsonDocument::fromJson (body).object ()
                          .value ("items").toArray ();
      for (const QJsonValue& value: items) {
        QJsonObject item= value.toObject ();
        GoogleTaskList list;
        list.id= item.value ("id").toString ();
        list.title= item.value ("title").toString ();
        if (!list.id.isEmpty ()) lists << list;
      }
      reply->deleteLater ();
      callback (std::move (lists), QString ());
    });
  }, [=] (bool, const QString& error) { callback ({}, error); });
}

void
GoogleTasksClient::listTasks (const QString& taskListId, bool showCompleted,
                              TasksCallback callback) {
  GoogleAsyncOrigin origin= google_async_origin ();
  google_dispatch_to_qt (
    [this, origin, taskListId, showCompleted,
     callback= std::move (callback)] () mutable {
      listTasksOnQt (
        taskListId, showCompleted,
        [origin, callback= std::move (callback)] (
          QVector<GoogleTask> tasks, QString error) mutable {
          google_dispatch_to_origin (
            origin,
            [callback= std::move (callback), tasks= std::move (tasks),
             error= std::move (error)] () mutable {
              callback (tasks, error);
            });
        });
    });
}

void
GoogleTasksClient::listTasksOnQt (QString taskListId, bool showCompleted,
                                  TasksResult callback) {
  if (taskListId.isEmpty ()) {
    callback ({}, "No task list selected.");
    return;
  }
  authorizedRequest ([=] (const QString& token) {
    QSharedPointer<QVector<GoogleTask>> accumulated (
      new QVector<GoogleTask>);
    listTasksPage (taskListId, showCompleted, QString (), token, accumulated,
                   callback);
  }, [=] (bool, const QString& error) { callback ({}, error); });
}

void
GoogleTasksClient::listTasksPage (
  const QString& taskListId, bool showCompleted, const QString& pageToken,
  const QString& token, QSharedPointer<QVector<GoogleTask>> accumulated,
  TasksResult callback) {
    QUrl url ("https://tasks.googleapis.com/tasks/v1/lists/" +
              QString::fromLatin1 (QUrl::toPercentEncoding (taskListId)) +
              "/tasks");
    QUrlQuery query;
    query.addQueryItem ("showCompleted", showCompleted? "true": "false");
    query.addQueryItem ("showHidden", showCompleted? "true": "false");
    query.addQueryItem ("maxResults", "100");
    if (!pageToken.isEmpty ()) query.addQueryItem ("pageToken", pageToken);
    url.setQuery (query);
    QNetworkReply* reply= networkManager ()->get (jsonRequest (url, token));
    QObject::connect (reply, &QNetworkReply::finished, reply, [=] () {
      QByteArray body= reply->readAll ();
      if (reply->error () != QNetworkReply::NoError) {
        QString error= replyError (reply, body, reply->errorString ());
        reply->deleteLater ();
        callback ({}, error);
        return;
      }
      QJsonObject root= QJsonDocument::fromJson (body).object ();
      QJsonArray items= root.value ("items").toArray ();
      for (const QJsonValue& value: items) {
        QJsonObject item= value.toObject ();
        GoogleTask task;
        task.id= item.value ("id").toString ();
        task.title= item.value ("title").toString ();
        task.notes= item.value ("notes").toString ();
        task.status= item.value ("status").toString ();
        task.due= item.value ("due").toString ();
        if (!task.id.isEmpty ()) accumulated->append (task);
      }
      QString next= root.value ("nextPageToken").toString ();
      if (!next.isEmpty ()) {
        reply->deleteLater ();
        listTasksPage (taskListId, showCompleted, next, token, accumulated,
                       callback);
        return;
      }
      reply->deleteLater ();
      callback (std::move (*accumulated), QString ());
    });
}

void
GoogleTasksClient::insertTask (const QString& taskListId, const QString& title,
                               DoneCallback callback) {
  insertTaskDetailed (
    taskListId, title,
    [callback] (bool ok, const GoogleTask&, const QString& message) {
      callback (ok, message);
    });
}

void
GoogleTasksClient::insertTaskDetailed (const QString& taskListId,
                                       const QString& title,
                                       InsertCallback callback) {
  GoogleAsyncOrigin origin= google_async_origin ();
  google_dispatch_to_qt (
    [this, origin, taskListId, title,
     callback= std::move (callback)] () mutable {
      insertTaskDetailedOnQt (
        taskListId, title,
        [origin, callback= std::move (callback)] (
          bool ok, GoogleTask task, QString message) mutable {
          google_dispatch_to_origin (
            origin,
            [callback= std::move (callback), ok, task= std::move (task),
             message= std::move (message)] () mutable {
              callback (ok, task, message);
            });
        });
    });
}

void
GoogleTasksClient::insertTaskDetailedOnQt (QString taskListId,
                                           QString title,
                                           InsertResult callback) {
  QString trimmed= title.trimmed ();
  if (taskListId.isEmpty () || trimmed.isEmpty ()) {
    callback (false, GoogleTask (), "Task list and title are required.");
    return;
  }
  authorizedRequest ([=] (const QString& token) {
    QUrl url ("https://tasks.googleapis.com/tasks/v1/lists/" +
              QString::fromLatin1 (QUrl::toPercentEncoding (taskListId)) +
              "/tasks");
    QJsonObject payload;
    payload["title"]= trimmed;
    QNetworkReply* reply= networkManager ()->post (
      jsonRequest (url, token), QJsonDocument (payload).toJson ());
    QObject::connect (reply, &QNetworkReply::finished, reply, [=] () {
      QByteArray body= reply->readAll ();
      bool ok= reply->error () == QNetworkReply::NoError;
      QString error= ok? QString (): replyError (reply, body,
                                                 reply->errorString ());
      GoogleTask task;
      if (ok) {
        QJsonObject item= QJsonDocument::fromJson (body).object ();
        task.id= item.value ("id").toString ();
        task.title= item.value ("title").toString ();
        task.notes= item.value ("notes").toString ();
        task.status= item.value ("status").toString ();
        task.due= item.value ("due").toString ();
      }
      reply->deleteLater ();
      callback (ok, std::move (task),
                ok? QString ("Task created."): std::move (error));
    });
  }, [=] (bool, const QString& error) {
    callback (false, GoogleTask (), error);
  });
}

void
GoogleTasksClient::completeTask (const QString& taskListId,
                                 const QString& taskId,
                                 DoneCallback callback) {
  setTaskCompleted (taskListId, taskId, true, callback);
}

void
GoogleTasksClient::setTaskCompleted (const QString& taskListId,
                                     const QString& taskId, bool completed,
                                     DoneCallback callback) {
  GoogleAsyncOrigin origin= google_async_origin ();
  google_dispatch_to_qt (
    [this, origin, taskListId, taskId, completed,
     callback= std::move (callback)] () mutable {
      setTaskCompletedOnQt (
        taskListId, taskId, completed,
        [origin, callback= std::move (callback)] (
          bool ok, QString message) mutable {
          google_dispatch_to_origin (
            origin,
            [callback= std::move (callback), ok,
             message= std::move (message)] () mutable {
              callback (ok, message);
            });
        });
    });
}

void
GoogleTasksClient::setTaskCompletedOnQt (QString taskListId,
                                         QString taskId, bool completed,
                                         DoneResult callback) {
  if (taskListId.isEmpty () || taskId.isEmpty ()) {
    callback (false, "Task list and task are required.");
    return;
  }
  authorizedRequest ([=] (const QString& token) {
    QUrl url ("https://tasks.googleapis.com/tasks/v1/lists/" +
              QString::fromLatin1 (QUrl::toPercentEncoding (taskListId)) +
              "/tasks/" +
              QString::fromLatin1 (QUrl::toPercentEncoding (taskId)));
    QJsonObject payload;
    payload["status"]= completed? "completed": "needsAction";
    if (!completed) payload["completed"]= QJsonValue ();
    QNetworkReply* reply= networkManager ()->sendCustomRequest (
      jsonRequest (url, token), "PATCH", QJsonDocument (payload).toJson ());
    QObject::connect (reply, &QNetworkReply::finished, reply, [=] () {
      QByteArray body= reply->readAll ();
      bool ok= reply->error () == QNetworkReply::NoError;
      QString error= ok? QString (): replyError (reply, body,
                                                 reply->errorString ());
      reply->deleteLater ();
      callback (ok, ok? (completed? QString ("Task completed."):
                                    QString ("Task reopened.")): error);
    });
  }, callback);
}
