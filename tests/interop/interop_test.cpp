/******************************************************************************
* MODULE     : interop_test.cpp
* DESCRIPTION: AUDM resolution, identity, protocol and standalone REPL tests
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#include "resolution.hpp"
#include "session.hpp"
#include "audmap_server.hpp"
#include "identity.hpp"
#include "repl_session.hpp"
#include "athena/audmap/client.hpp"
#include <zmq.hpp>
#include <fstream>
#include <chrono>
#include <condition_variable>
#include <future>
#include <iostream>
#include <set>
#include <stdexcept>
#include <thread>
#include <spawn.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

using namespace athena::interop;
using namespace std::chrono_literals;

static void require (bool b, const char* reason) {
  if (!b) throw std::runtime_error (reason);
}

struct private_test_directory {
  std::filesystem::path path;
  private_test_directory () {
    char name[] = "/tmp/athena-audmap-test-XXXXXX";
    if (!mkdtemp (name)) throw std::runtime_error ("Cannot create test directory");
    path = name;
  }
  ~private_test_directory () { std::error_code ignored; std::filesystem::remove_all (path, ignored); }
};

static void identity_test () {
  private_test_directory dir;
  const auto path = dir.path / "identity.json";
  const auto key = load_client_identity (path);
  const auto again = load_client_identity (path);
  require (key.public_key == again.public_key && key.secret_key == again.secret_key, "Client identity changed on restart");
  authorization_store store (dir.path / "clients.json");
  require (!store.lookup (key.public_key), "Unknown client was authorized");
  for (auto mode: {trust_mode::full_access, trust_mode::confirm_operations, trust_mode::confirm_requests}) {
    store.remember (key.public_key, {true, mode});
    const auto rule = authorization_store (dir.path / "clients.json").lookup (key.public_key);
    require (rule && rule->allow && rule->trust == mode, "Persistent trust mode was not preserved");
  }
  store.remember (key.public_key, {false, trust_mode::confirm_requests});
  require (!authorization_store (dir.path / "clients.json").lookup (key.public_key)->allow, "Permanent rejection was lost");
  const auto other = load_client_identity (dir.path / "other.json");
  require (!store.lookup (other.public_key), "Authorization leaked to another key");
  ::chmod (path.c_str (), 0644);
  bool denied = false;
  try { load_client_identity (path); } catch (const std::exception&) { denied = true; }
  require (denied, "World-readable secret key was accepted");
}

static void repl_test () {
  std::vector<value> sent;
  std::string output;
  repl_session repl ([&] (value v) { sent.push_back (std::move (v)); },
                     [&] (std::string s) { output += s; });
  repl.line ("@");
  require (repl.busy () && sent.back () == value::array ({1, 1, "@", value::array ({0})}), "REPL did not enter resolution");
  require (repl.prompt ().empty (), "Resolution wait must not offer an input prompt");
  repl.receive (value::parse ("[4,1,[[[1,0],[2,1]],[]]]"));
  require (!repl.busy () && repl.prompt () == "handle 2> ", "REPL did not select the leaf handle");
  require (output.find ("     2       1  selected") != std::string::npos, "Handle columns or selection label are unclear");
  repl.line ("use 1"); repl.line ("lineage");
  require (sent.back () == value::array ({9,1,1,1}), "REPL lineage uses wrong handle");
  repl.receive (value::parse ("[6,1,1,\"OK\",[1]]"));
  require (sent.back () == value::array ({8,1,1}), "REPL did not release operation result");
  repl.line ("get {}");
  require (sent.back () == value::parse ("[5,1,2,1,\"get\",{}]"), "REPL operation frame is wrong");
  repl.line ("exit");
  require (!repl.done () && repl.prompt () == "audm> " && sent.back () == value::array ({11,1}), "First exit must retire ticket, not quit");
  repl.receive (value::parse ("[6,1,2,\"OK\",{}]"));
  require (repl.prompt () == "audm> ", "Late result restored a closed ticket");
  repl.line ("@"); repl.line ("exit");
  require (sent.back () == value::array ({10,2}), "Exit during resolution did not cancel");
  repl.line ("exit"); require (repl.done (), "Selector exit did not quit");
}

class test_resource final: public resource {
  std::string id;
public:
  explicit test_resource (std::string id): id (std::move (id)) {}
  std::string type () const override { return "test"; }
  std::string identity () const override { return id; }
  value properties () const override { return {{"name", id}, {"type", "test"}}; }
  value inspect () const override { return value::array (); }
  operation_result operate (const std::string& command, const value& parameters) const override {
    return command == "echo" ? operation_result {"OK", parameters} : operation_result {"OK", properties ()};
  }
};

class fork_resolver final: public resolver {
public:
  mutable std::mutex mutex;
  mutable std::condition_variable cv;
  mutable std::set<std::thread::id> threads;
  mutable unsigned active = 0, peak = 0, entered = 0;
  bool rendezvous = false, fault = false, miss = false, truncation = false;
  std::chrono::milliseconds start_delay {0};
  resolver_outcome resolve (const resolution_request& req,
                            resolution_output& out) const override {
    if (req.offset == 0) {
      if (start_delay.count ()) std::this_thread::sleep_for (start_delay);
      out.publish (std::make_shared<test_resource> ("root"), 1);
    }
    else if (req.offset == 1) {
      for (int i = 0; i < 4; ++i)
        out.publish (std::make_shared<test_resource> ("same-identity"), 2);
    }
    else {
      {
        std::unique_lock<std::mutex> lock (mutex);
        threads.insert (std::this_thread::get_id ());
        ++active;
        ++entered;
        peak = std::max (peak, active);
        cv.notify_all ();
        if (rendezvous && !cv.wait_for (lock, 3s, [&] { return entered >= 2; }))
          throw std::runtime_error ("Same-ticket branches never ran concurrently");
        --active;
      }
      if (fault) throw std::runtime_error ("domain read failed");
      if (miss) return resolver_outcome::miss;
      if (truncation) out.truncated.push_back ({truncation_kind::max_matches, "Match limit"});
      out.publish (std::make_shared<test_resource> ("leaf"), 3);
    }
    return resolver_outcome::resolved;
  }
};

static void parser_test () {
  for (const auto& query: {
      R"(?($name = "strong *stellensatz"))",
      R"(??($name = "strong *stellensatz"))",
      R"(???($name = "strong *stellensatz" | $max_depth = 3))"}) {
    const auto p = parse_selection (query).front ().filter;
    require (p.matches ({{"name", "strong nullstellensatz"}}), "Star predicate did not match");
    require (!p.matches ({{"name", "weak nullstellensatz"}}), "Star predicate lost its prefix");
  }
  const auto matches = [] (const std::string& op, const std::string& pattern,
                           const value& actual) {
    return parse_selection ("?($name " + op + " " + value (pattern).dump () + ")")
      .front ().filter.matches ({{"name", actual}});
  };
  require (matches ("=", "*", ""), "Star must match empty text");
  require (matches ("=", "a**b", "a\nb"), "Star must match newlines");
  require (matches ("=", "a*b", std::string ("a\0b", 3)), "Star must match embedded NUL");
  require (!matches ("=", "a", std::string ("a\0b", 3)), "NUL truncated a predicate");
  require (matches ("=", "*\xce\xb1*", "x\xce\xb1y"), "UTF-8 literal was corrupted");
  require (matches ("=", "x\\*y", "x*y"), "Escaped star is not literal");
  require (!matches ("=", "a?b", "acb"), "Question mark became a wildcard");
  require (matches ("=", "[a]*", "[a]bc"), "Brackets are not literal");
  require (!matches ("=", "[a]*", "abc"), "Bracket expression became active");
  require (matches ("contains", "strong *satz", "the strong nullstellensatz theorem"), "Contains star failed");
  require (matches ("starts_with", "strong *", "strong theorem"), "Starts-with star failed");
  require (matches ("ends_with", "*satz", "nullstellensatz"), "Ends-with star failed");
  require (!matches ("!=", "strong*", "strong theorem"), "Wildcard inequality failed");
  require (matches ("<", "b*", "a*"), "Lexical comparison changed");
  require (!matches ("=", "*", 12), "String wildcard coerced a number");
  auto s = parse_selection ("@/vaults/@/namespaces/\"a/b\"");
  require (s.size () == 5 && s.back ().name == "a/b", "Quoted names are not paths");
  s = parse_selection (R"(@/vaults/@/filesystem/"[0].ath"/online/[0]/?($tag = "math")[0, 3, 4])");
  require (s.at (4).name == "[0].ath" && s.at (4).positions.empty (), "Quoted bracket filename was indexed");
  require (s.at (6).type == selector::kind::index && s.at (6).positions == std::vector<std::uint64_t> {0},
           "Explicit child index was not parsed");
  require (s.back ().type == selector::kind::local && s.back ().positions == std::vector<std::uint64_t> {0, 3, 4},
           "Predicate result indices were not parsed");
  require (s.back ().filter.matches ({{"tag", "math"}}), "Index suffix changed the predicate");
  s = parse_selection ("0");
  require (s.front ().type == selector::kind::name, "Bare numeric names became child indices");
  s = parse_selection (R"(@/vaults/@/namespaces/???($type = "namespace", NOT ($name = "other" OR exists($missing)) | $max_depth = "3", $max_matches = 2))");
  require (s.back ().limits.max_depth == 3 && s.back ().limits.max_matches == 2,
           "Typed and quoted bounds");
  require (s.back ().filter.matches ({{"type", "namespace"}, {"name", "wanted"}}),
           "Boolean predicate precedence");
  require (!s.back ().filter.matches ({{"type", "namespace"}, {"name", "other"}}),
           "Negated group");
  for (const auto& text: {"", "@/", "@//a", R"(@/???($name = 1))",
       "@/?($name =)", R"(@/???($name = 1 | $max_matches = 0))",
       R"(@/???($name = 1 | $max_depth = 1, $max_depth = 2))",
       "[]", "[-1]", "[1.5]", "[0,]", "[18446744073709551616]", "[0][1]"}) {
    bool rejected = false;
    try { parse_selection (text); }
    catch (const std::invalid_argument&) { rejected = true; }
    require (rejected, "Malformed selection accepted");
  }
  s = parse_selection (R"(?(exists($name) AND $name starts_with "ab" AND $count >= 2))");
  require (s[0].filter.matches ({{"name", "abc"}, {"count", 3}}), "Typed comparison");
  require (!s[0].filter.matches ({{"name", "abc"}, {"count", "3"}}), "No type coercion");
}

static resolution_result execute (resolution_workers& workers,
                                  std::shared_ptr<fork_resolver> resolver,
                                  std::size_t limit = 65536) {
  auto registry = std::make_shared<const resolver_registry> (resolver_registry {resolver});
  std::promise<resolution_result> promise;
  auto future = promise.get_future ();
  resolution_ticket ticket (workers, registry, parse_selection ("@/fork/leaf"),
    [&] (resolution_result r) { promise.set_value (std::move (r)); }, limit);
  require (future.wait_for (5s) == std::future_status::ready, "Resolution did not finish");
  return future.get ();
}

static void scheduler_test () {
  for (std::size_t count: {1, 2, 4}) {
    resolution_workers workers (count);
    auto r = std::make_shared<fork_resolver> ();
    r->rendezvous = count > 1;
    auto result = execute (workers, r);
    require (result.state == resolution_result::status::complete, result.error.c_str ());
    require (result.tree.size () == 9 && result.leaves.size () == 4, "Occurrence tree shape");
    require (r->threads.size () <= count && r->peak <= count, "Fixed thread budget exceeded");
    require (count == 1 || r->peak >= 2, "Same-ticket work was serialized");
    require (result.tree[1]->accessor->identity () == result.tree[2]->accessor->identity () &&
             result.tree[1]->id != result.tree[2]->id, "Do not deduplicate occurrences");
    r = std::make_shared<fork_resolver> ();
    r->miss = true;
    result = execute (workers, r);
    require (result.tree.empty () && result.state == resolution_result::status::complete,
             "Prune prefixes with no full match");
    r = std::make_shared<fork_resolver> ();
    r->fault = true;
    result = execute (workers, r);
    require (result.tree.empty () && result.state == resolution_result::status::fault,
             "Fault must not return partial accessors");
    r = std::make_shared<fork_resolver> ();
    r->truncation = true;
    result = execute (workers, r);
    require (result.truncated.size () == 1 && result.state == resolution_result::status::complete,
             "Truncation metadata");
    result = execute (workers, r, 2);
    require (result.state == resolution_result::status::fault, "Enforce node admission budget");
  }
}

static void cancellation_test () {
  resolution_workers workers (1);
  std::promise<void> release;
  auto gate = release.get_future ().share ();
  workers.post ([gate] { gate.wait (); });
  auto r = std::make_shared<fork_resolver> ();
  auto registry = std::make_shared<const resolver_registry> (resolver_registry {r});
  std::promise<resolution_result> promise;
  auto result = promise.get_future ();
  resolution_ticket ticket (workers, registry, parse_selection ("@/fork/leaf"),
    [&] (resolution_result r) { promise.set_value (std::move (r)); });
  ticket.cancel ();
  release.set_value ();
  require (result.wait_for (3s) == std::future_status::ready, "Cancellation did not drain");
  require (result.get ().state == resolution_result::status::cancelled, "Queued ticket ignored cancellation");
  require (r->entered == 0, "Cancelled work entered resolver");
}

static value frame (opcode op, std::initializer_list<value> arguments) {
  value out = value::array ({static_cast<unsigned> (op)});
  for (const auto& a: arguments) out.push_back (a);
  return out;
}

static void protocol_test () {
  auto packed = frame (opcode::req, {1, "@/fork/leaf", value::array ({1, 2})});
  require (decode_message (encode_message (packed)) == packed, "MessagePack round trip");
  auto binary = frame (opcode::rsp, {1, 2, "OK", value::binary ({0, 255, 0, 1})});
  require (decode_message (encode_message (binary)) == binary, "Binary round trip");
  for (const auto& invalid: {std::string ("\x91\x01\xc0", 3),
                            std::string ("\x92\x01\x82\xa1x\x01\xa1x\x02", 9)}) {
    bool rejected = false;
    try { decode_message (invalid); } catch (const std::exception&) { rejected = true; }
    require (rejected, "Malformed MessagePack accepted");
  }
  resolution_workers workers (2), operations (2);
  auto r = std::make_shared<fork_resolver> ();
  auto registry = std::make_shared<const resolver_registry> (resolver_registry {r});
  std::vector<value> replies;
  std::vector<std::function<void (bool)>> approvals;
  protocol_session session (workers, operations, registry, trust_mode::confirm_requests,
    [&] (value, std::function<void (bool)> decision) { approvals.push_back (std::move (decision)); },
    [&] (value reply) { replies.push_back (std::move (reply)); });
  auto wait_reply = [&] (opcode op, std::uint64_t oid = 0) {
    const auto deadline = std::chrono::steady_clock::now () + 3s;
    while (std::chrono::steady_clock::now () < deadline) {
      session.poll ();
      for (const auto& reply: replies)
        if (reply[0] == static_cast<unsigned> (op) && (!oid || reply[2] == oid)) return reply;
      std::this_thread::sleep_for (1ms);
    }
    throw std::runtime_error ("Protocol reply timed out");
  };
  session.receive (packed);
  session.receive (packed);
  require (approvals.size () == 1, "REQ replay repeated authorization");
  approvals[0] (true);
  auto acx = wait_reply (opcode::acx);
  require (acx[2][0].size () == 2 && r->entered == 4,
           "LEAVES(max) stopped resolution prematurely");
  const auto h = acx[2][0][0];
  replies.clear ();
  session.receive (frame (opcode::lin, {1, 1, h}));
  auto lineage = wait_reply (opcode::rsp, 1);
  require (lineage[4].size () == 3 && approvals.size () == 1, "LIN must not require authorization");
  replies.clear ();
  auto opr = frame (opcode::opr, {1, 2, h, "inspect", value::object ()});
  session.receive (opr); session.receive (opr);
  require (approvals.size () == 2, "Inspect confirmation/replay mismatch");
  approvals[1] (true);
  auto result = wait_reply (opcode::rsp, 2);
  replies.clear ();
  session.receive (frame (opcode::ask, {1, 2}));
  require (replies.back () == result, "ASK did not replay cached terminal response");
  session.receive (frame (opcode::rel, {1, 2}));
  session.receive (opr);
  require (replies.back ()[0] == static_cast<unsigned> (opcode::err) && approvals.size () == 2,
           "REL allowed an operation ID to execute again");
  session.receive (frame (opcode::fin, {1}));
  session.receive (packed);
  require (replies.back ()[0] == static_cast<unsigned> (opcode::err), "FIN allowed ticket ID reuse");
  session.receive (frame (opcode::req, {2, "@/fork/leaf", value::array ({0})}));
  session.receive (frame (opcode::cnl, {2}));
  approvals.back () (true);
  session.poll ();
  session.receive (frame (opcode::ask, {2}));
  require (replies.back ()[0] == static_cast<unsigned> (opcode::err), "CNL revived by a late approval");
}

static void transport_test () {
  auto r = std::make_shared<fork_resolver> ();
  r->start_delay = 250ms;
  auto registry = std::make_shared<const resolver_registry> (resolver_registry {r});
  std::atomic<unsigned> approvals {0};
  std::string approved_key;
  authorization_ui ui;
  ui.connect = [&] (std::string, std::string key, std::string, auto reply) { approved_key = key; ++approvals; reply (trust_mode::full_access); };
  ui.confirm = [] (std::string, value, auto reply) { reply (true); };
  local_server server (registry, std::move (ui), 2);
  std::ifstream discovery (server.discovery_file ());
  const auto descriptor = value::parse (discovery);
  zmq::context_t context (1);
  zmq::socket_t client (context, zmq::socket_type::dealer);
  char public_key[41], secret_key[41];
  require (zmq_curve_keypair (public_key, secret_key) == 0, "Test CURVE key generation");
  client.set (zmq::sockopt::routing_id, public_key);
  client.set (zmq::sockopt::curve_publickey, public_key);
  client.set (zmq::sockopt::curve_secretkey, secret_key);
  client.set (zmq::sockopt::curve_serverkey, descriptor.at ("server_key").get<std::string> ());
  client.set (zmq::sockopt::rcvtimeo, 3000);
  client.set (zmq::sockopt::linger, 0);
  client.connect (descriptor.at ("endpoint").get<std::string> ());
  auto send = [&] (const value& v) { const auto bytes = encode_message (v); client.send (zmq::buffer (bytes)); };
  auto receive = [&] {
    zmq::message_t bytes;
    require (client.recv (bytes).has_value (), "Authenticated IPC response timed out");
    return decode_message (bytes.to_string_view ());
  };
  send (value::array ({static_cast<unsigned> (transport_opcode::hello), 1, "interop-test"}));
  auto response = receive ();
  if (response[0] == static_cast<unsigned> (transport_opcode::pending)) response = receive ();
  require (response[0] == static_cast<unsigned> (transport_opcode::welcome) && approvals == 1,
           "CURVE identity was not authorized");
  require (approved_key == public_key, "Authorization UI did not receive authenticated key");
  send (frame (opcode::req, {1, "@/fork/leaf", value::array ({1})}));
  require (receive ()[0] == static_cast<unsigned> (opcode::ack), "Missing wire ACK");
  require (receive ()[0] == static_cast<unsigned> (opcode::acx), "Missing wire ACX");
  send (value::array ({static_cast<unsigned> (transport_opcode::bye)}));

  // Exercise the actual standalone binary, not just the REPL state machine.
  private_test_directory dir;
  const auto script = (dir.path / "input").string (), output = (dir.path / "output").string ();
  {
    std::ofstream input (script);
    input << "@/fork/leaf\nhandles\nlineage\ninspect\nuse 1\nget {}\nexit\n@\nexit\nexit\n";
  }
  posix_spawn_file_actions_t actions;
  posix_spawn_file_actions_init (&actions);
  posix_spawn_file_actions_addopen (&actions, STDIN_FILENO, script.c_str (), O_RDONLY, 0);
  posix_spawn_file_actions_addopen (&actions, STDOUT_FILENO, output.c_str (), O_WRONLY | O_CREAT | O_TRUNC, 0600);
  posix_spawn_file_actions_adddup2 (&actions, STDOUT_FILENO, STDERR_FILENO);
  std::string endpoint = server.discovery_file ().string (), identity = (dir.path / "repl.json").string ();
  const char* args[] = {AUDMAP_REPL_PATH, "--endpoint", endpoint.c_str (), "--identity", identity.c_str (), nullptr};
  pid_t child;
  const int spawned = posix_spawn (&child, args[0], &actions, nullptr, const_cast<char**> (args), environ);
  posix_spawn_file_actions_destroy (&actions);
  require (!spawned, "Cannot spawn AUDMAP REPL");
  int status = 0;
  const auto deadline = std::chrono::steady_clock::now () + 5s;
  while (waitpid (child, &status, WNOHANG) == 0) {
    if (std::chrono::steady_clock::now () > deadline) {
      kill (child, SIGKILL); waitpid (child, &status, 0);
      throw std::runtime_error ("REPL integration timed out");
    }
    std::this_thread::sleep_for (20ms);
  }
  std::ifstream result (output);
  const std::string transcript ((std::istreambuf_iterator<char> (result)), {});
  require (WIFEXITED (status) && WEXITSTATUS (status) == 0 && transcript.find ("Error:") == std::string::npos &&
    transcript.find ("OK") != std::string::npos && transcript.find ("Handle  Parent  Target") != std::string::npos,
    transcript.c_str ());

  athena::audmap::options config;
  config.endpoint = server.discovery_file ();
  config.identity = dir.path / "sdk.json";
  athena::audmap::client sdk (config);
  auto selected = sdk.resolve ("@/fork/leaf", true);
  require (selected.result.wait_for (3s) == std::future_status::ready, "SDK resolve hung");
  const auto handle = selected.result.get ().data.at (0).at (0).get<std::uint64_t> ();
  std::vector<athena::audmap::pending_request> operations;
  for (unsigned i = 0; i < 16; ++i)
    operations.push_back (sdk.operate (selected.ticket, handle, "echo", {{"sequence", i}}));
  for (unsigned i = 0; i < operations.size (); ++i) {
    const auto& operation = operations[i];
    require (sdk.ask (operation.ticket, operation.operation).result.get ().data["sequence"] == i, "SDK reply correlation failed");
    require (operation.result.get ().data["sequence"] == i, "SDK original result was lost");
    sdk.release (operation.ticket, operation.operation).result.get ();
  }
  std::this_thread::sleep_for (16s);
  require (sdk.lineage (selected.ticket, handle).result.get ().data.size () == 3, "SDK idle heartbeat did not preserve ticket");
  sdk.finish (selected.ticket).result.get ();
  auto cancelled = sdk.resolve ("@/fork/leaf");
  sdk.cancel (cancelled.ticket).result.get ();
  bool cancelled_error = false;
  try { cancelled.result.get (); } catch (const std::exception&) { cancelled_error = true; }
  require (cancelled_error, "SDK cancellation left resolution future pending");
  sdk.close ();
  bool closed_error = false;
  try { sdk.resolve ("@").result.get (); } catch (const std::exception&) { closed_error = true; }
  require (closed_error, "Closed SDK accepted a new request");
}

int main (int argc, char** argv) {
  try {
    if (argc == 2 && std::string (argv[1]) == "--repl-test-server") {
      auto resolver = std::make_shared<fork_resolver> ();
      resolver->start_delay = 250ms;
      authorization_ui ui;
      ui.connect = [] (std::string, std::string, std::string, auto reply) { reply (trust_mode::full_access); };
      ui.confirm = [] (std::string, value, auto reply) { reply (true); };
      local_server server (std::make_shared<const resolver_registry> (resolver_registry {resolver}), std::move (ui), 2);
      std::cout << server.discovery_file ().string () << std::endl;
      std::string stop;
      std::getline (std::cin, stop);
      return 0;
    }
    parser_test ();
    identity_test ();
    repl_test ();
    scheduler_test ();
    cancellation_test ();
    protocol_test ();
    transport_test ();
    std::cout << "AUDM parser, bounded workers, cancellation and protocol tests passed\n";
    return 0;
  }
  catch (const std::exception& e) {
    std::cerr << e.what () << '\n';
    return 1;
  }
}
