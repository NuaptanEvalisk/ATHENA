/******************************************************************************
* MODULE     : plugin-runtime-fixture.cpp
* DESCRIPTION: Self-contained sandboxed AUDMAP subprocess fixture
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
******************************************************************************/

#include <athena/audmap/client.hpp>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unistd.h>

namespace {
using athena::audmap::client;
using athena::audmap::options;
using athena::audmap::value;

std::atomic<bool> stopping {false};

void stop (int) { stopping.store (true, std::memory_order_relaxed); }

std::string environment (const char* name) {
  const char* value= std::getenv (name);
  if (!value || !*value) throw std::runtime_error (std::string ("Missing ") + name);
  return value;
}

athena::audmap::response completed (athena::audmap::pending_request request) {
  auto response= request.result.get ();
  return response;
}

void release (client& connection, const athena::audmap::pending_request& request) {
  completed (connection.release (request.ticket, request.operation));
}
}

int main () {
  try {
    std::signal (SIGTERM, stop);
    std::signal (SIGINT, stop);
    const std::string guid= environment ("ATHENA_SUBSCRIPTION_GUID");
    options config;
    config.endpoint= environment ("ATHENA_AUDMAP_ENDPOINT");
    config.identity= environment ("ATHENA_AUDMAP_IDENTITY");
    config.name= "Untrusted self-declared plugin name";
    client connection (config);
    auto selected= connection.resolve ("@/subscription/" + guid, true);
    auto selection= completed (selected);
    if (selection.status != "OK" || selection.data.empty () ||
        selection.data.at (0).empty ())
      throw std::runtime_error ("Subscription did not resolve");
    const auto handle= selection.data.at (0).at (0).get<std::uint64_t> ();
    std::cout << "CONNECTED " << guid << std::endl;

    while (!stopping.load (std::memory_order_relaxed)) {
      auto poll= connection.operate (selected.ticket, handle, "get");
      auto response= completed (poll);
      release (connection, poll);
      if (response.status != "OK") break;
      for (const auto& command: response.data.at ("commands")) {
        value result {{"pid", ::getpid ()}, {"guid", guid},
                      {"parameters", command.at ("parameters")}};
        const std::string name= command.at ("command").get<std::string> ();
        if (name == "probe") {
          bool allowed= false;
          try {
            auto root= connection.resolve ("@", true);
            auto roots= completed (root);
            if (roots.status == "OK" && !roots.data.empty () &&
                !roots.data.at (0).empty ()) {
              auto write= connection.operate (
                root.ticket, roots.data.at (0).at (0).get<std::uint64_t> (), "write");
              allowed= completed (write).status == "OK";
              release (connection, write);
            }
            completed (connection.finish (root.ticket));
          }
          catch (...) { allowed= false; }
          result["allowed"]= allowed;
        }
        else if (name == "child") {
          const pid_t child= ::fork ();
          if (child < 0) throw std::runtime_error ("fork failed");
          if (child == 0) {
            std::signal (SIGTERM, SIG_IGN);
            for (;;) ::pause ();
          }
          result["child"]= child;
        }
        else if (name == "ignore-stop")
          std::signal (SIGTERM, SIG_IGN);

        value reply {{"id", command.at ("id")}, {"status", "OK"},
                     {"result", std::move (result)}};
        auto sent= connection.operate (selected.ticket, handle, "reply", reply);
        if (completed (sent).status != "OK")
          throw std::runtime_error ("Subscription reply failed");
        release (connection, sent);
        if (name == "ignore-stop") {
          std::cout << "IGNORING TERM" << std::endl;
          for (;;) ::pause ();
        }
      }
      std::this_thread::sleep_for (std::chrono::milliseconds (50));
    }
    completed (connection.finish (selected.ticket));
    return 0;
  }
  catch (const std::exception& error) {
    std::cerr << error.what () << '\n';
    return 1;
  }
}
