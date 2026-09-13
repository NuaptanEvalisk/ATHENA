/******************************************************************************
* MODULE     : resolution.hpp
* DESCRIPTION: Resource bindings, occurrence lineage and resolution worker interfaces
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#pragma once

#include "selection.hpp"
#include <atomic>
#include <functional>
#include <mutex>

namespace athena::interop {

// A fixed executor shared by tickets. Resolvers enqueue continuations and return;
// they must never wait for other work submitted to this executor.
class resolution_workers {
  struct impl;
  std::unique_ptr<impl> implementation;
public:
  explicit resolution_workers (std::size_t count, std::size_t queue_limit = 65536);
  ~resolution_workers ();
  resolution_workers (const resolution_workers&) = delete;
  resolution_workers& operator= (const resolution_workers&) = delete;
  bool post (std::function<void ()> task);
  std::size_t size () const;
};

struct operation_result {
  std::string status = "OK";
  value data;
};

class resource {
public:
  virtual ~resource () = default;
  virtual std::string type () const = 0;
  virtual std::string identity () const = 0;
  virtual value properties () const = 0;
  virtual value inspect () const = 0;
  virtual operation_result operate (const std::string& command,
                                    const value& parameters) const = 0;
};
using binding = std::shared_ptr<const resource>;
using handle = std::uint64_t;

struct occurrence {
  handle id;
  binding accessor;
  std::shared_ptr<const occurrence> parent;
};

// Opaque to the orchestrator: domain-specific recursion state belongs to the
// resolver that created it, and is immutable once dispatched.
struct continuation_state { virtual ~continuation_state () = default; };
using continuation = std::shared_ptr<const continuation_state>;

struct resolution_request {
  const selection& selectors;
  std::size_t offset;
  std::shared_ptr<const occurrence> basepoint; // null is the distinguished void
  continuation state;
  const std::atomic<bool>& stopped;
};

enum class truncation_kind { max_matches = 1, max_duration = 2 };
struct truncation {
  truncation_kind reason;
  std::string message;
};

struct emission {
  binding accessor;
  struct next { std::size_t offset; continuation state; };
  std::vector<next> continuations;
};

// Invocation-local output, merged only after the resolver returns. A resolver
// can read its ancestors but cannot inspect sibling tasks or the working tree.
struct resolution_output {
  std::vector<emission> branches;
  std::vector<emission::next> redispatch;
  std::vector<truncation> truncated;
  void publish (binding accessor, std::size_t offset, continuation state = {});
};

enum class resolver_outcome { irrelevant, miss, resolved };
class resolver {
public:
  virtual ~resolver () = default;
  // Genuine failures throw; an exception never becomes MISS or partial ACX.
  virtual resolver_outcome resolve (const resolution_request& request,
                                    resolution_output& output) const = 0;
};
using resolver_registry = std::vector<std::shared_ptr<const resolver>>;

struct resolution_result {
  enum class status { complete, cancelled, fault };
  status state = status::complete;
  std::string error;
  std::vector<std::shared_ptr<const occurrence>> tree;
  std::vector<handle> leaves;
  std::vector<truncation> truncated;
};

class resolution_ticket {
  struct impl;
  std::shared_ptr<impl> implementation;
public:
  using completion = std::function<void (resolution_result)>;
  resolution_ticket (resolution_workers& workers,
                     std::shared_ptr<const resolver_registry> registry,
                     selection selectors, completion finished,
                     std::size_t node_limit = 65536);
  void cancel () const;
};

} // namespace athena::interop
