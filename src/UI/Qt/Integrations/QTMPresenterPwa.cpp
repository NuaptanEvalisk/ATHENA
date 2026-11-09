/******************************************************************************
* MODULE     : QTMPresenterPwa.cpp
* DESCRIPTION: Local-device presenter PWA, served using GNU libmicrohttpd
* COPYRIGHT  : (C) 2026 ATHENA contributors
******************************************************************************/
#include "UI/Qt/Integrations/QTMPresenterPwa.hpp"
#include "UI/Qt/Document/QTMSlideshowWindow.hpp"
#include "UI/Qt/Shell/QTMMainTabWindow.hpp"
#include "UI/Qt/Toolkit/qt_utilities.hpp"
#include "boot.hpp"

#include <QApplication>
#include <QBuffer>
#include <QCoreApplication>
#include <QFile>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QNetworkInterface>
#include <QRandomGenerator>
#include <QSet>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <mutex>

#if ATHENA_HAVE_MICROHTTPD
#include <microhttpd.h>
#include <arpa/inet.h>
#include <netinet/in.h>

namespace {

QString preference (const char* name, const char* fallback) {
  return to_qstring (get_preference (name, fallback));
}

QByteArray fresh_token () {
  QByteArray random (32, '\0');
  for (int i=0; i<4; ++i) {
    const auto bits= QRandomGenerator::system ()->generate64 ();
    std::memcpy (random.data () + i*8, &bits, sizeof (bits));
  }
  return random.toHex ();
}

QString client_ip (MHD_Connection* connection) {
  const auto* info= MHD_get_connection_info (
    connection, MHD_CONNECTION_INFO_CLIENT_ADDRESS);
  if (!info || !info->client_addr) return QStringLiteral ("Unknown");
  const sockaddr* address= info->client_addr;
  if (address->sa_family == AF_INET) {
    const auto* v4= reinterpret_cast<const sockaddr_in*> (address);
    return QHostAddress (ntohl (v4->sin_addr.s_addr)).toString ();
  }
  if (address->sa_family == AF_INET6) {
    const auto* v6= reinterpret_cast<const sockaddr_in6*> (address);
    Q_IPV6ADDR ipv6 {};
    std::memcpy (ipv6.c, &v6->sin6_addr, 16);
    return QHostAddress (ipv6).toString ();
  }
  return QStringLiteral ("Unknown");
}

const char* header (MHD_Connection* connection, const char* name) {
  return MHD_lookup_connection_value (connection, MHD_HEADER_KIND, name);
}

QByteArray http_bytes (const QJsonObject& data) {
  return QJsonDocument (data).toJson (QJsonDocument::Compact);
}

struct request_body {
  QByteArray bytes;
  bool oversized= false;
};

class PresenterPwaServer final {
  struct client_session {
    enum class decision { pending, allowed, denied } state= decision::pending;
    QString address, agent, reported_name;
    std::chrono::steady_clock::time_point last_command {};
  };

  mutable std::mutex mutex_;
  QHash<QByteArray, client_session> clients_;
  QJsonObject state_ {{"active", false}};
  QImage current_, next_;
  quint64 revision_= 0;
  quint64 configuration_generation_= 0;
  bool approval_required_= true;
  bool previews_enabled_= true;
  bool tls_= false;
  bool active_= false;
  QString bind_address_;
  QString asset_root_;
  quint16 port_= 0;
  QString result_= QStringLiteral ("Disabled");
  QStringList addresses_;
  QSet<QString> accepted_hosts_;
  QByteArray cert_, private_key_;
  sockaddr_storage socket_address_ {};
  MHD_Daemon* daemon_= nullptr;

