/******************************************************************************
* MODULE     : QTMDelegationClient.cpp
* DESCRIPTION: Qt client helpers for authenticated ATHENA delegation
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "QTMDelegationClient.hpp"

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
#include <QHash>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QUuid>

#include <algorithm>
#include <filesystem>
#include <limits>
#include <set>
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
  QString override= QString::fromUtf8 (
    qgetenv ("ATHENA_DELEGATION_CONFIG_DIR")).trimmed ();
  if (!override.isEmpty ()) return QDir::cleanPath (override);

  QString generic= QStandardPaths::writableLocation (
    QStandardPaths::GenericConfigLocation);
  if (generic.isEmpty ()) generic= QDir::homePath () + "/.config";
  QString athena= QDir (generic).filePath ("ATHENA");
  QString current= QDir (athena).filePath ("delegation");
  static bool migrated= false;
  if (!migrated) {
    migrated= true;
    QString app= QStandardPaths::writableLocation (
      QStandardPaths::AppConfigLocation);
    QStringList legacy;
    legacy << QDir (athena).filePath ("rag-delegation");
    if (!app.isEmpty ()) {
      legacy << QDir (app).filePath ("delegation");
      legacy << QDir (app).filePath ("rag-delegation");
    }
    for (const QString& path: legacy)
      if (!QFileInfo::exists (current) && QFileInfo::exists (path)) {
        QDir ().mkpath (athena);
        QDir ().rename (path, current);
      }
  }
  return current;
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
    if (error) *error= "Could not create ATHENA delegation config directory.";
    return false;
  }
  QFile f (path);
  if (!f.open (QIODevice::WriteOnly | QIODevice::Truncate)) {
    if (error) *error= "Could not write ATHENA delegation server list.";
    return false;
  }
  f.write (QJsonDocument (root).toJson (QJsonDocument::Indented));
  return true;
}

QByteArray
sync_request (const QNetworkRequest& request, const QByteArray& body,
              bool post, QString* error, int timeoutMs= 30000,
              const std::function<bool ()>& keepGoing= {}) {
  QNetworkAccessManager manager;
  QNetworkReply* reply= post ? manager.post (request, body):
                              manager.get (request);
  QEventLoop loop;
  QTimer timer;
  QTimer cancellation;
  timer.setSingleShot (true);
  QObject::connect (reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
  QObject::connect (&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
  if (keepGoing) {
    cancellation.setInterval (50);
    QObject::connect (&cancellation, &QTimer::timeout, &loop, [&] () {
      if (!keepGoing ()) {
        reply->abort ();
        loop.quit ();
      }
    });
    cancellation.start ();
  }
  timer.start (timeoutMs);
  loop.exec ();
  cancellation.stop ();
  if (keepGoing && !keepGoing ()) {
    if (error) *error= "ATHENA delegation request cancelled.";
    reply->deleteLater ();
    return QByteArray ();
  }
  if (!timer.isActive ()) {
    reply->abort ();
    if (error) *error= "ATHENA delegation request timed out.";
    reply->deleteLater ();
    return QByteArray ();
  }
  if (reply->error () != QNetworkReply::NoError) {
    if (error) *error= reply->errorString ();
    reply->deleteLater ();
    return QByteArray ();
  }
  QByteArray bytes= reply->readAll ();
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
call_rpc (const QTMDelegationServer& server, const QString& method,
          const QJsonObject& params, QJsonObject& result, QString* error,
          int timeoutMs= 30000,
          const std::function<bool ()>& keepGoing= {}) {
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
                             "/athena-delegation/v1/rpc"));
  req.setHeader (QNetworkRequest::ContentTypeHeader,
                 "application/json; charset=utf-8");
  QByteArray replyBytes= sync_request (
    req, QJsonDocument (env).toJson (QJsonDocument::Compact), true, error,
    timeoutMs, keepGoing);
  if (replyBytes.isEmpty ()) return false;
  QJsonParseError parse;
  QJsonDocument replyDoc= QJsonDocument::fromJson (replyBytes, &parse);
  if (parse.error != QJsonParseError::NoError || !replyDoc.isObject ()) {
    if (error) *error= "Invalid ATHENA delegation response.";
    return false;
  }
  QJsonObject reply= replyDoc.object ();
  if (!reply.value ("ok").toBool ()) {
    if (error)
      *error= reply.value ("error").toString ("ATHENA delegation failed.");
    return false;
  }

  std::string sender;
  if (!athena::rag::delegation::base64_decode (
        reply.value ("sender").toString ().toStdString (), sender, err)) {
    if (error) *error= QString::fromStdString (err);
    return false;
  }
  if (sender != serverPublic) {
    if (error) *error= "ATHENA delegation server key does not match the pinned "
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
    if (error) *error= "Invalid encrypted ATHENA delegation payload.";
    return false;
  }
  result= payload.object ();
  return true;
}

bool
retryable_transport_failure (const QString& message) {
  QString lower= message.toLower ();
  return lower.contains ("timed out") || lower.contains ("status code 524") ||
         lower.contains ("http/2 protocol error") ||
         lower.contains ("http/2 stream") ||
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
wait_for_server_reconnect (const QTMDelegationServer& server,
                           QString* error) {
  QElapsedTimer total;
  total.start ();
  QString lastError;
  while (total.elapsed () < 15 * 60 * 1000) {
    QNetworkRequest req (QUrl (normalize_base_url (server.url) +
                               "/athena-delegation/v1/identity"));
    QByteArray bytes= sync_request (req, QByteArray (), false, &lastError,
                                    10 * 1000);
    if (!bytes.isEmpty ()) {
      QJsonParseError parse;
      QJsonDocument doc= QJsonDocument::fromJson (bytes, &parse);
      if (parse.error == QJsonParseError::NoError && doc.isObject () &&
          doc.object ().value ("public_key").toString () == server.publicKey)
        return true;
      lastError= "ATHENA delegation endpoint identity changed while reconnecting.";
    }
    QThread::msleep (2000);
  }
  if (error)
    *error= lastError.isEmpty () ? "ATHENA delegation server did not reconnect.":
                                  lastError;
  return false;
}

} // namespace

QString
qtm_delegation_config_dir () {
  return config_dir ();
}

QVector<QTMDelegationServer>
qtm_delegation_servers () {
  QVector<QTMDelegationServer> out;
  QJsonObject root;
  if (!read_json_file (servers_path (), root)) return out;
  for (const QJsonValue& value: root.value ("servers").toArray ()) {
    QJsonObject obj= value.toObject ();
    QTMDelegationServer server;
    server.name= obj.value ("name").toString ();
    server.url= obj.value ("url").toString ();
    server.publicKey= obj.value ("public_key").toString ();
    server.fingerprint= obj.value ("fingerprint").toString ();
    for (const QJsonValue& capability: obj.value ("capabilities").toArray ())
      server.capabilities << capability.toString ();
    QJsonObject artifactLimits=
      obj.value ("limits").toObject ().value ("artifact_definition_span")
         .toObject ();
    server.artifactMaxRequests=
      artifactLimits.value ("max_requests_per_job").toInt (
        obj.value ("artifact_max_requests").toInt (512));
    server.artifactMaxPlaintextBytes=
      artifactLimits.value ("max_plaintext_bytes").toInt (
        obj.value ("artifact_max_plaintext_bytes").toInt (8 * 1024 * 1024));
    if (!server.url.isEmpty ()) out << server;
  }
  return out;
}

bool
qtm_delegation_selected_server (
  const QString& configuredUrl, QTMDelegationServer& selected) {
  QVector<QTMDelegationServer> servers= qtm_delegation_servers ();
  if (servers.isEmpty ()) return false;
  if (!configuredUrl.trimmed ().isEmpty ())
    for (const QTMDelegationServer& server: servers)
      if (server.url == configuredUrl.trimmed ()) {
        selected= server;
        return true;
      }
  selected= servers.first ();
  return true;
}

bool
qtm_delegation_save_servers (
  const QVector<QTMDelegationServer>& servers, QString* error) {
  QJsonArray arr;
  for (const QTMDelegationServer& server: servers) {
    QJsonObject obj;
    obj["name"]= server.name;
    obj["url"]= server.url;
    obj["public_key"]= server.publicKey;
    obj["fingerprint"]= server.fingerprint;
    QJsonArray capabilities;
    for (const QString& capability: server.capabilities)
      capabilities.append (capability);
    obj["capabilities"]= capabilities;
    QJsonObject artifactLimits;
    artifactLimits["max_requests_per_job"]= server.artifactMaxRequests;
    artifactLimits["max_plaintext_bytes"]=
      server.artifactMaxPlaintextBytes;
    QJsonObject limits;
    limits["artifact_definition_span"]= artifactLimits;
    obj["limits"]= limits;
    arr.append (obj);
  }
  QJsonObject root;
  root["servers"]= arr;
  return write_json_file (servers_path (), root, error);
}

bool
qtm_delegation_fetch_identity (
  const QString& baseUrl, QTMDelegationServer& server, QString* error) {
  QString url= normalize_base_url (baseUrl);
  QNetworkRequest req (QUrl (url + "/athena-delegation/v1/identity"));
  QByteArray bytes= sync_request (req, QByteArray (), false, error);
  if (bytes.isEmpty ()) return false;
  QJsonParseError parse;
  QJsonDocument doc= QJsonDocument::fromJson (bytes, &parse);
  if (parse.error != QJsonParseError::NoError || !doc.isObject ()) {
    if (error) *error= "ATHENA delegation identity response is not valid JSON.";
    return false;
  }
  QJsonObject obj= doc.object ();
  if (obj.value ("protocol").toInt () != 1 ||
      obj.value ("public_key").toString ().isEmpty ()) {
    if (error) *error= "This endpoint is not an ATHENA Delegation Server.";
    return false;
  }
  server.url= url;
  server.name= obj.value ("name").toString ("ATHENA Delegation Server");
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
    if (error) *error= "ATHENA delegation identity fingerprint does not match its "
                       "public key.";
    return false;
  }
  for (const QJsonValue& capability: obj.value ("capabilities").toArray ())
    server.capabilities << capability.toString ();
  QJsonObject artifactLimits=
    obj.value ("limits").toObject ().value ("artifact_definition_span")
       .toObject ();
  server.artifactMaxRequests=
    artifactLimits.value ("max_requests_per_job").toInt (512);
  server.artifactMaxPlaintextBytes=
    artifactLimits.value ("max_plaintext_bytes").toInt (8 * 1024 * 1024);
  return true;
}

bool
qtm_delegation_enroll (
  const QTMDelegationServer& server, QString* status, QString* error) {
  QJsonObject result;
  if (!call_rpc (server, "auth.enroll", QJsonObject (), result, error))
    return false;
  if (!result.value ("ok").toBool ()) {
    if (error) *error= result.value ("error").toString ();
    return false;
  }
  if (status) *status= result.value ("status").toString ("pending");
  return true;
}

bool
qtm_delegation_check_auth (
  const QTMDelegationServer& server, QString* status, QString* error) {
  QJsonObject result;
  if (!call_rpc (server, "auth.check", QJsonObject (), result, error))
    return false;
  if (!result.value ("ok").toBool ()) {
    if (error) *error= result.value ("error").toString ();
    return false;
  }
  if (status) *status= result.value ("status").toString ("pending");
  return true;
}

bool
qtm_delegation_run_embedding (
  const QTMDelegationServer& server, const QString& vaultRoot,
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

  auto jobParams= [&] (const athena::rag::delegation::DelegatedJob& current,
                       const QString& requestId) {
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
    params["request_id"]= requestId;
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
    QString requestId= QUuid::createUuid ().toString (
      QUuid::WithoutBraces);
    while (true) {
      retryCount++;
      io_info << "rag delegation: submitting batch " << (batchNumber + 1)
              << " attempt=" << retryCount << " files="
              << current.files.size () << " bytes=" << currentBytes
              << " remaining=" << (totalFiles - nextFile) << "\n";
      QElapsedTimer requestTimer;
      requestTimer.start ();
      QString batchError;
      QJsonObject params= jobParams (current, requestId);
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
        requestId= QUuid::createUuid ().toString (QUuid::WithoutBraces);
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
    if (result.value ("request_id").toString () != requestId) {
      if (error) *error= "Delegation server returned a mismatched RAG request id.";
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

bool
qtm_delegation_select_artifact_ranges (
  const QTMDelegationServer& configuredServer,
  const std::vector<AthenaArtifactRangeRequest>& requests,
  std::vector<std::vector<int>>& results,
  const QTMArtifactDelegationProgress& progress, QString* error) {
  results.assign (requests.size (), {});
  if (requests.empty ()) return true;

  QTMDelegationServer server;
  QString identityError;
  if (!qtm_delegation_fetch_identity (configuredServer.url, server,
                                      &identityError)) {
    if (error) *error= identityError;
    return false;
  }
  if (server.publicKey != configuredServer.publicKey ||
      server.fingerprint != configuredServer.fingerprint) {
    if (error) *error= "ATHENA delegation endpoint identity changed.";
    return false;
  }
  if (!server.capabilities.contains ("artifact-definition-span-v2")) {
    if (error) *error= "The selected server does not support artifact "
                       "definition-span delegation.";
    return false;
  }

  struct Payload {
    QJsonObject params;
    std::vector<size_t> indexes;
  };
  std::vector<Payload> payloads;
  size_t next= 0;
  // Submit RPCs cross an HTTPS proxy before reaching the transmitter.  Keep
  // jobs comfortably below the backend's storage limit so large source
  // paragraphs cannot spend the entire request timeout in transit.
  int maxRequests= std::clamp (server.artifactMaxRequests, 1, 128);
  int maxBytes= std::clamp (server.artifactMaxPlaintextBytes,
                            64 * 1024, 1024 * 1024);
  QString buildId= QUuid::createUuid ().toString (QUuid::WithoutBraces);
  while (next < requests.size ()) {
    QJsonArray catalog;
    QHash<QString,int> catalogIndexes;
    QJsonArray requestArray;
    Payload payload;
    size_t start= next;
    while (next < requests.size () &&
           requestArray.size () < maxRequests) {
      const AthenaArtifactRangeRequest& request= requests[next];
      QJsonObject object;
      object["id"]= QString ("r%1").arg ((qulonglong) next);
      object["keyword_latex"]= QString::fromStdString (request.keyword_latex);
      QJsonArray candidates;
      for (const auto& paragraph: request.paragraphs) {
        QString text= QString::fromStdString (paragraph.second);
        int catalogIndex= catalogIndexes.value (text, -1);
        if (catalogIndex < 0) {
          catalogIndex= catalog.size ();
          catalogIndexes.insert (text, catalogIndex);
          catalog.append (text);
        }
        QJsonObject candidate;
        candidate["offset"]= paragraph.first;
        candidate["catalog"]= catalogIndex;
        candidates.append (candidate);
      }
      object["candidates"]= candidates;
      requestArray.append (object);
      QJsonObject probe;
      probe["submission_id"]= QString ("%1-%2")
        .arg (buildId).arg ((qulonglong) payloads.size ());
      probe["catalog"]= catalog;
      probe["requests"]= requestArray;
      if (QJsonDocument (probe).toJson (QJsonDocument::Compact).size () >
            maxBytes && requestArray.size () > 1) {
        requestArray.removeLast ();
        // Rebuild the catalog without the rejected request.
        catalog= QJsonArray ();
        catalogIndexes.clear ();
        QJsonArray rebuiltRequests;
        for (size_t acceptedIndex: payload.indexes) {
          QJsonObject acceptedObject;
          acceptedObject["id"]= QString ("r%1").arg (
            (qulonglong) acceptedIndex);
          acceptedObject["keyword_latex"]= QString::fromStdString (
            requests[acceptedIndex].keyword_latex);
          QJsonArray rebuilt;
          for (const auto& paragraph: requests[acceptedIndex].paragraphs) {
            QString text= QString::fromStdString (paragraph.second);
            int catalogIndex= catalogIndexes.value (text, -1);
            if (catalogIndex < 0) {
              catalogIndex= catalog.size ();
              catalogIndexes.insert (text, catalogIndex);
              catalog.append (text);
            }
            QJsonObject candidate;
            candidate["offset"]= paragraph.first;
            candidate["catalog"]= catalogIndex;
            rebuilt.append (candidate);
          }
          acceptedObject["candidates"]= rebuilt;
          rebuiltRequests.append (acceptedObject);
        }
        requestArray= rebuiltRequests;
        break;
      }
      payload.indexes.push_back (next);
      next++;
    }
    if (next == start) {
      if (error) *error= "One artifact definition-span request exceeds the "
                         "server plaintext limit.";
      return false;
    }
    payload.params["submission_id"]= QString ("%1-%2")
      .arg (buildId).arg ((qulonglong) payloads.size ());
    payload.params["catalog"]= catalog;
    payload.params["requests"]= requestArray;
    if (QJsonDocument (payload.params).toJson (QJsonDocument::Compact).size () >
        maxBytes) {
      if (error) *error= "One artifact definition-span request exceeds the "
                         "server plaintext limit.";
      return false;
    }
    payloads.push_back (std::move (payload));
  }

  struct ActiveJob {
    size_t payload= 0;
    QString id;
    int cursor= 0;
    int queued= 0;
    int running= 0;
  };
  std::vector<ActiveJob> active;
  std::vector<bool> received (requests.size (), false);
  size_t completed= 0;
  size_t nextPayload= 0;

  auto keepGoing= [&] () {
    size_t queued= 0, running= 0;
    for (const ActiveJob& job: active) {
      queued += (size_t) std::max (0, job.queued);
      running += (size_t) std::max (0, job.running);
    }
    return !progress || progress (completed, requests.size (), queued, running);
  };
  auto rpc= [&] (const QString& method, const QJsonObject& params,
                 QJsonObject& result, int timeout) {
    QString lastError;
    for (int attempt=0; attempt<3; attempt++) {
      if (call_rpc (server, method, params, result, &lastError, timeout,
                    keepGoing)) return true;
      if (lastError.contains ("cancelled", Qt::CaseInsensitive)) break;
      if (!retryable_transport_failure (lastError)) break;
      QString reconnectError;
      if (!wait_for_server_reconnect (server, &reconnectError)) {
        lastError= reconnectError;
        break;
      }
    }
    if (error) *error= method + ": " + lastError;
    return false;
  };
  auto cancelActive= [&] () {
    for (const ActiveJob& job: active) {
      QJsonObject params;
      params["job_id"]= job.id;
      QJsonObject ignored;
      QString ignoredError;
      call_rpc (server, "artifact.definition_span.cancel", params, ignored,
                &ignoredError, 10000);
    }
  };
  auto submitOne= [&] (size_t index) {
    QJsonObject response;
    if (!rpc ("artifact.definition_span.submit", payloads[index].params,
              response, 120000)) return false;
    if (!response.value ("ok").toBool () ||
        response.value ("job_id").toString ().isEmpty ()) {
      if (error) *error= response.value ("error").toString (
        "Artifact delegation did not return a job id.");
      return false;
    }
    ActiveJob job;
    job.payload= index;
    job.id= response.value ("job_id").toString ();
    job.queued= (int) payloads[index].indexes.size ();
    active.push_back (std::move (job));
    return true;
  };

  while (nextPayload < payloads.size () && active.size () < 4)
    if (!submitOne (nextPayload++)) { cancelActive (); return false; }

  size_t activeIndex= 0;
  while (!active.empty ()) {
    if (!keepGoing ()) {
      cancelActive ();
      if (error) *error= "Artifact build cancelled";
      return false;
    }
    if (activeIndex >= active.size ()) activeIndex= 0;
    ActiveJob& job= active[activeIndex];
    QJsonObject params;
    params["job_id"]= job.id;
    params["cursor"]= job.cursor;
    params["wait_ms"]= 20000;
    QJsonObject response;
    if (!rpc ("artifact.definition_span.wait", params, response, 25000)) {
      cancelActive ();
      return false;
    }
    if (!response.value ("ok").toBool ()) {
      if (error) *error= response.value ("error").toString ();
      cancelActive ();
      return false;
    }
    QJsonObject counts= response.value ("counts").toObject ();
    job.queued= counts.value ("queued").toInt ();
    job.running= counts.value ("running").toInt ();
    job.cursor= response.value ("cursor").toInt (job.cursor);
    for (const QJsonValue& value: response.value ("results").toArray ()) {
      QJsonObject item= value.toObject ();
      QString id= item.value ("id").toString ();
      bool idOk= false;
      size_t index= id.mid (1).toULongLong (&idOk);
      if (!id.startsWith ('r') || !idOk || index >= requests.size () ||
          received[index]) {
        if (error) *error= "Artifact delegation returned an invalid request id.";
        cancelActive ();
        return false;
      }
      std::vector<int> offsets;
      for (const QJsonValue& offset: item.value ("offsets").toArray ())
        offsets.push_back (offset.toInt ());
      std::set<int> allowed;
      for (const auto& candidate: requests[index].paragraphs)
        allowed.insert (candidate.first);
      bool valid= !offsets.empty () &&
        std::find (offsets.begin (), offsets.end (), 0) != offsets.end () &&
        std::is_sorted (offsets.begin (), offsets.end ()) &&
        std::adjacent_find (offsets.begin (), offsets.end ()) == offsets.end ();
      for (size_t i=0; valid && i<offsets.size (); i++)
        valid= allowed.count (offsets[i]) &&
               (i == 0 || offsets[i] == offsets[i - 1] + 1);
      if (!valid) {
        if (error) *error= "Artifact delegation returned invalid offsets.";
        cancelActive ();
        return false;
      }
      results[index]= std::move (offsets);
      received[index]= true;
      completed++;
    }
    QString state= response.value ("state").toString ();
    if (state == "failed" || state == "cancelled") {
      if (error) *error= response.value ("error").toString (
        "Artifact definition-span job failed.");
      cancelActive ();
      return false;
    }
    if (state == "complete") {
      for (size_t index: payloads[job.payload].indexes)
        if (!received[index]) {
          if (error) *error= "Artifact definition-span job returned an "
                             "incomplete result.";
          cancelActive ();
          return false;
        }
      QJsonObject ackParams;
      ackParams["job_id"]= job.id;
      QJsonObject ack;
      if (!rpc ("artifact.definition_span.ack", ackParams, ack, 10000)) {
        cancelActive ();
        return false;
      }
      active.erase (active.begin () + (ptrdiff_t) activeIndex);
      if (nextPayload < payloads.size () && !submitOne (nextPayload++)) {
        cancelActive ();
        return false;
      }
    }
    else activeIndex++;
  }
  return completed == requests.size ();
}
