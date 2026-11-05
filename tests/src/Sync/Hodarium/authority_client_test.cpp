/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#include "authority_client.hpp"
#include "enrollment.hpp"
#include "client_settings.hpp"
#include "profile_session.hpp"
#include "control_http.hpp"
#include "rendezvous.hpp"
#include "decisions.hpp"
#include <QCoreApplication>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QTimer>
#include <nlohmann/json.hpp>
#include <cstdlib>
#include <fstream>
#include <iostream>

using namespace athena::hodarium;
using json= nlohmann::json;
int main (int argc, char** argv) {
  QCoreApplication app (argc, argv);
  try {
    if (argc != 3 || !std::getenv ("ATHENA_HODARIUM_ISOLATED_KEYRING"))
      throw std::runtime_error ("Use the isolated authority smoke launcher");
    auto root= std::filesystem::path (argv[2]);
    if (std::string (argv[1]) == "prepare") {
      auto device= create_device_identity ();
      std::ofstream out (root / "device.json");
      out << json{{"handle", device.handle}, {"public_key", device.public_key}};
      if (!out) throw std::runtime_error ("Cannot write isolated public identity");
      return 0;
    }
    json config, device;
    std::ifstream (root / "ready.json") >> config;
    std::ifstream (root / "device.json") >> device;
    // Add this isolated authority's certificate as a trust root, not an SSL
    // error bypass. Production uses the platform's normal certificate trust.
    auto tls= QSslConfiguration::defaultConfiguration ();
    auto pem= QByteArray::fromStdString (config.at ("certificate").get<std::string> ());
    tls.addCaCertificate (QSslCertificate (pem));
    QSslConfiguration::setDefaultConfiguration (tls);
    authority_pin pin{config.at ("group"), config.at ("authority"), config.at ("generation")};
    device_identity identity{device.at ("handle"), device.at ("public_key")};
    auto origin= QUrl (QString::fromStdString (config.at ("origin")));
    {
      client_settings settings (root / "settings.sqlite");
      client_profile profile{origin, pin, config.at ("recovery_public_key"),
        identity, "Isolated native client", "", false};
      settings.add_pending (profile);
      bool rejected= false;
      try { settings.set_enabled (pin.group, true); }
      catch (const std::invalid_argument&) { rejected= true; }
      if (!rejected) throw std::runtime_error ("Pending enrollment enabled");
    }
    enrollment joining (origin, pin, identity);
    std::unique_ptr<enrollment> resumed;
    std::unique_ptr<authority_client> client;
    std::unique_ptr<rendezvous_client> presence;
    std::unique_ptr<decision_client> decisions;
    std::unique_ptr<client_settings> registration_settings;
    std::unique_ptr<vault_registration_client> registration;
    std::unique_ptr<client_settings> service_settings;
    std::unique_ptr<profile_session> service;
    int authorizations= 0;
    std::string member;
    QTimer deadline;
    deadline.setSingleShot (true);
    QObject::connect (&deadline, &QTimer::timeout, &app, [&] {
      std::cerr << "Native authority smoke timed out\n"; app.exit (1);
    });
    deadline.start (20000);
    auto failure= [&] (const control_result& result) {
      if (result.failure != control_failure::none) {
        std::cerr << result.diagnostic << '\n'; app.exit (1); return true;
      }
      return false;
    };
    std::function<void ()> poll;
    poll= [&] {
      joining.poll ([&] (control_result result, enrollment_status status) {
        if (failure (result)) return;
        if (status.member.empty ()) { QTimer::singleShot (100, &app, poll); return; }
        member= status.member;
        // A new enrollment object has no pending credential, as after restart.
        resumed= std::make_unique<enrollment> (origin, pin, identity);
        resumed->resolve ([&] (control_result result, enrollment_status recovered) {
          if (failure (result)) return;
          if (recovered.member != member) { app.exit (1); return; }
          {
            client_settings settings (root / "settings.sqlite");
            auto pending= settings.find (pin.group);
            if (!pending || pending->device.handle != identity.handle || !pending->member.empty ()) {
              app.exit (1); return;
            }
            settings.complete_admission (pin.group, identity.handle, identity.public_key, member);
            settings.set_enabled (pin.group, true);
          }
          client_settings reopened (root / "settings.sqlite");
          auto profile= reopened.find (pin.group);
          if (!profile || !profile->enabled || profile->member != member) { app.exit (1); return; }
          client= std::make_unique<authority_client> (profile->origin, profile->pin,
            profile->device, profile->member, root / "client.sqlite");
          if (client->authorized ()) { app.exit (1); return; }
          client->refresh ([&] (control_result result) {
            if (failure (result)) return;
            auto state= client->current ();
            if (!client->authorized () || !state || !client->peer_allowed (
              member, identity.public_key, state->epoch)) {
              std::cerr << "Fresh authority validation did not authorize member\n";
              app.exit (1); return;
            }
            auto continue_session= [&] {
            client->suspend ();
            if (client->authorized ()) { app.exit (1); return; }
            service_settings= std::make_unique<client_settings> (root / "settings.sqlite");
            auto profile= service_settings->find (pin.group);
            service= std::make_unique<profile_session> (*service_settings, *profile,
              root / "client.sqlite", [&] (profile_status status) {
                if (status.phase == profile_phase::error || status.phase == profile_phase::denied) {
                  std::cerr << status.diagnostic << '\n'; app.exit (1); return;
                }
                if (!status.discovery_diagnostic.empty ()) {
                  std::cerr << status.discovery_diagnostic << '\n'; app.exit (1); return;
                }
                if (status.phase != profile_phase::authorized || status.discovered_devices != 1) return;
                ++authorizations;
                QTimer::singleShot (0, &app, [&] {
                  auto membership= service->membership ();
                  auto discovered= service->discovered_peers ();
                  if (discovered.peers.size () != 1 || discovered.peers[0].member != config.at ("peer")) {
                    std::cerr << "Automatic discovery did not return peer\n"; app.exit (1); return;
                  }
                  if (!membership || !service->peer_allowed (member, identity.public_key, membership->epoch)) {
                    app.exit (1); return;
                  }
                  service->suspend ();
                  if (!service->discovered_peers ().peers.empty ()) {
                    std::cerr << "Suspension retained discovery candidates\n"; app.exit (1); return;
                  }
                  if (service->peer_allowed (member, identity.public_key, membership->epoch)) {
                    app.exit (1); return;
                  }
                  if (authorizations == 1) { service->resume (); return; }
                  std::ofstream done (root / "done"); done << "passed\n"; done.close ();
                  std::cout << "Native enrollment, presence discovery, persistence and suspend/resume validation passed\n";
                  app.exit (0);
                });
              });
            service->start ();
            };
            auto register_then_continue= [&, continue_session, epoch= state->epoch] {
              const std::string vault= "10000000-0000-4000-8000-000000000001";
              std::filesystem::create_directory (root / "vault");
              registration_settings= std::make_unique<client_settings> (root / "settings.sqlite");
              registration_settings->bind_vault ({pin.group, vault, root / "vault", true});
              registration= std::make_unique<vault_registration_client> (*registration_settings,
                *registration_settings->find (pin.group));
              auto allowed= [&] (const std::string& value) { return client->context_current (pin.group, pin.generation, value); };
              registration->ensure (vault, epoch, allowed,
                [&, vault, epoch, allowed, continue_session] (control_result result, std::optional<vault_secret_registration> first) {
                  if (failure (result)) return;
                  client_settings reopened (root / "settings.sqlite");
                  auto commitment= reopened.vault_secret_commitment (pin.group, pin.generation, vault);
                  if (!first || !commitment || *commitment != first->commitment) { app.exit (1); return; }
                  auto key= reopened.find_vault_secret (pin.group, pin.generation, vault, *commitment);
                  if (!key) { app.exit (1); return; }
                  verify_vault_secret (*key);
                  registration->ensure (vault, epoch, allowed,
                    [&, first, continue_session] (control_result result, std::optional<vault_secret_registration> again) {
                      if (failure (result)) return;
                      if (!again || first->commitment != again->commitment || first->created != again->created) {
                        std::cerr << "Native Vault registration changed on retry\n"; app.exit (1); return;
                      }
                      continue_session ();
                    });
                });
            };
            auto decide_then_continue= [&, register_then_continue, epoch= state->epoch] {
              decisions= std::make_unique<decision_client> (origin, pin, identity, member);
              auto permitted= [&] (const std::string& current_epoch) {
                return client->context_current (pin.group, pin.generation, current_epoch);
              };
              decision_request request{identity.handle, pin.group, identity.public_key, member, pin.generation, 0};
              decisions->request (epoch, request, permitted,
                [&, epoch, request, permitted, register_then_continue] (control_result result, std::optional<conflict_decision> decision) {
                  if (failure (result)) return;
                  if (!decision || decision->version != 1 || decision->request != request.request_id) {
                    std::cerr << "Native decision submission was not acknowledged\n"; app.exit (1); return;
                  }
                  decisions->request (epoch, request, permitted,
                    [&, request, register_then_continue] (control_result result, std::optional<conflict_decision> repeated) {
                      if (failure (result)) return;
                      if (!repeated || repeated->version != 1 || repeated->resolution != request.resolution) {
                        std::cerr << "Native decision retry was not idempotent\n"; app.exit (1); return;
                      }
                      register_then_continue ();
                    });
                });
            };
            presence= std::make_unique<rendezvous_client> (origin, identity);
            presence_context context{pin.group, member, pin.generation, state->epoch};
            auto allowed= [&] (const presence_context& c) {
              return client->context_current (c.group, c.generation, c.epoch) &&
                client->peer_allowed (c.member, identity.public_key, c.epoch);
            };
            presence_request publish;
            publish.operation= presence_operation::publish;
            publish.direct= {"192.168.1.2:12345", "[fd00::2]:12345"};
            publish.relays= {"https://relay.example.invalid"};
            presence->request (context, publish, allowed,
              [&, context, allowed, decide_then_continue] (control_result result, presence_page) {
                if (failure (result)) return;
                presence->request (context, {}, allowed,
                  [&, context, allowed, decide_then_continue] (control_result result, presence_page page) {
                    if (failure (result)) return;
                    if (page.entries.size () != 1 || !page.next.empty () ||
                        page.entries[0].member != config.at ("peer") ||
                        page.entries[0].direct != std::vector<std::string>{"192.168.1.3:9445"} ||
                        page.entries[0].relays != std::vector<std::string>{"https://relay.example.invalid"}) {
                      std::cerr << "Presence did not return the expected remote device\n"; app.exit (1); return;
                    }
                    presence_request withdraw; withdraw.operation= presence_operation::withdraw;
                    presence->request (context, withdraw, allowed,
                      [&, decide_then_continue] (control_result result, presence_page) {
                        if (!failure (result)) decide_then_continue ();
                      });
                  });
              });
          });
        });
      });
    };
    control_http discovery (origin, nullptr);
    discover_authority (discovery, [&] (control_result result, authority_description description) {
      if (failure (result)) return;
      if (!description.initialized || description.pin.group != pin.group ||
          description.pin.public_key != pin.public_key || description.pin.generation != pin.generation ||
          description.recovery_public_key != config.at ("recovery_public_key")) {
        std::cerr << "Discovered authority does not match isolated server\n"; app.exit (1); return;
      }
      joining.join ("Isolated native client", [&] (control_result result, enrollment_status status) {
        if (failure (result)) return;
        if (status.code.size () != 8 || status.request.empty ()) { app.exit (1); return; }
        poll ();
      });
    });
    return app.exec ();
  }
  catch (const std::exception& e) { std::cerr << e.what () << '\n'; return 1; }
}