  static MHD_Result respond (MHD_Connection* connection, unsigned status,
                             const QByteArray& bytes, const char* type,
                             const QByteArray& cookie= {}, bool tls= false) {
    MHD_Response* response= MHD_create_response_from_buffer (
      bytes.size (), const_cast<char*> (bytes.constData ()),
      MHD_RESPMEM_MUST_COPY);
    if (!response) return MHD_NO;
    MHD_add_response_header (response, "Content-Type", type);
    MHD_add_response_header (response, "Cache-Control", "no-store, max-age=0");
    MHD_add_response_header (response, "X-Content-Type-Options", "nosniff");
    MHD_add_response_header (response, "X-Frame-Options", "DENY");
    MHD_add_response_header (response, "Referrer-Policy", "no-referrer");
    MHD_add_response_header (response, "Content-Security-Policy",
      "default-src 'self'; script-src 'self'; style-src 'self'; "
      "img-src 'self' data: blob:; connect-src 'self'; manifest-src 'self'; "
      "worker-src 'self'; object-src 'none'; base-uri 'none'; "
      "frame-ancestors 'none'");
    if (!cookie.isEmpty ()) {
      QByteArray value= "athena_presenter=" + cookie +
        "; Path=/; HttpOnly; SameSite=Strict" +
        (tls ? QByteArray ("; Secure") : QByteArray {});
      MHD_add_response_header (response, "Set-Cookie", value.constData ());
    }
    const MHD_Result result= MHD_queue_response (connection, status, response);
    MHD_destroy_response (response);
    return result;
  }

  static MHD_Result json (MHD_Connection* connection, unsigned status,
                          const QJsonObject& object,
                          const QByteArray& cookie= {}, bool tls= false) {
    return respond (connection, status, http_bytes (object),
                    "application/json; charset=utf-8", cookie, tls);
  }

  static MHD_Result access (void* cls, MHD_Connection* connection,
                            const char* url, const char* method,
                            const char*, const char* upload,
                            size_t* size, void** context) {
    if (!*context) {
      *context= new request_body;
      return MHD_YES;
    }
    auto* request= static_cast<request_body*> (*context);
    if (*size) {
      if (request->bytes.size () + *size > 4096) request->oversized= true;
      else request->bytes.append (upload, qsizetype (*size));
      *size= 0;
      return MHD_YES;
    }
    return static_cast<PresenterPwaServer*> (cls)->route (
      connection, QByteArray (url), QByteArray (method), *request);
  }

  static void finished (void*, MHD_Connection*, void** context,
                        MHD_RequestTerminationCode) {
    delete static_cast<request_body*> (*context);
    *context= nullptr;
  }

  bool origin_allowed (MHD_Connection* connection, bool mutation) const {
    const char* raw_host= header (connection, "Host");
    if (!raw_host) return false;
    const QUrl host (QStringLiteral ("http://") + QString::fromLatin1 (raw_host));
    if (!host.isValid () || host.port (-1) != port_ ||
        host.host ().isEmpty () || !host.userInfo ().isEmpty () ||
        (!host.path ().isEmpty () && host.path () != QStringLiteral ("/")))
      return false;
    const QString name= host.host ();
    if (!accepted_hosts_.contains (name)) return false;
    const char* raw_origin= header (connection, "Origin");
    if (raw_origin) {
      const QString expected= (tls_ ? QStringLiteral ("https://") :
                                      QStringLiteral ("http://")) +
                              QString::fromLatin1 (raw_host);
      if (QString::fromLatin1 (raw_origin).compare (
            expected, Qt::CaseInsensitive) != 0) return false;
    }
    if (mutation) {
      const char* proof= header (connection, "X-Athena-Presenter");
      if (!proof || std::strcmp (proof, "1") != 0) return false;
      const char* fetch_site= header (connection, "Sec-Fetch-Site");
      if (fetch_site && std::strcmp (fetch_site, "same-origin") != 0 &&
          std::strcmp (fetch_site, "none") != 0) return false;
    }
    return true;
  }

  void ask_for_approval (QByteArray token, quint64 generation) {
    QTimer::singleShot (0, qApp, [this, token=std::move (token), generation] {
      QString address, agent, device;
      {
        std::lock_guard<std::mutex> lock (mutex_);
        auto found= clients_.constFind (token);
        if (generation != configuration_generation_ ||
            found == clients_.cend () ||
            found->state != client_session::decision::pending) return;
        address= found->address;
        agent= found->agent;
        device= found->reported_name;
      }
      const QString description= QObject::tr (
        "A device requests access to the ATHENA presenter controller.\n\n"
        "Address: %1\nDevice: %2\nUser-Agent: %3\n\n"
        "Allow this device to navigate slides and view the presentation "
        "until the controller server is stopped?")
        .arg (address, device.isEmpty () ? QObject::tr ("Unspecified") : device,
              agent.left (300));
      QMessageBox approval (
        QMessageBox::Question, QObject::tr ("Presenter access request"),
        description, QMessageBox::Yes | QMessageBox::No,
        QTMMainTabWindow::topTabWindow ());
      // User-Agent and reported platform originate from an untrusted device.
      // Display them literally; Qt's automatic rich-text detection must not
      // let a browser forge the contents or format of the approval dialog.
      approval.setTextFormat (Qt::PlainText);
      approval.setDefaultButton (QMessageBox::No);
      const bool approve= approval.exec () == QMessageBox::Yes;
      std::lock_guard<std::mutex> lock (mutex_);
      if (generation != configuration_generation_) return;
      auto found= clients_.find (token);
      if (found != clients_.end () &&
          found->state == client_session::decision::pending)
        found->state= approve ? client_session::decision::allowed :
                                client_session::decision::denied;
    });
  }

