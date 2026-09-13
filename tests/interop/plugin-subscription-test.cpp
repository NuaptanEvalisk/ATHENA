/******************************************************************************
* MODULE     : plugin-subscription-test.cpp
* DESCRIPTION: Plugin mailbox replay, lifecycle and authenticated isolation tests
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#include "subscription.hpp"
#include "audmap_server.hpp"
#include "identity.hpp"
#include <athena/audmap/client.hpp>
#include <iostream>
#include <thread>
#include <unistd.h>

using namespace athena::interop;
using athena::plugins::subscription;
static void require (bool condition, const char* message) {
  if (!condition) throw std::runtime_error (message);
}
template<class F> static void rejected (F f, const char* message) {
  bool failed = false;
  try { f (); } catch (const std::exception&) { failed = true; }
  require (failed, message);
}
struct root_resource final: resource {
  std::string type () const override { return "root"; }
  std::string identity () const override { return "root"; }
  value properties () const override { return {{"type", "root"}}; }
  value inspect () const override { return properties (); }
  operation_result operate (const std::string&, const value&) const override { return {"OK", 1}; }
};
struct root_resolver final: resolver {
  resolver_outcome resolve (const resolution_request& req, resolution_output& out) const override {
    if (req.basepoint || req.offset >= req.selectors.size () ||
        req.selectors[req.offset].type != selector::kind::default_resource)
      return resolver_outcome::irrelevant;
    out.publish (std::make_shared<root_resource> (), req.offset + 1);
    return resolver_outcome::resolved;
  }
};

static void mailbox_test () {
  subscription c ("1234-abcd", "test", 2, 8192);
  auto first = c.enqueue ("one", value::object ());
  auto second = c.enqueue ("two", {{"x", 2}});
  require (c.get () == c.get () && c.get ().at ("commands").size () == 2, "Polling consumed commands");
  require (c.get (first).at ("commands").at (0).at ("id") == second, "Cursor skipped a command");
  rejected ([&] { c.enqueue ("three", value::object ()); }, "Queue exceeded capacity");
  c.reply (first, "OK", 42); c.reply (first, "OK", 42);
  rejected ([&] { c.reply (first, "OK", 43); }, "Conflicting reply accepted");
  rejected ([&] { c.enqueue ("three", value::object ()); }, "Unobserved result was discarded");
  require (c.take_responses ().size () == 1 && c.take_responses ().empty (), "Host result delivery repeated");
  c.enqueue ("three", value::object ());
  require (c.get ().at ("history_truncated") == true, "Expired history was not reported");
  rejected ([&] { c.reply (first, "OK", 42); }, "Expired command was resurrected");
  rejected ([&] { c.reply (second, "OK", std::string (4000, 'x')); }, "Oversized reply accepted");
  c.reply (second, "ERROR", "failed");
  c.close ();
  require (c.take_responses ().size () == 1, "Close lost an admitted reply");
  rejected ([&] { c.get (); }, "Closed subscription was polled");
  rejected ([&] { c.reply (second, "ERROR", "failed"); }, "Old launch handle accepted a reply");
  rejected ([&] { c.enqueue ("new", value::object ()); }, "Closed subscription accepted work");

  subscription concurrent ("1234-ffff", "test");
  std::vector<std::thread> producers;
  std::atomic<unsigned> failures {0};
  for (int i = 0; i < 4; ++i) producers.emplace_back ([&] {
    try { for (int j = 0; j < 32; ++j) concurrent.enqueue ("run", value::object ()); }
    catch (...) { ++failures; }
  });
  for (auto& p: producers) p.join ();
  auto all = concurrent.get (0, 256).at ("commands");
  require (!failures && all.size () == 128, "Concurrent enqueue lost commands");
  for (std::size_t i = 0; i < all.size (); ++i) require (all[i]["id"] == i + 1, "IDs are not monotonic");
}

static void transport_test () {
  char temp[] = "/tmp/athena-plugin-subscription-XXXXXX";
  require (mkdtemp (temp), "Cannot create isolated profile");
  struct cleanup { std::filesystem::path p; ~cleanup () { std::filesystem::remove_all (p); } } dir {temp};
  const auto key = load_client_identity (dir.p / "owner.json");
  auto channel = std::make_shared<subscription> ("01234567-89ab-cdef-0123-456789abcdef", "test-plugin");
  auto root = std::make_shared<root_resolver> ();
  auto ordinary = std::make_shared<const resolver_registry> (resolver_registry {root});
  auto scoped = std::make_shared<const resolver_registry> (
    resolver_registry {root, athena::plugins::subscription_resolver (channel)});
  authorization_ui ui;
  ui.connect = [=] (auto, std::string authenticated_key, auto, auto reply) {
    connection_grant grant (trust_mode::full_access);
    if (authenticated_key == key.public_key) {
      grant.registry = scoped;
      grant.capabilities = {{"*", {true, {}}}, {"subscription", {true, {"get", "reply", "inspect"}}}};
    }
    reply (std::move (grant));
  };
  ui.confirm = [] (auto, auto, auto reply) { reply (true); };
  local_server server (ordinary, std::move (ui), 2);
  athena::audmap::options options;
  options.endpoint = server.discovery_file ();
  options.identity = dir.p / "owner.json";
  options.name = "same-self-reported-name";
  athena::audmap::client owner (options);
  options.identity = dir.p / "stranger.json";
  athena::audmap::client stranger (options);
  const auto selector = "@/subscription/" + channel->guid ();
  auto selected = owner.resolve (selector, true);
  auto h = selected.result.get ().data.at (0).at (0).get<std::uint64_t> ();
  require (stranger.resolve (selector, true).result.get ().data.at (0).empty (),
           "Another key with the same name resolved a private subscription");
  auto root_selection = owner.resolve ("@", true);
  auto rh = root_selection.result.get ().data.at (0).at (0).get<std::uint64_t> ();
  rejected ([&] { owner.operate (root_selection.ticket, rh, "get").result.get (); },
            "Default-deny capability mask was not passed through transport");
  auto id = channel->enqueue ("hello", {{"x", 1}});
  auto commands = owner.operate (selected.ticket, h, "get").result.get ().data;
  require (commands.at ("commands").at (0).at ("id") == id, "Wire polling lost command");
  const value reply {{"id", id}, {"status", "OK"}, {"result", {{"x", 2}}}};
  require (owner.operate (selected.ticket, h, "reply", reply).result.get ().status == "OK", "Wire reply failed");
  require (owner.operate (selected.ticket, h, "reply", reply).result.get ().status == "OK", "Wire retry failed");
  require (channel->take_responses ().size () == 1, "Wire reply was duplicated");
  require (owner.operate (selected.ticket, h, "get", {{"after", -1}}).result.get ().status == "ERROR",
           "Negative cursor accepted");
  channel->close ();
  require (owner.operate (selected.ticket, h, "get").result.get ().status == "ERROR", "Old accessor survived launch close");
  require (owner.resolve (selector, true).result.get ().data.at (0).empty (), "Closed channel still resolves");
}
int main () {
  try { mailbox_test (); transport_test (); std::cout << "Plugin subscription tests passed\n"; return 0; }
  catch (const std::exception& e) { std::cerr << e.what () << '\n'; return 1; }
}
