/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#include "recovery.hpp"
#include "control_http.hpp"
#include <nlohmann/json.hpp>
#include <sodium.h>
#include <set>
#include <stdexcept>

namespace athena::hodarium {
namespace {
using json= nlohmann::json;
json parse (const QByteArray& bytes) {
  if (bytes.size () > 16384) throw std::invalid_argument ("Recovery proof exceeds budget");
  std::vector<std::set<std::string>> keys;
  return json::parse (bytes.constData (), bytes.constData () + bytes.size (),
    [&] (int depth, json::parse_event_t event, json& item) {
      if (depth > 8) throw std::invalid_argument ("Recovery proof exceeds depth budget");
      if (event == json::parse_event_t::object_start) keys.emplace_back ();
      else if (event == json::parse_event_t::object_end) keys.pop_back ();
      else if (event == json::parse_event_t::key && !keys.back ().insert (item.get<std::string> ()).second)
        throw std::invalid_argument ("Duplicate recovery proof field");
      return true;
    });
}
QByteArray decode (const std::string& value, std::size_t size= 0) {
  if (value.size () > 16384) throw std::invalid_argument ("Recovery field exceeds budget");
  QByteArray bytes (value.size (), '\0');
  std::size_t count= 0;
  if (sodium_base642bin (reinterpret_cast<unsigned char*> (bytes.data ()), bytes.size (),
      value.data (), value.size (), nullptr, &count, nullptr,
      sodium_base64_VARIANT_URLSAFE_NO_PADDING) != 0 || (size && count != size))
    throw std::invalid_argument ("Invalid recovery proof encoding");
  bytes.resize (count);
  if (bytes.toBase64 (QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals).toStdString () != value)
    throw std::invalid_argument ("Noncanonical recovery proof encoding");
  return bytes;
}
void signature (const std::string& key, const std::string& sig, const QByteArray& message) {
  auto k= decode (key, crypto_sign_PUBLICKEYBYTES), s= decode (sig, crypto_sign_BYTES);
  if (crypto_sign_verify_detached (reinterpret_cast<const unsigned char*> (s.constData ()),
      reinterpret_cast<const unsigned char*> (message.constData ()), message.size (),
      reinterpret_cast<const unsigned char*> (k.constData ())) != 0)
    throw std::invalid_argument ("Invalid Hodarium recovery signature");
}
}
authority_pin verify_current_recovery (const authority_pin& previous,
  const std::string& recovery_key, const QByteArray& envelope, const std::string& nonce) {
  if (sodium_init () < 0) throw std::runtime_error ("Cannot initialize recovery verification");
  decode (nonce, 32); decode (previous.group, 32);
  const auto wrapper= parse (envelope);
  auto payload= decode (wrapper.at ("payload").get<std::string> ());
  auto value= parse (payload);
  authority_pin next {value.at ("group").get<std::string> (),
    value.at ("authority").get<std::string> (), value.at ("generation").get<std::string> ()};
  decode (next.generation, 32);
  if (!value.at ("protocol").is_number_integer () || value.at ("protocol") != 1 ||
      next.group != previous.group || next.generation == previous.generation || value.at ("nonce") != nonce)
    throw std::invalid_argument ("Recovery proof does not match the requested trust transition");
  const auto& proof= value.at ("recovery");
  if (proof.at ("generation") != next.generation)
    throw std::invalid_argument ("Recovery authorization generation mismatch");
  std::string challenge= proof.at ("challenge"); decode (challenge, 32);
  auto message= json::array ({"ATHENA-HODARIUM-PROOF-v1", next.group, "recover",
    next.generation + "." + next.public_key, challenge}).dump ();
  signature (recovery_key, proof.at ("signature").get<std::string> (), QByteArray::fromStdString (message));
  signature (next.public_key, wrapper.at ("signature").get<std::string> (),
    QByteArrayLiteral ("ATHENA-HODARIUM-RECOVERY-CURRENT-v1\0") + payload);
  return next;
}

void query_authority_recovery (control_http& http, const client_profile& previous,
  std::function<void (control_result, std::optional<recovery_candidate>)> completed) {
  if (!completed) throw std::invalid_argument ("Recovery query requires completion");
  if (sodium_init () < 0) throw std::runtime_error ("Cannot initialize recovery query");
  QByteArray random (32, '\0'); randombytes_buf (random.data (), random.size ());
  auto nonce= random.toBase64 (QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals).toStdString ();
  auto deadline= std::chrono::steady_clock::now () + std::chrono::minutes (2);
  http.request (QString::fromStdString ("/api/recovery/current/" + nonce), {},
    [previous, nonce, deadline, completed= std::move (completed)] (control_response response) {
      if (!response.error.empty ()) {
        completed ({response.oversized ? control_failure::invalid_state : control_failure::transport,
          std::move (response.error)}, std::nullopt); return;
      }
      std::optional<recovery_candidate> candidate;
      try {
        if (std::chrono::steady_clock::now () >= deadline)
          throw std::invalid_argument ("Recovery query expired; query again before approval");
        recovery_candidate next;
        next.proposed_= verify_current_recovery (previous.pin, previous.recovery_public_key, response.body, nonce);
        next.previous_= previous; next.envelope_= std::move (response.body);
        next.nonce_= nonce; next.deadline_= deadline;
        candidate= std::move (next);
      } catch (const std::exception& e) {
        completed ({control_failure::invalid_state, e.what ()}, std::nullopt); return;
      }
      completed ({}, std::move (candidate));
    }, true);
}
} // namespace athena::hodarium