  MHD_Result route (MHD_Connection* connection, QByteArray path,
                    QByteArray method, const request_body& request) {
    if (request.oversized)
      return json (connection, MHD_HTTP_CONTENT_TOO_LARGE,
                   {{"error", "Request too large"}});
    const bool mutation= method == "POST";
    if (method != "GET" && !mutation)
      return json (connection, MHD_HTTP_METHOD_NOT_ALLOWED,
                   {{"error", "Use GET or POST"}});
    if (!origin_allowed (connection, mutation))
      return json (connection, MHD_HTTP_FORBIDDEN,
                   {{"error", "Unrecognized host or origin"}});

    if (method == "GET" &&
        (path == "/" || path == "/app.js" || path == "/app.css" ||
         path == "/manifest.webmanifest" || path == "/service-worker.js" ||
         path == "/logo.svg" || path == "/icon-192.png" ||
         path == "/icon-512.png")) {
      static const QHash<QByteArray,QByteArray> files {
        {"/", "index.html"}, {"/app.js", "app.js"},
        {"/app.css", "app.css"},
        {"/manifest.webmanifest", "manifest.webmanifest"},
        {"/service-worker.js", "service-worker.js"},
        {"/logo.svg", "logo.svg"},
        {"/icon-192.png", "icon-192.png"},
        {"/icon-512.png", "icon-512.png"}};
      QFile file (asset_root_ +
                  QString::fromLatin1 (files.value (path)));
      if (!file.open (QIODevice::ReadOnly))
        return json (connection, MHD_HTTP_NOT_FOUND,
                     {{"error", "PWA resource missing"}});
      const char* content_type= path == "/" ? "text/html; charset=utf-8" :
        path == "/app.js" || path == "/service-worker.js" ?
          "application/javascript; charset=utf-8" :
        path == "/app.css" ? "text/css; charset=utf-8" :
        path == "/manifest.webmanifest" ? "application/manifest+json" :
        path == "/logo.svg" ? "image/svg+xml" :
        "image/png";
      return respond (connection, MHD_HTTP_OK, file.readAll (), content_type);
    }

    const QString peer= client_ip (connection);
    const char* cookie= MHD_lookup_connection_value (
      connection, MHD_COOKIE_KIND, "athena_presenter");
    const QByteArray token= cookie ? QByteArray (cookie) : QByteArray {};
    const bool connect= path == "/api/connect" && mutation;
    if (!connect && path != "/api/status" &&
        path != "/api/action" && path != "/api/current.png" &&
        path != "/api/next.png")
      return json (connection, MHD_HTTP_NOT_FOUND,
                   {{"error", "Unknown endpoint"}});

    if (connect) {
      const auto posted= QJsonDocument::fromJson (request.bytes);
      const QString reported= posted.isObject () ?
        posted.object ().value ("device").toString ().left (100) : QString {};
      QByteArray assigned= token;
      bool prompt= false;
      quint64 generation= 0;
      client_session::decision decision;
      {
        std::lock_guard<std::mutex> lock (mutex_);
        auto found= clients_.find (token);
        if (found != clients_.end () && found->address == peer) {
          decision= found->state;
        }
        else {
          if (clients_.size () >= 32)
            return json (connection, MHD_HTTP_TOO_MANY_REQUESTS,
                         {{"error", "Too many controller connections"}});
          assigned= fresh_token ();
          decision= approval_required_ ? client_session::decision::pending :
                                         client_session::decision::allowed;
          const char* agent= header (connection, "User-Agent");
          clients_.insert (assigned, client_session {
            decision, peer, agent ? QString::fromUtf8 (agent).left (300) : QString {},
            reported, {}});
          prompt= approval_required_;
          generation= configuration_generation_;
        }
      }
      if (prompt) ask_for_approval (assigned, generation);
      const char* state= decision == client_session::decision::allowed ?
        "approved" : decision == client_session::decision::pending ?
        "pending" : "denied";
      return json (connection, MHD_HTTP_OK, {{"state", state}}, assigned, tls_);
    }

    client_session::decision decision;
    {
      std::lock_guard<std::mutex> lock (mutex_);
      auto found= clients_.constFind (token);
      if (found == clients_.cend () || found->address != peer)
        return json (connection, MHD_HTTP_UNAUTHORIZED,
                     {{"state", "disconnected"}});
      decision= found->state;
    }
    if (decision != client_session::decision::allowed) {
      const char* state= decision == client_session::decision::pending ?
        "pending" : "denied";
      // Approval is asynchronous. Polling the session's admission state is a
      // normal request, not a forbidden attempt to read the presentation.
      // Do not expose slide metadata or pixels until approval completes.
      if (path == "/api/status" && method == "GET")
        return json (connection, MHD_HTTP_OK, {{"state", state}});
      return json (connection, MHD_HTTP_FORBIDDEN, {{"state", state}});
    }

    if (path == "/api/status" && method == "GET") {
      QJsonObject snapshot;
      {
        std::lock_guard<std::mutex> lock (mutex_);
        snapshot= state_;
        snapshot.insert ("revision", QString::number (revision_));
        snapshot.insert ("hasCurrent", !current_.isNull ());
        snapshot.insert ("hasNext", !next_.isNull ());
      }
      snapshot.insert ("state", "approved");
      return json (connection, MHD_HTTP_OK, snapshot);
    }
    if ((path == "/api/current.png" || path == "/api/next.png") &&
        method == "GET") {
      QImage source;
      {
        std::lock_guard<std::mutex> lock (mutex_);
        source= path == "/api/current.png" ? current_ : next_;
      }
      if (source.isNull ())
        return json (connection, MHD_HTTP_NOT_FOUND,
                     {{"error", "Preview not ready"}});
      QImage small= source.width () > 960 ?
        source.scaledToWidth (960, Qt::SmoothTransformation) : source;
      QByteArray encoded;
      QBuffer buffer (&encoded);
      buffer.open (QIODevice::WriteOnly);
      if (!small.save (&buffer, "PNG", 4))
        return json (connection, MHD_HTTP_INTERNAL_SERVER_ERROR,
                     {{"error", "Preview encoding failed"}});
      return respond (connection, MHD_HTTP_OK, encoded, "image/png");
    }
    if (path == "/api/action" && mutation) {
      const char* type= header (connection, "Content-Type");
      if (!type || std::strncmp (type, "application/json", 16) != 0)
        return json (connection, MHD_HTTP_UNSUPPORTED_MEDIA_TYPE,
                     {{"error", "Expected application/json"}});
      const auto input= QJsonDocument::fromJson (request.bytes);
      if (!input.isObject ())
        return json (connection, MHD_HTTP_BAD_REQUEST,
                     {{"error", "Invalid JSON"}});
      const auto action= input.object ();
      const QString command= action.value ("command").toString ();
      const static QSet<QString> allowed {
        "start", "next", "previous", "first", "last", "jump", "black",
        "end", "timer-reset", "timer-pause", "pointer", "pointer-hide"};
      if (!allowed.contains (command) ||
          (command == "jump" &&
           (!action.value ("page").isDouble () ||
            action.value ("page").toInt () <= 0 ||
            action.value ("page").toInt () > 100000)))
        return json (connection, MHD_HTTP_BAD_REQUEST,
                     {{"error", "Unknown or invalid presenter command"}});
      const double x= action.value ("x").toDouble (-1);
      const double y= action.value ("y").toDouble (-1);
      if (command == "pointer" &&
          (x < 0 || x > 1 || y < 0 || y > 1))
        return json (connection, MHD_HTTP_BAD_REQUEST,
                     {{"error", "Pointer coordinates must be between 0 and 1"}});
      {
        std::lock_guard<std::mutex> lock (mutex_);
        auto found= clients_.find (token);
        if (found == clients_.end () ||
            found->state != client_session::decision::allowed)
          return json (connection, MHD_HTTP_UNAUTHORIZED,
                       {{"error", "Access revoked"}});
        const auto now= std::chrono::steady_clock::now ();
        if (command != QStringLiteral ("pointer-hide") &&
            found->last_command != std::chrono::steady_clock::time_point {} &&
            now - found->last_command < std::chrono::milliseconds (25))
          return json (connection, MHD_HTTP_TOO_MANY_REQUESTS,
                       {{"error", "Commands sent too quickly"}});
        found->last_command= now;
      }
      QTimer::singleShot (0, qApp, [command, action] {
        qtm_slideshow_presenter_action (command, action);
      });
      return json (connection, MHD_HTTP_ACCEPTED, {{"accepted", true}});
    }
    return json (connection, MHD_HTTP_METHOD_NOT_ALLOWED,
                 {{"error", "Unsupported method"}});
  }

public:
  ~PresenterPwaServer () { stop (); }

