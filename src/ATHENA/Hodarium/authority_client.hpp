/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#pragma once
#include "device_identity.hpp"
#include "membership.hpp"
#include <QObject>
#include <QUrl>
#include <functional>
#include <memory>
#include <unordered_map>

class QTimer;

namespace athena::hodarium {
class control_http;
enum class control_failure { none, transport, denied, invalid_state, key_store, cancelled };
struct control_result {
  control_failure failure= control_failure::none;
  std::string diagnostic;
};

// Construct and use on one identity/network worker with a Qt event loop. Never
// move this object between threads: its SQLite connection has the same owner.
class authority_client: public QObject {
public:
  authority_client (QUrl origin, authority_pin pin, device_identity device,
    std::string member, const std::filesystem::path& trust_database);
  ~authority_client () override;
  using completion= std::function<void (control_result)>;
  void refresh (completion completed);
  // Suspension revokes current authorization and discards in-flight responses.
  void suspend ();
  bool authorized ();
  bool peer_allowed (const std::string& member, const std::string& public_key,
                     const std::string& epoch);
  std::optional<membership_state> current () const;
  bool context_current (const std::string& group, const std::string& generation,
                        const std::string& epoch);

private:
  QUrl origin_;
  authority_pin pin_;
  device_identity device_;
  std::string member_;
  membership_store store_;
  membership_lease lease_;
  std::optional<membership_state> state_;
  std::unordered_map<std::string, std::string> members_;
  control_http* http_;
  QTimer* refresh_timer_;
  completion completion_;
  std::uint64_t operation_= 0;
  membership_lease::steady::time_point sent_;
  membership_lease::wall::time_point sent_wall_;

  void owner () const;
  void index_members ();
  void finish (control_result result);
  void post (const QString& path, const QByteArray& body,
             std::function<void (QByteArray)> success);
};
} // namespace athena::hodarium
