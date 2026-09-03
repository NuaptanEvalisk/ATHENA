/******************************************************************************
* MODULE     : GoogleOAuth.cpp
* DESCRIPTION: OAuth 2.0 desktop authorization for Google services
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "GoogleOAuth.hpp"

#include "GoogleAsyncDispatch.hpp"
#include "boot.hpp"
#include "scheme.hpp"
#include "tm_ostream.hpp"

#include <QByteArray>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <QUrl>
#include <QUrlQuery>

namespace {

static QString
to_qstring_google (string s) {
  return QString::fromUtf8 (as_charp (s), N(s));
}

static string
from_qstring_google (const QString& s) {
  QByteArray bytes= s.toUtf8 ();
  return string (bytes.constData ());
}

static QString
preference (const char* key, const char* def= "") {
  return to_qstring_google (get_preference (string (key), string (def)));
}

static QString
base64url (const QByteArray& bytes) {
  return QString::fromLatin1 (
    bytes.toBase64 (QByteArray::Base64UrlEncoding |
                    QByteArray::OmitTrailingEquals));
}

static QString
random_url_token (int bytes) {
  QByteArray data;
  data.resize (bytes);
  for (int i=0; i<bytes; i++)
    data[i]= char (QRandomGenerator::global ()->bounded (256));
  return base64url (data);
}

static QString
pkce_challenge (const QString& verifier) {
  QByteArray hash= QCryptographicHash::hash (
    verifier.toUtf8 (), QCryptographicHash::Sha256);
  return base64url (hash);
}

static QByteArray
form_body (const QUrlQuery& query) {
  return query.toString (QUrl::FullyEncoded).toUtf8 ();
}

static QString
json_error (const QJsonObject& root, const QString& fallback) {
  QString description= root.value ("error_description").toString ();
  if (!description.isEmpty ()) return description;
  QString error= root.value ("error").toString ();
  if (!error.isEmpty ()) return error;
  return fallback;
}

} // namespace

GoogleOAuth&
GoogleOAuth::instance () {
  static GoogleOAuth oauth;
  return oauth;
}

GoogleOAuth::GoogleOAuth ()
  = default;

QNetworkAccessManager*
GoogleOAuth::networkManager () {
  QCoreApplication* app= QCoreApplication::instance ();
  Q_ASSERT (app != nullptr && QThread::currentThread () == app->thread ());
  if (manager == nullptr) manager= new QNetworkAccessManager (app);
  return manager;
}

QString
GoogleOAuth::clientId () const {
  return preference ("google oauth client id");
}

void
GoogleOAuth::setClientId (const QString& value) {
  set_preference (string ("google oauth client id"),
                  from_qstring_google (value.trimmed ()));
}

QString
GoogleOAuth::clientSecret () const {
  return preference ("google oauth client secret");
}

void
GoogleOAuth::setClientSecret (const QString& value) {
  set_preference (string ("google oauth client secret"),
                  from_qstring_google (value.trimmed ()));
}

QString
GoogleOAuth::tokenPath () const {
  QString dir= QStandardPaths::writableLocation (
    QStandardPaths::AppConfigLocation);
  if (dir.isEmpty ())
    dir= QDir::homePath () + "/.config/ATHENA";
  return QDir (dir).filePath ("google-tasks-token.json");
}

GoogleOAuth::TokenInfo
GoogleOAuth::loadToken () const {
  TokenInfo token;
  QFile file (tokenPath ());
  if (!file.open (QIODevice::ReadOnly)) return token;
  QJsonDocument doc= QJsonDocument::fromJson (file.readAll ());
  if (!doc.isObject ()) return token;
  QJsonObject root= doc.object ();
  token.accessToken= root.value ("access_token").toString ();
  token.refreshToken= root.value ("refresh_token").toString ();
  token.expiresAtSecs= qint64 (root.value ("expires_at").toDouble ());
  return token;
}

bool
GoogleOAuth::saveToken (const TokenInfo& token, QString* error) const {
  QString path= tokenPath ();
  QDir dir= QFileInfo (path).absoluteDir ();
  if (!dir.exists () && !dir.mkpath (".")) {
    if (error) *error= "Could not create token directory.";
    return false;
  }

  QJsonObject root;
  root["access_token"]= token.accessToken;
  root["refresh_token"]= token.refreshToken;
  root["expires_at"]= double (token.expiresAtSecs);
  QFile file (path);
  if (!file.open (QIODevice::WriteOnly | QIODevice::Truncate)) {
    if (error) *error= file.errorString ();
    return false;
  }
  file.write (QJsonDocument (root).toJson (QJsonDocument::Indented));
  file.close ();
  file.setPermissions (QFile::ReadOwner | QFile::WriteOwner);
  return true;
}

bool
GoogleOAuth::accessTokenFresh (const TokenInfo& token) const {
  if (token.accessToken.isEmpty ()) return false;
  qint64 now= QDateTime::currentSecsSinceEpoch ();
  return token.expiresAtSecs > now + 60;
}

bool
GoogleOAuth::hasRefreshToken () const {
  return !loadToken ().refreshToken.isEmpty ();
}

void
GoogleOAuth::forgetTokens () {
  QFile::remove (tokenPath ());
}

void
GoogleOAuth::authorizeTasks (QWidget*, BoolCallback callback) {
  QCoreApplication* app= QCoreApplication::instance ();
  Q_ASSERT (app != nullptr && QThread::currentThread () == app->thread ());
  QString cid= clientId ().trimmed ();
  if (cid.isEmpty ()) {
    callback (false, "Set the Google OAuth client ID first.");
    return;
  }

  QTcpServer* server= new QTcpServer (app);
  if (!server->listen (QHostAddress::LocalHost, 0)) {
    QString error= server->errorString ();
    server->deleteLater ();
    callback (false, "Could not start OAuth loopback listener: " + error);
    return;
  }

  quint16 port= server->serverPort ();
  QString redirectUri= QString ("http://127.0.0.1:%1/").arg (port);
  QString verifier= random_url_token (48);
  QString state= random_url_token (24);

  QUrl auth ("https://accounts.google.com/o/oauth2/v2/auth");
  QUrlQuery q;
  q.addQueryItem ("client_id", cid);
  q.addQueryItem ("redirect_uri", redirectUri);
  q.addQueryItem ("response_type", "code");
  q.addQueryItem ("scope", "https://www.googleapis.com/auth/tasks");
  q.addQueryItem ("access_type", "offline");
  q.addQueryItem ("prompt", "consent");
  q.addQueryItem ("code_challenge", pkce_challenge (verifier));
  q.addQueryItem ("code_challenge_method", "S256");
  q.addQueryItem ("state", state);
  auth.setQuery (q);

  bool* completed= new bool (false);
  QObject::connect (server, &QTcpServer::newConnection, server,
                    [=] () mutable {
    QTcpSocket* socket= server->nextPendingConnection ();
    if (socket == nullptr) return;
    QObject::connect (socket, &QTcpSocket::readyRead, socket,
                      [=] () mutable {
      if (*completed) return;
      QByteArray request= socket->readAll ();
      QList<QByteArray> lines= request.split ('\n');
      if (lines.isEmpty ()) return;
      QList<QByteArray> parts= lines[0].trimmed ().split (' ');
      if (parts.size () < 2) return;

      QUrl callbackUrl ("http://127.0.0.1" +
                        QString::fromUtf8 (parts[1]));
      QUrlQuery query (callbackUrl);
      QString incomingState= query.queryItemValue ("state");
      QString code= query.queryItemValue ("code");
      QString error= query.queryItemValue ("error");

      QByteArray body=
        "<html><body><h2>ATHENA Google Tasks</h2>"
        "<p>You may close this browser tab and return to ATHENA.</p>"
        "</body></html>";
      QByteArray response=
        "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\n"
        "Connection: close\r\nContent-Length: " +
        QByteArray::number (body.size ()) + "\r\n\r\n" + body;
      socket->write (response);
      socket->flush ();
      socket->disconnectFromHost ();
      *completed= true;
      server->close ();
      server->deleteLater ();
      delete completed;

      if (!error.isEmpty ()) {
        callback (false, "Google authorization failed: " + error);
        return;
      }
      if (incomingState != state || code.isEmpty ()) {
        callback (false, "OAuth callback did not match the pending request.");
        return;
      }
      exchangeCode (code, verifier, redirectUri, callback);
    });
  });

  if (!QDesktopServices::openUrl (auth)) {
    server->close ();
    server->deleteLater ();
    delete completed;
    callback (false, "Could not open the authorization URL in a browser.");
  }
}

void
GoogleOAuth::exchangeCode (const QString& code, const QString& verifier,
                           const QString& redirectUri, BoolCallback callback) {
  QUrl url ("https://oauth2.googleapis.com/token");
  QNetworkRequest request (url);
  request.setHeader (QNetworkRequest::ContentTypeHeader,
                     "application/x-www-form-urlencoded");

  QUrlQuery q;
  q.addQueryItem ("client_id", clientId ().trimmed ());
  QString secret= clientSecret ().trimmed ();
  if (!secret.isEmpty ()) q.addQueryItem ("client_secret", secret);
  q.addQueryItem ("code", code);
  q.addQueryItem ("code_verifier", verifier);
  q.addQueryItem ("redirect_uri", redirectUri);
  q.addQueryItem ("grant_type", "authorization_code");

  QNetworkReply* reply= networkManager ()->post (request, form_body (q));
  QObject::connect (reply, &QNetworkReply::finished, reply, [=] () {
    QByteArray body= reply->readAll ();
    QJsonDocument doc= QJsonDocument::fromJson (body);
    QJsonObject root= doc.object ();
    if (reply->error () != QNetworkReply::NoError) {
      QString error= json_error (root, reply->errorString ());
      reply->deleteLater ();
      callback (false, "Token exchange failed: " + error);
      return;
    }

    TokenInfo token;
    token.accessToken= root.value ("access_token").toString ();
    token.refreshToken= root.value ("refresh_token").toString ();
    int expiresIn= root.value ("expires_in").toInt (3600);
    token.expiresAtSecs= QDateTime::currentSecsSinceEpoch () + expiresIn;
    if (token.accessToken.isEmpty () || token.refreshToken.isEmpty ()) {
      reply->deleteLater ();
      callback (false, "Google did not return a complete OAuth token set.");
      return;
    }

    QString saveError;
    bool ok= saveToken (token, &saveError);
    reply->deleteLater ();
    callback (ok, ok? QString ("Connected to Google Tasks."):
                     QString ("Could not save token: ") + saveError);
  });
}

void
GoogleOAuth::getAccessToken (TokenCallback callback) {
  GoogleAsyncOrigin origin= google_async_origin ();
  google_dispatch_to_qt (
    [this, origin, callback= std::move (callback)] () mutable {
      getAccessTokenOnQt (
        [origin, callback= std::move (callback)] (
          QString token, QString error) mutable {
          google_dispatch_to_origin (
            origin,
            [callback= std::move (callback),
             token= std::move (token),
             error= std::move (error)] () mutable {
              callback (token, error);
            });
        });
    });
}

void
GoogleOAuth::getAccessTokenOnQt (TokenResult callback) {
  TokenInfo token= loadToken ();
  if (accessTokenFresh (token)) {
    callback (std::move (token.accessToken), QString ());
    return;
  }
  if (token.refreshToken.isEmpty ()) {
    callback (QString (), "Google Tasks is not connected.");
    return;
  }
  refreshToken (std::move (token), std::move (callback));
}

void
GoogleOAuth::refreshToken (TokenInfo token, TokenResult callback) {
  QUrl url ("https://oauth2.googleapis.com/token");
  QNetworkRequest request (url);
  request.setHeader (QNetworkRequest::ContentTypeHeader,
                     "application/x-www-form-urlencoded");

  QUrlQuery q;
  q.addQueryItem ("client_id", clientId ().trimmed ());
  QString secret= clientSecret ().trimmed ();
  if (!secret.isEmpty ()) q.addQueryItem ("client_secret", secret);
  q.addQueryItem ("refresh_token", token.refreshToken);
  q.addQueryItem ("grant_type", "refresh_token");

  QNetworkReply* reply= networkManager ()->post (request, form_body (q));
  QObject::connect (reply, &QNetworkReply::finished, reply, [=] () {
    QByteArray body= reply->readAll ();
    QJsonDocument doc= QJsonDocument::fromJson (body);
    QJsonObject root= doc.object ();
    if (reply->error () != QNetworkReply::NoError) {
      QString error= json_error (root, reply->errorString ());
      reply->deleteLater ();
      callback (QString (), "Token refresh failed: " + error);
      return;
    }

    TokenInfo updated= token;
    updated.accessToken= root.value ("access_token").toString ();
    QString maybeRefresh= root.value ("refresh_token").toString ();
    if (!maybeRefresh.isEmpty ()) updated.refreshToken= maybeRefresh;
    int expiresIn= root.value ("expires_in").toInt (3600);
    updated.expiresAtSecs= QDateTime::currentSecsSinceEpoch () + expiresIn;
    QString saveError;
    if (!saveToken (updated, &saveError)) {
      reply->deleteLater ();
      callback (QString (), "Could not save refreshed token: " + saveError);
      return;
    }
    reply->deleteLater ();
    callback (std::move (updated.accessToken), QString ());
  });
}
