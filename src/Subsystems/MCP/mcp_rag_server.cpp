/******************************************************************************
* MODULE     : mcp_rag_server.cpp
* DESCRIPTION: MCP Streamable HTTP server for Continuous RAG
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "mcp_rag_server.hpp"
#include "artifact_delegation_queue.hpp"

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
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

#include <algorithm>
#include <chrono>
#include <mutex>
#include <memory>
#include <sstream>
#include <fstream>
#include <thread>
#include <unordered_map>

namespace athena::mcp {
namespace {

struct HttpRequest {
  QByteArray method;
  QByteArray path;
  std::unordered_map<QByteArray, QByteArray> headers;
  QByteArray body;
};

static std::unique_ptr<QTcpServer> server;
static std::unique_ptr<QTimer> scan_timer;
static std::unique_ptr<athena::rag::RagIndex> indexer;
static std::shared_ptr<athena::rag::RagEmbedder> embedding_runtime;
static std::unique_ptr<ArtifactDelegationQueue> artifact_queue;
static std::mutex rag_delegation_mutex;
struct RagPatchCacheEntry {
  QJsonObject result;
  std::chrono::steady_clock::time_point completed_at;
};
static std::unordered_map<std::string,RagPatchCacheEntry> rag_patch_cache;
static std::string bearer_token;
static RagServerOptions active_options;
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
  out += "Access-Control-Allow-Origin: http://127.0.0.1\r\n";
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

static QJsonObject
jsonrpc_error (const QJsonValue& id, int code, const QString& message) {
  QJsonObject error;
  error["code"]= code;
  error["message"]= message;
  QJsonObject root;
  root["jsonrpc"]= "2.0";
  root["id"]= id;
  root["error"]= error;
  return root;
}

static QJsonObject
jsonrpc_result (const QJsonValue& id, const QJsonObject& result) {
  QJsonObject root;
  root["jsonrpc"]= "2.0";
  root["id"]= id;
  root["result"]= result;
  return root;
}

static QJsonObject
text_tool_result (const QString& text) {
  QJsonArray content;
  QJsonObject item;
  item["type"]= "text";
  item["text"]= text;
  content.append (item);
  QJsonObject result;
  result["content"]= content;
  return result;
}

static QString
chunk_uri (const std::string& chunk_id) {
  return "athena-rag://chunk/" + QString::fromStdString (chunk_id);
}

static QString
document_uri (const std::string& rel_path) {
  return "athena-rag://document/" +
         QString::fromLatin1 (QUrl::toPercentEncoding (
           QString::fromStdString (rel_path)));
}

static QJsonObject
chunk_json (const athena::rag::RagChunk& c) {
  QJsonObject o;
  o["chunk_id"]= QString::fromStdString (c.chunk_id);
  o["resource"]= chunk_uri (c.chunk_id);
  o["document_resource"]= document_uri (c.rel_path);
  o["rel_path"]= QString::fromStdString (c.rel_path);
  o["kind"]= QString::fromStdString (c.kind);
  o["tree_path"]= QString::fromStdString (c.tree_path);
  o["anchor"]= QString::fromStdString (c.anchor);
  o["title"]= QString::fromStdString (c.title);
  o["heading_path"]= QString::fromStdString (c.heading_path);
  o["snippet"]= QString::fromStdString (c.source);
  o["score"]= c.score;
  return o;
}

static QString
chunks_text (const std::vector<athena::rag::RagChunk>& chunks,
             bool include_text= false) {
  QJsonArray arr;
  for (const athena::rag::RagChunk& c: chunks) {
    QJsonObject o= chunk_json (c);
    if (include_text) o["text"]= QString::fromStdString (c.text);
    arr.append (o);
  }
  return QString::fromUtf8 (QJsonDocument (arr).toJson (
    QJsonDocument::Indented));
}

static QJsonObject
tool_schema (const QString& name, const QString& description,
             const QJsonObject& properties,
             const QJsonArray& required= QJsonArray ()) {
  QJsonObject schema;
  schema["type"]= "object";
  schema["properties"]= properties;
  schema["required"]= required;
  QJsonObject tool;
  tool["name"]= name;
  tool["description"]= description;
  tool["inputSchema"]= schema;
  return tool;
}

static QJsonObject
tools_list_result () {
  QJsonArray tools;
  QJsonObject queryProp;
  queryProp["type"]= "string";
  QJsonObject limitProp;
  limitProp["type"]= "integer";
  limitProp["minimum"]= 1;
  limitProp["maximum"]= 50;
  QJsonObject chunkProp;
  chunkProp["type"]= "string";
  QJsonObject relProp;
  relProp["type"]= "string";

  QJsonObject searchProps;
  searchProps["query"]= queryProp;
  searchProps["limit"]= limitProp;
  tools.append (tool_schema ("athena_rag_search",
                             "Search indexed ATHENA vault chunks.",
                             searchProps, QJsonArray { "query" }));

  QJsonObject chunkProps;
  chunkProps["chunk_id"]= chunkProp;
  tools.append (tool_schema ("athena_rag_read_chunk",
                             "Read a single indexed chunk.",
                             chunkProps, QJsonArray { "chunk_id" }));

  QJsonObject docProps;
  docProps["rel_path"]= relProp;
  tools.append (tool_schema ("athena_rag_read_document",
                             "Read a vault document by relative path.",
                             docProps, QJsonArray { "rel_path" }));

  tools.append (tool_schema ("athena_rag_status",
                             "Return Continuous RAG index status.",
                             QJsonObject ()));

  QJsonObject relatedProps;
  relatedProps["chunk_id"]= chunkProp;
  relatedProps["limit"]= limitProp;
  tools.append (tool_schema ("athena_rag_related",
                             "Return nearby chunks from the same document.",
                             relatedProps, QJsonArray { "chunk_id" }));

  QJsonObject backlinkProps;
  backlinkProps["target"]= queryProp;
  backlinkProps["limit"]= limitProp;
  tools.append (tool_schema ("athena_rag_backlinks",
                             "Return chunks mentioning a target link.",
                             backlinkProps, QJsonArray { "target" }));

  QJsonObject result;
  result["tools"]= tools;
  return result;
}

static QJsonObject
call_tool (const QString& name, const QJsonObject& args) {
  if (name == "athena_rag_status") {
    athena::rag::RagStatus s= indexer->status ();
    QJsonObject o;
    o["open"]= s.open;
    o["vault_root"]= QString::fromStdString (s.vault_root);
    o["db_path"]= QString::fromStdString (s.db_path);
    o["embedding_model"]= QString::fromStdString (s.embedding_model);
    o["embeddings_enabled"]= s.embeddings_enabled;
    o["embedding_warning"]= QString::fromStdString (s.embedding_warning);
    o["document_count"]= s.document_count;
    o["chunk_count"]= s.chunk_count;
    o["malformed_count"]= s.malformed_count;
    o["last_error"]= QString::fromStdString (s.last_error);
    return text_tool_result (QString::fromUtf8 (
      QJsonDocument (o).toJson (QJsonDocument::Indented)));
  }
  if (name == "athena_rag_search") {
    std::string query= args.value ("query").toString ().toStdString ();
    int limit= args.value ("limit").toInt (10);
    return text_tool_result (chunks_text (indexer->search (query, limit)));
  }
  if (name == "athena_rag_read_chunk") {
    std::string id= args.value ("chunk_id").toString ().toStdString ();
    std::optional<athena::rag::RagChunk> c= indexer->read_chunk (id);
    if (!c) return text_tool_result ("Chunk not found.");
    QJsonObject o= chunk_json (*c);
    o["text"]= QString::fromStdString (c->text);
    return text_tool_result (QString::fromUtf8 (
      QJsonDocument (o).toJson (QJsonDocument::Indented)));
  }
  if (name == "athena_rag_read_document") {
    std::string rel= args.value ("rel_path").toString ().toStdString ();
    return text_tool_result (QString::fromStdString (
      indexer->read_document (rel)));
  }
  if (name == "athena_rag_related") {
    std::string id= args.value ("chunk_id").toString ().toStdString ();
    int limit= args.value ("limit").toInt (10);
    return text_tool_result (chunks_text (indexer->related (id, limit)));
  }
  if (name == "athena_rag_backlinks") {
    std::string target= args.value ("target").toString ().toStdString ();
    int limit= args.value ("limit").toInt (10);
    return text_tool_result (chunks_text (indexer->backlinks (target, limit)));
  }
  return text_tool_result ("Unknown tool: " + name);
}

static QJsonObject
resources_list_result () {
  QJsonArray resources;
  for (const athena::rag::RagChunk& c: indexer->list_chunks (100)) {
    QJsonObject o;
    o["uri"]= chunk_uri (c.chunk_id);
    o["name"]= QString::fromStdString (c.title.empty ()? c.chunk_id: c.title);
    o["description"]= QString::fromStdString (c.rel_path + " " + c.tree_path);
    o["mimeType"]= "text/plain";
    resources.append (o);
  }
  QJsonObject result;
  result["resources"]= resources;
  return result;
}

static QJsonObject
resource_templates_result () {
  QJsonArray templates;
  QJsonObject chunk;
  chunk["uriTemplate"]= "athena-rag://chunk/{chunk_id}";
  chunk["name"]= "ATHENA RAG chunk";
  chunk["description"]= "Indexed ATHENA semantic chunk.";
  chunk["mimeType"]= "text/plain";
  templates.append (chunk);
  QJsonObject doc;
  doc["uriTemplate"]= "athena-rag://document/{rel_path}";
  doc["name"]= "ATHENA vault document";
  doc["description"]= "Raw ATHENA document by vault-relative path.";
  doc["mimeType"]= "text/plain";
  templates.append (doc);
  QJsonObject result;
  result["resourceTemplates"]= templates;
  return result;
}

static QJsonObject
resources_read_result (const QString& uri) {
  QJsonArray contents;
  QJsonObject item;
  item["uri"]= uri;
  item["mimeType"]= "text/plain";
  if (uri.startsWith ("athena-rag://chunk/")) {
    std::string id= uri.mid (QString ("athena-rag://chunk/").size ())
                     .toStdString ();
    std::optional<athena::rag::RagChunk> c= indexer->read_chunk (id);
    item["text"]= c? QString::fromStdString (c->text): QString ();
  }
  else if (uri.startsWith ("athena-rag://document/")) {
    QString rel= QUrl::fromPercentEncoding (
      uri.mid (QString ("athena-rag://document/").size ()).toUtf8 ());
    item["text"]= QString::fromStdString (
      indexer->read_document (rel.toStdString ()));
  }
  else {
    item["text"]= "";
  }
  contents.append (item);
  QJsonObject result;
  result["contents"]= contents;
  return result;
}

static QJsonObject
handle_rpc (const QJsonObject& request) {
  QJsonValue id= request.value ("id");
  QString method= request.value ("method").toString ();
  QJsonObject params= request.value ("params").toObject ();
  if (method == "initialize") {
    QJsonObject caps;
    caps["tools"]= QJsonObject ();
    caps["resources"]= QJsonObject ();
    QJsonObject info;
    info["name"]= "athena-continuous-rag";
    info["version"]= "0.1";
    QJsonObject result;
    result["protocolVersion"]= "2025-06-18";
    result["capabilities"]= caps;
    result["serverInfo"]= info;
    return jsonrpc_result (id, result);
  }
  if (method == "tools/list")
    return jsonrpc_result (id, tools_list_result ());
  if (method == "tools/call") {
    QString name= params.value ("name").toString ();
    QJsonObject args= params.value ("arguments").toObject ();
    return jsonrpc_result (id, call_tool (name, args));
  }
  if (method == "resources/list")
    return jsonrpc_result (id, resources_list_result ());
  if (method == "resources/templates/list")
    return jsonrpc_result (id, resource_templates_result ());
  if (method == "resources/read")
    return jsonrpc_result (
      id, resources_read_result (params.value ("uri").toString ()));
  if (method.startsWith ("notifications/")) return QJsonObject ();
  return jsonrpc_error (id, -32601, "Unknown method: " + method);
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

static bool
authorized_origin (const HttpRequest& req) {
  auto it= req.headers.find ("origin");
  if (it == req.headers.end ()) return true;
  QByteArray origin= it->second;
  return origin.startsWith ("http://127.0.0.1") ||
         origin.startsWith ("http://localhost");
}

static bool
authorized_bearer (const HttpRequest& req) {
  auto it= req.headers.find ("authorization");
  if (it == req.headers.end ()) return false;
  QByteArray expected= "Bearer " + QByteArray::fromStdString (bearer_token);
  return it->second == expected;
}

static std::filesystem::path
default_delegation_key_dir () {
  const char* xdg= getenv ("XDG_CONFIG_HOME");
  const char* home= getenv ("HOME");
  std::filesystem::path base=
    xdg != nullptr && xdg[0] != '\0' ? std::filesystem::path (xdg):
    (home == nullptr || home[0] == '\0' ? std::filesystem::path ("."):
      std::filesystem::path (home) / ".config");
  std::filesystem::path current= base / "ATHENA" / "delegation";
  std::filesystem::path legacy= base / "ATHENA" / "rag-delegation";
  std::error_code ec;
  if (!std::filesystem::exists (current) &&
      std::filesystem::exists (legacy))
    std::filesystem::rename (legacy, current, ec);
  return current;
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
  o["protocol"]= 1;
  o["public_key"]= QString::fromStdString (
    athena::rag::delegation::base64_encode (
      delegation_keypair.public_key));
  o["fingerprint"]= QString::fromStdString (
    athena::rag::delegation::fingerprint_for_public_key (
      delegation_keypair.public_key));
  QJsonArray caps;
  caps.append ("athena-delegation-v1");
  caps.append ("rag-embedding-v1");
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
      file.content_hash= obj.value ("content_hash").toString ().toStdString ();
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
    if (req.path != "/mcp") {
      write_response (socket, 404, "text/plain", "Not found");
      delete buffer;
      return;
    }
    if (!authorized_origin (req)) {
      write_response (socket, 403, "text/plain", "Bad Origin");
      delete buffer;
      return;
    }
    if (!authorized_bearer (req)) {
      write_response (socket, 401, "text/plain", "Authorization required");
      delete buffer;
      return;
    }
    if (req.method == "OPTIONS") {
      write_response (socket, 200, "text/plain", "");
      delete buffer;
      return;
    }
    if (req.method == "GET") {
      QByteArray body= "event: message\ndata: {\"jsonrpc\":\"2.0\","
                       "\"method\":\"notifications/initialized\"}\n\n";
      write_response (socket, 200, "text/event-stream", body);
      delete buffer;
      return;
    }
    if (req.method != "POST") {
      write_response (socket, 405, "text/plain", "Method not allowed");
      delete buffer;
      return;
    }

    QJsonParseError error;
    QJsonDocument doc= QJsonDocument::fromJson (req.body, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject ()) {
      write_response (socket, 400, "application/json",
                      json_bytes (jsonrpc_error (QJsonValue (),
                                                 -32700, "Invalid JSON")));
      delete buffer;
      return;
    }
    QJsonObject response= handle_rpc (doc.object ());
    if (response.isEmpty ())
      write_response (socket, 202, "application/json", "{}");
    else
      write_response (socket, 200, "application/json", json_bytes (response));
    delete buffer;
  });
  QObject::connect (socket, &QTcpSocket::disconnected, socket,
                    &QTcpSocket::deleteLater);
}

} // namespace

bool
start_rag_server (const RagServerOptions& options) {
  if (options.vault_root.empty ()) return false;
  if (options.bearer_token.empty ()) {
    std_error << "rag mcp: bearer token is empty" << "\n";
    return false;
  }

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

  indexer.reset (new athena::rag::RagIndex);
  embedding_runtime= std::make_shared<athena::rag::RagEmbedder> ();
  athena::rag::RagConfig config;
  config.vault_root= options.vault_root;
  config.db_path= options.vault_root /
                  athena::rag::rag_read_vault_db_path (options.vault_root);
  config.embedding_model= options.embedding_model;
  config.embedding_device= options.embedding_device;
  config.embedding_runtime= embedding_runtime;
  config.force_reindex= options.force_reindex;

  if (options.index_jobs > 1) {
    athena::rag::RagConfig preconfig= config;
    preconfig.force_reindex= true;
    preconfig.load_embedding_model= false;
    athena::rag::RagIndex preindexer;
    if (!preindexer.open (preconfig)) return false;
    if (!preindexer.parallel_reindex (options.index_jobs)) return false;
    config.force_reindex= false;
  }

  if (!indexer->open (config)) return false;
  if (options.index_jobs <= 1) indexer->scan_once ();
  indexer->set_progress_enabled (false);

  bearer_token= options.bearer_token;
  server.reset (new QTcpServer);
  QHostAddress listen (QString::fromStdString (active_options.listen_address));
  if (listen.isNull ()) listen= QHostAddress::LocalHost;
  if (!server->listen (listen, quint16 (options.port))) {
    QByteArray error= server->errorString ().toUtf8 ();
    std_error << "rag mcp: failed to listen on "
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

  scan_timer.reset (new QTimer);
  scan_timer->setInterval (2000);
  QObject::connect (scan_timer.get (), &QTimer::timeout,
                    scan_timer.get (), [] () { indexer->scan_once (); });
  scan_timer->start ();

  io_info << "rag mcp: listening on http://"
          << active_options.listen_address.c_str () << ":" << options.port
          << "/mcp" << "\n";
  return true;
}

} // namespace athena::mcp
