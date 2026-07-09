/******************************************************************************
* MODULE     : QTMRagDelegationClient.cpp
* DESCRIPTION: Qt client helpers for ATHENA RAG delegation
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "QTMRagDelegationClient.hpp"

#include "rag_delegation_crypto.hpp"
#include "rag_delegation_patch.hpp"

#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QTimer>
#include <QUrl>

#include <filesystem>

namespace fs = std::filesystem;

namespace {

QString
normalize_base_url (QString url) {
  url= url.trimmed ();
  if (!url.startsWith ("http://") && !url.startsWith ("https://"))
    url= "http://" + url;
  while (url.endsWith ("/")) url.chop (1);
  return url;
}

QString
config_dir () {
  QString dir= QStandardPaths::writableLocation (
    QStandardPaths::AppConfigLocation);
  if (dir.isEmpty ()) dir= QDir::homePath () + "/.config/ATHENA";
  return QDir (dir).filePath ("rag-delegation");
}

QString
servers_path () {
  return QDir (config_dir ()).filePath ("servers.json");
}

bool
read_json_file (const QString& path, QJsonObject& root) {
  QFile f (path);
  if (!f.open (QIODevice::ReadOnly)) return false;
  QJsonParseError parse;
  QJsonDocument doc= QJsonDocument::fromJson (f.readAll (), &parse);
  if (parse.error != QJsonParseError::NoError || !doc.isObject ())
    return false;
  root= doc.object ();
  return true;
}

bool
write_json_file (const QString& path, const QJsonObject& root,
                 QString* error) {
  QDir dir= QFileInfo (path).absoluteDir ();
  if (!dir.exists () && !dir.mkpath (".")) {
    if (error) *error= "Could not create RAG delegation config directory.";
    return false;
  }
  QFile f (path);
  if (!f.open (QIODevice::WriteOnly | QIODevice::Truncate)) {
    if (error) *error= "Could not write RAG delegation server list.";
    return false;
  }
  f.write (QJsonDocument (root).toJson (QJsonDocument::Indented));
  return true;
}

QByteArray
sync_request (const QNetworkRequest& request, const QByteArray& body,
              bool post, QString* error, int timeoutMs= 30000) {
  QNetworkAccessManager manager;
  QNetworkReply* reply= post ? manager.post (request, body):
                              manager.get (request);
  QEventLoop loop;
  QTimer timer;
  timer.setSingleShot (true);
  QObject::connect (reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
  QObject::connect (&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
  timer.start (timeoutMs);
  loop.exec ();
  if (!timer.isActive ()) {
    reply->abort ();
    if (error) *error= "RAG delegation request timed out.";
    reply->deleteLater ();
    return QByteArray ();
  }
  QByteArray bytes= reply->readAll ();
  if (reply->error () != QNetworkReply::NoError) {
    if (error) *error= reply->errorString ();
    reply->deleteLater ();
    return QByteArray ();
  }
  reply->deleteLater ();
  return bytes;
}

bool
local_keypair (athena::rag::delegation::KeyPair& keys, QString* error) {
  std::string err;
  bool generated= false;
  if (!athena::rag::delegation::ensure_keypair (
        fs::path (config_dir ().toStdString ()), "client",
        keys, &generated, err)) {
    if (error) *error= QString::fromStdString (err);
    return false;
  }
  return true;
}

bool
call_rpc (const QTMRagDelegationServer& server, const QString& method,
          const QJsonObject& params, QJsonObject& result, QString* error,
          int timeoutMs= 30000) {
  athena::rag::delegation::KeyPair client;
  if (!local_keypair (client, error)) return false;
  std::string serverPublic;
  std::string err;
  if (!athena::rag::delegation::base64_decode (
        server.publicKey.toStdString (), serverPublic, err)) {
    if (error) *error= QString::fromStdString (err);
    return false;
  }

  QJsonObject plain;
  plain["method"]= method;
  plain["params"]= params;
  QByteArray plainBytes= QJsonDocument (plain).toJson (QJsonDocument::Compact);
  std::string nonce, cipher;
  if (!athena::rag::delegation::encrypt_payload (
        client, serverPublic, plainBytes.toStdString (), nonce, cipher, err)) {
    if (error) *error= QString::fromStdString (err);
    return false;
  }
  QJsonObject env;
  env["sender"]= QString::fromStdString (
    athena::rag::delegation::base64_encode (client.public_key));
  env["nonce"]= QString::fromStdString (nonce);
  env["ciphertext"]= QString::fromStdString (cipher);

  QNetworkRequest req (QUrl (normalize_base_url (server.url) +
                             "/athena-rag/v1/rpc"));
  req.setHeader (QNetworkRequest::ContentTypeHeader,
                 "application/json; charset=utf-8");
  QByteArray replyBytes= sync_request (
    req, QJsonDocument (env).toJson (QJsonDocument::Compact), true, error,
    timeoutMs);
  if (replyBytes.isEmpty ()) return false;
  QJsonParseError parse;
  QJsonDocument replyDoc= QJsonDocument::fromJson (replyBytes, &parse);
  if (parse.error != QJsonParseError::NoError || !replyDoc.isObject ()) {
    if (error) *error= "Invalid RAG delegation response.";
    return false;
  }
  QJsonObject reply= replyDoc.object ();
  if (!reply.value ("ok").toBool ()) {
    if (error) *error= reply.value ("error").toString ("RAG delegation failed.");
    return false;
  }

  std::string sender;
  if (!athena::rag::delegation::base64_decode (
        reply.value ("sender").toString ().toStdString (), sender, err)) {
    if (error) *error= QString::fromStdString (err);
    return false;
  }
  if (sender != serverPublic) {
    if (error) *error= "RAG delegation server key does not match the pinned "
                       "fingerprint.";
    return false;
  }
  std::string plainReply;
  if (!athena::rag::delegation::decrypt_payload (
        client, sender, reply.value ("nonce").toString ().toStdString (),
        reply.value ("ciphertext").toString ().toStdString (),
        plainReply, err)) {
    if (error) *error= QString::fromStdString (err);
    return false;
  }
  QJsonDocument payload= QJsonDocument::fromJson (
    QByteArray::fromStdString (plainReply), &parse);
  if (parse.error != QJsonParseError::NoError || !payload.isObject ()) {
    if (error) *error= "Invalid encrypted RAG delegation payload.";
    return false;
  }
  result= payload.object ();
  return true;
}

} // namespace

QString
qtm_rag_delegation_config_dir () {
  return config_dir ();
}

QVector<QTMRagDelegationServer>
qtm_rag_delegation_servers () {
  QVector<QTMRagDelegationServer> out;
  QJsonObject root;
  if (!read_json_file (servers_path (), root)) return out;
  for (const QJsonValue& value: root.value ("servers").toArray ()) {
    QJsonObject obj= value.toObject ();
    QTMRagDelegationServer server;
    server.name= obj.value ("name").toString ();
    server.url= obj.value ("url").toString ();
    server.publicKey= obj.value ("public_key").toString ();
    server.fingerprint= obj.value ("fingerprint").toString ();
    if (!server.url.isEmpty ()) out << server;
  }
  return out;
}

bool
qtm_rag_delegation_save_servers (
  const QVector<QTMRagDelegationServer>& servers, QString* error) {
  QJsonArray arr;
  for (const QTMRagDelegationServer& server: servers) {
    QJsonObject obj;
    obj["name"]= server.name;
    obj["url"]= server.url;
    obj["public_key"]= server.publicKey;
    obj["fingerprint"]= server.fingerprint;
    arr.append (obj);
  }
  QJsonObject root;
  root["servers"]= arr;
  return write_json_file (servers_path (), root, error);
}

bool
qtm_rag_delegation_fetch_identity (
  const QString& baseUrl, QTMRagDelegationServer& server, QString* error) {
  QString url= normalize_base_url (baseUrl);
  QNetworkRequest req (QUrl (url + "/athena-rag/v1/identity"));
  QByteArray bytes= sync_request (req, QByteArray (), false, error);
  if (bytes.isEmpty ()) return false;
  QJsonParseError parse;
  QJsonDocument doc= QJsonDocument::fromJson (bytes, &parse);
  if (parse.error != QJsonParseError::NoError || !doc.isObject ()) {
    if (error) *error= "RAG server identity response is not valid JSON.";
    return false;
  }
  QJsonObject obj= doc.object ();
  if (obj.value ("protocol").toInt () != 1 ||
      obj.value ("public_key").toString ().isEmpty ()) {
    if (error) *error= "This endpoint is not an ATHENA RAG Server.";
    return false;
  }
  server.url= url;
  server.name= obj.value ("name").toString ("ATHENA RAG Server");
  server.publicKey= obj.value ("public_key").toString ();
  server.fingerprint= obj.value ("fingerprint").toString ();
  std::string publicKey;
  std::string err;
  if (!athena::rag::delegation::base64_decode (
        server.publicKey.toStdString (), publicKey, err)) {
    if (error) *error= QString::fromStdString (err);
    return false;
  }
  if (QString::fromStdString (
        athena::rag::delegation::fingerprint_for_public_key (publicKey)) !=
      server.fingerprint) {
    if (error) *error= "RAG server identity fingerprint does not match its "
                       "public key.";
    return false;
  }
  return true;
}

bool
qtm_rag_delegation_enroll (
  const QTMRagDelegationServer& server, QString* status, QString* error) {
  QJsonObject result;
  if (!call_rpc (server, "rag.enroll", QJsonObject (), result, error))
    return false;
  if (!result.value ("ok").toBool ()) {
    if (error) *error= result.value ("error").toString ();
    return false;
  }
  if (status) *status= result.value ("status").toString ("pending");
  return true;
}

bool
qtm_rag_delegation_check_auth (
  const QTMRagDelegationServer& server, QString* status, QString* error) {
  QJsonObject result;
  if (!call_rpc (server, "rag.auth.check", QJsonObject (), result, error))
    return false;
  if (!result.value ("ok").toBool ()) {
    if (error) *error= result.value ("error").toString ();
    return false;
  }
  if (status) *status= result.value ("status").toString ("pending");
  return true;
}

bool
qtm_rag_delegation_run_embedding (
  const QTMRagDelegationServer& server, const QString& vaultRoot,
  const QString& dbPath, const QString& embeddingModel,
  const QString& embeddingDevice, QString* summary, QString* error) {
  athena::rag::delegation::DelegatedJob job;
  std::string err;
  if (!athena::rag::delegation::collect_delegated_job (
        fs::path (vaultRoot.toStdString ()), fs::path (dbPath.toStdString ()),
        job, err)) {
    if (error) *error= QString::fromStdString (err);
    return false;
  }
  QJsonArray files;
  for (const auto& file: job.files) {
    QJsonObject obj;
    obj["rel_path"]= QString::fromStdString (file.rel_path);
    obj["content"]= QString::fromLatin1 (
      QByteArray::fromStdString (file.content).toBase64 ());
    obj["size"]= double (file.size);
    obj["mtime_ns"]= double (file.mtime_ns);
    obj["content_hash"]= QString::fromStdString (file.content_hash);
    files.append (obj);
  }
  QJsonArray deleted;
  for (const std::string& rel: job.deleted)
    deleted.append (QString::fromStdString (rel));
  QJsonObject jobRoot;
  jobRoot["files"]= files;
  jobRoot["deleted"]= deleted;
  QJsonObject params;
  params["job"]= QString::fromLatin1 (
    QJsonDocument (jobRoot).toJson (QJsonDocument::Compact).toBase64 ());
  params["embedding_model"]= embeddingModel;
  params["embedding_device"]= embeddingDevice;

  QJsonObject result;
  if (!call_rpc (server, "rag.embedding.build_patch", params, result, error,
                 60 * 60 * 1000))
    return false;
  if (!result.value ("ok").toBool ()) {
    if (error) *error= result.value ("error").toString ();
    return false;
  }
  QByteArray patchBytes= QByteArray::fromBase64 (
    result.value ("patch").toString ().toUtf8 ());
  QTemporaryFile patchFile;
  patchFile.setAutoRemove (true);
  if (!patchFile.open ()) {
    if (error) *error= "Could not create temporary patch database.";
    return false;
  }
  patchFile.write (patchBytes);
  patchFile.flush ();
  patchFile.close ();
  if (!athena::rag::delegation::apply_patch_database (
        fs::path (vaultRoot.toStdString ()), fs::path (dbPath.toStdString ()),
        fs::path (patchFile.fileName ().toStdString ()), job.deleted, err)) {
    if (error) *error= QString::fromStdString (err);
    return false;
  }
  if (summary)
    *summary= QString ("Delegated %1 changed .ath files and %2 deletions.")
                .arg (job.files.size ()).arg (job.deleted.size ());
  return true;
}
