/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#pragma once
#include "authority_client.hpp"
#include <vector>

namespace athena::hodarium {
struct presence_context {
  std::string group, member, generation, epoch;
};
struct peer_presence {
  std::string member;
  std::vector<std::string> direct, relays;
  std::int64_t expires= 0;
};
struct presence_page {
  std::vector<peer_presence> entries;
  std::string next;
};
enum class presence_operation { publish, list, withdraw };
struct presence_request {
  presence_operation operation= presence_operation::list;
  std::vector<std::string> direct, relays;
  std::string after;
};

// A single page/operation, owned by one network worker. The predicate must
// check the exact context against the owner's live membership lease. Presence
// never grants authorization and is not stored as durable device identity.
class rendezvous_client: public QObject {
public:
  rendezvous_client (QUrl origin, device_identity device);
  ~rendezvous_client () override;
  using completion= std::function<void (control_result, presence_page)>;
  void request (presence_context context, presence_request request,
    std::function<bool (const presence_context&)> authorized, completion completed);
  void cancel ();
private:
  device_identity device_;
  control_http* http_;
  QTimer* deadline_;
  presence_context context_;
  presence_request request_;
  std::function<bool (const presence_context&)> authorized_;
  completion completion_;
  void finish (control_result result, presence_page page= {});
  void post (const QString& path, const QByteArray& bytes,
    std::function<void (QByteArray)> success);
};

struct presence_snapshot {
  std::vector<peer_presence> peers;
  std::string diagnostic;
};
// Ephemeral discovery only. Never infer membership from a directory record.
class presence_directory: public QObject {
public:
  using authorization= std::function<bool (const presence_context&)>;
  using observer= std::function<void (const presence_snapshot&)>;
  presence_directory (QUrl origin, device_identity device,
    authorization authorized, observer changed);
  ~presence_directory () override;
  void start (presence_context context);
  void stop ();
  void set_routes (std::vector<std::string> direct, std::vector<std::string> relays);
  presence_snapshot snapshot () const;
private:
  using clock= std::chrono::steady_clock;
  rendezvous_client client_;
  authorization authorized_;
  observer changed_;
  presence_context context_;
  presence_snapshot snapshot_;
  presence_request publication_;
  std::vector<peer_presence> collecting_;
  QTimer* timer_;
  clock::time_point next_{}, cycle_started_{}, received_{};
  bool active_= false, busy_= false;
  std::uint64_t serial_= 0;
  void tick ();
  void page (std::string after);
  void failed (control_result result);
  void notify ();
};
} // namespace athena::hodarium
