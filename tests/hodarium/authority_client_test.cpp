/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#include "authority_client.hpp"
#include "enrollment.hpp"
#include "client_settings.hpp"
#include "profile_session.hpp"
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
            client->suspend ();
            if (client->authorized ()) { app.exit (1); return; }
            service_settings= std::make_unique<client_settings> (root / "settings.sqlite");
            auto profile= service_settings->find (pin.group);
            service= std::make_unique<profile_session> (*service_settings, *profile,
              root / "client.sqlite", [&] (profile_status status) {
                if (status.phase == profile_phase::error || status.phase == profile_phase::denied) {
                  std::cerr << status.diagnostic << '\n'; app.exit (1); return;
                }
                if (status.phase != profile_phase::authorized) return;
                ++authorizations;
                QTimer::singleShot (0, &app, [&] {
                  auto membership= service->membership ();
                  if (!membership || !service->peer_allowed (member, identity.public_key, membership->epoch)) {
                    app.exit (1); return;
                  }
                  service->suspend ();
                  if (service->peer_allowed (member, identity.public_key, membership->epoch)) {
                    app.exit (1); return;
                  }
                  if (authorizations == 1) { service->resume (); return; }
                  std::ofstream done (root / "done"); done << "passed\n"; done.close ();
                  std::cout << "Native enrollment, recovery, persistence and suspend/resume validation passed\n";
                  app.exit (0);
                });
              });
            service->start ();
          });
        });
      });
    };
    joining.join ("Isolated native client", [&] (control_result result, enrollment_status status) {
      if (failure (result)) return;
      if (status.code.size () != 8 || status.request.empty ()) { app.exit (1); return; }
      poll ();
    });
    return app.exec ();
  }
  catch (const std::exception& e) { std::cerr << e.what () << '\n'; return 1; }
}
