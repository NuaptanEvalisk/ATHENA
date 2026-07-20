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
#include "tm_ostream.hpp"

#include <QDir>
#include <QElapsedTimer>
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
#include <QThread>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <filesystem>
#include <limits>
#include <vector>

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

bool
retryable_transport_failure (const QString& message) {
  QString lower= message.toLower ();
  return lower.contains ("timed out") || lower.contains ("status code 524") ||
         lower.contains ("error transferring") ||
         lower.contains ("connection closed") ||
         lower.contains ("connection reset") ||
         lower.contains ("connection refused") ||
         lower.contains ("remote host closed") ||
         lower.contains ("host not found") ||
         lower.contains ("network is unreachable") ||
         lower.contains ("temporary network") ||
         lower.contains ("temporary failure") || lower.contains ("proxy") ||
         lower.contains ("tls connect");
}

bool
transport_window_too_large (const QString& message) {
  QString lower= message.toLower ();
  return lower.contains ("timed out") || lower.contains ("status code 524");
}

bool
wait_for_server_reconnect (const QTMRagDelegationServer& server,
                           QString* error) {
  QElapsedTimer total;
  total.start ();
  QString lastError;
  while (total.elapsed () < 15 * 60 * 1000) {
    QNetworkRequest req (QUrl (normalize_base_url (server.url) +
                               "/athena-rag/v1/identity"));
    QByteArray bytes= sync_request (req, QByteArray (), false, &lastError,
                                    10 * 1000);
    if (!bytes.isEmpty ()) {
      QJsonParseError parse;
      QJsonDocument doc= QJsonDocument::fromJson (bytes, &parse);
      if (parse.error == QJsonParseError::NoError && doc.isObject () &&
          doc.object ().value ("public_key").toString () == server.publicKey)
        return true;
      lastError= "RAG delegation endpoint identity changed while reconnecting.";
    }
    QThread::msleep (2000);
  }
  if (error)
    *error= lastError.isEmpty () ? "RAG delegation server did not reconnect.":
                                  lastError;
  return false;
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
  std::string expectedModel;
  if (!athena::rag::delegation::cached_embedding_model_fingerprint (
        fs::path (dbPath.toStdString ()),
        fs::path (embeddingModel.toStdString ()), expectedModel, err)) {
    if (error) *error= QString::fromStdString (err);
    return false;
  }
  if (!athena::rag::delegation::collect_delegated_job (
        fs::path (vaultRoot.toStdString ()), fs::path (dbPath.toStdString ()),
        expectedModel, job, err)) {
    if (error) *error= QString::fromStdString (err);
    return false;
  }
  if (job.files.empty () && job.deleted.empty ()) {
    if (summary)
      *summary= "Delegated 0 changed .ath files and 0 deletions.";
    return true;
  }

  const size_t totalFiles= job.files.size ();
  const size_t totalDeleted= job.deleted.size ();
  size_t completedFiles= 0;
  size_t completedDeleted= 0;
  size_t nextFile= 0;
  size_t fileWindow= std::min<size_t> (16, std::max<size_t> (1, totalFiles));
  size_t byteWindow= 768 * 1024;
  size_t batchNumber= 0;

  auto makeBatch= [&] (size_t start, size_t filesLimit, size_t bytesLimit) {
    athena::rag::delegation::DelegatedJob current;
    size_t bytes= 0;
    for (size_t i=start; i<job.files.size (); i++) {
      const auto& file= job.files[i];
      if (!current.files.empty () &&
          (current.files.size () >= filesLimit ||
           bytes + file.content.size () > bytesLimit))
        break;
      bytes += file.content.size ();
      current.files.push_back (file);
    }
    if (completedDeleted == 0) current.deleted= job.deleted;
    return current;
  };

  auto jobParams= [&] (const athena::rag::delegation::DelegatedJob& current) {
    QJsonArray files;
    for (const auto& file: current.files) {
      QJsonObject obj;
      obj["rel_path"]= QString::fromStdString (file.rel_path);
      obj["content"]= QString::fromLatin1 (
        QByteArray::fromStdString (file.content).toBase64 ());
      // JSON numbers cannot exactly represent timestamps above 2^53.
      obj["size"]= QString::number (file.size);
      obj["mtime_ns"]= QString::number (file.mtime_ns);
      obj["content_hash"]= QString::fromStdString (file.content_hash);
      files.append (obj);
    }
    QJsonArray deleted;
    for (const std::string& rel: current.deleted)
      deleted.append (QString::fromStdString (rel));
    QJsonObject jobRoot;
    jobRoot["files"]= files;
    jobRoot["deleted"]= deleted;
    QJsonObject params;
    params["job"]= QString::fromLatin1 (
      QJsonDocument (jobRoot).toJson (QJsonDocument::Compact).toBase64 ());
    params["embedding_model"]= embeddingModel;
    params["embedding_device"]= embeddingDevice;
    return params;
  };

  while (nextFile < totalFiles || completedDeleted < totalDeleted) {
    auto current= makeBatch (nextFile, fileWindow, byteWindow);
    size_t currentBytes= 0;
    for (const auto& file: current.files) currentBytes += file.content.size ();
    unsigned retryCount= 0;
    QJsonObject result;
    qint64 rpcMilliseconds= 0;
    while (true) {
      retryCount++;
      io_info << "rag delegation: submitting batch " << (batchNumber + 1)
              << " attempt=" << retryCount << " files="
              << current.files.size () << " bytes=" << currentBytes
              << " remaining=" << (totalFiles - nextFile) << "\n";
      QElapsedTimer requestTimer;
      requestTimer.start ();
      QString batchError;
      QJsonObject params= jobParams (current);
      if (call_rpc (server, "rag.embedding.build_patch", params, result,
                    &batchError, 85 * 1000)) {
        rpcMilliseconds= requestTimer.elapsed ();
        break;
      }
      if (!retryable_transport_failure (batchError)) {
        if (error)
          *error= QString ("Batch %1 failed after %2 file(s): %3")
                    .arg (batchNumber + 1).arg (completedFiles).arg (batchError);
        return false;
      }

      io_info << "rag delegation: transport protection triggered for batch "
              << (batchNumber + 1) << "; waiting to reconnect\n";
      QString reconnectError;
      if (!wait_for_server_reconnect (server, &reconnectError)) {
        if (error) *error= reconnectError;
        return false;
      }
      if (transport_window_too_large (batchError) &&
          current.files.size () > 1) {
        fileWindow= std::max<size_t> (1, current.files.size () / 2);
        byteWindow= std::max<size_t> (64 * 1024, currentBytes / 2);
        current= makeBatch (nextFile, fileWindow, byteWindow);
        currentBytes= 0;
        for (const auto& file: current.files)
          currentBytes += file.content.size ();
        retryCount= 0;
        io_info << "rag delegation: reconnected; reduced pending batch to "
                << current.files.size () << " files / " << currentBytes
                << " bytes\n";
      }
      else if (retryCount >= 3) {
        if (error)
          *error= QString ("Delegation batch failed three times: %1")
                    .arg (batchError);
        return false;
      }
      else
        io_info << "rag delegation: reconnected; retrying the same batch\n";
    }
    if (!result.value ("ok").toBool ()) {
      if (error) *error= result.value ("error").toString ();
      return false;
    }
    QByteArray patchBytes= QByteArray::fromBase64 (
      result.value ("patch").toString ().toUtf8 ());
    QString patchEncoding= result.value ("patch_encoding").toString ("raw");
    if (patchEncoding == "qcompress") {
      QByteArray uncompressed= qUncompress (patchBytes);
      if (uncompressed.isEmpty ()) {
        if (error) *error= "Could not decompress delegated patch database.";
        return false;
      }
      patchBytes= std::move (uncompressed);
    }
    else if (patchEncoding != "raw") {
      if (error)
        *error= "Unsupported delegated patch encoding: " + patchEncoding;
      return false;
    }
    io_info << "rag delegation: received patch encoding="
            << patchEncoding.toStdString ().c_str () << " wire-bytes="
            << result.value ("patch_wire_bytes").toString ().toStdString ().c_str ()
            << " raw-bytes=" << patchBytes.size () << "\n";
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
          fs::path (patchFile.fileName ().toStdString ()), current.deleted,
          err)) {
      if (error) *error= QString::fromStdString (err);
      return false;
    }
    completedFiles += current.files.size ();
    completedDeleted += current.deleted.size ();
    nextFile += current.files.size ();
    batchNumber++;
    io_info << "rag delegation: applied batch " << batchNumber
            << " completed-files=" << completedFiles << "/" << totalFiles
            << " rpc-ms=" << rpcMilliseconds << "\n";

    // Estimate the next safe window from measured throughput instead of
    // converging on a fixed file count.  Keep enough headroom below the
    // request timeout for documents whose embedding cost differs from their
    // source size, and damp each adjustment to avoid oscillation.
    if (nextFile < totalFiles && rpcMilliseconds > 0 &&
        !current.files.empty ()) {
      constexpr qint64 targetMilliseconds= 65 * 1000;
      auto boundedTarget= [] (size_t budget, size_t measured,
                              qint64 elapsed, size_t minimum) {
        long double projected=
          static_cast<long double> (measured) * targetMilliseconds / elapsed;
        size_t target= static_cast<size_t> (
          std::max<long double> (minimum, projected));
        size_t lower= std::max<size_t> (minimum, (budget + 1) / 2);
        size_t upper= budget > std::numeric_limits<size_t>::max () / 2 ?
                      std::numeric_limits<size_t>::max (): budget * 2;
        return std::clamp (target, lower, upper);
      };
      bool fileLimited= current.files.size () >= fileWindow;
      bool byteLimited= !fileLimited &&
                        nextFile < totalFiles;
      if (fileLimited) {
        size_t targetFiles= boundedTarget (
          fileWindow, current.files.size (), rpcMilliseconds, 1);
        fileWindow= (fileWindow + targetFiles + 1) / 2;
      }
      if (byteLimited) {
        size_t targetBytes= boundedTarget (
          byteWindow, currentBytes, rpcMilliseconds, 64 * 1024);
        byteWindow= (byteWindow + targetBytes + 1) / 2;
      }
      io_info << "rag delegation: adapted next window to " << fileWindow
              << " files / " << byteWindow << " bytes from measured "
              << current.files.size () << " files / " << currentBytes
              << " bytes in " << rpcMilliseconds << " ms; limiter="
              << (fileLimited ? "files": "bytes") << "\n";
    }
  }
  if (summary)
    *summary= QString ("Delegated %1 changed .ath files and %2 deletions.")
                .arg (completedFiles).arg (completedDeleted);
  return true;
}
