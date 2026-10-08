/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#pragma once
#include "authority_client.hpp"

namespace athena::hodarium {
struct enrollment_status {
  std::string request, code, member;
  std::int64_t code_expires= 0;
  std::int64_t request_expires= 0;
};
// The owner must persist the protected key HANDLE and initial HTTPS trust pin
// before join(). Pending retrieval credentials stay process-local. After an
// interrupted enrollment, resolve() recovers an already-approved member using
// its protected private key; it neither admits a device nor grants a lease.
class enrollment: public QObject {
public:
  enrollment (QUrl origin, authority_pin pin, device_identity device);
  ~enrollment () override;
  using completion= std::function<void (control_result, enrollment_status)>;
  void join (const std::string& name, completion completed);
  void poll (completion completed);
  void resolve (completion completed);
  void cancel ();
private:
  authority_pin pin_;
  device_identity device_;
  control_http* http_;
  enrollment_status status_;
  std::string credential_;
  completion completion_;
  void begin (completion completed);
  void finish (control_result result= {});
  void clear_credential ();
  void request (const QString& path, const QByteArray& body,
                std::function<void (QByteArray)> success);
  void proof (const std::string& purpose, const std::string& subject,
               std::function<void (std::string, std::string)> success);
  void admission (const QByteArray& bytes, bool initial);
};
} // namespace athena::hodarium
