/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#pragma once
#include "authority_client.hpp"

namespace athena::hodarium {
struct decision_request {
  std::string vault, conflict;
  // Empty request_id selects a read. Writes carry an idempotency identifier
  // that the caller persists before sending, including across process restarts.
  std::string request_id, branches, resolution;
  std::int64_t expected= 0;
};
struct decision_evidence {
  std::string envelope, epoch, nonce, subject;
};

class decision_client: public QObject {
public:
  using completion= std::function<void (control_result, std::optional<conflict_decision>)>;
  using authorization= std::function<bool (const std::string& epoch)>;
  decision_client (QUrl origin, authority_pin pin, device_identity device,
                   std::string member);
  ~decision_client () override;
  void request (std::string epoch, decision_request request,
                authorization authorized, completion completed);
  void cancel ();
  const decision_evidence& evidence () const { return evidence_; }
private:
  decision_evidence evidence_;
  authority_pin pin_;
  device_identity device_;
  std::string member_, epoch_;
  decision_request request_;
  authorization authorized_;
  completion completed_;
  control_http* http_;
  QTimer* deadline_;
  void finish (control_result result, std::optional<conflict_decision> decision= {});
  void post (const QString& path, const QByteArray& bytes,
             std::function<void (QByteArray)> success);
};
} // namespace athena::hodarium
