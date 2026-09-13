/******************************************************************************
* MODULE     : session.cpp
* DESCRIPTION: Ticket lifecycle, authorization, operation dispatch and reply replay
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#include "session.hpp"
#include <deque>
#include <stdexcept>

namespace athena::interop {
namespace {
value message (opcode op, std::initializer_list<value> arguments) {
  value result = value::array ({static_cast<unsigned> (op)});
  for (const auto& a: arguments) result.push_back (a);
  return result;
}
std::uint64_t identifier (const value& v) {
  if (!v.is_number_unsigned () && !(v.is_number_integer () && v.get<std::int64_t> () > 0))
    throw std::invalid_argument ("Identifiers must be positive uint64 values");
  auto id = v.get<std::uint64_t> ();
  if (!id) throw std::invalid_argument ("Zero is not an identifier");
  return id;
}
} // namespace

struct protocol_session::impl: std::enable_shared_from_this<impl> {
  struct operation {
    value request;
    value terminal;
    bool released = false;
  };
  struct ticket {
    value request;
    value terminal;
    bool resolving = true, closing = false;
    std::unique_ptr<resolution_ticket> resolution;
    std::map<handle, std::shared_ptr<const occurrence>> accessors;
    std::map<std::uint64_t, operation> operations;
  };
  resolution_workers& resolutions;
  resolution_workers& operations;
  const std::shared_ptr<const resolver_registry> registry;
  const trust_mode trust;
  authorization authorize;
  std::function<void (value)> send;
  const std::map<std::string, capability_mask> capabilities;
  std::map<std::uint64_t, ticket> tickets;
  std::set<std::uint64_t> used_tickets;
  std::mutex events_mutex;
  std::deque<std::function<void (impl&)>> events;
  bool connected = true;

  impl (resolution_workers& r, resolution_workers& o,
        std::shared_ptr<const resolver_registry> registry, trust_mode trust,
        authorization authorize, std::function<void (value)> send,
        std::map<std::string, capability_mask> masks): resolutions (r), operations (o),
    registry (std::move (registry)), trust (trust), authorize (std::move (authorize)),
    send (std::move (send)), capabilities (std::move (masks)) {}

  void event (std::function<void (impl&)> action) {
    std::lock_guard<std::mutex> lock (events_mutex);
    events.push_back (std::move (action));
  }
  void reply (value data) { if (connected) send (std::move (data)); }
  void error (std::uint64_t tid, std::string reason) {
    auto it = tickets.find (tid);
    if (it != tickets.end ()) {
      if (it->second.resolution) it->second.resolution->cancel ();
      tickets.erase (it);
    }
    reply (message (opcode::err, {tid, std::move (reason)}));
  }
  void op_error (std::uint64_t tid, std::uint64_t oid, std::string reason) {
    reply (message (opcode::err, {tid, oid, std::move (reason)}));
  }
  void check_close (std::uint64_t id) {
    auto it = tickets.find (id);
    if (it == tickets.end () || !it->second.closing) return;
    for (const auto& op: it->second.operations)
      if (op.second.terminal.is_null () && !op.second.released) return;
    tickets.erase (it);
  }

  void resolve (std::uint64_t id, std::shared_ptr<const selection> selectors) {
    auto it = tickets.find (id);
    if (it == tickets.end ()) return;
    std::weak_ptr<impl> weak = shared_from_this ();
    it->second.resolution = std::make_unique<resolution_ticket> (resolutions, registry,
      *selectors, [weak, id] (resolution_result result) mutable {
        if (auto self = weak.lock ()) self->event ([id, result = std::move (result)] (impl& s) mutable {
          auto it = s.tickets.find (id);
          if (it == s.tickets.end () || !it->second.resolving) return;
          if (result.state != resolution_result::status::complete) {
            s.error (id, result.error.empty () ? "Resolution cancelled" : result.error);
            return;
          }
          auto& t = it->second;
          t.resolving = false;
          value projected = value::array ();
          const auto& projection = t.request[3];
          for (const auto& node: result.tree) {
            t.accessors.emplace (node->id, node);
            if (projection[0] == 0)
              projected.push_back (value::array ({node->id, node->parent ? node->parent->id : 0}));
          }
          if (projection[0] == 1) {
            std::size_t count = result.leaves.size ();
            if (projection.size () == 2) count = std::min (count, projection[1].get<std::size_t> ());
            for (std::size_t i = 0; i < count; ++i) projected.push_back (result.leaves[i]);
          }
          value reasons = value::array ();
          for (const auto& r: result.truncated)
            reasons.push_back (value::array ({static_cast<unsigned> (r.reason), r.message}));
          t.terminal = message (opcode::acx, {id, value::array ({projected, reasons})});
          s.reply (t.terminal);
        });
      });
  }

  void finish_operation (std::uint64_t tid, std::uint64_t oid, value result) {
    auto it = tickets.find (tid);
    if (it == tickets.end ()) return;
    it->second.operations.at (oid).terminal = std::move (result);
    reply (it->second.operations.at (oid).terminal);
    check_close (tid);
  }

  void operate (std::uint64_t tid, std::uint64_t oid, binding accessor,
                std::string command, value parameters) {
    if (!tickets.count (tid)) return;
    std::weak_ptr<impl> weak = shared_from_this ();
    if (!operations.post ([weak, tid, oid, accessor = std::move (accessor),
                            command = std::move (command), parameters = std::move (parameters)] {
      value result;
      try {
        operation_result r;
        if (command == "inspect") r.data = accessor->inspect ();
        else r = accessor->operate (command, parameters);
        result = message (opcode::rsp, {tid, oid, r.status, r.data});
        // Encoding failures are operation failures, not transport-loop failures.
        encode_message (result);
      }
      catch (const std::exception& e) { result = message (opcode::rsp, {tid, oid, "ERROR", e.what ()}); }
      catch (...) { result = message (opcode::rsp, {tid, oid, "ERROR", "Unhandled resource failure"}); }
      if (auto self = weak.lock ()) self->event ([tid, oid, result = std::move (result)] (impl& s) mutable {
        s.finish_operation (tid, oid, std::move (result));
      });
    })) finish_operation (tid, oid, message (opcode::err, {tid, oid, "Operation queue capacity exceeded"}));
  }

  void receive (const value& msg) {
    std::uint64_t tid = 0, oid = 0;
    try {
      if (!msg.is_array () || msg.size () < 2) throw std::invalid_argument ("Invalid message structure");
      const auto op = static_cast<opcode> (identifier (msg[0]));
      tid = identifier (msg[1]);
      if (op == opcode::req) {
        if (msg.size () != 4 || !msg[2].is_string () || !msg[3].is_array ())
          throw std::invalid_argument ("REQ requires selection and projection");
        const auto& projection = msg[3];
        if (projection.empty () || projection.size () > 2 ||
            !(projection[0] == 0 || projection[0] == 1) ||
            (projection[0] == 0 && projection.size () != 1))
          throw std::invalid_argument ("Invalid projection");
        if (projection.size () == 2) identifier (projection[1]);
        auto it = tickets.find (tid);
        if (it != tickets.end ()) {
          if (it->second.request != msg) throw std::invalid_argument ("Ticket ID already used");
          reply (it->second.terminal.is_null () ? message (opcode::ack, {tid}) : it->second.terminal);
          return;
        }
        if (used_tickets.count (tid)) throw std::invalid_argument ("Ticket ID retired");
        if (tickets.size () >= 128 || used_tickets.size () >= 65536)
          throw std::invalid_argument ("Connection ticket capacity exceeded");
        auto selectors = std::make_shared<const selection> (parse_selection (msg[2]));
        used_tickets.insert (tid);
        tickets[tid].request = msg;
        reply (message (opcode::ack, {tid}));
        if (trust != trust_mode::confirm_requests) resolve (tid, selectors);
        else {
          std::weak_ptr<impl> weak = shared_from_this ();
          authorize (msg, [weak, tid, selectors] (bool allow) {
            if (auto self = weak.lock ()) self->event ([tid, selectors, allow] (impl& s) {
              if (!s.tickets.count (tid)) return;
              if (allow) s.resolve (tid, selectors);
              else { s.error (tid, "Resolution denied"); s.tickets.erase (tid); }
            });
          });
        }
        return;
      }
      auto it = tickets.find (tid);
      if (it == tickets.end ()) throw std::invalid_argument ("Unknown ticket");
      auto& t = it->second;
      if (op == opcode::ask) {
        if (msg.size () == 2) reply (t.terminal.is_null () ? message (opcode::ack, {tid}) : t.terminal);
        else if (msg.size () == 3) {
          oid = identifier (msg[2]);
          auto o = t.operations.find (oid);
          if (o == t.operations.end ()) throw std::invalid_argument ("Unknown operation");
          if (o->second.released) throw std::invalid_argument ("Operation result released");
          reply (o->second.terminal.is_null () ? message (opcode::ack, {tid, oid}) : o->second.terminal);
        }
        else throw std::invalid_argument ("Invalid ASK arity");
        return;
      }
      if (op == opcode::cnl) {
        if (msg.size () != 2 || !t.resolving) throw std::invalid_argument ("CNL requires a resolving ticket");
        if (t.resolution) t.resolution->cancel ();
        tickets.erase (it); reply (message (opcode::ack, {tid})); return;
      }
      if (t.resolving) throw std::invalid_argument ("Ticket is still resolving");
      if (op == opcode::fin) {
        if (msg.size () != 2) throw std::invalid_argument ("Invalid FIN arity");
        t.closing = true; reply (message (opcode::ack, {tid})); check_close (tid); return;
      }
      if (msg.size () < 3) throw std::invalid_argument ("Missing operation ID");
      oid = identifier (msg[2]);
      if (op == opcode::rel) {
        auto o = t.operations.find (oid);
        if (msg.size () != 3 || o == t.operations.end ()) throw std::invalid_argument ("Unknown operation");
        if (o->second.terminal.is_null () && !o->second.released)
          throw std::invalid_argument ("Cannot release a pending operation");
        o->second.terminal = nullptr; o->second.request = nullptr; o->second.released = true;
        reply (message (opcode::ack, {tid, oid})); return;
      }
      if (op != opcode::opr && op != opcode::lin) throw std::invalid_argument ("Unexpected opcode");
      auto existing = t.operations.find (oid);
      if (existing != t.operations.end ()) {
        if (existing->second.released || existing->second.request != msg)
          throw std::invalid_argument ("Operation ID already used");
        reply (existing->second.terminal.is_null () ? message (opcode::ack, {tid, oid}) : existing->second.terminal);
        return;
      }
      if (t.closing) throw std::invalid_argument ("Ticket is closing");
      if (t.operations.size () >= 4096) throw std::invalid_argument ("Ticket operation capacity exceeded");
      if (msg.size () != (op == opcode::lin ? 4 : 6)) throw std::invalid_argument ("Invalid operation arity");
      auto h = t.accessors.find (identifier (msg[3]));
      if (h == t.accessors.end ()) throw std::invalid_argument ("Unknown handle");
      if (op == opcode::opr && (!msg[4].is_string () || !msg[5].is_object ()))
        throw std::invalid_argument ("OPR requires command and parameter map");
      t.operations[oid].request = msg;
      reply (message (opcode::ack, {tid, oid}));
      if (op == opcode::lin) {
        value lineage = value::array ();
        for (auto n = h->second; n; n = n->parent) lineage.push_back (n->id);
        std::reverse (lineage.begin (), lineage.end ());
        finish_operation (tid, oid, message (opcode::rsp, {tid, oid, "OK", lineage})); return;
      }
      auto accessor = h->second->accessor;
      const std::string command = msg[4];
      auto mask = capabilities.find (accessor->type ());
      if (mask != capabilities.end () && mask->second.enforced && !mask->second.commands.count (command)) {
        finish_operation (tid, oid, message (opcode::err, {tid, oid, "Capability denied"})); return;
      }
      if (trust == trust_mode::full_access) operate (tid, oid, accessor, command, msg[5]);
      else {
        std::weak_ptr<impl> weak = shared_from_this ();
        const value parameters = msg[5];
        const value confirmation {{"request", msg}, {"selection", t.request[2]},
          {"resource_type", accessor->type ()}, {"resource_identity", accessor->identity ()}};
        authorize (confirmation, [weak, tid, oid, accessor, command, parameters] (bool allow) {
          if (auto self = weak.lock ()) self->event ([tid, oid, accessor, command, parameters, allow] (impl& s) {
            if (allow) s.operate (tid, oid, accessor, command, parameters);
            else s.finish_operation (tid, oid, message (opcode::err, {tid, oid, "Operation denied"}));
          });
        });
      }
    }
    catch (const std::exception& e) {
      if (oid) {
        auto t = tickets.find (tid);
        if (t != tickets.end () && t->second.operations.count (oid) &&
            t->second.operations.at (oid).terminal.is_null () &&
            !t->second.operations.at (oid).released && t->second.operations.at (oid).request == msg)
          finish_operation (tid, oid, message (opcode::err, {tid, oid, e.what ()}));
        else op_error (tid, oid, e.what ());
      }
      else error (tid, e.what ());
    }
  }
};

protocol_session::protocol_session (resolution_workers& resolutions, resolution_workers& operations,
    std::shared_ptr<const resolver_registry> registry, trust_mode trust,
    authorization authorize, std::function<void (value)> send,
    std::map<std::string, capability_mask> masks) {
  if (!registry || !send || (trust != trust_mode::full_access && !authorize))
    throw std::invalid_argument ("Invalid session configuration");
  implementation = std::make_shared<impl> (resolutions, operations, std::move (registry),
    trust, std::move (authorize), std::move (send), std::move (masks));
}
protocol_session::~protocol_session () { disconnect (); }
void protocol_session::receive (const value& msg) {
  if (implementation && implementation->connected) implementation->receive (msg);
}
void protocol_session::poll () {
  if (!implementation) return;
  std::deque<std::function<void (impl&)>> pending;
  { std::lock_guard<std::mutex> lock (implementation->events_mutex); pending.swap (implementation->events); }
  for (auto& event: pending) if (implementation->connected) event (*implementation);
}
void protocol_session::disconnect () {
  if (!implementation) return;
  implementation->connected = false;
  for (auto& t: implementation->tickets) if (t.second.resolution) t.second.resolution->cancel ();
  implementation.reset ();
}
} // namespace athena::interop
