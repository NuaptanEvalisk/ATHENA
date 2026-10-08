/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#include "control_http.hpp"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSslConfiguration>
#include <QSslSocket>
#include <QTimer>
#include <QThread>
#include <memory>
#include <stdexcept>

namespace athena::hodarium {
void validate_authority_origin (const QUrl& origin) {
  if (!origin.isValid () || origin.scheme () != "https" || origin.host ().isEmpty () ||
      !origin.userInfo ().isEmpty () || !origin.path ().isEmpty () ||
      origin.hasQuery () || origin.hasFragment ())
    throw std::invalid_argument ("Hodarium authority must be an HTTPS origin");
}
control_http::control_http (QUrl origin, QObject* parent): QObject (parent),
  origin_ (std::move (origin)), network_ (new QNetworkAccessManager (this)) {
  validate_authority_origin (origin_);
}
control_http::~control_http () { cancel (); }
void control_http::cancel () {
  ++serial_;
  if (pending_) {
    auto* reply= pending_.data (); pending_.clear ();
    disconnect (reply, nullptr, this, nullptr);
    reply->abort (); reply->deleteLater ();
  }
}
void control_http::request (const QString& path, const QByteArray& body,
  std::function<void (control_response)> completed, bool get) {
  if (QThread::currentThread () != thread () || pending_)
    throw std::logic_error ("Hodarium HTTP owner or request state violation");
  QUrl address= origin_; address.setPath (path);
  QNetworkRequest request (address);
  request.setHeader (QNetworkRequest::ContentTypeHeader, "application/json");
  request.setAttribute (QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
  request.setAttribute (QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
  request.setAttribute (QNetworkRequest::CacheSaveControlAttribute, false);
  auto tls= QSslConfiguration::defaultConfiguration ();
  tls.setProtocol (QSsl::TlsV1_3OrLater); tls.setPeerVerifyMode (QSslSocket::VerifyPeer);
  request.setSslConfiguration (tls); request.setTransferTimeout (15000);
  auto* reply= get ? network_->get (request) : network_->post (request, body);
  pending_= reply;
  const auto serial= ++serial_;
  auto response= std::make_shared<control_response> ();
  auto* deadline= new QTimer (reply); deadline->setSingleShot (true);
  connect (deadline, &QTimer::timeout, reply, &QNetworkReply::abort);
  deadline->start (15000);
  reply->setReadBufferSize (64*1024);
  connect (reply, &QNetworkReply::readyRead, this, [reply, response] {
    response->body+= reply->readAll ();
    if (response->body.size () > 8*1024*1024) {
      response->oversized= true; reply->abort ();
    }
  });
  connect (reply, &QNetworkReply::finished, this,
    [this, reply, response, deadline, serial, completed= std::move (completed)] () mutable {
      deadline->stop (); reply->deleteLater ();
      if (serial != serial_) return;
      pending_.clear ();
      response->status= reply->attribute (QNetworkRequest::HttpStatusCodeAttribute).toInt ();
      if (response->oversized) response->error= "Hodarium control response exceeds budget";
      else if (reply->error () != QNetworkReply::NoError || response->status != 200)
        response->error= "Hodarium HTTPS request failed (HTTP " + std::to_string (response->status) +
          "): " + reply->errorString ().toStdString ();
      completed (std::move (*response));
    });
}
} // namespace athena::hodarium
