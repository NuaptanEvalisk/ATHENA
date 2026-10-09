/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#pragma once
#include "client_settings.hpp"
#include "authority_client.hpp"

namespace athena::hodarium {
// Independent of the candidate key: every replica addresses the same slot.
std::string vault_registration_slot (const std::string& group, const std::string& vault);

// Owner-thread controller: query, persist a candidate before publication if
// absent, register and verify the authority winner. Never silently rotates it.
class vault_registration_client: public QObject {
public:
  using authorization= std::function<bool (const std::string& epoch)>;
  using completion= std::function<void (control_result, std::optional<vault_secret_registration>)>;
  vault_registration_client (client_settings& settings, client_profile profile);
  ~vault_registration_client () override;
  void ensure (std::string vault, std::string epoch, authorization allowed, completion done);
  void cancel ();
private:
  client_settings& settings_;
  client_profile profile_;
  control_http* http_;
  QTimer* deadline_;
  std::string vault_, epoch_, slot_;
  authorization allowed_;
  completion done_;
  void finish (control_result result, std::optional<vault_secret_registration> value= {});
  void post (const QString& path, const QByteArray& bytes, std::function<void (QByteArray)> completed);
  void request (const std::string& commitment);
};
} // namespace athena::hodarium
