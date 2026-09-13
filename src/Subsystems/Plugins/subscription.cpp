/******************************************************************************
* MODULE     : subscription.cpp
* DESCRIPTION: Replayable plugin IPC with launch-scoped resolver ownership
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#include "subscription.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace athena::plugins {
subscription::subscription (std::string guid, std::string plugin,
    std::size_t capacity, std::size_t byte_limit): guid_ (std::move (guid)),
  plugin_ (std::move (plugin)), capacity_ (capacity), byte_limit_ (byte_limit) {
  if (guid_.empty () || guid_.find_first_not_of ("0123456789abcdefABCDEF-") != std::string::npos ||
      plugin_.empty () || !capacity || byte_limit / 2 / capacity < 64)
    throw std::invalid_argument ("Invalid plugin subscription configuration");
}

value subscription::properties () const {
  std::lock_guard<std::mutex> lock (mutex_);
  return {{"type", "subscription"}, {"name", guid_}, {"plugin", plugin_},
          {"active", active_}, {"last_id", sequence_},
          {"message_byte_limit", byte_limit_ / 2 / capacity_}};
}

std::uint64_t subscription::enqueue (const std::string& command, const value& parameters) {
  if (command.empty () || command.size () > 256 || !parameters.is_object ())
    throw std::invalid_argument ("Command requires a name and parameter object");
  value request {{"command", command}, {"parameters", parameters}};
  const auto size = request.dump ().size ();
  std::lock_guard<std::mutex> lock (mutex_);
  if (!active_) throw std::runtime_error ("Subscription is closed");
  // Each retained entry reserves equally sized command and reply slots.
  if (size > byte_limit_ / 2 / capacity_) throw std::runtime_error ("Plugin command exceeds byte limit");
  while (!entries_.empty () && entries_.front ().delivered && entries_.size () >= capacity_) {
    bytes_ -= entries_.front ().bytes;
    entries_.pop_front ();
  }
  if (entries_.size () >= capacity_)
    throw std::runtime_error ("Plugin command queue is full");
  if (sequence_ == std::numeric_limits<std::uint64_t>::max ())
    throw std::runtime_error ("Plugin command identifiers exhausted");
  entries_.push_back ({++sequence_, std::move (request), nullptr, size});
  bytes_ += size;
  return sequence_;
}

value subscription::get (std::uint64_t after, std::size_t limit) const {
  if (!limit || limit > 256) throw std::invalid_argument ("limit must be between 1 and 256");
  std::lock_guard<std::mutex> lock (mutex_);
  if (!active_) throw std::runtime_error ("Subscription is closed");
  if (after > sequence_) throw std::invalid_argument ("Cursor is ahead of this subscription");
  const auto first = entries_.empty () ? sequence_ + 1 : entries_.front ().id;
  value commands = value::array ();
  auto next = after;
  for (const auto& entry: entries_) {
    if (entry.id <= after) continue;
    next = entry.id;
    if (entry.response.is_null ()) {
      auto item = entry.request; item["id"] = entry.id;
      commands.push_back (std::move (item));
      if (commands.size () == limit) break;
    }
  }
  return {{"commands", std::move (commands)}, {"cursor", next},
          {"first_retained_id", first}, {"history_truncated", after < first - 1}};
}

void subscription::reply (std::uint64_t id, const std::string& status, const value& result) {
  if (status != "OK" && status != "ERROR") throw std::invalid_argument ("Reply status must be OK or ERROR");
  value response {{"id", id}, {"status", status}, {"result", result}};
  const auto size = response.dump ().size ();
  std::lock_guard<std::mutex> lock (mutex_);
  if (!active_) throw std::runtime_error ("Subscription is closed");
  const auto it = std::find_if (entries_.begin (), entries_.end (),
    [id] (const entry& e) { return e.id == id; });
  if (it == entries_.end ()) throw std::invalid_argument ("Unknown or expired plugin command");
  if (!it->response.is_null ()) {
    if (it->response != response) throw std::invalid_argument ("Conflicting duplicate plugin reply");
    return;
  }
  if (size > byte_limit_ / 2 / capacity_ || bytes_ > byte_limit_ - size)
    throw std::runtime_error ("Plugin reply exceeds remaining byte limit");
  it->response = std::move (response);
  it->bytes += size;
  bytes_ += size;
}

value subscription::take_responses () {
  std::lock_guard<std::mutex> lock (mutex_);
  value result = value::array ();
  for (auto& e: entries_) if (!e.response.is_null () && !e.delivered) {
    result.push_back (e.response);
    e.delivered = true;
  }
  return result;
}

void subscription::close () {
  std::lock_guard<std::mutex> lock (mutex_);
  active_ = false;
}

namespace {
using namespace interop;
std::uint64_t unsigned_parameter (const value& parameters, const char* name, std::uint64_t fallback) {
  if (!parameters.contains (name)) return fallback;
  const auto& n = parameters.at (name);
  if (!n.is_number_unsigned () && (!n.is_number_integer () || n.get<std::int64_t> () < 0))
    throw std::invalid_argument (std::string (name) + " must be a nonnegative integer");
  return n.get<std::uint64_t> ();
}
class subscription_resource final: public resource {
  const std::shared_ptr<subscription> channel;
public:
  explicit subscription_resource (std::shared_ptr<subscription> c): channel (std::move (c)) {}
  std::string type () const override { return "subscription"; }
  std::string identity () const override { return channel->guid (); }
  value properties () const override { return channel->properties (); }
  value inspect () const override {
    auto result = properties ();
    result["commands"] = {"inspect", "get", "reply"};
    return result;
  }
  operation_result operate (const std::string& command, const value& parameters) const override {
    if (!parameters.is_object ()) throw std::invalid_argument ("Parameters must be an object");
    if (command == "inspect") return {"OK", inspect ()};
    if (command == "get") return {"OK", channel->get (
      unsigned_parameter (parameters, "after", 0), unsigned_parameter (parameters, "limit", 64))};
    if (command == "reply") {
      channel->reply (unsigned_parameter (parameters, "id", 0),
        parameters.at ("status").get<std::string> (), parameters.at ("result"));
      return {"OK", {{"accepted", true}}};
    }
    throw std::invalid_argument ("Unknown subscription command");
  }
};
class subscription_resolver_rep final: public resolver {
  const std::shared_ptr<subscription> channel;
public:
  explicit subscription_resolver_rep (std::shared_ptr<subscription> c): channel (std::move (c)) {}
  resolver_outcome resolve (const resolution_request& req, resolution_output& out) const override {
    if (!req.basepoint || req.basepoint->accessor->type () != "root" || req.state ||
        req.offset >= req.selectors.size ()) return resolver_outcome::irrelevant;
    const auto& entry = req.selectors[req.offset];
    if (entry.type != selector::kind::name || entry.name != "subscription" || !entry.positions.empty ())
      return resolver_outcome::irrelevant;
    if (req.stopped.load () || req.offset + 1 >= req.selectors.size ()) return resolver_outcome::miss;
    const auto& target = req.selectors[req.offset + 1];
    if (target.type != selector::kind::name || target.name != channel->guid () ||
        !target.positions.empty () || !channel->properties ().at ("active").get<bool> ())
      return resolver_outcome::miss;
    out.publish (std::make_shared<subscription_resource> (channel), req.offset + 2);
    return resolver_outcome::resolved;
  }
};
} // namespace
std::shared_ptr<const interop::resolver> subscription_resolver (std::shared_ptr<subscription> channel) {
  if (!channel) throw std::invalid_argument ("Missing subscription");
  return std::make_shared<subscription_resolver_rep> (std::move (channel));
}
} // namespace athena::plugins
