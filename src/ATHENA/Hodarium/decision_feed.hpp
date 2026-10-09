/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#pragma once
#include "decisions.hpp"
#include <filesystem>
#include <vector>
struct sqlite3;
namespace athena::hodarium {
struct decision_feed_record {
  std::int64_t sequence= 0;
  conflict_decision decision;
  decision_evidence evidence;
};
// Dedicated owner-thread database, pinned to one authority generation. Latest
// records and the download cursor commit atomically; acknowledgement is separate
// so a crash or unavailable payload cannot lose work after downloading a page.
class decision_feed_store {
public:
  decision_feed_store (const std::filesystem::path& path, authority_pin pin);
  ~decision_feed_store ();
  decision_feed_store (const decision_feed_store&)= delete;
  decision_feed_store& operator= (const decision_feed_store&)= delete;
  std::int64_t cursor () const;
  // Completeness against the last verified watermark, not a membership lease
  // or a claim that the authority has had no newer decisions since that page.
  bool caught_up () const;
  bool accept (const decision_evidence& page, std::int64_t requested_after,
               std::uint32_t requested_limit= 64); // true: more pages
  std::vector<decision_feed_record> pending (std::int64_t after= 0,
                                           std::uint32_t limit= 64) const;
  std::optional<decision_feed_record> latest (const std::string& vault,
                                             const std::string& conflict) const;
  void acknowledge (const std::string& vault, const std::string& conflict,
                    std::int64_t version);
private:
  sqlite3* db_= nullptr;
  authority_pin pin_;
};
class decision_feed_client: public QObject {
public:
  using authorization= decision_client::authorization;
  using completion= std::function<void (control_result, bool more)>;
  decision_feed_client (QUrl origin, authority_pin pin, device_identity device,
                        std::string member, decision_feed_store& store);
  ~decision_feed_client () override;
  void refresh (std::string epoch, authorization authorized, completion completed);
  void cancel ();
private:
  authority_pin pin_;
  device_identity device_;
  std::string member_, epoch_;
  decision_feed_store& store_;
  authorization authorized_;
  completion completed_;
  control_http* http_;
  QTimer* deadline_;
  void finish (control_result result, bool more= false);
  void post (const QString& path, const QByteArray& bytes,
             std::function<void (QByteArray)> success);
};
} // namespace athena::hodarium
