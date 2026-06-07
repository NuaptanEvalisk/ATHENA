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

#include "rag_index.hpp"

#include "tm_ostream.hpp"

#include <QByteArray>
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
#include <memory>
#include <sstream>
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
static std::string bearer_token;

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

  indexer.reset (new athena::rag::RagIndex);
  athena::rag::RagConfig config;
  config.vault_root= options.vault_root;
  config.db_path= options.vault_root /
                  athena::rag::rag_read_vault_db_path (options.vault_root);
  config.embedding_model= options.embedding_model;
  config.embedding_device= options.embedding_device;
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
  if (!server->listen (QHostAddress::LocalHost, quint16 (options.port))) {
    QByteArray error= server->errorString ().toUtf8 ();
    std_error << "rag mcp: failed to listen on 127.0.0.1:" << options.port
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

  io_info << "rag mcp: listening on http://127.0.0.1:" << options.port
          << "/mcp" << "\n";
  return true;
}

} // namespace athena::mcp
