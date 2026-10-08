/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#pragma once
#include <QObject>
#include <QByteArray>
#include <QPointer>
#include <QUrl>
#include <functional>
#include <cstdint>
#include <string>
class QNetworkAccessManager;
class QNetworkReply;
namespace athena::hodarium {
void validate_authority_origin (const QUrl& origin);
struct control_response {
  int status= 0;
  QByteArray body;
  std::string error;
  bool oversized= false;
};
// Shared bounded HTTPS transport for enrollment and enrolled-device control.
class control_http: public QObject {
public:
  control_http (QUrl origin, QObject* parent);
  ~control_http () override;
  void request (const QString& path, const QByteArray& body,
    std::function<void (control_response)> completed, bool get= false);
  void cancel ();
private:
  QUrl origin_;
  QNetworkAccessManager* network_;
  QPointer<QNetworkReply> pending_;
  std::uint64_t serial_= 0;
};
} // namespace athena::hodarium
