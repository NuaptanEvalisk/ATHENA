/******************************************************************************
* MODULE     : client.cpp
* DESCRIPTION: Socket-owned asynchronous AUDMAP SDK with idle heartbeats and correlation
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#include "athena/audmap/client.hpp"
#include "connection.hpp"
#include <atomic>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <thread>
#include <tuple>

namespace athena::audmap {
using namespace std::chrono_literals;
protocol_error::protocol_error (value f):
  std::runtime_error (f.dump ()), frame (std::move (f)) {}

struct client::impl {
  struct request {
    value frame;
    std::shared_ptr<std::promise<response>> promise;
  };
  using key = std::tuple<id, id, bool>;
  std::atomic<id> next_ticket {1}, next_operation {1};
  std::mutex mutex;
  std::mutex close_mutex;
  bool stopping = false;
  std::exception_ptr failure;
  std::deque<request> outgoing;
  std::map<key, std::vector<std::shared_ptr<std::promise<response>>>> waiting;
  std::set<std::pair<id, id>> admitted;
  std::thread worker;
  std::promise<void> authorized;

  explicit impl (options config) {
    worker = std::thread ([this, config = std::move (config)] { run (config); });
  }
  ~impl () { close (); }
  void close () {
    std::lock_guard<std::mutex> closing (close_mutex);
    { std::lock_guard<std::mutex> guard (mutex); stopping = true; }
    if (worker.joinable ()) worker.join ();
  }
  pending_request enqueue (value frame) {
    auto p = std::make_shared<std::promise<response>> ();
    const auto op = frame.at (0).get<unsigned> ();
    pending_request result {frame.at (1).get<id> (),
      (op == 5 || op == 8 || op == 9 || (op == 3 && frame.size () == 3)) ?
        frame.at (2).get<id> () : 0, p->get_future ().share ()};
    std::lock_guard<std::mutex> guard (mutex);
    if (stopping) p->set_exception (failure ? failure :
      std::make_exception_ptr (std::runtime_error ("AUDMAP client is closed")));
    else outgoing.push_back ({std::move (frame), std::move (p)});
    return result;
  }
  static key correlation (const value& frame) {
    const unsigned op = frame[0];
    return {frame[1].get<id> (),
      (op == 5 || op == 8 || op == 9 || (op == 3 && frame.size () == 3)) ? frame[2].get<id> () : 0,
      op == 8 || op == 10 || op == 11};
  }
  void fail_ticket (id ticket, const std::exception_ptr& error) {
    for (auto i = waiting.begin (); i != waiting.end ();) {
      if (std::get<0> (i->first) != ticket) { ++i; continue; }
      for (auto& p: i->second) p->set_exception (error);
      i = waiting.erase (i);
    }
  }
  void receive (const value& f) {
    const unsigned op = f.at (0);
    if (op == 105) throw protocol_error (f);
    if (op == 101 || op == 102) return;
    const id ticket = f.at (1);
    if (op == 7 && f.size () == 3) {
      fail_ticket (ticket, std::make_exception_ptr (protocol_error (f)));
      return;
    }
    const id operation = ((op == 6 || op == 7) || (op == 2 && f.size () == 3)) ?
      f.at (2).get<id> () : 0;
    if (op == 2) admitted.emplace (ticket, operation);
    const key k {ticket, operation, op == 2};
    auto found = waiting.find (k);
    if (found == waiting.end () && op == 7) found = waiting.find ({ticket, operation, true});
    if (found == waiting.end ()) return;
    for (auto& p: found->second) {
      if (op == 7) p->set_exception (std::make_exception_ptr (protocol_error (f)));
      else if (op == 6) p->set_value ({f.at (3).get<std::string> (), f.at (4)});
      else p->set_value ({"OK", op == 4 ? f.at (2) : value (nullptr)});
    }
    waiting.erase (found);
  }
  void run (const options& config) {
    bool connected = false;
    try {
      athena::interop::client_connection connection (config.endpoint,
        config.identity.empty () ? athena::interop::default_client_identity ("cpp") : config.identity, config.name);
      const auto deadline = std::chrono::steady_clock::now () + config.authorization_timeout;
      while (!connected) {
        { std::lock_guard<std::mutex> guard (mutex); if (stopping) throw std::runtime_error ("Client closed during authorization"); }
        zmq::pollitem_t item {connection.handle (), 0, ZMQ_POLLIN, 0};
        zmq::poll (&item, 1, 50ms);
        connection.tick ();
        if (auto f = connection.receive ()) {
          if (f->at (0) == 101) connected = true;
          else if (f->at (0) == 105) throw protocol_error (*f);
        }
        if (!connected && std::chrono::steady_clock::now () >= deadline) throw std::runtime_error ("AUDMAP authorization timed out");
      }
      authorized.set_value ();
      for (;;) {
        std::deque<request> batch;
        {
          std::lock_guard<std::mutex> guard (mutex);
          if (stopping) break;
          batch.swap (outgoing);
        }
        for (auto& request: batch) {
          const key k = correlation (request.frame);
          const auto ticket = std::get<0> (k), operation = std::get<1> (k);
          const key terminal {ticket, operation, false};
          if (request.frame[0] == 3 && waiting.count (terminal)) {
            waiting[terminal].push_back (request.promise);
            continue;
          }
          if ((std::get<2> (k) && waiting.count (k)) ||
              (request.frame[0] == 10 && waiting.count (terminal) &&
               !admitted.count ({ticket, 0}))) {
            std::lock_guard<std::mutex> guard (mutex);
            outgoing.push_back (std::move (request));
            continue;
          }
          if ((request.frame[0] == 8 || request.frame[0] == 11) && waiting.count (terminal)) {
            request.promise->set_exception (std::make_exception_ptr (
              std::invalid_argument ("Wait for the terminal result before REL/FIN; use CNL for resolution")));
            continue;
          }
          waiting[k].push_back (request.promise);
          try { connection.send (request.frame); }
          catch (...) {
            request.promise->set_exception (std::current_exception ());
            waiting[k].pop_back ();
            // The connection failure below also settles requests not yet sent.
            for (auto& remaining: batch)
              if (remaining.promise != request.promise && remaining.promise)
                try { remaining.promise->set_exception (std::current_exception ()); } catch (const std::future_error&) {}
            throw;
          }
          if (request.frame[0] == 10) {
            auto i = waiting.find ({request.frame[1].get<id> (), 0, false});
            if (i != waiting.end ()) {
              for (auto& p: i->second) p->set_exception (std::make_exception_ptr (std::runtime_error ("Resolution cancelled")));
              waiting.erase (i);
            }
          }
        }
        connection.tick ();
        zmq::pollitem_t item {connection.handle (), 0, ZMQ_POLLIN, 0};
        zmq::poll (&item, 1, 50ms);
        while (auto f = connection.receive ()) receive (*f);
      }
      throw std::runtime_error ("AUDMAP client closed");
    }
    catch (...) {
      const auto error = std::current_exception ();
      if (!connected) authorized.set_exception (error);
      for (auto& pair: waiting)
        for (auto& p: pair.second)
          try { p->set_exception (error); } catch (const std::future_error&) {}
      waiting.clear ();
      std::lock_guard<std::mutex> guard (mutex);
      failure = error;
      stopping = true;
      for (auto& r: outgoing) r.promise->set_exception (error);
      outgoing.clear ();
    }
  }
};

client::client (options config): implementation (std::make_unique<impl> (std::move (config))) {
  implementation->authorized.get_future ().get ();
}
client::~client () = default;
void client::close () { implementation->close (); }
pending_request client::resolve (std::string selector, bool leaves, id limit) {
  if (limit && !leaves) throw std::invalid_argument ("A limit requires leaves projection");
  value projection = value::array ({leaves ? 1 : 0});
  if (limit) projection.push_back (limit);
  return implementation->enqueue (value::array ({1, implementation->next_ticket++, std::move (selector), projection}));
}
pending_request client::operate (id ticket, id handle, std::string command, value parameters) {
  if (!ticket || !handle || !parameters.is_object ()) throw std::invalid_argument ("OPR requires positive IDs and an object");
  return implementation->enqueue (value::array ({5, ticket, implementation->next_operation++, handle, std::move (command), std::move (parameters)}));
}
pending_request client::lineage (id ticket, id handle) {
  return implementation->enqueue (value::array ({9, ticket, implementation->next_operation++, handle}));
}
pending_request client::ask (id ticket, id operation) {
  value frame = value::array ({3, ticket}); if (operation) frame.push_back (operation);
  return implementation->enqueue (std::move (frame));
}
pending_request client::release (id ticket, id operation) {
  return implementation->enqueue (value::array ({8, ticket, operation}));
}
pending_request client::cancel (id ticket) { return implementation->enqueue (value::array ({10, ticket})); }
pending_request client::finish (id ticket) { return implementation->enqueue (value::array ({11, ticket})); }
}
