/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#include "recovery.hpp"
#include "control_http.hpp"
#include <QCoreApplication>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QTimer>
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>

using namespace athena::hodarium;
using json= nlohmann::json;
namespace {
void require (bool value, const char* message) {
  if (!value) throw std::runtime_error (message);
}
template<typename F> void rejects (F action) {
  bool rejected= false;
  try { action (); } catch (const std::exception&) { rejected= true; }
  require (rejected, "Invalid recovery/settings operation was accepted");
}
}
int main (int argc, char** argv) {
  QCoreApplication app (argc, argv);
  try {
    require (argc == 2, "Pass an isolated recovery fixture");
    auto root= std::filesystem::path (argv[1]).parent_path ();
    json config; std::ifstream (argv[1]) >> config;
    auto tls= QSslConfiguration::defaultConfiguration ();
    tls.addCaCertificate (QSslCertificate (QByteArray::fromStdString (config.at ("certificate"))));
    QSslConfiguration::setDefaultConfiguration (tls);
    client_profile initial{QUrl (QString::fromStdString (config.at ("origin"))),
      {config.at ("group"),config.at ("authority"),config.at ("generation")},
      config.at ("recovery_public_key"),{config.at ("identity"),config.at ("identity")},
      "Isolated recovery client","",false};
    auto proof= QByteArray::fromStdString (config.at ("proof").dump ());
    auto nonce= config.at ("nonce").get<std::string> ();
    auto verified= verify_current_recovery (initial.pin,initial.recovery_public_key,proof,nonce);
    require (verified.generation != initial.pin.generation,"Recovery proof did not verify");
    rejects ([&] {verify_current_recovery (initial.pin,initial.recovery_public_key,proof,initial.device.handle);});
    rejects ([&] {verify_current_recovery (initial.pin,initial.device.public_key,proof,nonce);});
    auto wrong_group= initial.pin; wrong_group.group= initial.device.public_key;
    rejects ([&] {verify_current_recovery (wrong_group,initial.recovery_public_key,proof,nonce);});
    rejects ([&] {verify_current_recovery (verified,initial.recovery_public_key,proof,nonce);});
    auto changed= config.at ("proof");
    auto signature= changed.at ("signature").get<std::string> (); signature[0]= signature[0] == 'A' ? 'B' : 'A';
    changed["signature"]= signature;
    rejects ([&] {verify_current_recovery (initial.pin,initial.recovery_public_key,QByteArray::fromStdString (changed.dump ()),nonce);});
    rejects ([&] {verify_current_recovery (initial.pin,initial.recovery_public_key,QByteArray (16385,'x'),nonce);});
    auto duplicate= proof; duplicate.insert (1,"\"payload\":\"\",");
    rejects ([&] {verify_current_recovery (initial.pin,initial.recovery_public_key,duplicate,nonce);});
    client_settings settings (root / "settings.sqlite");
    settings.add_pending (initial);
    settings.complete_admission (initial.pin.group,initial.device.handle,initial.device.public_key,initial.device.handle);
    settings.set_enabled (initial.pin.group,true);
    auto before= *settings.find (initial.pin.group);
    const std::string vault= "dba12a60-8980-4c59-adce-a4f29785fb47";
    std::filesystem::create_directory (root / "vault");
    settings.bind_vault ({initial.pin.group,vault,root / "vault",true});
    require (!settings.vaults (initial.pin.group).at (0).allow_code_resources,"Code resources allowed by default");
    settings.set_vault_code_resources (initial.pin.group,vault,true);
    require (settings.vaults (initial.pin.group).at (0).allow_code_resources,"Explicit code policy not persisted");
    for (int i= 0; i < 8; ++i)
      settings.set_relay ({initial.pin.group,QUrl (QString ("https://relay%1.example").arg (i)),initial.device.handle});
    settings.set_relay ({initial.pin.group,QUrl ("https://relay0.example:443"),initial.device.handle});
    require (settings.relays (initial.pin.group).size () == 8,"Relay canonical origin duplicated");
    rejects ([&] {settings.set_relay ({initial.pin.group,QUrl ("https://ninth.example"),initial.device.handle});});
    rejects ([&] {settings.set_relay ({initial.pin.group,QUrl ("http://relay0.example"),initial.device.handle});});
    rejects ([&] {settings.set_relay ({initial.pin.group,QUrl ("https://relay0.example"),"a plaintext token"});});
    auto original_database= settings.trust_database_path (root,initial.pin.group);
    std::ofstream (original_database) << "preserved high-water marker";
    control_http http (initial.origin,nullptr);
    QTimer deadline; deadline.setSingleShot (true);
    QObject::connect (&deadline,&QTimer::timeout,&app,[&] { app.exit (1); });
    deadline.start (10000);
    query_authority_recovery (http,before,[&] (control_result result,std::optional<recovery_candidate> candidate) {
      try {
        require (result.failure == control_failure::none,result.diagnostic.c_str ());
        require (candidate.has_value (),"No verified recovery candidate");
        require (settings.find (initial.pin.group)->enabled,"Inspection changed trust before approval");
        auto recovered= settings.accept_recovery (std::move (*candidate));
        require (recovered.member.empty () && !recovered.enabled,"Recovery retained admission");
        require (recovered.pin.generation != initial.pin.generation,"Recovery did not advance generation");
        rejects ([&] {settings.accept_recovery (std::move (*candidate));});
        rejects ([&] {settings.set_enabled (initial.pin.group,true);});
        rejects ([&] {settings.complete_admission (initial.pin.group,initial.device.handle,initial.device.public_key,initial.device.handle);});
        rejects ([&] {settings.complete_admission (initial.pin.group,initial.device.handle,initial.device.public_key,initial.device.handle,initial.pin.generation);});
        require (settings.vaults (initial.pin.group).size () == 1,"Recovery lost Vault binding");
        require (settings.relays (initial.pin.group).size () == 8,"Recovery lost Relay bindings");
        require (!settings.vault_secret_candidate (initial.pin.group,recovered.pin.generation,vault),"Recovery reused old secret");
        auto next_database= settings.trust_database_path (root,initial.pin.group);
        require (next_database != original_database && std::filesystem::exists (original_database),"Recovery discarded old trust database");
        client_settings reopened (root / "settings.sqlite");
        require (reopened.find (initial.pin.group)->pin.generation == recovered.pin.generation,"Recovery not durable");
        query_authority_recovery (http,recovered,[&, recovered] (control_result result,std::optional<recovery_candidate> rollback) {
          try {
            require (result.failure == control_failure::none,result.diagnostic.c_str ());
            require (rollback.has_value (),"Rollback fixture was not cryptographically valid");
            client_settings reopened (root / "settings.sqlite");
            rejects ([&] {reopened.accept_recovery (std::move (*rollback));});
            require (settings.find (initial.pin.group)->pin.generation == recovered.pin.generation,"Rejected rollback changed settings");
            std::cout << "Recovery approval, durable rollback rejection, trust isolation and Relay settings passed\n";
            app.exit (0);
          } catch (const std::exception& e) {std::cerr << e.what () << '\n'; app.exit (1);}
        });
      } catch (const std::exception& e) {std::cerr << e.what () << '\n'; app.exit (1);}
    });
    return app.exec ();
  } catch (const std::exception& e) {std::cerr << e.what () << '\n'; return 1;}
}
