/******************************************************************************
* MODULE     : delegation_server.cpp
* DESCRIPTION: Authenticated embedding and artifact delegation server
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "Services/Delegation/delegation_server.hpp"
#include "Services/Delegation/artifact_delegation_queue.hpp"

#include "rag_delegation_crypto.hpp"
#include "rag_delegation_patch.hpp"
#include "rag_embedding.hpp"
#include "rag_index.hpp"

#include "tm_ostream.hpp"

#include <QByteArray>
#include <QCoreApplication>
#include <QDateTime>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>

#include <algorithm>
#include <chrono>
#include <mutex>
#include <memory>
#include <sstream>
#include <fstream>
#include <thread>
#include <unordered_map>

namespace athena::delegation {
namespace {

struct HttpRequest {
  QByteArray method;
  QByteArray path;
  std::unordered_map<QByteArray, QByteArray> headers;
  QByteArray body;
};

static std::unique_ptr<QTcpServer> server;
static std::shared_ptr<athena::rag::RagEmbedder> embedding_runtime;
static std::unique_ptr<ArtifactDelegationQueue> artifact_queue;
static std::mutex rag_delegation_mutex;
struct RagPatchCacheEntry {
  QJsonObject result;
  std::chrono::steady_clock::time_point completed_at;
};
static std::unordered_map<std::string,RagPatchCacheEntry> rag_patch_cache;
static DelegationServerOptions active_options;
static athena::rag::delegation::KeyPair delegation_keypair;
static bool delegation_ready= false;

static QByteArray
lower (QByteArray s) {
  return s.toLower ();
}

static QByteArray
json_bytes (const QJsonObject& obj) {
  return QJsonDocument (obj).toJson (QJsonDocument::Compact);
}

static void
write_response (QTcpSocket* socket, int status, const QByteArray& type,
                const QByteArray& body) {
  QByteArray reason= "OK";
  if (status == 202) reason= "Accepted";
  else if (status == 400) reason= "Bad Request";
  else if (status == 401) reason= "Unauthorized";
  else if (status == 403) reason= "Forbidden";
  else if (status == 404) reason= "Not Found";
  else if (status == 405) reason= "Method Not Allowed";
  QByteArray out;
  out += "HTTP/1.1 " + QByteArray::number (status) + " " + reason + "\r\n";
  out += "Content-Type: " + type + "\r\n";
  out += "Content-Length: " + QByteArray::number (body.size ()) + "\r\n";
  out += "Connection: close\r\n\r\n";
  out += body;
  socket->write (out);
  socket->disconnectFromHost ();
}

static QJsonObject
jsonrpc_error_object (const QString& message) {
  QJsonObject o;
  o["ok"]= false;
  o["error"]= message;
  return o;
}

static bool
parse_request (const QByteArray& data, HttpRequest& req) {
  int split= data.indexOf ("\r\n\r\n");
  if (split < 0) return false;
  QByteArray head= data.left (split);
  req.body= data.mid (split + 4);
  QList<QByteArray> lines= head.split ('\n');
  if (lines.isEmpty ()) return false;
  QList<QByteArray> first= lines[0].trimmed ().split (' ');
  if (first.size () < 2) return false;
  req.method= first[0].trimmed ();
  req.path= first[1].trimmed ();
  for (int i=1; i<lines.size (); i++) {
    QByteArray line= lines[i].trimmed ();
    int colon= line.indexOf (':');
    if (colon < 0) continue;
    req.headers[lower (line.left (colon).trimmed ())]= line.mid (colon + 1).trimmed ();
  }
  return true;
}

static std::filesystem::path
default_delegation_key_dir () {
  const char* xdg= getenv ("XDG_CONFIG_HOME");
  const char* home= getenv ("HOME");
  std::filesystem::path base=
    xdg != nullptr && xdg[0] != '\0' ? std::filesystem::path (xdg):
    (home == nullptr || home[0] == '\0' ? std::filesystem::path ("."):
      std::filesystem::path (home) / ".config");
  return base / "ATHENA" / "delegation";
}

struct AcceptedClient {
  std::string key;
  std::string role= "client";
};

static bool
load_public_key_list (const std::filesystem::path& path,
                      std::vector<AcceptedClient>& keys,
                      std::string& error) {
  keys.clear ();
  if (path.empty ()) return true;
  std::ifstream in (path, std::ios::binary);
  if (!in) return true;
  QByteArray bytes;
  std::ostringstream ss;
  ss << in.rdbuf ();
  bytes= QByteArray::fromStdString (ss.str ());
  QJsonParseError parse;
  QJsonDocument doc= QJsonDocument::fromJson (bytes, &parse);
  if (parse.error != QJsonParseError::NoError || !doc.isObject ()) {
    error= "invalid accepted clients JSON";
    return false;
  }
  QJsonArray arr= doc.object ().value ("accepted").toArray ();
  for (const QJsonValue& value: arr) {
    QString encoded;
    QString role= "client";
    if (value.isString ()) encoded= value.toString ();
    else if (value.isObject ()) {
      encoded= value.toObject ().value ("public_key").toString ();
      role= value.toObject ().value ("role").toString ("client");
    }
    std::string decoded;
    std::string local_error;
    if (athena::rag::delegation::base64_decode (
          encoded.toStdString (), decoded, local_error))
      keys.push_back ({decoded, role.toStdString ()});
  }
  return true;
}

static std::string
accepted_public_key_role (const std::string& public_key) {
  std::vector<AcceptedClient> keys;
  std::string error;
  if (!load_public_key_list (active_options.delegation_accepted_clients,
                             keys, error))
    return "";
  for (const AcceptedClient& client: keys)
    if (client.key == public_key) return client.role;
  return "";
}

static bool
public_key_accepted (const std::string& public_key) {
  return !accepted_public_key_role (public_key).empty ();
}

static bool
append_pending_client (const std::string& public_key, std::string& error) {
  std::filesystem::path dir= active_options.delegation_key_dir.empty ()?
    default_delegation_key_dir (): active_options.delegation_key_dir;
  std::filesystem::path path= dir / "pending-clients.json";
  QJsonArray pending;
  {
    std::ifstream in (path, std::ios::binary);
    if (in) {
      std::ostringstream ss;
      ss << in.rdbuf ();
      QJsonDocument doc= QJsonDocument::fromJson (
        QByteArray::fromStdString (ss.str ()));
      pending= doc.object ().value ("pending").toArray ();
    }
  }
  QString key64= QString::fromStdString (
    athena::rag::delegation::base64_encode (public_key));
  for (const QJsonValue& value: pending) {
    if (value.isString () && value.toString () == key64)
      return true;
    if (value.isObject () &&
        value.toObject ().value ("public_key").toString () == key64)
      return true;
  }
  QJsonObject item;
  item["public_key"]= key64;
  item["fingerprint"]= QString::fromStdString (
    athena::rag::delegation::fingerprint_for_public_key (public_key));
  item["requested_at"]= QDateTime::currentDateTimeUtc ().toString (Qt::ISODate);
  pending.append (item);
  QJsonObject root;
  root["pending"]= pending;
  std::error_code ec;
  std::filesystem::create_directories (path.parent_path (), ec);
  std::ofstream out (path, std::ios::binary | std::ios::trunc);
  if (!out) {
    error= "failed to write pending clients";
    return false;
  }
  QByteArray bytes= QJsonDocument (root).toJson (QJsonDocument::Indented);
  out.write (bytes.constData (), bytes.size ());
  return true;
}

static QJsonObject
identity_result () {
  QJsonObject o;
  o["name"]= "ATHENA Delegation Server";
  o["kind"]= "backend";
  o["protocol"]= athena::rag::delegation::rag_delegation_protocol_version;
  o["public_key"]= QString::fromStdString (
    athena::rag::delegation::base64_encode (
      delegation_keypair.public_key));
  o["fingerprint"]= QString::fromStdString (
    athena::rag::delegation::fingerprint_for_public_key (
      delegation_keypair.public_key));
  QJsonArray caps;
  caps.append ("athena-delegation-v2");
  caps.append ("rag-embedding-v3");
  if (artifact_queue && artifact_queue->available ())
    caps.append ("artifact-definition-span-v2");
  caps.append ("pending-enrollment");
  o["capabilities"]= caps;
  QJsonObject limits;
  if (artifact_queue)
    limits["artifact_definition_span"]= artifact_queue->limits ();
  o["limits"]= limits;
  return o;
}

static QJsonObject
handle_delegation_plain_rpc (const QJsonObject& request,
                             const std::string& sender_public_key) {
  QString method= request.value ("method").toString ();
  QJsonObject params= request.value ("params").toObject ();
  if (method == "auth.enroll") {
    std::string error;
    if (!append_pending_client (sender_public_key, error))
      return jsonrpc_error_object (QString::fromStdString (error));
    QJsonObject result;
    result["ok"]= true;
    result["status"]= public_key_accepted (sender_public_key)?
      "accepted": "pending";
    result["fingerprint"]= QString::fromStdString (
      athena::rag::delegation::fingerprint_for_public_key (
        sender_public_key));
    return result;
  }
  if (method == "auth.check") {
    QJsonObject result;
    result["ok"]= true;
    result["status"]= public_key_accepted (sender_public_key)?
      "accepted": "pending";
    return result;
  }
  std::string sender_role= accepted_public_key_role (sender_public_key);
  if (sender_role.empty ())
    return jsonrpc_error_object ("client public key is not accepted");
  std::string principal=
    athena::rag::delegation::fingerprint_for_public_key (sender_public_key);
  if (sender_role == "proxy") {
    QString forwarded= request.value ("_delegation_principal").toString ();
    if (!forwarded.isEmpty ()) principal= forwarded.toStdString ();
  }
  if (method == "artifact.definition_span.submit") {
    if (!artifact_queue || !artifact_queue->available ())
      return jsonrpc_error_object ("artifact definition-span model is unavailable");
    return artifact_queue->submit (principal, params);
  }
  if (method == "artifact.definition_span.wait") {
    if (!artifact_queue) return jsonrpc_error_object ("artifact queue is unavailable");
    return artifact_queue->wait (principal, params);
  }
  if (method == "artifact.definition_span.cancel") {
    if (!artifact_queue) return jsonrpc_error_object ("artifact queue is unavailable");
    return artifact_queue->cancel (principal, params);
  }
  if (method == "artifact.definition_span.ack") {
    if (!artifact_queue) return jsonrpc_error_object ("artifact queue is unavailable");
    return artifact_queue->acknowledge (principal, params);
  }
  if (method == "delegation.queue.status") {
    if (sender_role != "proxy")
      return jsonrpc_error_object ("queue status is restricted to proxies");
    QJsonObject result;
    result["ok"]= true;
    result["artifact"]= artifact_queue ? artifact_queue->counts ():
                                         QJsonObject ();
    return result;
  }
  if (method == "rag.embedding.build_patch") {
    std::lock_guard<std::mutex> rag_guard (rag_delegation_mutex);
    QString requestId= params.value ("request_id").toString ();
    if (requestId.isEmpty () || requestId.size () > 128)
      return jsonrpc_error_object ("invalid RAG request id");
    const auto now= std::chrono::steady_clock::now ();
    const auto ttl= std::chrono::minutes (15);
    for (auto it= rag_patch_cache.begin (); it != rag_patch_cache.end ();)
      if (now - it->second.completed_at > ttl)
        it= rag_patch_cache.erase (it);
      else ++it;
    std::string cacheKey= principal + "\n" + requestId.toStdString ();
    auto cached= rag_patch_cache.find (cacheKey);
    if (cached != rag_patch_cache.end ()) {
      athena_spdlog_info ("rag delegation: returning cached patch for retry");
      return cached->second.result;
    }
    QByteArray jobBytes= QByteArray::fromBase64 (
      params.value ("job").toString ().toUtf8 ());
    QJsonParseError parse;
    QJsonDocument jobDoc= QJsonDocument::fromJson (jobBytes, &parse);
    if (parse.error != QJsonParseError::NoError || !jobDoc.isObject ())
      return jsonrpc_error_object ("invalid delegation job");
    QJsonObject jobRoot= jobDoc.object ();
    if (jobRoot.value ("version").toInt () !=
          athena::rag::delegation::rag_delegation_job_version ||
        jobRoot.value ("persistence_model_version").toInt () !=
          athena::rag::delegation::rag_persistence_model_version)
      return jsonrpc_error_object ("incompatible delegation job/persistence model version");
    athena::rag::delegation::DelegatedJob job;
    for (const QJsonValue& value: jobRoot.value ("files").toArray ()) {
      QJsonObject obj= value.toObject ();
      athena::rag::delegation::DelegatedFile file;
      file.rel_path= obj.value ("rel_path").toString ().toStdString ();
      file.content= QByteArray::fromBase64 (
        obj.value ("content").toString ().toUtf8 ()).toStdString ();
      QJsonValue sizeValue= obj.value ("size");
      QJsonValue mtimeValue= obj.value ("mtime_ns");
      file.size= sizeValue.isString () ? sizeValue.toString ().toLongLong ():
                                        qint64 (sizeValue.toDouble ());
      file.mtime_ns= mtimeValue.isString () ?
        mtimeValue.toString ().toLongLong ():
        qint64 (mtimeValue.toDouble ());
      file.storage_revision=
        obj.value ("storage_revision").toString ().toStdString ();
      file.semantic_revision=
        obj.value ("semantic_revision").toString ().toStdString ();
      if (file.storage_revision.empty () || file.semantic_revision.empty ())
        return jsonrpc_error_object ("delegated file is missing revision hashes");
      job.files.push_back (std::move (file));
    }
    for (const QJsonValue& value: jobRoot.value ("deleted").toArray ())
      job.deleted.push_back (value.toString ().toStdString ());

    std::filesystem::path temp= std::filesystem::temp_directory_path ();
    std::filesystem::path patch= temp /
      ("athena-rag-patch-" +
       athena::rag::delegation::random_hex_id (12) + ".sqlite");
    athena::rag::RagConfig config;
    config.embedding_model= active_options.embedding_model;
    config.embedding_device= active_options.embedding_device;
    config.embedding_runtime= embedding_runtime;
    config.force_reindex= true;
    config.progress= false;
    std::string error;
    bool ok= athena::rag::delegation::build_patch_for_job (
      job, patch, temp, config, error);
    if (!ok) return jsonrpc_error_object (QString::fromStdString (error));
    std::string patchBytes;
    if (!athena::rag::delegation::read_file_bytes (patch, patchBytes)) {
      std::filesystem::remove (patch);
      return jsonrpc_error_object ("failed to read delegated patch");
    }
    std::filesystem::remove (patch);
    QByteArray rawPatch= QByteArray::fromStdString (patchBytes);
    QByteArray compressedPatch= qCompress (rawPatch, 1);
    bool useCompression= !compressedPatch.isEmpty () &&
                         compressedPatch.size () < rawPatch.size ();
    const QByteArray& wirePatch= useCompression ? compressedPatch: rawPatch;
    athena_spdlog_info (
      "rag delegation: patch transport raw-bytes=" +
      std::to_string (rawPatch.size ()) +
      " wire-bytes=" + std::to_string (wirePatch.size ()) +
      " encoding=" + (useCompression ? "qcompress": "raw"));
    QJsonObject result;
    result["ok"]= true;
    const std::string patch_space=
      embedding_runtime && embedding_runtime->available ()
        ? embedding_runtime->space_id ()
        : athena::rag::rag_embedding_space_id_for_model (
            active_options.embedding_model.string ());
    result["embedding_space"]= QString::fromStdString (patch_space);
    result["patch"]= QString::fromLatin1 (
      wirePatch.toBase64 ());
    result["patch_encoding"]= useCompression ? "qcompress": "raw";
    result["patch_uncompressed_bytes"]= QString::number (rawPatch.size ());
    result["patch_wire_bytes"]= QString::number (wirePatch.size ());
    result["request_id"]= requestId;
    QJsonArray deleted;
    for (const std::string& rel: job.deleted)
      deleted.append (QString::fromStdString (rel));
    result["deleted"]= deleted;
    rag_patch_cache[cacheKey]= {result, std::chrono::steady_clock::now ()};
    while (rag_patch_cache.size () > 4) {
      auto oldest= std::min_element (
        rag_patch_cache.begin (), rag_patch_cache.end (),
        [] (const auto& a, const auto& b) {
          return a.second.completed_at < b.second.completed_at;
        });
      if (oldest == rag_patch_cache.end ()) break;
      rag_patch_cache.erase (oldest);
    }
    return result;
  }
  return jsonrpc_error_object ("unknown delegation method");
}

static QJsonObject
handle_delegation_envelope (const QJsonObject& envelope) {
  std::string sender64= envelope.value ("sender").toString ().toStdString ();
  std::string sender;
  std::string error;
  if (!athena::rag::delegation::base64_decode (sender64, sender, error))
    return jsonrpc_error_object (QString::fromStdString (error));
  std::string plain;
  if (!athena::rag::delegation::decrypt_payload (
        delegation_keypair, sender,
        envelope.value ("nonce").toString ().toStdString (),
        envelope.value ("ciphertext").toString ().toStdString (),
        plain, error))
    return jsonrpc_error_object (QString::fromStdString (error));
  QJsonParseError parse;
  QJsonDocument doc= QJsonDocument::fromJson (
    QByteArray::fromStdString (plain), &parse);
  if (parse.error != QJsonParseError::NoError || !doc.isObject ())
    return jsonrpc_error_object ("invalid encrypted JSON-RPC");
  QJsonObject result= handle_delegation_plain_rpc (doc.object (), sender);
  QByteArray resultBytes= QJsonDocument (result).toJson (QJsonDocument::Compact);
  std::string nonce64, cipher64;
  if (!athena::rag::delegation::encrypt_payload (
        delegation_keypair, sender, resultBytes.toStdString (),
        nonce64, cipher64, error))
    return jsonrpc_error_object (QString::fromStdString (error));
  QJsonObject out;
  out["ok"]= true;
  out["sender"]= QString::fromStdString (
    athena::rag::delegation::base64_encode (
      delegation_keypair.public_key));
  out["nonce"]= QString::fromStdString (nonce64);
  out["ciphertext"]= QString::fromStdString (cipher64);
  return out;
}

static void
handle_socket (QTcpSocket* socket) {
  QByteArray* buffer= new QByteArray;
  QObject::connect (socket, &QTcpSocket::readyRead, socket, [socket, buffer] () {
    buffer->append (socket->readAll ());
    int split= buffer->indexOf ("\r\n\r\n");
    if (split < 0) return;
    QByteArray head= buffer->left (split);
    int contentLength= 0;
    for (const QByteArray& line: head.split ('\n')) {
      QByteArray l= line.trimmed ();
      if (l.toLower ().startsWith ("content-length:"))
        contentLength= l.mid (15).trimmed ().toInt ();
    }
    if (buffer->size () < split + 4 + contentLength) return;

    HttpRequest req;
    if (!parse_request (*buffer, req)) {
      write_response (socket, 400, "text/plain", "Bad request");
      delete buffer;
      return;
    }
    if (req.path == "/athena-delegation/v1/identity") {
      if (req.method != "GET") {
        write_response (socket, 405, "text/plain", "Method not allowed");
        delete buffer;
        return;
      }
      write_response (socket, 200, "application/json",
                      json_bytes (identity_result ()));
      delete buffer;
      return;
    }
    if (req.path == "/athena-delegation/v1/rpc") {
      if (req.method != "POST") {
        write_response (socket, 405, "text/plain", "Method not allowed");
        delete buffer;
        return;
      }
      QJsonParseError parse;
      QJsonDocument doc= QJsonDocument::fromJson (req.body, &parse);
      if (parse.error != QJsonParseError::NoError || !doc.isObject ())
        write_response (socket, 400, "application/json",
                        json_bytes (jsonrpc_error_object ("invalid JSON")));
      else {
        QPointer<QTcpSocket> guarded (socket);
        QJsonObject envelope= doc.object ();
        std::thread ([guarded, envelope] () {
          QJsonObject response= handle_delegation_envelope (envelope);
          QMetaObject::invokeMethod (
            QCoreApplication::instance (), [guarded, response] () {
              if (guarded)
                write_response (guarded, 200, "application/json",
                                json_bytes (response));
            }, Qt::QueuedConnection);
        }).detach ();
      }
      delete buffer;
      return;
    }
    write_response (socket, 404, "text/plain", "Not found");
    delete buffer;
  });
  QObject::connect (socket, &QTcpSocket::disconnected, socket,
                    &QTcpSocket::deleteLater);
}

} // namespace

bool
start_delegation_server (const DelegationServerOptions& options) {
  active_options= options;
  if (active_options.listen_address.empty ())
    active_options.listen_address= "127.0.0.1";
  if (active_options.delegation_key_dir.empty ())
    active_options.delegation_key_dir= default_delegation_key_dir ();
  if (active_options.delegation_accepted_clients.empty ())
    active_options.delegation_accepted_clients=
      active_options.delegation_key_dir / "accepted-clients.json";

  bool generated= false;
  std::string key_error;
  if (!athena::rag::delegation::ensure_keypair (
        active_options.delegation_key_dir, "server",
        delegation_keypair, &generated, key_error)) {
    std_error << "rag delegation: failed to load server keypair: "
              << key_error.c_str () << "\n";
    return false;
  }
  delegation_ready= true;
  if (generated)
    io_info << "rag delegation: generated server keypair in "
            << active_options.delegation_key_dir.generic_string ().c_str ()
            << "\n";
  io_info << "rag delegation: server fingerprint "
          << athena::rag::delegation::fingerprint_for_public_key (
               delegation_keypair.public_key).c_str () << "\n";

  ArtifactDelegationQueueOptions artifact_options;
  artifact_options.model_path= active_options.artifact_range_model;
  artifact_options.batch_size=
    std::clamp (active_options.artifact_range_batch_size, 1, 16);
  artifact_options.max_queued_items=
    std::max (1, active_options.artifact_queue_limit);
  artifact_options.max_stored_bytes=
    std::max (1024 * 1024, active_options.artifact_queue_bytes);
  artifact_queue= std::make_unique<ArtifactDelegationQueue> (
    std::move (artifact_options));

  embedding_runtime= std::make_shared<athena::rag::RagEmbedder> ();
  server.reset (new QTcpServer);
  QHostAddress listen (QString::fromStdString (active_options.listen_address));
  if (listen.isNull ()) listen= QHostAddress::LocalHost;
  if (!server->listen (listen, quint16 (options.port))) {
    QByteArray error= server->errorString ().toUtf8 ();
    std_error << "delegation: failed to listen on "
              << active_options.listen_address.c_str () << ":"
              << options.port
              << ": " << error.constData () << "\n";
    return false;
  }

  QObject::connect (server.get (), &QTcpServer::newConnection,
                    server.get (), [] () {
    while (server->hasPendingConnections ())
      handle_socket (server->nextPendingConnection ());
  });

  io_info << "delegation: listening on http://"
          << active_options.listen_address.c_str () << ":" << options.port
          << "/athena-delegation/v1/" << "\n";
  return true;
}

} // namespace athena::delegation