  void stop () {
    if (daemon_) {
      MHD_stop_daemon (daemon_);
      daemon_= nullptr;
    }
    std::lock_guard<std::mutex> lock (mutex_);
    ++configuration_generation_;
    clients_.clear ();
    active_= false;
    addresses_.clear ();
    accepted_hosts_.clear ();
    cert_.clear ();
    private_key_.clear ();
  }

  void configure () {
    stop ();
    if (preference ("presenter pwa enabled", "off") != "on") {
      result_= QStringLiteral ("Disabled");
      return;
    }
    bind_address_= preference ("presenter pwa listen address", "127.0.0.1").trimmed ();
    const QHostAddress address (bind_address_);
    bool good_port= false;
    const int port= preference ("presenter pwa listen port", "48726")
      .toInt (&good_port);
    if (address.isNull () || !good_port || port < 1 || port > 65535) {
      result_= QStringLiteral ("Invalid address or port");
      return;
    }
    port_= quint16 (port);
    approval_required_= preference ("presenter pwa require approval", "on") == "on";
    previews_enabled_= preference ("presenter pwa previews", "on") == "on";
    asset_root_= qEnvironmentVariable ("ATHENA_PATH") +
                 QStringLiteral ("/misc/pwa/presenter/");
    const QString cert_path= preference ("presenter pwa tls certificate", "");
    const QString key_path= preference ("presenter pwa tls key", "");
    tls_= !cert_path.isEmpty () || !key_path.isEmpty ();
    if (tls_) {
      QFile certificate (cert_path), secret (key_path);
      if (cert_path.isEmpty () || key_path.isEmpty () ||
          !certificate.open (QIODevice::ReadOnly) ||
          !secret.open (QIODevice::ReadOnly)) {
        result_= QStringLiteral ("Cannot read HTTPS certificate/private key");
        return;
      }
      cert_= certificate.readAll ();
      private_key_= secret.readAll ();
      if (MHD_is_feature_supported (MHD_FEATURE_TLS) != MHD_YES) {
        result_= QStringLiteral ("libmicrohttpd was built without HTTPS support");
        return;
      }
    }
    std::memset (&socket_address_, 0, sizeof (socket_address_));
    socklen_t address_length;
    unsigned flags= MHD_USE_INTERNAL_POLLING_THREAD |
                    MHD_USE_THREAD_PER_CONNECTION | MHD_USE_ERROR_LOG;
    if (address.protocol () == QAbstractSocket::IPv6Protocol) {
      flags|= MHD_USE_IPv6;
      auto* v6= reinterpret_cast<sockaddr_in6*> (&socket_address_);
      v6->sin6_family= AF_INET6;
      v6->sin6_port= htons (port_);
      const Q_IPV6ADDR bytes= address.toIPv6Address ();
      std::memcpy (&v6->sin6_addr, bytes.c, 16);
      address_length= sizeof (*v6);
    }
    else {
      auto* v4= reinterpret_cast<sockaddr_in*> (&socket_address_);
      v4->sin_family= AF_INET;
      v4->sin_port= htons (port_);
      v4->sin_addr.s_addr= htonl (address.toIPv4Address ());
      address_length= sizeof (*v4);
    }
    if (tls_) flags|= MHD_USE_TLS;

    if (tls_) daemon_= MHD_start_daemon (
      flags, port_, nullptr, nullptr, &access, this,
      MHD_OPTION_SOCK_ADDR_LEN, address_length, &socket_address_,
      MHD_OPTION_CONNECTION_TIMEOUT, unsigned (15),
      MHD_OPTION_CONNECTION_LIMIT, unsigned (64),
      MHD_OPTION_NOTIFY_COMPLETED, &finished, this,
      MHD_OPTION_HTTPS_MEM_CERT, cert_.constData (),
      MHD_OPTION_HTTPS_MEM_KEY, private_key_.constData (),
      MHD_OPTION_END);
    else daemon_= MHD_start_daemon (
      flags, port_, nullptr, nullptr, &access, this,
      MHD_OPTION_SOCK_ADDR_LEN, address_length, &socket_address_,
      MHD_OPTION_CONNECTION_TIMEOUT, unsigned (15),
      MHD_OPTION_CONNECTION_LIMIT, unsigned (64),
      MHD_OPTION_NOTIFY_COMPLETED, &finished, this,
      MHD_OPTION_END);
    if (!daemon_) {
      result_= QStringLiteral ("Could not listen on %1:%2").arg (bind_address_)
        .arg (port_);
      return;
    }
    const QString scheme= tls_ ? QStringLiteral ("https://") :
                                 QStringLiteral ("http://");
    const bool wildcard= address == QHostAddress::Any ||
                         address == QHostAddress::AnyIPv6;
    QStringList hosts;
    if (wildcard) {
      for (const QHostAddress& candidate: QNetworkInterface::allAddresses ())
        if (!candidate.isNull () &&
            candidate.protocol () == address.protocol () &&
            !candidate.isLoopback ()) hosts.append (candidate.toString ());
      hosts.append (address.protocol () == QAbstractSocket::IPv6Protocol ?
                    QStringLiteral ("::1") : QStringLiteral ("127.0.0.1"));
    }
    else hosts.append (address.toString ());
    hosts.removeDuplicates ();
    for (const QString& host: hosts) {
      accepted_hosts_.insert (host);
      const QString display= host.contains (':') ?
        QStringLiteral ("[%1]").arg (host) : host;
      addresses_.append (scheme + display + QStringLiteral (":%1/").arg (port_));
    }
    if (hosts.contains (QStringLiteral ("127.0.0.1")) ||
        hosts.contains (QStringLiteral ("::1")))
      accepted_hosts_.insert (QStringLiteral ("localhost"));
    result_= QStringLiteral ("Listening: ") + addresses_.join (", ") +
      (tls_ ? QString {} :
        QStringLiteral (" (HTTP: trusted HTTPS required for LAN PWA installation)"));
    active_= true;
  }

  QString status () const { return result_; }
  QStringList urls () const { return addresses_; }

  void publish (const QJsonObject& state, const QImage& current,
                const QImage& next) {
    std::lock_guard<std::mutex> lock (mutex_);
    const QImage new_current= previews_enabled_ ? current : QImage {};
    const QImage new_next= previews_enabled_ ? next : QImage {};
    if (state_.value ("page") != state.value ("page") ||
        current_.cacheKey () != new_current.cacheKey () ||
        next_.cacheKey () != new_next.cacheKey ()) ++revision_;
    state_= state;
    current_= new_current;
    next_= new_next;
  }

  void clear () {
    std::lock_guard<std::mutex> lock (mutex_);
    state_= {{"active", false}};
    current_= QImage {};
    next_= QImage {};
    ++revision_;
  }
};

PresenterPwaServer& server () {
  static PresenterPwaServer value;
  return value;
}

} // namespace

void qtm_presenter_pwa_initialize () {
  server ().configure ();
  QObject::connect (qApp, &QCoreApplication::aboutToQuit, qApp, [] {
    server ().stop ();
  });
}
void qtm_presenter_pwa_configure () { server ().configure (); }
QString qtm_presenter_pwa_status () { return server ().status (); }
QStringList qtm_presenter_pwa_urls () { return server ().urls (); }
void qtm_presenter_pwa_publish (const QJsonObject& state,
                                 const QImage& current, const QImage& next) {
  server ().publish (state, current, next);
}
void qtm_presenter_pwa_clear () { server ().clear (); }

#else

void qtm_presenter_pwa_initialize () {}
void qtm_presenter_pwa_configure () {}
QString qtm_presenter_pwa_status () {
  return QStringLiteral ("libmicrohttpd is not available in this build");
}
QStringList qtm_presenter_pwa_urls () { return {}; }
void qtm_presenter_pwa_publish (const QJsonObject&, const QImage&, const QImage&) {}
void qtm_presenter_pwa_clear () {}

#endif
